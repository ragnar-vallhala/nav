#include "nav/core/config.hpp"

#include <toml++/toml.hpp>

namespace fs = std::filesystem;

namespace nav::core {

std::optional<fs::path> find_project_root(fs::path start) {
    std::error_code ec;
    fs::path cur = fs::absolute(start, ec);
    if (ec) return std::nullopt;

    while (true) {
        if (fs::exists(cur / "nav.toml", ec)) {
            return cur;
        }
        if (!cur.has_parent_path() || cur.parent_path() == cur) {
            return std::nullopt;
        }
        cur = cur.parent_path();
    }
}

std::optional<ProjectConfig> load_project_config(const fs::path& project_root) {
    const fs::path toml_path = project_root / "nav.toml";
    toml::table tbl;
    try {
        tbl = toml::parse_file(toml_path.string());
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }

    ProjectConfig cfg;
    cfg.root = project_root;
    cfg.project_name  = tbl["project"]["name"].value_or<std::string>("");
    cfg.target_arch   = tbl["target"]["arch"].value_or<std::string>("");
    cfg.target_vendor = tbl["target"]["vendor"].value_or<std::string>("");
    cfg.target_board  = tbl["target"]["board"].value_or<std::string>("");
    cfg.build_backend = tbl["build"]["backend"].value_or<std::string>("");
    return cfg;
}

} // namespace nav::core
