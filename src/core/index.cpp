#include "nav/core/index.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nav::core {

std::optional<PackageKind> parse_kind(const std::string& s) {
    if (s == "library")   return PackageKind::Library;
    if (s == "toolchain") return PackageKind::Toolchain;
    return std::nullopt;
}

std::string to_string(PackageKind k) {
    return k == PackageKind::Toolchain ? "toolchain" : "library";
}

namespace {

std::vector<std::string> read_string_array(const json& node) {
    std::vector<std::string> out;
    if (!node.is_array()) return out;
    for (const auto& el : node) {
        if (el.is_string()) out.push_back(el.get<std::string>());
    }
    return out;
}

std::map<std::string, VersionReq> read_deps(const json& node) {
    std::map<std::string, VersionReq> deps;
    if (!node.is_object()) return deps;
    for (auto it = node.begin(); it != node.end(); ++it) {
        if (!it.value().is_string()) continue;
        if (auto req = parse_version_req(it.value().get<std::string>())) {
            deps.emplace(it.key(), *req);
        }
    }
    return deps;
}

std::map<std::string, Download> read_downloads(const json& node) {
    std::map<std::string, Download> out;
    if (!node.is_object()) return out;
    for (auto it = node.begin(); it != node.end(); ++it) {
        const auto& v = it.value();
        if (!v.is_object()) continue;
        Download d;
        if (v.contains("url")    && v["url"].is_string())    d.url    = v["url"].get<std::string>();
        if (v.contains("sha256") && v["sha256"].is_string()) d.sha256 = v["sha256"].get<std::string>();
        if (!d.url.empty()) out.emplace(it.key(), d);
    }
    return out;
}

} // namespace

std::optional<IndexPackage> parse_index_string(const std::string& json_text) {
    json doc;
    try {
        doc = json::parse(json_text);
    } catch (const json::parse_error&) {
        return std::nullopt;
    }

    if (!doc.is_object()) return std::nullopt;

    IndexPackage pkg;
    if (!doc.contains("name") || !doc["name"].is_string()) return std::nullopt;
    pkg.name = doc["name"].get<std::string>();
    if (pkg.name.empty()) return std::nullopt;

    if (!doc.contains("versions") || !doc["versions"].is_array()) return std::nullopt;

    for (const auto& el : doc["versions"]) {
        if (!el.is_object()) continue;

        if (!el.contains("version") || !el["version"].is_string()) continue;
        auto v = parse_version(el["version"].get<std::string>());
        if (!v) continue;

        IndexVersion iv;
        iv.version = *v;

        const std::string kind_str = (el.contains("kind") && el["kind"].is_string())
                                       ? el["kind"].get<std::string>()
                                       : "library";
        auto kind = parse_kind(kind_str);
        if (!kind) continue;
        iv.kind = *kind;

        if (el.contains("downloads"))          iv.downloads          = read_downloads(el["downloads"]);
        if (el.contains("dependencies"))       iv.dependencies       = read_deps(el["dependencies"]);
        if (el.contains("toolchain_binaries")) iv.toolchain_binaries = read_string_array(el["toolchain_binaries"]);

        pkg.versions.push_back(std::move(iv));
    }

    std::sort(pkg.versions.begin(), pkg.versions.end(),
              [](const IndexVersion& a, const IndexVersion& b) { return a.version < b.version; });

    return pkg;
}

std::optional<IndexPackage> parse_index_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;
    std::string contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse_index_string(contents);
}

fs::path LocalIndexClient::index_path(const std::string& package_name) const {
    if (package_name.empty()) return root_;
    std::string lowered;
    lowered.reserve(package_name.size());
    for (char c : package_name) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    const std::size_t prefix_len = lowered.size() >= 2 ? 2 : 1;
    const std::string prefix = lowered.substr(0, prefix_len);
    return root_ / prefix / (package_name + ".json");
}

std::optional<IndexPackage> LocalIndexClient::fetch(const std::string& package_name) {
    const fs::path p = index_path(package_name);
    std::error_code ec;
    if (!fs::exists(p, ec)) return std::nullopt;
    return parse_index_file(p);
}

} // namespace nav::core
