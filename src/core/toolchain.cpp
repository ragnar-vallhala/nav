#include "nav/core/toolchain.hpp"

#include "nav/core/registry.hpp"

#include <chrono>
#include <filesystem>
#include <sstream>
#include <system_error>

#ifndef _WIN32
#include <unistd.h>
#endif
namespace fs = std::filesystem;

namespace nav::core {

ToolchainManager::ToolchainManager() {}

static bool find_binary_in_path(const std::string& name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return false;

    std::string path_str(path_env);
#ifdef _WIN32
    // Windows separates PATH with ';' and has no executable bit; a command is
    // runnable if a file with one of the PATHEXT-style extensions exists.
    const char sep = ';';
    static const char* const exts[] = {"", ".exe", ".com", ".bat", ".cmd"};
#else
    const char sep = ':';
#endif
    std::stringstream ss(path_str);
    std::string segment;
    while (std::getline(ss, segment, sep)) {
        if (segment.empty()) continue;
        try {
            fs::path full_path = fs::path(segment) / name;
#ifdef _WIN32
            for (const char* ext : exts) {
                fs::path candidate = full_path;
                candidate += ext;
                std::error_code ec;
                if (fs::is_regular_file(candidate, ec)) {
                    return true;
                }
            }
#else
            // access() resolves symlinks and tests the executable bit against
            // the current uid/gid, which is what we actually want.
            if (::access(full_path.c_str(), X_OK) == 0) {
                return true;
            }
#endif
        } catch(...) {}
    }
    return false;
}

std::optional<PackageManager> detect_package_manager() {
#if defined(_WIN32)
    if (find_binary_in_path("winget")) return PackageManager::Winget;
    if (find_binary_in_path("choco"))  return PackageManager::Choco;
    if (find_binary_in_path("scoop"))  return PackageManager::Scoop;
#elif defined(__APPLE__)
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
        case PackageManager::Winget: cmd = {"winget", "install", "--accept-package-agreements",
                                            "--accept-source-agreements", "--disable-interactivity"}; break;
        case PackageManager::Choco:  cmd = {"choco", "install", "-y"}; break;
        case PackageManager::Scoop:  cmd = {"scoop", "install"}; break;
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
        case PackageManager::Winget:
            if (binary_name == "cmake")             return "Kitware.CMake";
            if (binary_name == "git")               return "Git.Git";
            if (binary_name == "ninja")             return "Ninja-build.Ninja";
            if (binary_name == "python3")           return "Python.Python.3.12";
            if (binary_name == "arm-none-eabi-gcc") return "Arm.GnuArmEmbeddedToolchain";
            break;
        case PackageManager::Choco:
            if (binary_name == "cmake")             return "cmake";
            if (binary_name == "git")               return "git";
            if (binary_name == "ninja")             return "ninja";
            if (binary_name == "python3")           return "python";
            if (binary_name == "arm-none-eabi-gcc") return "gcc-arm-embedded";
            break;
        case PackageManager::Scoop:
            if (binary_name == "cmake")             return "cmake";
            if (binary_name == "git")               return "git";
            if (binary_name == "ninja")             return "ninja";
            if (binary_name == "python3")           return "python";
            if (binary_name == "arm-none-eabi-gcc") return "gcc-arm-none-eabi";
            break;
    }
    return std::nullopt;
}

std::vector<ToolRequirement> ToolchainManager::get_system_requirements() const {
    // Core, board-independent tools come from the registry (data/toolchain.json),
    // so the list stays in sync with what `nav update` can actually provision.
    std::vector<ToolRequirement> system_tools;
    for (const auto& id : registry().core_tool_ids()) {
        if (const ToolDef* t = registry().tool(id))
            system_tools.push_back({id, t->binary, true});
    }

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

std::vector<ToolRequirement> ToolchainManager::get_project_requirements(const Board& board) const {
    std::vector<ToolRequirement> reqs;

    if (!board.compiler.empty()) {
        reqs.push_back({"Cross Compiler", board.compiler, true});
    }
    if (!board.flash_tool.empty()) {
        reqs.push_back({"Flasher", board.flash_tool, false});
    }

    // Helpful across all targets.
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
