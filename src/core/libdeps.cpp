#include "nav/core/libdeps.hpp"

#include "nav/core/navhal.hpp"
#include "nav/core/ui.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

// A git URL with the cosmetic differences removed (trailing slash, ".git"), so
// "https://github.com/a/b" and "https://github.com/a/b.git/" are one source.
std::string normalize_git_url(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.size() >= 4 && url.compare(url.size() - 4, 4, ".git") == 0) url.resize(url.size() - 4);
    return url;
}

// "vX.Y.Z..." -> "X.Y.Z..."; empty if `ref` is not a version tag.
std::string version_from_tag(const std::string& ref) {
    if (ref.size() < 2 || ref[0] != 'v' || !std::isdigit(static_cast<unsigned char>(ref[1]))) return {};
    return ref.substr(1);
}

// Depth-first resolver with cycle detection. Pushes each library in post-order
// (dependencies before dependents) into `out`, deduped by canonical name.
struct Resolver {
    IExecutionContext& ctx;
    std::vector<ResolvedLib>& out;
    std::unordered_set<std::string> done;      // fully resolved canonical names
    std::unordered_set<std::string> on_stack;  // cycle detection
    // canonical name -> (source description, project that first required it),
    // so one library pulled from two different places is caught.
    std::unordered_map<std::string, std::pair<std::string, std::string>> sources;
    bool ok = true;

    // Resolve one dependency declared by the project rooted at `decl_root`
    // (named `via`, for error messages). Returns the canonical name, or nullopt
    // on a hard error.
    std::optional<std::string> visit(const Dependency& dep, const fs::path& decl_root,
                                     const std::string& via) {
        fs::path src;
        std::string source;  // where this library comes from, for conflict checks
        std::string commit;
        if (dep.is_path()) {
            std::error_code ec;
            src = fs::weakly_canonical(decl_root / dep.path, ec);
            if (ec) src = (decl_root / dep.path).lexically_normal();
            if (!fs::exists(src / "nav.toml", ec)) {
                ui::error("Dependency '" + dep.name + "': no nav.toml at " + src.string());
                ok = false;
                return std::nullopt;
            }
            source = "path " + src.generic_string();
        } else { // git
            if (dep.ref.empty())
                ui::warning("Dependency '" + dep.name + "' has no ref; tracking the default branch "
                            "is not reproducible. Pin a release tag: ref = \"vX.Y.Z\".");
            src = ensure_git_cached(ctx, dep.git, dep.ref);
            if (src.empty()) {
                ui::error("Dependency '" + dep.name + "': failed to fetch " + dep.git + ".");
                ok = false;
                return std::nullopt;
            }
            source = normalize_git_url(dep.git) + (dep.ref.empty() ? "" : " @ " + dep.ref);
            commit = git_head_commit(ctx, src);
        }

        auto depcfg = load_project_config(src);
        if (!depcfg) {
            ui::error("Dependency '" + dep.name + "': could not parse " + (src / "nav.toml").string());
            ok = false;
            return std::nullopt;
        }
        if (!depcfg->is_library()) {
            ui::error("Dependency '" + dep.name + "' (" + src.string() +
                      ") is not a library project (set [project].type = \"library\").");
            ok = false;
            return std::nullopt;
        }

        const std::string canonical = depcfg->project_name.empty() ? dep.name : depcfg->project_name;

        // Diamond check: every path to a library must name the same source.
        // No version unification -- two different refs is an error.
        if (auto it = sources.find(canonical); it != sources.end()) {
            if (it->second.first != source) {
                ui::error("Library '" + canonical + "' is required from two different sources:");
                ui::error("  " + it->second.first + "  (by " + it->second.second + ")");
                ui::error("  " + source + "  (by " + via + ")");
                ui::error("Make both name the same URL and ref.");
                ok = false;
                return std::nullopt;
            }
        } else {
            sources.emplace(canonical, std::make_pair(source, via));
        }

        // A release is a vX.Y.Z tag equal to the manifest version at that commit.
        if (dep.is_git()) {
            const std::string tagged = version_from_tag(dep.ref);
            if (!tagged.empty() && !depcfg->project_version.empty() &&
                tagged != depcfg->project_version) {
                ui::error("Dependency '" + canonical + "': tag " + dep.ref + " has [project].version = \"" +
                          depcfg->project_version + "\" in its nav.toml. A release tag must match it.");
                ok = false;
                return std::nullopt;
            }
        }

        if (done.count(canonical)) return canonical; // already resolved elsewhere
        if (on_stack.count(canonical)) {
            ui::error("Dependency cycle detected at '" + canonical + "'.");
            ok = false;
            return std::nullopt;
        }

        on_stack.insert(canonical);
        std::vector<std::string> child_names;
        for (const auto& child : depcfg->dependencies) {
            auto cn = visit(child, src, canonical);
            if (!cn) { on_stack.erase(canonical); return std::nullopt; }
            child_names.push_back(*cn);
        }
        on_stack.erase(canonical);

        done.insert(canonical);
        ResolvedLib lib{canonical, src, std::move(child_names), dep.options,
                        depcfg->navhal_submodule};
        if (dep.is_git()) {
            lib.git_url = dep.git;
            lib.git_ref = dep.ref;
            lib.commit = commit;
        }
        lib.hal_api_version = depcfg->hal_api_version;
        out.push_back(std::move(lib));
        return canonical;
    }
};

// CMake path literal: forward slashes, quoted.
std::string q(const fs::path& p) { return "\"" + p.generic_string() + "\""; }

} // namespace

std::optional<std::vector<ResolvedLib>>
resolve_library_deps(IExecutionContext& ctx, const fs::path& root,
                     const ProjectConfig& cfg, std::vector<std::string>* out_direct) {
    std::vector<ResolvedLib> libs;
    Resolver r{ctx, libs};

    std::vector<std::string> direct;
    for (const auto& dep : cfg.dependencies) {
        auto name = r.visit(dep, root, cfg.project_name.empty() ? "this project" : cfg.project_name);
        if (!name) return std::nullopt;
        direct.push_back(*name);
    }
    if (!r.ok) return std::nullopt;
    if (out_direct) *out_direct = std::move(direct);
    return libs;
}

GitLockResult reconcile_git_lock(Lockfile& lock, const std::vector<ResolvedLib>& libs) {
    GitLockResult r;
    std::vector<LockedGit> next;
    for (const auto& lib : libs) {
        if (lib.git_url.empty()) continue;
        const LockedGit* pin = lock.find_git(lib.name);
        if (lib.commit.empty()) {
            r.notes.push_back(lib.name + ": could not read the fetched commit; not pinned");
            if (pin) next.push_back(*pin);
            continue;
        }
        LockedGit want{lib.name, lib.git_url, lib.git_ref, lib.commit};
        if (!pin) {
            r.notes.push_back("pinned " + lib.name + " at " + lib.commit.substr(0, 12));
            r.changed = true;
        } else if (pin->url != lib.git_url || pin->ref != lib.git_ref) {
            r.notes.push_back("re-pinned " + lib.name + " (" + pin->url + " @ " + pin->ref +
                              " -> " + lib.git_url + " @ " + lib.git_ref + ")");
            r.changed = true;
        } else if (pin->commit != lib.commit) {
            r.errors.push_back(lib.name + ": " + lib.git_url + " @ " +
                               (lib.git_ref.empty() ? "<default branch>" : lib.git_ref) +
                               " resolved to " + lib.commit + ", but nav.lock pins " + pin->commit);
            next.push_back(*pin);
            continue;
        }
        next.push_back(std::move(want));
    }
    for (const auto& g : lock.git) {
        bool kept = false;
        for (const auto& n : next) kept = kept || n.name == g.name;
        if (!kept) {
            r.notes.push_back("dropped pin for " + g.name + " (no longer a git dependency)");
            r.changed = true;
        }
    }
    lock.git = std::move(next);
    return r;
}

std::string render_libdeps_cmake(const std::vector<ResolvedLib>& libs,
                                 const std::vector<std::string>& direct) {
    std::ostringstream os;
    os << "# Auto-generated by `nav build` from nav.toml [dependencies].\n";
    os << "# Do not edit; re-run `nav build` to regenerate.\n\n";

    // Add every library subtree first (dependencies-first order), so later
    // target_link_libraries() edges can reference any of them. A library's
    // declared CMake options are forced into the cache just before its
    // add_subdirectory() so option-gated builds (e.g. vaios needs NAVHAL=ON)
    // configure without a manual cmake invocation.
    for (const auto& lib : libs) {
        for (const auto& opt : lib.options) {
            const auto eq = opt.find('=');
            const std::string key = eq == std::string::npos ? opt : opt.substr(0, eq);
            const std::string val = eq == std::string::npos ? "ON" : opt.substr(eq + 1);
            os << "set(" << key << " \"" << val << "\" CACHE STRING \"nav dep option\" FORCE)\n";
        }
        os << "add_subdirectory(" << q(lib.src)
           << " \"${CMAKE_BINARY_DIR}/deps/" << lib.name << "\")\n";
    }
    os << "\n";

    // Wire inter-library edges (PUBLIC so headers propagate transitively).
    for (const auto& lib : libs) {
        for (const auto& child : lib.deps) {
            os << "target_link_libraries(" << lib.name << " PUBLIC " << child << ")\n";
        }
    }
    os << "\n";

    os << "set(NAV_DEP_TARGETS";
    for (const auto& d : direct) os << " " << d;
    os << ")\n";
    return os.str();
}

bool write_libdeps_cmake(const std::vector<ResolvedLib>& libs,
                         const std::vector<std::string>& direct,
                         const fs::path& out_path) {
    std::error_code ec;
    if (out_path.has_parent_path()) {
        fs::create_directories(out_path.parent_path(), ec);
        if (ec) return false;
    }
    std::ofstream f(out_path);
    if (!f) return false;
    f << render_libdeps_cmake(libs, direct);
    return f.good();
}

} // namespace nav::core
