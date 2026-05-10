#include "nav/core/toolchain.hpp"
#include <sstream>

#include <filesystem>
namespace fs = std::filesystem;

namespace nav::core {

ToolchainManager::ToolchainManager() {}

std::vector<ToolRequirement> ToolchainManager::get_system_requirements() const {
    std::vector<ToolRequirement> system_tools = {
        {"Build Orchestrator", "cmake", true},
        {"VCS Driver", "git", true}
    };

    // OS-Specific Package Manager Discovery
#ifdef __linux__
    if (fs::exists("/usr/bin/apt")) {
        system_tools.push_back({"Host Pkg Manager (APT)", "apt", false});
    } else if (fs::exists("/usr/bin/dnf")) {
        system_tools.push_back({"Host Pkg Manager (DNF)", "dnf", false});
    } else if (fs::exists("/usr/bin/pacman")) {
        system_tools.push_back({"Host Pkg Manager (PACMAN)", "pacman", false});
    }
#elif __APPLE__
    system_tools.push_back({"Host Pkg Manager (BREW)", "brew", false});
#endif

    return system_tools;
}

std::vector<ToolRequirement> ToolchainManager::get_project_requirements(const std::string& board_id) const {
    std::vector<ToolRequirement> reqs;

    // Dynamic mapping based on targeted hardware family
    if (board_id.find("nucleo") != std::string::npos || board_id.find("stm32") != std::string::npos) {
        reqs.push_back({"Cross Compiler (Cortex)", "arm-none-eabi-gcc", true});
        reqs.push_back({"ST Flasher Engine", "st-flash", false});
    } else if (board_id.find("rp2040") != std::string::npos) {
        reqs.push_back({"Cross Compiler (Cortex)", "arm-none-eabi-gcc", true});
    }
    
    // Always helpful across all
    reqs.push_back({"Config Helper", "python3", false});

    return reqs;
}

ProbeResult ToolchainManager::probe_tool(IExecutionContext& ctx, const ToolRequirement& req) {
    ProbeResult res;
    res.tool_name = req.name;
    res.is_critical = req.is_critical;
    res.is_found = false;
    res.version_info = "N/A";

    // Passive probe using silent execution mode
    auto call_result = ctx.execute({req.binary_name, "--version"}, "", true);

    if (call_result.exit_code == 0) {
        res.is_found = true;
        
        // Extract basic line-one version string
        std::stringstream ss(call_result.stdout_output);
        std::string first_line;
        if (std::getline(ss, first_line)) {
             // Trim it down to reasonable length for UI table
             if (first_line.length() > 50) {
                 res.version_info = first_line.substr(0, 47) + "...";
             } else {
                 res.version_info = first_line;
             }
        }
    }
    
    return res;
}

} // namespace nav::core
