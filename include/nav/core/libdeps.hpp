#pragma once

#include "nav/core/config.hpp"
#include "nav/core/execution_context.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nav::core {

// One library in a project's resolved dependency graph.
struct ResolvedLib {
    // Canonical name = the library's own [project].name, used as the CMake
    // target name (so add_subdirectory + target_link_libraries line up).
    std::string name;
    // Absolute source directory of the library project.
    std::filesystem::path src;
    // Canonical names of this library's own direct dependencies (graph edges).
    std::vector<std::string> deps;
    // CMake "KEY=VALUE" options to set before this library's add_subdirectory().
    std::vector<std::string> options;
};

// Resolve the full transitive set of library dependencies of the project at
// `root` (config `cfg`). Path deps resolve relative to their declaring
// project's root; git deps are cloned into the shared cache. The result is
// deduped by canonical name and ordered dependencies-first (a library precedes
// anything that links it). `out_direct`, if non-null, receives the consumer's
// direct dependency names (canonical). Returns nullopt on a hard error: missing
// path, clone failure, dependency cycle, or a dependency that is not a library.
std::optional<std::vector<ResolvedLib>>
resolve_library_deps(IExecutionContext& ctx, const std::filesystem::path& root,
                     const ProjectConfig& cfg, std::vector<std::string>* out_direct = nullptr);

// Render build/nav-deps-libs.cmake: add_subdirectory() each resolved library,
// wire inter-library link edges, and set NAV_DEP_TARGETS to the consumer's
// direct dependencies for the top-level CMake to link.
std::string render_libdeps_cmake(const std::vector<ResolvedLib>& libs,
                                 const std::vector<std::string>& direct);

// Write render_libdeps_cmake() to `out_path`, creating parent dirs. True on success.
bool write_libdeps_cmake(const std::vector<ResolvedLib>& libs,
                         const std::vector<std::string>& direct,
                         const std::filesystem::path& out_path);

} // namespace nav::core
