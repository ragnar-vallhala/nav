#include "nav/core/serial.hpp"

#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace nav::core {

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

} // namespace nav::core
