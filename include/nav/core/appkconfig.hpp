#pragma once

#include "nav/core/config.hpp"
#include "nav/core/execution_context.hpp"
#include "nav/core/libdeps.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace nav::core {

// App-level Kconfig.
//
// An app may ship a `Kconfig` at its project root defining its own symbols (for
// example feature switches). When it does, nav builds ONE merged Kconfig tree
// -- NavHAL's, every dependency's root Kconfig, and the app's -- and validates
// the app's single `.config` against it with kconfiglib. The app's symbols
// become `build/app_autoconf.h` (force-included into the app's sources) and
// CMake variables; dependency symbols set in the same `.config` reach the
// dependency through the resolved `build/nav.config`.
//
// An app without a root Kconfig takes none of these paths and builds exactly as
// before.
//
// Symbol names are the app's own business. No prefix is enforced; `APP_` or the
// project name keeps them from colliding with NavHAL's and a dependency's, and
// a symbol defined twice is a kconfiglib duplicate-definition error.

// One symbol the app's Kconfig defines, as resolved against the .config.
struct AppSymbol {
    std::string name;   // without the CONFIG_ prefix
    std::string type;   // "bool", "int", "hex" or "string"
    std::string value;  // bool: "y"/"n"; others: the value as kconfiglib prints it
};

// A dependency that ships a root Kconfig.
struct KconfigDep {
    std::string name;
    std::filesystem::path kconfig;
};

// `<root>/Kconfig` exists: the opt-in for everything in this header.
bool app_has_kconfig(const std::filesystem::path& root);

// Text of build/nav-Kconfig: sources NavHAL's Kconfig, each dependency's, and
// the app's (inside an "Application options" menu, which is how the symbols
// that belong to the app are told apart from the rest). Paths are written
// absolute with forward slashes.
std::string render_merged_kconfig(const std::string& project_name,
                                  const std::filesystem::path& navhal_kconfig,
                                  const std::vector<KconfigDep>& deps,
                                  const std::filesystem::path& app_kconfig);

// Text of app_autoconf.h. bool y -> `#define CONFIG_X 1`; an unset bool is
// absent (a comment only); int/hex are emitted as written; strings are quoted
// and escaped.
std::string render_app_autoconf(const std::vector<AppSymbol>& syms);

// Text of nav-app-kconfig.cmake: NAV_APP_AUTOCONF (the header's path) plus a
// CMake variable CONFIG_<X> per symbol (bool as ON/OFF).
std::string render_app_cmake(const std::vector<AppSymbol>& syms,
                             const std::filesystem::path& autoconf);

// The python (kconfiglib) validator nav runs; exposed for tests.
std::string kconfig_validator_script();

// `build_dir` (default <root>/build) and `config` (default <root>/.config) let a
// build variant keep its own output directory and .config.
// Write build/nav-Kconfig for the project. Used by `nav config` (menuconfig over
// the merged tree) and by prepare_app_kconfig(). Returns its path, or empty.
std::filesystem::path write_merged_kconfig(const std::filesystem::path& root,
                                           const ProjectConfig& cfg,
                                           const std::vector<ResolvedLib>& libs,
                                           const std::filesystem::path& navhal_dir,
                                           const std::filesystem::path& build_dir = {});

// Merge, validate and generate: writes build/nav-Kconfig, runs kconfiglib on the
// app's .config, and writes build/nav.config (the fully resolved config handed
// to dependencies), build/app_autoconf.h and build/nav-app-kconfig.cmake.
// An unknown symbol or invalid value in .config is an error, reported with its
// name in `*error`; anything else kconfiglib says is passed to `*warnings`.
bool prepare_app_kconfig(IExecutionContext& ctx,
                         const std::filesystem::path& root,
                         const ProjectConfig& cfg,
                         const std::vector<ResolvedLib>& libs,
                         const std::filesystem::path& navhal_dir,
                         std::string* error,
                         std::vector<std::string>* warnings = nullptr,
                         const std::string& python = "python3",
                         const std::filesystem::path& build_dir = {},
                         const std::filesystem::path& config = {});

} // namespace nav::core
