#pragma once

#include <filesystem>
#include <optional>
#include <map>
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

// One [[build.file_flags]] rule: extra compiler flags for the sources whose
// project-relative path matches any of the `match` globs.
struct FileFlagRule {
    std::vector<std::string> match;
    std::vector<std::string> flags;
};

// nav.toml [build] source/flag settings for an executable app. Globs are
// relative to the project root; nav expands them at `nav build` time.
struct BuildSettings {
    // True when any of the keys below is present. An app without them builds
    // exactly as it always did (src/main.c + include/), so it also works with a
    // CMakeLists.txt generated from the older template.
    bool declared = false;

    std::vector<std::string> sources{"src/main.c"};
    std::vector<std::string> exclude;
    std::vector<std::string> include_dirs{"include"};
    std::vector<std::string> defines;
    std::vector<std::string> c_flags;
    std::optional<int> c_standard;
    std::vector<FileFlagRule> file_flags;

    // App linker fragment (relative to the project root), passed as a second
    // -T after NavHAL's board script; it should use `INSERT AFTER ...;`.
    std::string linker_fragment;
    // "none" (freestanding, -nostdlib: today's behaviour) or "newlib"
    // (-lc -lm -lnosys with nosys.specs).
    std::string libc = "none";
    // Extra libraries, each linked as -l<name>.
    std::vector<std::string> libs;
};

// nav.toml [upload]: defaults for `nav upload` (command line and environment win).
struct UploadSettings {
    // Which debug probe to flash through: a USB serial or a USB port path.
    std::string probe;
    // Attach with the target held in reset (st-flash --connect-under-reset).
    bool connect_under_reset = false;
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

    BuildSettings build;
    UploadSettings upload;

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

    // [navhal] — which NavHAL this project builds against. `ref` is a tag,
    // branch or commit; `git` overrides the upstream URL. Both empty when the
    // table is absent (the NavHAL default then applies, see navhal_spec()).
    std::string navhal_ref;
    std::string navhal_git;

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
