#include "nav/core/navconfig.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Symbol a .config line assigns ("CONFIG_X=v" or "# CONFIG_X is not set"), or "".
std::string line_symbol(const std::string& line) {
    const std::string t = trim(line);
    if (t.rfind("# CONFIG_", 0) == 0) {
        const auto e = t.find(" is not set");
        if (e == std::string::npos) return "";
        return trim(t.substr(2, e - 2));
    }
    if (t.rfind("CONFIG_", 0) == 0) {
        const auto eq = t.find('=');
        if (eq == std::string::npos) return "";
        return trim(t.substr(0, eq));
    }
    return "";
}

} // namespace

std::string apply_config_assignments(const std::string& text,
                                     const std::vector<std::string>& assignments,
                                     const std::string& header) {
    std::map<std::string, std::string> want; // last assignment per symbol wins
    std::vector<std::string> order;
    for (const auto& a : assignments) {
        const std::string sym = line_symbol(a);
        if (sym.empty()) continue;
        if (!want.count(sym)) order.push_back(sym);
        want[sym] = trim(a);
    }
    std::set<std::string> done;
    std::ostringstream out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string sym = line_symbol(line);
        auto it = sym.empty() ? want.end() : want.find(sym);
        if (it == want.end()) { out << line << "\n"; continue; }
        if (done.insert(sym).second) out << it->second << "\n";
    }
    bool first = true;
    for (const auto& sym : order) {
        if (done.count(sym)) continue;
        if (first) {
            out << "\n";
            if (!header.empty()) out << header << "\n";
            first = false;
        }
        out << want[sym] << "\n";
    }
    return out.str();
}

bool update_config_file(const fs::path& path, const std::vector<std::string>& assignments,
                        const std::string& header) {
    std::string old;
    {
        std::ifstream in(path);
        std::ostringstream ss;
        ss << in.rdbuf();
        old = ss.str();
    }
    const std::string next = apply_config_assignments(old, assignments, header);
    if (next == old) return true;
    std::ofstream f(path, std::ios::trunc);
    f << next;
    return static_cast<bool>(f);
}

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

RequirementDiff diff_requirements(
    const std::map<std::string, std::string>& have,
    const std::map<std::string, std::string>& required) {
    RequirementDiff d;
    // `required` is a std::map, so iteration is already key-sorted — both output
    // lists inherit that ordering without a further sort.
    for (const auto& [key, val] : required) {
        auto it = have.find(key);
        if (it == have.end()) d.missing.push_back(key + "=" + val);
        else if (it->second != val) d.conflicts.push_back({key, it->second, val});
    }
    return d;
}

} // namespace nav::core
