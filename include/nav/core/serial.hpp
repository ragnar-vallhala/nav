#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nav::core {

// Scan `dev_dir` (default /dev) for serial-device candidates whose name
// contains "ttyACM" or "ttyUSB". Result is sorted ascending so callers don't
// depend on filesystem iteration order. Silently returns an empty list if
// the directory cannot be read.
std::vector<std::string> find_serial_ports(const std::filesystem::path& dev_dir = "/dev");

} // namespace nav::core
