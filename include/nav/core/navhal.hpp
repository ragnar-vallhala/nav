#pragma once

#include "nav/core/execution_context.hpp"

#include <filesystem>
#include <string>

namespace nav::core {

// NavHAL upstream and the default ref (branch/tag) used when a project does not
// pin a specific version.
inline constexpr const char* kNavhalRepoUrl = "https://github.com/ragnar-vallhala/NavHAL.git";
inline constexpr const char* kDefaultNavhalRef = "stable";

// Ensure NavHAL <ref> exists in the shared per-user cache (see navhal_cache_dir),
// cloning it exactly once if absent and reusing it on every later call. Returns
// the absolute cache path, or an empty path on failure. Network output is shown
// only on the one-time clone; cached reuse is silent.
std::filesystem::path ensure_navhal_cached(IExecutionContext& ctx, const std::string& ref);

// Ensure an arbitrary git dependency <url>@<ref> exists in the shared cache
// (see git_cache_dir), cloning it once if absent and reusing it afterwards.
// `ref` may be empty to track the repository's default branch. Returns the
// absolute cache path, or an empty path on failure.
std::filesystem::path ensure_git_cached(IExecutionContext& ctx, const std::string& url,
                                        const std::string& ref);

// Point `dest` (e.g. <project>/extern/NavHAL) at the cached NavHAL at `src`,
// preferring a symlink, then a Windows directory junction, then a full copy as
// a last resort. Any existing `dest` is replaced. Returns false on failure.
bool link_navhal(IExecutionContext& ctx, const std::filesystem::path& src,
                 const std::filesystem::path& dest);

} // namespace nav::core
