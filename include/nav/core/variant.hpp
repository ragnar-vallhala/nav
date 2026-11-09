#pragma once

#include "nav/core/config.hpp"
#include "nav/core/libdeps.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nav::core {

// Build variants ([variant.<name>] in nav.toml): another image built from the
// same project, e.g. a hardware-test firmware next to the production one.
//
// A variant layers over [build]: its sources_add globs join `sources`, its
// sources_remove globs join `exclude`, its defines are appended, `output`
// renames the ELF, `kconfig` names another .config, and per-dependency
// options are merged over the library's own. It builds in build/<name>/, so
// it never overwrites the base build; `nav upload --variant <name>` flashes it.

// The variant called `name`, or nullptr.
const Variant* find_variant(const ProjectConfig& cfg, const std::string& name);

// "a, b, c" for messages; "none" when the project declares no variants.
std::string variant_names(const ProjectConfig& cfg);

// `cfg` with the variant folded into cfg.build (and its output name). Returns
// nullopt with `error` set for an unknown variant (listing the known ones).
// An empty `name` returns cfg unchanged.
std::optional<ProjectConfig> apply_variant(const ProjectConfig& cfg, const std::string& name, std::string& error);

// Directory (relative to the project root) a build lands in: "build", or
// "build/<variant>".
std::filesystem::path variant_build_dir(const std::string& variant);

// Name of the firmware ELF: the variant's `output`, else the project name.
std::string firmware_name(const ProjectConfig& cfg, const std::string& variant);

// The alternative .config for the variant (absolute), or <root>/.config.
std::filesystem::path variant_config_path(const ProjectConfig& cfg, const std::string& variant);

// `base` with `overrides` merged over it: an override replaces a base entry
// with the same KEY (text before '='), anything else is appended.
std::vector<std::string> merge_options(const std::vector<std::string>& base,
                                       const std::vector<std::string>& overrides);

// Apply the variant's per-dependency options to the resolved libraries (by
// name; transitive libraries too). A name that matches no library in the graph
// is an error: it would silently do nothing.
bool apply_variant_options(std::vector<ResolvedLib>& libs, const Variant& variant, std::string& error);

} // namespace nav::core
