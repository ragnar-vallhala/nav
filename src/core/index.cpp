#include "nav/core/index.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

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

std::vector<std::string> read_string_array(const toml::array* arr) {
    std::vector<std::string> out;
    if (!arr) return out;
    for (const auto& el : *arr) {
        if (auto s = el.value<std::string>()) out.push_back(*s);
    }
    return out;
}

// Parse `[[versions]] dependencies = { name = "^1.0.0", ... }` style block.
// Requirements must use full 3-part SemVer (parse_version_req's contract).
std::map<std::string, VersionReq> read_deps(const toml::table* tbl) {
    std::map<std::string, VersionReq> deps;
    if (!tbl) return deps;
    for (const auto& [k, v] : *tbl) {
        if (auto s = v.value<std::string>()) {
            if (auto req = parse_version_req(*s)) {
                deps.emplace(std::string(k.str()), *req);
            }
        }
    }
    return deps;
}

// Parse `[[versions.downloads]]` keyed by platform.
std::map<std::string, Download> read_downloads(const toml::table* tbl) {
    std::map<std::string, Download> out;
    if (!tbl) return out;
    for (const auto& [k, v] : *tbl) {
        if (auto sub = v.as_table()) {
            Download d;
            d.url    = (*sub)["url"].value_or<std::string>("");
            d.sha256 = (*sub)["sha256"].value_or<std::string>("");
            if (!d.url.empty()) {
                out.emplace(std::string(k.str()), d);
            }
        }
    }
    return out;
}

} // namespace

std::optional<IndexPackage> parse_index_file(const fs::path& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }

    IndexPackage pkg;
    pkg.name = tbl["name"].value_or<std::string>("");
    if (pkg.name.empty()) return std::nullopt;

    auto versions_arr = tbl["versions"].as_array();
    if (!versions_arr) return std::nullopt;

    for (const auto& el : *versions_arr) {
        const auto* vt = el.as_table();
        if (!vt) continue;

        const std::string version_str = (*vt)["version"].value_or<std::string>("");
        auto v = parse_version(version_str);
        if (!v) continue;

        IndexVersion iv;
        iv.version = *v;

        const std::string kind_str = (*vt)["kind"].value_or<std::string>("library");
        auto kind = parse_kind(kind_str);
        if (!kind) continue;
        iv.kind = *kind;

        iv.downloads = read_downloads((*vt)["downloads"].as_table());
        iv.dependencies = read_deps((*vt)["dependencies"].as_table());
        iv.toolchain_binaries = read_string_array((*vt)["toolchain_binaries"].as_array());

        pkg.versions.push_back(std::move(iv));
    }

    std::sort(pkg.versions.begin(), pkg.versions.end(),
              [](const IndexVersion& a, const IndexVersion& b) { return a.version < b.version; });

    return pkg;
}

fs::path LocalIndexClient::index_path(const std::string& package_name) const {
    if (package_name.empty()) return root_;
    std::string lowered;
    lowered.reserve(package_name.size());
    for (char c : package_name) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

    const std::size_t prefix_len = lowered.size() >= 2 ? 2 : 1;
    const std::string prefix = lowered.substr(0, prefix_len);
    return root_ / prefix / (package_name + ".toml");
}

std::optional<IndexPackage> LocalIndexClient::fetch(const std::string& package_name) {
    const fs::path p = index_path(package_name);
    std::error_code ec;
    if (!fs::exists(p, ec)) return std::nullopt;
    return parse_index_file(p);
}

} // namespace nav::core
