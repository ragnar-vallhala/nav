#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <vector>

namespace nav::core {

// A single entry from nav.toml's [dependencies] table. Exactly one of `path`
// (a directory, relative to the project root or absolute) or `git` (a clone
// URL) is set. `ref` is the branch/tag/commit for git deps (empty -> default
// branch); it is ignored for path deps.
struct Dependency {
    std::string name;
    std::string path;
    std::string git;
    std::string ref;

    // CMake options passed to this dependency's build, each "KEY=VALUE" (e.g.
    // "NAVHAL=ON"). nav emits `set(KEY VALUE CACHE … FORCE)` before the
    // dependency's add_subdirectory() so option-gated libraries configure
    // correctly without a manual cmake invocation.
    std::vector<std::string> options;

    bool is_git() const { return !git.empty(); }
    bool is_path() const { return !path.empty(); }
};

struct ProjectConfig {
    std::filesystem::path root;
    std::string project_name;
    // [project].version — semver string; a library fetched by a v* tag must
    // match it (see docs/LIBRARY_ABI.md §12).
    std::string project_version;
    // "executable" (default) builds firmware; "library" builds a static .a that
    // other nav projects can depend on.
    std::string project_type = "executable";
    std::string target_arch;
    std::string target_vendor;
    std::string target_board;
    std::string build_backend;

    // Declared [dependencies] (nav library projects this one links against).
    std::vector<Dependency> dependencies;
    // [overrides] — the source to use for a library wherever it appears in the
    // dependency graph, settling a conflict between requirers. Same form as a
    // dependency. Only the project being built applies its overrides.
    std::vector<Dependency> overrides;

    // [library].navhal_submodule — a library that owns the NavHAL-ceded CPU
    // vectors (SysTick/PendSV/SVCall/HardFault), e.g. an RTOS like vaios. When a
    // firmware project depends on such a library, nav builds NavHAL with
    // -DSUBMODULE so NavHAL yields those handlers instead of emitting its own
    // (which would collide at link).
    bool navhal_submodule = false;

    // [abi].hal_api_version — the NavHAL HAL_API_VERSION this project targets.
    // 0 means undeclared (not checked).
    int hal_api_version = 0;

    bool is_library() const { return project_type == "library"; }
};

// Walk up from `start` (default: CWD) until a directory containing nav.toml is
// found. Returns nullopt if no project root is reachable.
std::optional<std::filesystem::path> find_project_root(
    std::filesystem::path start = std::filesystem::current_path());

// Parse nav.toml at the given project root. Returns nullopt on parse failure;
// missing fields default to empty strings.
std::optional<ProjectConfig> load_project_config(
    const std::filesystem::path& project_root);

} // namespace nav::core
