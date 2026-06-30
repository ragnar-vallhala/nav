#pragma once

#include "nav/core/execution_context.hpp"
#include "nav/core/registry.hpp"

#include <filesystem>
#include <vector>

namespace nav::core {

// Root for nav-managed portable toolchains: <nav_home_base>/toolchains. Tools
// nav downloads itself (no system package manager, no admin) live here so a bare
// machine can be brought to a buildable state.
std::filesystem::path toolchains_root();

std::filesystem::path tool_install_dir(const ToolDef& t); // toolchains_root()/<id>
std::filesystem::path tool_bin_dir(const ToolDef& t);     // install_dir/<bin_subdir>
bool tool_installed(const ToolDef& t);                    // extracted + marker

// Download + unpack `t` (for the host platform) into its install dir. Idempotent.
// Runs the download's post steps (alias:/pip:). Returns false on any failure or
// when the tool has no download entry for this platform.
bool provision_tool(IExecutionContext& ctx, const ToolDef& t);

// Absolute bin dirs of every currently-installed nav toolchain, for prepending
// to PATH so child processes resolve them.
std::vector<std::filesystem::path> provisioned_bin_dirs();

// Prepend provisioned_bin_dirs() to this process's PATH (and every child's).
void inject_toolchains_into_path();

// Ensure the given tool binaries are available: install via a detected system
// package manager (using the registry's per-manager package names), else via
// nav's portable downloader (the registry's per-platform download). Returns 0 if
// everything was handled, 1 if any tool has no known package/download.
int install_tools(IExecutionContext& ctx, const std::vector<std::string>& binaries);

} // namespace nav::core
