#pragma once

#include <optional>
#include <string>
#include <vector>

#include "nav/core/board.hpp"
#include "nav/core/execution_context.hpp"

namespace nav::core {

struct ToolRequirement {
    std::string name;
    std::string binary_name;
    bool is_critical;
    // Flag used to extract a version string. Defaults to "--version", which
    // works for every tool we currently probe; override per-entry if a future
    // tool uses -V / version / something else.
    std::string version_flag = "--version";
};

struct ProbeResult {
    std::string tool_name;
    bool is_found;
    std::string version_info;
    bool is_critical;
};

enum class PackageManager { Apt, Dnf, Pacman, Brew, Winget, Choco, Scoop };

// Returns the host's package manager if detected on PATH, else nullopt.
std::optional<PackageManager> detect_package_manager();

// Builds the install argv for the given manager and packages, e.g.
// {"sudo", "apt", "install", "-y", "cmake", "git"}.
std::vector<std::string> build_install_command(
    PackageManager pm, const std::vector<std::string>& packages);

// Maps a binary name to the package name for the given manager.
// Returns nullopt if no mapping is known.
std::optional<std::string> map_binary_to_package(
    PackageManager pm, const std::string& binary_name);

class ToolchainManager {
public:
    ToolchainManager();

    // Returns base system tools (cmake, git, pkgmgr)
    std::vector<ToolRequirement> get_system_requirements() const;

    // Returns toolchain requirements derived from the resolved board. The
    // compiler binary becomes a critical requirement; the flash tool (if any)
    // becomes optional. Python3 is appended as a generally-helpful optional.
    std::vector<ToolRequirement> get_project_requirements(const Board& board) const;

    // Probes a specific binary tool via provided execution context
    ProbeResult probe_tool(IExecutionContext& ctx, const ToolRequirement& req);
};

} // namespace nav::core
