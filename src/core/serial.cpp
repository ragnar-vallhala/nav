#include "nav/core/serial.hpp"

#include <algorithm>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace nav::core {

#ifdef _WIN32

std::vector<std::string> find_serial_ports(const fs::path& /*dev_dir*/) {
    // QueryDosDevice resolves a defined COMx symbolic link; probing COM1..COM255
    // enumerates the ports the OS currently knows about, in ascending order.
    std::vector<std::string> out;
    char target[1024];
    for (int i = 1; i <= 255; ++i) {
        std::string name = "COM" + std::to_string(i);
        if (::QueryDosDeviceA(name.c_str(), target, sizeof(target)) != 0) {
            out.push_back(std::move(name));
        }
    }
    return out;
}

#else

std::vector<std::string> find_serial_ports(const fs::path& dev_dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(dev_dir, ec)) return out;

    for (const auto& entry : fs::directory_iterator(dev_dir, ec)) {
        if (ec) break;
        std::string p = entry.path().string();
        std::string name = entry.path().filename().string();
        if (name.find("ttyACM") != std::string::npos ||
            name.find("ttyUSB") != std::string::npos) {
            out.push_back(std::move(p));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

#endif // _WIN32

} // namespace nav::core
