#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "nav/core/lockfile.hpp"

namespace nav::core {

// The cache directory a locked package occupies on this host, derived from its
// pinned version and the checksum for the applicable download key (library ->
// "source"; toolchain -> `platform_key`). Returns nullopt when the lock carries
// no checksum for that key.
std::optional<std::filesystem::path>
locked_cache_dir(const LockedPackage& lp, const std::filesystem::path& cache_root,
                 const std::string& platform_key);

// Render a `nav-deps.cmake` from the lockfile + the populated cache. Walks every
// locked package that is actually installed and emits:
//   NAV_DEPS_INCLUDE_DIRS  - <pkg>/include for each library that ships headers
//   NAV_DEPS_LINK_DIRS     - <pkg>/lib for each library that ships archives
//   NAV_DEPS_LIBRARIES     - absolute paths of each <pkg>/lib/*.a
//   NAV_DEPS_COMPILER      - <toolchain>/bin/<compiler_name>, when a toolchain
//                            package supplies the board's compiler binary
// `compiler_name` is the board's bare compiler (e.g. "arm-none-eabi-gcc"); pass
// "" for hosted targets to skip the toolchain override. Packages absent from
// the cache are silently skipped (a missing build input surfaces as a normal
// compile error, or the caller fetches first).
std::string render_deps_cmake(const Lockfile& lock,
                              const std::filesystem::path& cache_root,
                              const std::string& platform_key,
                              const std::string& compiler_name);

// Write render_deps_cmake() to `out_path`, creating parent dirs. True on success.
bool write_deps_cmake(const Lockfile& lock, const std::filesystem::path& cache_root,
                      const std::string& platform_key, const std::string& compiler_name,
                      const std::filesystem::path& out_path);

} // namespace nav::core
