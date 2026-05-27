#include "nav/core/toolchain.hpp"

#include <chrono>
#include <filesystem>
#include <sstream>

#include <unistd.h>
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
            // access() resolves symlinks and tests the executable bit against
            // the current uid/gid, which is what we actually want.
            if (::access(full_path.c_str(), X_OK) == 0) {
                return true;
            }
        } catch(...) {}
    }
    return false;
}

std::optional<PackageManager> detect_package_manager() {
#ifdef __APPLE__
    if (find_binary_in_path("brew")) return PackageManager::Brew;
#else
    if (find_binary_in_path("apt"))    return PackageManager::Apt;
    if (find_binary_in_path("dnf"))    return PackageManager::Dnf;
    if (find_binary_in_path("pacman")) return PackageManager::Pacman;
#endif
    return std::nullopt;
}

std::vector<std::string> build_install_command(
    PackageManager pm, const std::vector<std::string>& packages) {
    std::vector<std::string> cmd;
    switch (pm) {
        case PackageManager::Apt:    cmd = {"sudo", "apt", "install", "-y"}; break;
        case PackageManager::Dnf:    cmd = {"sudo", "dnf", "install", "-y"}; break;
        case PackageManager::Pacman: cmd = {"sudo", "pacman", "-S", "--noconfirm"}; break;
        case PackageManager::Brew:   cmd = {"brew", "install"}; break;
    }
    cmd.insert(cmd.end(), packages.begin(), packages.end());
    return cmd;
}

std::optional<std::string> map_binary_to_package(
    PackageManager pm, const std::string& binary_name) {
    // Per-manager mapping. Add entries here as new tools join the toolchain.
    switch (pm) {
        case PackageManager::Apt:
            if (binary_name == "cmake")             return "cmake";
            if (binary_name == "git")               return "git";
            if (binary_name == "python3")           return "python3";
            if (binary_name == "arm-none-eabi-gcc") return "gcc-arm-none-eabi";
            if (binary_name == "st-flash")          return "stlink-tools";
            break;
        case PackageManager::Dnf:
            if (binary_name == "cmake")             return "cmake";
            if (binary_name == "git")               return "git";
            if (binary_name == "python3")           return "python3";
            if (binary_name == "arm-none-eabi-gcc") return "arm-none-eabi-gcc-cs";
            if (binary_name == "st-flash")          return "stlink";
            break;
        case PackageManager::Pacman:
            if (binary_name == "cmake")             return "cmake";
            if (binary_name == "git")               return "git";
            if (binary_name == "python3")           return "python";
            if (binary_name == "arm-none-eabi-gcc") return "arm-none-eabi-gcc";
            if (binary_name == "st-flash")          return "stlink";
            break;
        case PackageManager::Brew:
            if (binary_name == "cmake")             return "cmake";
            if (binary_name == "git")               return "git";
            if (binary_name == "python3")           return "python@3";
            if (binary_name == "arm-none-eabi-gcc") return "gcc-arm-embedded";
            if (binary_name == "st-flash")          return "stlink";
            break;
    }
    return std::nullopt;
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
        // Passive probe using silent execution mode. 10s ceiling so a hung
        // --version (interactive prompt, license check, etc.) can't wedge nav.
        auto call_result = ctx.execute({req.binary_name, req.version_flag}, "", true,
                                       std::chrono::seconds(10));

        if (call_result.exit_code == 0) {
            // Extract basic line-one version string
            std::stringstream ss(call_result.output);
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
