#pragma once

#include <string>
#include <vector>
#include "nav/core/execution_context.hpp"

namespace nav::core {

struct ToolRequirement {
    std::string name;
    std::string binary_name;
    bool is_critical;
};

struct ProbeResult {
    std::string tool_name;
    bool is_found;
    std::string version_info;
    bool is_critical;
};

class ToolchainManager {
public:
    ToolchainManager();

    // Returns base system tools (cmake, git, pkgmgr)
    std::vector<ToolRequirement> get_system_requirements() const;

    // Returns list mapping dynamically to a target board ID
    std::vector<ToolRequirement> get_project_requirements(const std::string& board_id) const;

    // Probes a specific binary tool via provided execution context
    ProbeResult probe_tool(IExecutionContext& ctx, const ToolRequirement& req);
};

} // namespace nav::core
