#include "nav/core/libdeps.hpp"

#include "nav/core/navhal.hpp"
#include "nav/core/ui.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace fs = std::filesystem;

namespace nav::core {

std::string normalize_git_url(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.size() >= 4 && url.compare(url.size() - 4, 4, ".git") == 0) url.resize(url.size() - 4);
    // scp-style "git@host:owner/repo" -> "https://host/owner/repo"
    if (url.find("://") == std::string::npos) {
        const auto at = url.find('@');
        const auto colon = url.find(':');
        if (colon != std::string::npos && (at == std::string::npos || at < colon))
            url = "https://" + url.substr(at == std::string::npos ? 0 : at + 1, colon - (at == std::string::npos ? 0 : at + 1)) +
                  "/" + url.substr(colon + 1);
    }
    // Lowercase the scheme and host; drop user info ("ssh://git@host/...").
    if (const auto sch = url.find("://"); sch != std::string::npos) {
        std::string scheme = url.substr(0, sch);
        std::string rest = url.substr(sch + 3);
        auto slash = rest.find('/');
        std::string host = rest.substr(0, slash);
        if (auto at = host.find('@'); at != std::string::npos) host = host.substr(at + 1);
        for (auto& c : host) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (scheme == "ssh" || scheme == "git" || scheme == "http") scheme = "https";
        url = scheme + "://" + host + (slash == std::string::npos ? "" : rest.substr(slash));
    }
    return url;
}

std::vector<std::pair<std::string, std::string>> parse_gitmodules(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    std::istringstream in(text);
    std::string line, path, url;
    bool in_section = false;
    auto flush = [&] {
        if (in_section && !path.empty() && !url.empty()) out.emplace_back(path, url);
        path.clear();
        url.clear();
    };
    auto trim = [](std::string v) {
        const auto b = v.find_first_not_of(" \t\r");
        const auto e = v.find_last_not_of(" \t\r");
        return b == std::string::npos ? std::string() : v.substr(b, e - b + 1);
    };
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') {
            flush();
            in_section = line.rfind("[submodule", 0) == 0;
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        if (key == "path") path = val;
        else if (key == "url") url = val;
    }
    flush();
    return out;
}

const ResolvedLib* DepGraph::find(const std::string& name) const {
    for (const auto& l : libs)
        if (l.name == name) return &l;
    return nullptr;
}

namespace {

// "vX.Y.Z..." -> "X.Y.Z..."; empty if `ref` is not a version tag.
std::string version_from_tag(const std::string& ref) {
    if (ref.size() < 2 || ref[0] != 'v' || !std::isdigit(static_cast<unsigned char>(ref[1]))) return {};
    return ref.substr(1);
}

std::string join_chain(const std::vector<std::string>& chain) {
    std::string s;
    for (const auto& c : chain) s += (s.empty() ? "" : " -> ") + c;
    return s;
}

// Where a request points. Two requests are the same source when both are the
// same path, or the same normalized git URL with the same ref or commit.
struct Source {
    bool git = false;
    std::string where;   // canonical path, or normalized URL
    std::string ref;
    std::string commit;
    std::string label;   // for messages
    bool same(const Source& o) const {
        if (git != o.git || where != o.where) return false;
        if (!git) return true;
        return ref == o.ref || (!commit.empty() && commit == o.commit);
    }
};

// Submodules of the library at `src`, with their gitlink commits.
std::vector<BundledCopy> read_bundled(IExecutionContext& ctx, const fs::path& src) {
    std::vector<BundledCopy> out;
    std::ifstream f(src / ".gitmodules");
    if (!f) return out;
    std::stringstream ss;
    ss << f.rdbuf();
    for (auto& [path, url] : parse_gitmodules(ss.str())) {
        BundledCopy b{path, url, {}};
        auto res = ctx.execute({"git", "-C", src.string(), "ls-tree", "HEAD", "--", path}, "",
                               /*silent=*/true);
        // "160000 commit <sha>\t<path>"
        if (res.exit_code == 0) {
            std::istringstream ls(res.output);
            std::string mode, type, sha;
            if (ls >> mode >> type >> sha && type == "commit") b.commit = sha;
        }
        out.push_back(std::move(b));
    }
    return out;
}

// Depth-first walk that records every request instead of stopping at the
// first problem. Each library is explored once, from the first source seen
// (or the override); later requests are only compared against it.
struct Walker {
    IExecutionContext& ctx;
    const ProjectConfig& root_cfg;
    DepGraph& g;
    std::unordered_set<std::string> done;
    std::vector<std::string> stack;  // canonical names on the current path
    struct Seen {
        Source chosen;
        std::vector<DepRequest> distinct;  // first request per distinct source
        std::vector<Source> distinct_src;
    };
    std::map<std::string, Seen> seen;
    std::vector<std::string> conflict_order;

    const Dependency* override_for(const std::string& name) const {
        for (const auto& o : root_cfg.overrides)
            if (o.name == name) return &o;
        return nullptr;
    }

    // Locate (fetch if needed) the source a dependency names.
    bool locate(const Dependency& dep, const fs::path& decl_root, Source& s, fs::path& src) {
        if (dep.is_path()) {
            std::error_code ec;
            src = fs::weakly_canonical(decl_root / dep.path, ec);
            if (ec) src = (decl_root / dep.path).lexically_normal();
            s.git = false;
            s.where = src.generic_string();
            s.label = "path " + s.where;
            if (!fs::exists(src / "nav.toml", ec)) {
                g.errors.push_back("Dependency '" + dep.name + "': no nav.toml at " + src.string());
                return false;
            }
            return true;
        }
        if (dep.ref.empty())
            ui::warning("Dependency '" + dep.name + "' has no ref; tracking the default branch "
                        "is not reproducible. Pin a release tag: ref = \"vX.Y.Z\".");
        s.git = true;
        s.where = normalize_git_url(dep.git);
        s.ref = dep.ref;
        s.label = s.where + (dep.ref.empty() ? "" : " @ " + dep.ref);
        src = ensure_git_cached(ctx, dep.git, dep.ref);
        if (src.empty()) {
            g.errors.push_back("Dependency '" + dep.name + "': failed to fetch " + dep.git + ".");
            return false;
        }
        s.commit = git_head_commit(ctx, src);
        return true;
    }

    std::optional<std::string> visit(Dependency dep, const fs::path& decl_root,
                                     std::vector<std::string> chain, bool overridden = false) {
        // An override keyed by the dependency's name applies before fetching.
        if (!overridden) {
            if (const Dependency* o = override_for(dep.name)) {
                g.notes.push_back("[overrides] " + dep.name + " replaces the source requested by " +
                                  join_chain(chain) + " (" +
                                  (dep.is_git() ? normalize_git_url(dep.git) + (dep.ref.empty() ? "" : " @ " + dep.ref)
                                                : "path " + dep.path) + ")");
                Dependency r = *o;
                r.name = dep.name;
                if (r.options.empty()) r.options = dep.options;
                return visit(std::move(r), root_cfg.root, std::move(chain), true);
            }
        }

        Source s;
        fs::path src;
        if (!locate(dep, decl_root, s, src)) return std::nullopt;

        auto depcfg = load_project_config(src);
        if (!depcfg) {
            g.errors.push_back("Dependency '" + dep.name + "': could not parse " + (src / "nav.toml").string());
            return std::nullopt;
        }
        if (!depcfg->is_library()) {
            g.errors.push_back("Dependency '" + dep.name + "' (" + src.string() +
                               ") is not a library project (set [project].type = \"library\").");
            return std::nullopt;
        }
        const std::string canonical = depcfg->project_name.empty() ? dep.name : depcfg->project_name;

        // An override keyed by the canonical name (when it differs from the key).
        if (!overridden && canonical != dep.name) {
            if (const Dependency* o = override_for(canonical)) {
                g.notes.push_back("[overrides] " + canonical + " replaces the source requested by " +
                                  join_chain(chain) + " (" + s.label + ")");
                Dependency r = *o;
                r.name = canonical;
                if (r.options.empty()) r.options = dep.options;
                return visit(std::move(r), root_cfg.root, std::move(chain), true);
            }
        }

        if (dep.is_git()) {
            const std::string tagged = version_from_tag(dep.ref);
            if (!tagged.empty() && !depcfg->project_version.empty() && tagged != depcfg->project_version) {
                g.errors.push_back("Dependency '" + canonical + "': tag " + dep.ref +
                                   " has [project].version = \"" + depcfg->project_version +
                                   "\" in its nav.toml. A release tag must match it.");
                return std::nullopt;
            }
        }

        // Record the request; compare with what was chosen for this library.
        auto [it, fresh] = seen.try_emplace(canonical);
        Seen& sn = it->second;
        if (fresh) sn.chosen = s;
        bool known = false;
        for (const auto& d : sn.distinct_src) known = known || d.same(s);
        if (!known) {
            sn.distinct_src.push_back(s);
            sn.distinct.push_back({s.label, chain});
            if (sn.distinct.size() == 2) conflict_order.push_back(canonical);
        }
        if (!fresh) {
            if (std::find(stack.begin(), stack.end(), canonical) != stack.end()) {
                auto cyc = stack;
                cyc.erase(cyc.begin(), std::find(cyc.begin(), cyc.end(), canonical));
                cyc.push_back(canonical);
                g.errors.push_back("Dependency cycle: " + join_chain(cyc));
            }
            return canonical;  // explored from its chosen source already (or in progress)
        }

        stack.push_back(canonical);
        std::vector<std::string> child_chain = chain;
        child_chain.push_back(canonical);
        std::vector<std::string> child_names;
        for (const auto& child : depcfg->dependencies)
            if (auto cn = visit(child, src, child_chain)) child_names.push_back(*cn);
        stack.pop_back();

        done.insert(canonical);
        ResolvedLib lib{canonical, src, std::move(child_names), dep.options, depcfg->navhal_submodule};
        if (dep.is_git()) {
            lib.git_url = dep.git;
            lib.git_ref = dep.ref;
            lib.commit = s.commit;
        }
        lib.hal_api_version = depcfg->hal_api_version;
        lib.source = s.label;
        lib.version = depcfg->project_version;
        lib.bundled = read_bundled(ctx, src);
        lib.overridden = overridden;
        {
            std::ifstream cm(src / "CMakeLists.txt");
            std::stringstream body;
            body << cm.rdbuf();
            lib.uses_navhal_dir = body.str().find("NAVHAL_DIR") != std::string::npos;
        }
        {
            std::error_code ec_kc;
            lib.has_kconfig = fs::exists(src / "Kconfig", ec_kc);
        }
        g.libs.push_back(std::move(lib));
        return canonical;
    }
};

std::string short_sha(const std::string& c) { return c.substr(0, 7); }

// CMake path literal: forward slashes, quoted.
std::string q(const fs::path& p) { return "\"" + p.generic_string() + "\""; }

} // namespace

DepGraph resolve_dep_graph(IExecutionContext& ctx, const fs::path& root, const ProjectConfig& cfg) {
    DepGraph g;
    ProjectConfig rc = cfg;
    if (rc.root.empty()) rc.root = root;
    Walker w{ctx, rc, g};
    const std::string me = cfg.project_name.empty() ? "this project" : cfg.project_name;
    for (const auto& dep : cfg.dependencies)
        if (auto name = w.visit(dep, root, {me})) g.direct.push_back(*name);
    for (const auto& name : w.conflict_order)
        g.conflicts.push_back({name, w.seen[name].distinct});
    return g;
}

void report_dep_graph(const DepGraph& g) {
    for (const auto& n : g.notes) ui::info(n);
    for (const auto& e : g.errors) ui::error(e);
    for (const auto& c : g.conflicts) {
        ui::error("Library '" + c.name + "' is required from " + std::to_string(c.requests.size()) +
                  " different sources:");
        for (const auto& r : c.requests)
            ui::error("  " + r.source + "  (required by " + join_chain(r.chain) + ")");
    }
    if (!g.conflicts.empty()) {
        ui::error("Make the requirers agree, or choose one source for the whole project in "
                  "nav.toml, e.g.:");
        ui::error("  [overrides]");
        for (const auto& c : g.conflicts) {
            const std::string& src = c.requests.front().source;
            if (src.rfind("path ", 0) == 0) {
                ui::error("  " + c.name + " = { path = \"" + src.substr(5) + "\" }");
            } else {
                const auto at = src.find(" @ ");
                ui::error("  " + c.name + " = { git = \"" + src.substr(0, at) + "\"" +
                          (at == std::string::npos ? "" : ", ref = \"" + src.substr(at + 3) + "\"") + " }");
            }
        }
    }
}

std::optional<std::vector<ResolvedLib>>
resolve_library_deps(IExecutionContext& ctx, const fs::path& root,
                     const ProjectConfig& cfg, std::vector<std::string>* out_direct) {
    DepGraph g = resolve_dep_graph(ctx, root, cfg);
    report_dep_graph(g);
    if (!g.ok()) return std::nullopt;
    if (out_direct) *out_direct = g.direct;
    return std::move(g.libs);
}

std::string render_dep_tree(const DepGraph& g, const std::string& root_name) {
    std::ostringstream os;
    os << root_name << "\n";
    std::unordered_set<std::string> shown;
    std::map<std::string, size_t> conflicts;
    for (const auto& c : g.conflicts) conflicts[c.name] = c.requests.size();

    std::function<void(const std::string&, const std::string&, bool)> node =
        [&](const std::string& name, const std::string& prefix, bool last) {
            os << prefix << (last ? "└── " : "├── ") << name;
            const ResolvedLib* lib = g.find(name);
            const std::string child_prefix = prefix + (last ? "    " : "│   ");
            if (shown.count(name)) { os << " (*)\n"; return; }
            shown.insert(name);
            if (lib) {
                if (!lib->version.empty()) os << " v" << lib->version;
                os << "  (" << lib->source << (lib->commit.empty() ? "" : ", " + short_sha(lib->commit)) << ")";
                if (lib->overridden) os << "  [override]";
            }
            if (auto it = conflicts.find(name); it != conflicts.end())
                os << "  !! conflict: " << it->second << " sources";
            os << "\n";
            if (!lib) return;
            const size_t n = lib->deps.size() + lib->bundled.size();
            size_t i = 0;
            for (const auto& b : lib->bundled) {
                ++i;
                os << child_prefix << (i == n ? "└── " : "├── ") << "bundles " << normalize_git_url(b.url)
                   << (b.commit.empty() ? "" : " at " + short_sha(b.commit)) << " (" << b.path << ")";
                if (lib->uses_navhal_dir && normalize_git_url(b.url) == normalize_git_url(kNavhalRepoUrl))
                    os << "  unused: NAVHAL_DIR";
                os << "\n";
            }
            for (const auto& d : lib->deps) node(d, child_prefix, ++i == n);
        };
    for (size_t i = 0; i < g.direct.size(); ++i) node(g.direct[i], "", i + 1 == g.direct.size());
    return os.str();
}

std::vector<std::string> check_bundled_copies(const std::vector<ResolvedLib>& libs,
                                              const std::string& navhal_url,
                                              const std::string& navhal_commit) {
    std::vector<std::string> out;
    const std::string navhal = navhal_url.empty() ? "" : normalize_git_url(navhal_url);
    for (const auto& lib : libs) {
        for (const auto& b : lib.bundled) {
            if (b.commit.empty()) continue;
            const std::string url = normalize_git_url(b.url);
            std::string used_by, used_commit;
            if (!navhal.empty() && url == navhal) {
                if (lib.uses_navhal_dir) continue;  // nav points NAVHAL_DIR at the project's copy
                used_by = "the project's NavHAL (extern/NavHAL)";
                used_commit = navhal_commit;
            } else {
                for (const auto& other : libs)
                    if (&other != &lib && !other.git_url.empty() && normalize_git_url(other.git_url) == url) {
                        used_by = "library '" + other.name + "'";
                        used_commit = other.commit;
                    }
            }
            if (used_by.empty() || used_commit.empty() || used_commit == b.commit) continue;
            out.push_back(lib.name + " bundles " + url + " at " + short_sha(b.commit) + " (" + b.path +
                          "), but the build uses " + used_by + " at " + short_sha(used_commit) +
                          ". Only one copy is built; if " + lib.name +
                          " includes headers from " + b.path + " it compiles against the other version.");
        }
    }
    return out;
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
                                 const std::vector<std::string>& direct,
                                 bool merged_kconfig) {
    std::ostringstream os;
    os << "# Auto-generated by `nav build` from nav.toml [dependencies].\n";
    os << "# Do not edit; re-run `nav build` to regenerate.\n\n";
    // One NavHAL per build: libraries that bundle their own copy read
    // NAVHAL_DIR and build against the project's instead.
    os << "set(NAVHAL_DIR \"${CMAKE_SOURCE_DIR}/extern/NavHAL\" CACHE PATH "
          "\"NavHAL tree libraries build against (set by nav)\" FORCE)\n\n";
    // The board's CPU/FPU flags (-mcpu, -mfloat-abi, ...) are otherwise only on
    // the app target, and NavHAL's arch flags stay in NavHAL's directory scope,
    // so a library that does not link `hal` would compile for the toolchain's
    // default ABI and fail to link ("uses VFP register arguments").
    os << "if(NAV_BOARD_COMPILE_FLAGS)\n"
          "    add_compile_options(${NAV_BOARD_COMPILE_FLAGS})\n"
          "endif()\n\n";

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
            // Booleans go in as BOOL so they match the library's option() type.
            static const char* const kBool[] = {"ON", "OFF", "TRUE", "FALSE", "YES", "NO"};
            std::string up = val;
            for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            const bool is_bool = std::find(std::begin(kBool), std::end(kBool), up) != std::end(kBool);
            os << "set(" << key << " \"" << val << "\" CACHE " << (is_bool ? "BOOL" : "STRING")
               << " \"nav dep option\" FORCE)\n";
        }
        if (merged_kconfig && lib.has_kconfig) {
            std::string var;
            for (char ch : lib.name)
                var += std::isalnum(static_cast<unsigned char>(ch)) ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch))) : '_';
            var += "_CONFIG";
            const bool set_by_user = std::any_of(lib.options.begin(), lib.options.end(), [&](const std::string& o) {
                return o.substr(0, o.find('=')) == var;
            });
            if (!set_by_user)
                os << "set(" << var << " \"${CMAKE_BINARY_DIR}/nav.config\" CACHE FILEPATH \"nav app config\" FORCE)\n";
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
                         const fs::path& out_path,
                         bool merged_kconfig) {
    std::error_code ec;
    if (out_path.has_parent_path()) {
        fs::create_directories(out_path.parent_path(), ec);
        if (ec) return false;
    }
    std::ofstream f(out_path);
    if (!f) return false;
    f << render_libdeps_cmake(libs, direct, merged_kconfig);
    return f.good();
}

} // namespace nav::core
