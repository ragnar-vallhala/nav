#include "nav/core/navhal.hpp"

#include "nav/core/cache.hpp"
#include "nav/core/ui.hpp"

#include <chrono>
#include <fstream>
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
// heavyweight datasheets dir. Returns the cache dir or {} on failure.
fs::path clone_into_cache(IExecutionContext& ctx, const std::string& url,
                          const std::string& ref, const fs::path& dir,
                          const std::string& label, bool trim_datasheets) {
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
                            "NavHAL " + ref, /*trim_datasheets=*/true);
}

fs::path ensure_git_cached(IExecutionContext& ctx, const std::string& url,
                           const std::string& ref) {
    const std::string label = url + (ref.empty() ? "" : ("@" + ref));
    return clone_into_cache(ctx, url, ref, git_cache_dir(url, ref), label,
                            /*trim_datasheets=*/false);
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
