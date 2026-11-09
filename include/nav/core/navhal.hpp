#pragma once

#include "nav/core/config.hpp"
#include "nav/core/execution_context.hpp"
#include "nav/core/lockfile.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace nav::core {

// NavHAL upstream and the default ref (branch/tag) used when a project does not
// pin a specific version.
inline constexpr const char* kNavhalRepoUrl = "https://github.com/ragnar-vallhala/NavHAL.git";
inline constexpr const char* kDefaultNavhalRef = "stable";

// Which NavHAL a project builds against: nav.toml [navhal] git/ref, with the
// defaults above for whatever is absent.
struct NavhalSpec {
    std::string git = kNavhalRepoUrl;
    std::string ref = kDefaultNavhalRef;
    bool operator==(const NavhalSpec&) const = default;
};
NavhalSpec navhal_spec(const ProjectConfig& cfg);

// A NavHAL checkout at one exact commit (under navhal_commit_dir) and that commit.
struct ResolvedNavhal {
    std::filesystem::path dir;
    std::string commit;
};

// Resolve `spec` to an immutable per-commit checkout in the shared cache.
// `locked_commit` is the commit nav.lock pins for this spec, or empty. With a
// pin whose checkout is cached nothing touches the network; otherwise the ref
// is fetched, and if it no longer resolves to the pinned commit that is an
// error (nullopt, reported). Without a pin the ref is fetched and whatever it
// resolves to is returned for the caller to record. With `adopt_legacy`, an
// unpinned project first adopts the older per-ref cache clone (navhal/<ref>,
// what projects built before pinning linked to) instead of fetching, so its
// first pin is the NavHAL it already builds with. nullopt on any failure.
std::optional<ResolvedNavhal> resolve_navhal(IExecutionContext& ctx, const NavhalSpec& spec,
                                             const std::string& locked_commit,
                                             bool adopt_legacy = false);

// Re-resolve `spec` from upstream regardless of any pin (`nav update`).
std::optional<ResolvedNavhal> refresh_navhal(IExecutionContext& ctx, const NavhalSpec& spec);

// Bring the project at `root` in line with [navhal] and nav.lock: resolve (or,
// with `refresh`, re-resolve) NavHAL, record the pin in nav.lock when it is new
// or changed, and point extern/NavHAL at the checkout. An extern/NavHAL that is
// a real directory (vendored by the user) is left alone. Returns the NavHAL
// directory, or an empty path on failure (reported).
std::filesystem::path sync_project_navhal(IExecutionContext& ctx, const std::filesystem::path& root,
                                          const ProjectConfig& cfg, bool refresh = false);

// Set lock.navhal to {spec, commit}. Returns true if that changed the lock.
bool pin_navhal(Lockfile& lock, const NavhalSpec& spec, const std::string& commit);

// The newest version tag ("vX.Y...") in `ls_remote_tags` output (from
// `git ls-remote --tags`) whose commit is `commit`; empty if none.
std::string tag_at_commit(const std::string& ls_remote_tags, const std::string& commit);

// True for a git URL or ref that starts with '-': passed to git it would be read
// as an option. nav refuses such values (and also puts "--" before URLs).
bool is_unsafe_git_arg(const std::string& s);

// Re-fetch a git dependency's shared cache entry <url>@<ref> from upstream and
// swap it in (a branch ref otherwise never moves once cached). Returns the
// commit now cached, or empty on failure.
std::string refresh_git_cache(IExecutionContext& ctx, const std::string& url, const std::string& ref);

// Ensure NavHAL <ref> exists in the shared per-user cache (see navhal_cache_dir),
// cloning it exactly once if absent and reusing it on every later call. Returns
// the absolute cache path, or an empty path on failure. Network output is shown
// only on the one-time clone; cached reuse is silent.
std::filesystem::path ensure_navhal_cached(IExecutionContext& ctx, const std::string& ref);

// Ensure an arbitrary git dependency <url>@<ref> exists in the shared cache
// (see git_cache_dir), cloning it once if absent and reusing it afterwards.
// `ref` may be empty to track the repository's default branch. Submodules are
// fetched too (shallow). Returns the absolute cache path, or an empty path on
// failure.
std::filesystem::path ensure_git_cached(IExecutionContext& ctx, const std::string& url,
                                        const std::string& ref);

// The commit checked out in the git repository at `repo` (`git rev-parse HEAD`),
// or an empty string if it cannot be determined.
std::string git_head_commit(IExecutionContext& ctx, const std::filesystem::path& repo);

// HAL_API_VERSION as #defined in <navhal_dir>/include/navhal.h, or nullopt if
// the header or the define is missing.
std::optional<int> read_hal_api_version(const std::filesystem::path& navhal_dir);

// Point `dest` (e.g. <project>/extern/NavHAL) at the cached NavHAL at `src`,
// preferring a symlink, then a Windows directory junction, then a full copy as
// a last resort. Any existing `dest` is replaced. Returns false on failure.
bool link_navhal(IExecutionContext& ctx, const std::filesystem::path& src,
                 const std::filesystem::path& dest);

} // namespace nav::core
