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

// The additive subset of [build] keys: what [build.board.<b>],
// [build.kconfig.<SYM>] and a variant layer on top of the base [build].
struct BuildAdditions {
    std::vector<std::string> sources;
    std::vector<std::string> exclude;
    std::vector<std::string> include_dirs;
    std::vector<std::string> defines;
    std::vector<std::string> c_flags;
    std::vector<std::string> libs;
    std::vector<FileFlagRule> file_flags;
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
    // Firmware file name (set by a variant's `output`); empty = project name.
    std::string output;

    // [build.board.<board>] / [build.kconfig.<SYMBOL>]: settings applied on top
    // of the above when the project's board matches / CONFIG_<SYMBOL> is set.
    std::map<std::string, BuildAdditions> board;
    std::map<std::string, BuildAdditions> kconfig;

    // The selected variant's layer (set by apply_variant): the same shape, applied
    // after everything above, so a variant's exclude can drop what the base and
    // board tables add.
    BuildAdditions variant;
    std::map<std::string, BuildAdditions> variant_board;
    std::map<std::string, BuildAdditions> variant_kconfig;
};

// nav.toml [upload]: defaults for `nav upload` (command line and environment win).
struct UploadSettings {
    // Which debug probe to flash through: a USB serial or a USB port path.
    std::string probe;
    // Attach with the target held in reset (st-flash --connect-under-reset).
    bool connect_under_reset = false;
};

// One [variant.<name>] table: an alternative build of the same project (e.g. a
// hardware-test image) layered over [build]. See variant.hpp.
struct Variant {
    std::string name;
    std::vector<std::string> sources_add;     // globs added to [build].sources
    std::vector<std::string> sources_remove;  // globs added to [build].exclude
    std::vector<std::string> defines;         // appended to [build].defines
    std::string output;                       // ELF name; empty = project name
    std::string kconfig;                      // alternative .config; empty = .config
    // Every other [build] key, additive on top of the base ([variant.<n>] sources,
    // exclude, include_dirs, defines, c_flags, libs, file_flags). `defines`,
    // `sources_add` and `sources_remove` above are folded in as well.
    BuildAdditions add;
    std::map<std::string, BuildAdditions> board;    // [variant.<n>.board.<board>]
    std::map<std::string, BuildAdditions> kconfig_when;  // [variant.<n>.kconfig.<SYMBOL>]
    // [variant.<name>.dependencies.<lib>] options = [...]: "KEY=VALUE" entries
    // merged over the library's own options for this variant only.
    std::map<std::string, std::vector<std::string>> dep_options;
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
    std::vector<Variant> variants;  // [variant.*], in name order

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
    // [navhal].exclude — NavHAL source files (relative to the NavHAL root) to
    // drop from its library targets, e.g. one the app replaces.
    std::vector<std::string> navhal_exclude;

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
