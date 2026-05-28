#include "nav/core/lockfile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nav::core {

std::optional<LockedPackage> Lockfile::find(const std::string& name) const {
    for (const auto& p : packages) {
        if (p.name == name) return p;
    }
    return std::nullopt;
}

std::optional<Lockfile> load_lockfile(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;

    json doc;
    try {
        f >> doc;
    } catch (const json::parse_error&) {
        return std::nullopt;
    }
    if (!doc.is_object() || !doc.contains("packages") || !doc["packages"].is_array()) {
        return std::nullopt;
    }

    Lockfile lock;
    for (const auto& el : doc["packages"]) {
        if (!el.is_object()) continue;

        LockedPackage p;
        if (!el.contains("name") || !el["name"].is_string()) continue;
        p.name = el["name"].get<std::string>();

        if (!el.contains("version") || !el["version"].is_string()) continue;
        auto v = parse_version(el["version"].get<std::string>());
        if (!v) continue;
        p.version = *v;

        const std::string kind_str = (el.contains("kind") && el["kind"].is_string())
                                       ? el["kind"].get<std::string>() : "library";
        auto kind = parse_kind(kind_str);
        if (!kind) continue;
        p.kind = *kind;

        if (el.contains("source") && el["source"].is_string()) {
            p.source = el["source"].get<std::string>();
        }

        if (el.contains("checksums") && el["checksums"].is_object()) {
            for (auto it = el["checksums"].begin(); it != el["checksums"].end(); ++it) {
                if (it.value().is_string()) p.checksums.emplace(it.key(), it.value().get<std::string>());
            }
        }

        if (el.contains("dependencies") && el["dependencies"].is_array()) {
            for (const auto& d : el["dependencies"]) {
                if (d.is_string()) p.dependencies.push_back(d.get<std::string>());
            }
        }

        lock.packages.push_back(std::move(p));
    }

    return lock;
}

std::string render_lockfile(const Lockfile& lock) {
    // Stable ordering: packages by name, dependency refs sorted.
    std::vector<LockedPackage> sorted = lock.packages;
    std::sort(sorted.begin(), sorted.end(),
              [](const LockedPackage& a, const LockedPackage& b) { return a.name < b.name; });

    json doc;
    doc["packages"] = json::array();
    for (auto& p : sorted) {
        json e;
        e["name"]    = p.name;
        e["version"] = to_string(p.version);
        e["kind"]    = to_string(p.kind);
        e["source"]  = p.source;

        json checksums = json::object();
        for (const auto& [k, v] : p.checksums) checksums[k] = v;
        e["checksums"] = checksums;

        std::vector<std::string> deps = p.dependencies;
        std::sort(deps.begin(), deps.end());
        e["dependencies"] = deps;

        doc["packages"].push_back(std::move(e));
    }
    return doc.dump(2) + "\n";
}

bool save_lockfile(const Lockfile& lock, const fs::path& path) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) return false;
    }
    std::ofstream f(path);
    if (!f) return false;
    f << render_lockfile(lock);
    return f.good();
}

} // namespace nav::core
