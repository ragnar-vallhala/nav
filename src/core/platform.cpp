#include "nav/core/platform.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace nav::core {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::string platform_key_from_uname(const std::string& sysname,
                                    const std::string& machine) {
    std::string os = to_lower(sysname);
    if (os.rfind("linux", 0) == 0) {
        os = "linux";
    } else if (os.rfind("darwin", 0) == 0) {
        os = "darwin";
    } else if (os.find("mingw") != std::string::npos ||
               os.find("msys") != std::string::npos ||
               os.find("cygwin") != std::string::npos ||
               os.find("windows") != std::string::npos) {
        os = "windows";
    } else if (os.empty()) {
        os = "unknown";
    }

    std::string arch = to_lower(machine);
    if (arch == "x86_64" || arch == "amd64") {
        arch = "x86_64";
    } else if (arch == "aarch64" || arch == "arm64") {
        arch = "arm64";
    } else if (arch.empty()) {
        arch = "unknown";
    }

    return os + "_" + arch;
}

std::string host_platform_key(IExecutionContext& ctx) {
#ifdef _WIN32
    // No uname on Windows; resolve the arch natively instead of shelling out.
    (void)ctx;
    SYSTEM_INFO si{};
    ::GetNativeSystemInfo(&si);
    std::string machine;
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: machine = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: machine = "arm64"; break;
        default:                           machine = "unknown"; break;
    }
    return platform_key_from_uname("windows", machine);
#else
    auto res = ctx.execute({"uname", "-s", "-m"}, "", /*silent=*/true,
                           std::chrono::seconds(5));
    if (res.exit_code != 0) {
        return "unknown_unknown";
    }

    // Expected output: "Linux x86_64\n".
    std::istringstream iss(res.output);
    std::string sysname, machine;
    iss >> sysname >> machine;
    if (sysname.empty() || machine.empty()) {
        return "unknown_unknown";
    }
    return platform_key_from_uname(sysname, machine);
#endif
}

} // namespace nav::core
