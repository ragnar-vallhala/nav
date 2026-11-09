#include "nav/core/navhal.hpp"

#include "nav/core/cache.hpp"
#include "nav/core/semver.hpp"
#include "nav/core/ui.hpp"

#include <cctype>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace nav::core {

namespace {

// Per-run unique suffix so a partial clone never collides with another run.
std::string unique_suffix() {
    using namespace std::chrono;
    auto ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
#ifdef _WIN32
    auto pid = ::_getpid();
#else
    auto pid = ::getpid();
#endif
    return std::to_string(pid) + "-" + std::to_string(ns);
}

// Clone `url` at `ref` into the cache slot `dir`, via a temp sibling + atomic
// rename so an interrupted clone never leaves a half-populated entry. `ref` may
// be empty (clone the default branch). On success the completion marker is
// written and `dir` is returned; on failure an empty path is returned (unless a
// concurrent run already populated `dir`). `trim_datasheets` drops NavHAL's
// heavyweight datasheets dir. `recurse_submodules` also fetches the repo's
// submodules (shallow), so a library that vendors e.g. NavHAL arrives complete.
// Returns the cache dir or {} on failure.
fs::path clone_into_cache(IExecutionContext& ctx, const std::string& url,
                          const std::string& ref, const fs::path& dir,
                          const std::string& label, bool trim_datasheets,
                          bool recurse_submodules) {
    if (is_installed(dir)) {
        return dir; // already present — no clone, no network.
    }

    std::error_code ec;
    fs::create_directories(dir.parent_path(), ec);

    fs::path tmp = dir;
    tmp += ".tmp-" + unique_suffix();
    fs::remove_all(tmp, ec);

    ui::step("Caching", "Fetching " + label + " into the shared cache (one-time)");
    std::vector<std::string> cmd = {"git", "clone", "--depth", "1"};
    if (!ref.empty()) { cmd.push_back("--branch"); cmd.push_back(ref); }
    if (recurse_submodules) {
        cmd.push_back("--recurse-submodules");
        cmd.push_back("--shallow-submodules");
    }
    cmd.push_back("--progress");
    cmd.push_back(url);
    cmd.push_back(tmp.string());
    auto res = ctx.execute(cmd);
    if (res.exit_code != 0) {
        ui::error("Failed to clone " + label + ".");
        std::error_code rmec;
        fs::remove_all(tmp, rmec);
        // A concurrent run may have populated the cache in the meantime.
        return is_installed(dir) ? dir : fs::path{};
    }

    if (trim_datasheets) fs::remove_all(tmp / "datasheets", ec);

    // Completion marker — its presence is what is_installed() checks.
    std::ofstream(install_marker(tmp)) << "ok\n";

    fs::rename(tmp, dir, ec);
    if (ec) {
        // Lost a race (another run created `dir`) or cross-device move — clean up
        // and accept the cache only if it is now valid.
        std::error_code rmec;
        fs::remove_all(tmp, rmec);
        return is_installed(dir) ? dir : fs::path{};
    }
    return dir;
}

} // namespace

fs::path ensure_navhal_cached(IExecutionContext& ctx, const std::string& ref) {
    return clone_into_cache(ctx, kNavhalRepoUrl, ref, navhal_cache_dir(ref),
                            "NavHAL " + ref, /*trim_datasheets=*/true,
                            /*recurse_submodules=*/false);
}

fs::path ensure_git_cached(IExecutionContext& ctx, const std::string& url,
                           const std::string& ref) {
    const std::string label = url + (ref.empty() ? "" : ("@" + ref));
    return clone_into_cache(ctx, url, ref, git_cache_dir(url, ref), label,
                            /*trim_datasheets=*/false, /*recurse_submodules=*/true);
}

std::string git_head_commit(IExecutionContext& ctx, const fs::path& repo) {
    auto res = ctx.execute({"git", "-C", repo.string(), "rev-parse", "HEAD"}, "",
                           /*silent=*/true);
    if (res.exit_code != 0) return {};
    std::string out = res.output;
    while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back()))) out.pop_back();
    // A full SHA-1 (40) or SHA-256 (64) object name, hex only.
    if (out.size() != 40 && out.size() != 64) return {};
    for (char c : out)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return {};
    return out;
}

std::optional<int> read_hal_api_version(const fs::path& navhal_dir) {
    std::ifstream f(navhal_dir / "include" / "navhal.h");
    if (!f) return std::nullopt;
    static const std::regex re(R"(^\s*#\s*define\s+HAL_API_VERSION\s+(\d+)\b)");
    std::string line;
    std::smatch m;
    while (std::getline(f, line))
        if (std::regex_search(line, m, re)) return std::stoi(m[1].str());
    return std::nullopt;
}

namespace {

bool looks_like_commit(const std::string& r) {
    if (r.size() < 7 || r.size() > 64) return false;
    for (char c : r)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

// Populate `tmp` with `ref` of `url`: a tag or branch is shallow-cloned; a
// commit cannot be a --branch, so fetch exactly it (works for a full SHA on
// GitHub-like servers) and fall back to a full clone + checkout.
bool fetch_ref_into(IExecutionContext& ctx, const std::string& url, const std::string& ref,
                    const fs::path& tmp, const std::string& label) {
    std::error_code ec;
    fs::remove_all(tmp, ec);
    ui::step("Caching", "Fetching " + label + " into the shared cache");
    if (!looks_like_commit(ref)) {
        return ctx.execute({"git", "clone", "--depth", "1", "--branch", ref, "--progress", url,
                            tmp.string()}).exit_code == 0;
    }
    if ((ref.size() == 40 || ref.size() == 64) &&
        ctx.execute({"git", "init", "-q", tmp.string()}, "", true).exit_code == 0 &&
        ctx.execute({"git", "-C", tmp.string(), "fetch", "-q", "--depth", "1", url, ref}).exit_code == 0 &&
        ctx.execute({"git", "-C", tmp.string(), "checkout", "-q", "--detach", "FETCH_HEAD"}, "", true)
                .exit_code == 0)
        return true;
    fs::remove_all(tmp, ec);
    return ctx.execute({"git", "clone", "--progress", url, tmp.string()}).exit_code == 0 &&
           ctx.execute({"git", "-C", tmp.string(), "checkout", "-q", "--detach", ref}, "", true)
                   .exit_code == 0;
}

// Fetch `spec` into a temp sibling and move it to its per-commit cache dir
// (kept as is if that commit is already cached).
std::optional<ResolvedNavhal> fetch_navhal(IExecutionContext& ctx, const NavhalSpec& spec) {
    const fs::path tmp = navhal_commit_dir(spec.git, ".tmp-" + unique_suffix());
    std::error_code ec;
    fs::create_directories(tmp.parent_path(), ec);
    auto fail = [&](const std::string& why) -> std::optional<ResolvedNavhal> {
        std::error_code rmec;
        fs::remove_all(tmp, rmec);
        ui::error(why);
        return std::nullopt;
    };
    if (!fetch_ref_into(ctx, spec.git, spec.ref, tmp, "NavHAL " + spec.ref + " (" + spec.git + ")"))
        return fail("Failed to fetch NavHAL " + spec.ref + " from " + spec.git + ".");
    const std::string commit = git_head_commit(ctx, tmp);
    if (commit.empty()) return fail("Could not read the commit NavHAL " + spec.ref + " resolved to.");

    const fs::path dir = navhal_commit_dir(spec.git, commit);
    if (is_installed(dir)) {
        fs::remove_all(tmp, ec);
        return ResolvedNavhal{dir, commit};
    }
    fs::remove_all(tmp / "datasheets", ec);
    std::ofstream(install_marker(tmp)) << "ok\n";
    fs::rename(tmp, dir, ec);
    if (ec) { // lost a race with another run, or a cross-device move
        std::error_code rmec;
        fs::remove_all(tmp, rmec);
        if (is_installed(dir)) return ResolvedNavhal{dir, commit};
        return fail("Could not place NavHAL in the cache: " + ec.message());
    }
    return ResolvedNavhal{dir, commit};
}

} // namespace

NavhalSpec navhal_spec(const ProjectConfig& cfg) {
    NavhalSpec s;
    if (!cfg.navhal_git.empty()) s.git = cfg.navhal_git;
    if (!cfg.navhal_ref.empty()) s.ref = cfg.navhal_ref;
    return s;
}

// A project from before NavHAL was pinned was built against the per-ref clone
// (navhal/<ref>) its extern/NavHAL links to. Adopt that clone's commit as the
// first pin, so locking does not silently change what the project builds with.
static std::optional<ResolvedNavhal> adopt_legacy_clone(IExecutionContext& ctx, const NavhalSpec& spec) {
    if (spec.git != kNavhalRepoUrl || looks_like_commit(spec.ref)) return std::nullopt;
    const fs::path legacy = navhal_cache_dir(spec.ref);
    if (!is_installed(legacy)) return std::nullopt;
    const std::string commit = git_head_commit(ctx, legacy);
    if (commit.empty()) return std::nullopt;
    const fs::path dir = navhal_commit_dir(spec.git, commit);
    if (!is_installed(dir)) {
        const fs::path tmp = navhal_commit_dir(spec.git, ".tmp-" + unique_suffix());
        std::error_code ec;
        fs::create_directories(tmp.parent_path(), ec);
        fs::copy(legacy, tmp, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (!ec) fs::rename(tmp, dir, ec);
        if (ec) {
            std::error_code rmec;
            fs::remove_all(tmp, rmec);
            if (!is_installed(dir)) return std::nullopt;
        }
    }
    ui::info("Adopting the cached NavHAL " + spec.ref + " (" + commit.substr(0, 12) +
             ") as this project's pin; 'nav update' re-resolves it from upstream.");
    return ResolvedNavhal{dir, commit};
}

std::optional<ResolvedNavhal> resolve_navhal(IExecutionContext& ctx, const NavhalSpec& spec,
                                             const std::string& locked_commit, bool adopt_legacy) {
    if (!locked_commit.empty()) {
        const fs::path dir = navhal_commit_dir(spec.git, locked_commit);
        if (is_installed(dir)) return ResolvedNavhal{dir, locked_commit};
    } else if (adopt_legacy) {
        if (auto r = adopt_legacy_clone(ctx, spec)) return r;
    }
    auto r = fetch_navhal(ctx, spec);
    if (!r) return std::nullopt;
    if (!locked_commit.empty() && r->commit != locked_commit) {
        ui::error("A pinned git dependency no longer matches nav.lock:");
        ui::error("  navhal: " + spec.git + " @ " + spec.ref + " resolved to " + r->commit +
                  ", but nav.lock pins " + locked_commit);
        ui::error("The ref moved upstream. If the new commit is intended, run 'nav update' to");
        ui::error("re-pin it; otherwise pin a ref that still points at the locked commit.");
        return std::nullopt;
    }
    return r;
}

std::optional<ResolvedNavhal> refresh_navhal(IExecutionContext& ctx, const NavhalSpec& spec) {
    return fetch_navhal(ctx, spec);
}

bool pin_navhal(Lockfile& lock, const NavhalSpec& spec, const std::string& commit) {
    LockedNavhal want{spec.git, spec.ref, commit};
    if (lock.navhal && *lock.navhal == want) return false;
    lock.navhal = std::move(want);
    return true;
}

fs::path sync_project_navhal(IExecutionContext& ctx, const fs::path& root, const ProjectConfig& cfg,
                             bool refresh) {
    const fs::path dest = root / "extern" / "NavHAL";
    std::error_code ec;
    // A real directory is the user's own (vendored or a submodule): not ours to
    // pin or replace.
    if (fs::exists(dest, ec) && !fs::is_symlink(dest, ec)) return dest;

    const fs::path lock_path = root / "nav.lock";
    Lockfile lock;
    if (fs::exists(lock_path, ec)) {
        auto loaded = load_lockfile(lock_path);
        if (!loaded) {
            ui::error("nav.lock exists but could not be parsed; fix or remove it and retry.");
            return {};
        }
        lock = std::move(*loaded);
    }

    const NavhalSpec spec = navhal_spec(cfg);
    const bool same_spec = lock.navhal && lock.navhal->url == spec.git && lock.navhal->ref == spec.ref;
    const std::string locked = same_spec ? lock.navhal->commit : std::string{};
    auto r = refresh ? refresh_navhal(ctx, spec) : resolve_navhal(ctx, spec, locked, /*adopt_legacy=*/true);
    if (!r) return {};

    const std::optional<LockedNavhal> before = lock.navhal;
    if (pin_navhal(lock, spec, r->commit)) {
        if (!before)
            ui::info("nav.lock: pinned NavHAL " + spec.ref + " at " + r->commit.substr(0, 12));
        else if (!same_spec)
            ui::info("nav.lock: re-pinned NavHAL (" + before->url + " @ " + before->ref + " -> " +
                     spec.git + " @ " + spec.ref + ") at " + r->commit.substr(0, 12));
        else
            ui::info("nav.lock: NavHAL " + spec.ref + " " + before->commit.substr(0, 12) + " -> " +
                     r->commit.substr(0, 12));
        if (!save_lockfile(lock, lock_path)) {
            ui::error("Failed to write nav.lock.");
            return {};
        }
    }

    // extern/NavHAL must resolve to the pinned commit's checkout.
    bool linked = fs::exists(dest, ec);
    if (linked) {
        std::error_code e1, e2;
        linked = fs::equivalent(dest, r->dir, e1) && !e1 && !e2;
    }
    if (!linked) {
        if (fs::is_symlink(dest, ec) || fs::exists(dest, ec))
            ui::info("Linking extern/NavHAL to NavHAL " + r->commit.substr(0, 12));
        if (!link_navhal(ctx, r->dir, dest)) {
            ui::error("Failed to link NavHAL into the project.");
            return {};
        }
    }
    return dest;
}

std::string tag_at_commit(const std::string& ls_remote_tags, const std::string& commit) {
    // "<sha>\trefs/tags/<name>" and, for annotated tags, a peeled
    // "<sha>\trefs/tags/<name>^{}" line carrying the commit the tag points at.
    std::map<std::string, std::string> commit_of;
    std::istringstream in(ls_remote_tags);
    std::string line;
    while (std::getline(in, line)) {
        const auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string sha = line.substr(0, tab), name = line.substr(tab + 1);
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back()))) name.pop_back();
        if (name.rfind("refs/tags/", 0) != 0) continue;
        name = name.substr(10);
        const bool peeled = name.size() > 3 && name.compare(name.size() - 3, 3, "^{}") == 0;
        if (peeled) name.resize(name.size() - 3);
        if (peeled || !commit_of.count(name)) commit_of[name] = sha;
    }
    std::string best;
    std::optional<Version> best_v;
    for (const auto& [name, sha] : commit_of) {
        if (sha != commit || name.size() < 2 || name[0] != 'v') continue;
        auto v = parse_version(name.substr(1));
        if (!v) continue;
        if (!best_v || *v > *best_v) { best = name; best_v = v; }
    }
    return best;
}

std::string refresh_git_cache(IExecutionContext& ctx, const std::string& url, const std::string& ref) {
    const fs::path dir = git_cache_dir(url, ref);
    std::error_code ec;
    fs::create_directories(dir.parent_path(), ec);
    fs::path tmp = dir;
    tmp += ".tmp-" + unique_suffix();
    fs::remove_all(tmp, ec);

    ui::step("Caching", "Refreshing " + url + (ref.empty() ? "" : "@" + ref));
    std::vector<std::string> cmd = {"git", "clone", "--depth", "1"};
    if (!ref.empty()) { cmd.push_back("--branch"); cmd.push_back(ref); }
    cmd.insert(cmd.end(), {"--recurse-submodules", "--shallow-submodules", "--progress", url, tmp.string()});
    if (ctx.execute(cmd).exit_code != 0) {
        fs::remove_all(tmp, ec);
        ui::error("Failed to refresh " + url + (ref.empty() ? "" : "@" + ref) + ".");
        return {};
    }
    std::ofstream(install_marker(tmp)) << "ok\n";

    fs::path old = dir;
    old += ".old-" + unique_suffix();
    const bool had = fs::exists(dir, ec);
    if (had) fs::rename(dir, old, ec);
    fs::rename(tmp, dir, ec);
    if (ec) {
        if (had) fs::rename(old, dir, ec);
        fs::remove_all(tmp, ec);
        ui::error("Could not replace the cache entry for " + url + ".");
        return {};
    }
    if (had) fs::remove_all(old, ec);
    return git_head_commit(ctx, dir);
}

bool link_navhal(IExecutionContext& ctx, const fs::path& src, const fs::path& dest) {
    std::error_code ec;
    fs::remove_all(dest, ec); // replace whatever is there
    if (dest.has_parent_path()) {
        fs::create_directories(dest.parent_path(), ec);
    }

    const fs::path target = fs::absolute(src);

    // 1. Symlink — works on POSIX and on Windows with Developer Mode / privilege.
    std::error_code lec;
    fs::create_directory_symlink(target, dest, lec);
    if (!lec) return true;

#ifdef _WIN32
    // 2. Directory junction — no special privilege required. mklink is a cmd
    //    builtin, so it must run through cmd /c.
    auto res = ctx.execute({"cmd", "/c", "mklink", "/J", dest.string(), target.string()});
    if (res.exit_code == 0) return true;
#else
    (void)ctx;
#endif

    // 3. Copy the tree as a last resort (costs disk, but always works).
    std::error_code cec;
    fs::copy(target, dest,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks, cec);
    return !cec;
}

} // namespace nav::core
