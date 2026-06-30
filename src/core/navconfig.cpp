#include "nav/core/navconfig.hpp"

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

std::map<std::string, std::string> parse_kconfig(const fs::path& path) {
    std::map<std::string, std::string> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue; // blank / comment / "is not set"
        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(t.substr(0, eq));
        const std::string val = trim(t.substr(eq + 1));
        if (!key.empty()) out[key] = val;
    }
    return out;
}

std::optional<fs::path> find_navhal_config(const fs::path& root) {
    std::error_code ec;
    for (const char* name : {".config", "navhal.config"}) {
        const fs::path p = root / name;
        if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) return p;
    }
    return std::nullopt;
}

std::vector<std::string> unmet_requirements(
    const std::map<std::string, std::string>& have,
    const std::map<std::string, std::string>& required) {
    std::vector<std::string> unmet;
    for (const auto& [key, val] : required) {
        auto it = have.find(key);
        if (it == have.end() || it->second != val)
            unmet.push_back(key + "=" + val);
    }
    std::sort(unmet.begin(), unmet.end());
    return unmet;
}

} // namespace nav::core
