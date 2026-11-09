#include "nav/core/config.hpp"

#include <toml++/toml.hpp>

#include <iostream>

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
    } catch (const toml::parse_error& e) {
        const auto& src = e.source();
        std::cerr << "nav.toml: " << toml_path.string() << ":"
                  << src.begin.line << ":" << src.begin.column
                  << ": " << e.description() << "\n";
        return std::nullopt;
    }

    ProjectConfig cfg;
    cfg.root = project_root;
    cfg.project_name  = tbl["project"]["name"].value_or<std::string>("");
    cfg.project_type  = tbl["project"]["type"].value_or<std::string>("executable");
    cfg.project_version = tbl["project"]["version"].value_or<std::string>("");
    cfg.target_arch   = tbl["target"]["arch"].value_or<std::string>("");
    cfg.target_vendor = tbl["target"]["vendor"].value_or<std::string>("");
    cfg.target_board  = tbl["target"]["board"].value_or<std::string>("");
    cfg.build_backend = tbl["build"]["backend"].value_or<std::string>("");
    cfg.navhal_submodule = tbl["library"]["navhal_submodule"].value_or(false);
    cfg.hal_api_version = static_cast<int>(tbl["abi"]["hal_api_version"].value_or<int64_t>(0));

    // [dependencies] — each value is an inline table: { path = "..." } or
    // { git = "...", ref = "..." }. A bare string value is treated as a path.
    // [overrides] uses the same form.
    auto read_deps = [](const toml::table* t, std::vector<Dependency>& into) {
        if (!t) return;
        for (const auto& [key, node] : *t) {
            Dependency dep;
            dep.name = std::string(key.str());
            if (const auto* sub = node.as_table()) {
                dep.path = (*sub)["path"].value_or<std::string>("");
                dep.git  = (*sub)["git"].value_or<std::string>("");
                dep.ref  = (*sub)["ref"].value_or<std::string>("");
                if (const auto* opts = (*sub)["options"].as_array()) {
                    for (const auto& o : *opts)
                        if (auto s = o.value<std::string>()) dep.options.push_back(*s);
                }
            } else if (const auto* str = node.as_string()) {
                dep.path = str->get();
            }
            if (dep.is_path() || dep.is_git()) into.push_back(std::move(dep));
        }
    };
    read_deps(tbl["dependencies"].as_table(), cfg.dependencies);
    read_deps(tbl["overrides"].as_table(), cfg.overrides);
    return cfg;
}

} // namespace nav::core
