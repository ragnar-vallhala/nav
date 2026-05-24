#include "nav/core/toolchain.hpp"
#include <sstream>

#include <filesystem>
namespace fs = std::filesystem;

namespace nav::core {

ToolchainManager::ToolchainManager() {}

static bool find_binary_in_path(const std::string& name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return false;
    
    std::string path_str(path_env);
    std::stringstream ss(path_str);
    std::string segment;
    while (std::getline(ss, segment, ':')) {
        if (segment.empty()) continue;
        try {
            fs::path full_path = fs::path(segment) / name;
            if (fs::exists(full_path)) {
                return true;
            }
        } catch(...) {}
    }
    return false;
}

std::vector<ToolRequirement> ToolchainManager::get_system_requirements() const {
    std::vector<ToolRequirement> system_tools = {
        {"Build Orchestrator", "cmake", true},
        {"VCS Driver", "git", true}
    };

    // Dynamic Environmental Path Discovery
#ifdef __linux__
    if (find_binary_in_path("apt")) {
        system_tools.push_back({"Host Pkg Manager (APT)", "apt", false});
    } else if (find_binary_in_path("dnf")) {
        system_tools.push_back({"Host Pkg Manager (DNF)", "dnf", false});
    } else if (find_binary_in_path("pacman")) {
        system_tools.push_back({"Host Pkg Manager (PACMAN)", "pacman", false});
    }
#elif __APPLE__
    if (find_binary_in_path("brew")) {
        system_tools.push_back({"Host Pkg Manager (BREW)", "brew", false});
    }
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
    res.is_found = find_binary_in_path(req.binary_name);
    res.version_info = res.is_found ? "Detected." : "N/A";

    // If present in environment, attempt to perform metadata extraction
    if (res.is_found) {
        // Passive probe using silent execution mode
        auto call_result = ctx.execute({req.binary_name, "--version"}, "", true);

        if (call_result.exit_code == 0) {
            // Extract basic line-one version string
            std::stringstream ss(call_result.stdout_output);
            std::string first_line;
            if (std::getline(ss, first_line)) {
                 if (first_line.length() > 50) {
                     res.version_info = first_line.substr(0, 47) + "...";
                 } else if (!first_line.empty()) {
                     res.version_info = first_line;
                 }
            }
        }
    }
    
    return res;
}

} // namespace nav::core
