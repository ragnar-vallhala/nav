#include "nav/core/config.hpp"

#include <toml++/toml.hpp>

#include <iostream>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

// Read tbl[key] (an array of strings) into `out`. Absent key leaves `out`
// alone. A wrong type is reported against `where` and returns false.
bool read_string_array(const toml::table& tbl, std::string_view key,
                       const std::string& where, std::vector<std::string>& out,
                       bool* present = nullptr) {
    const toml::node* node = tbl.get(key);
    if (!node) return true;
    if (present) *present = true;
    const auto* arr = node->as_array();
    if (!arr) {
        std::cerr << "nav.toml: " << where << "." << key << " must be an array of strings\n";
        return false;
    }
    std::vector<std::string> v;
    for (const auto& e : *arr) {
        auto s = e.value<std::string>();
        if (!s) {
            std::cerr << "nav.toml: " << where << "." << key << " must contain only strings\n";
            return false;
        }
        v.push_back(*s);
    }
    out = std::move(v);
    return true;
}

bool parse_build_settings(const toml::table& tbl, BuildSettings& b) {
    const auto* bt = tbl["build"].as_table();
    if (!bt) return true;
    bool any = false;
    for (std::string_view k : {"sources", "exclude", "include_dirs", "defines", "c_flags", "libs"}) {
        std::vector<std::string>* dst =
            k == "sources" ? &b.sources : k == "exclude" ? &b.exclude
            : k == "include_dirs" ? &b.include_dirs : k == "defines" ? &b.defines
            : k == "libs" ? &b.libs : &b.c_flags;
        bool present = false;
        if (!read_string_array(*bt, k, "[build]", *dst, &present)) return false;
        any = any || present;
    }
    if (const toml::node* n = bt->get("c_standard")) {
        auto v = n->value<int64_t>();
        if (!v || (*v != 90 && *v != 99 && *v != 11 && *v != 17 && *v != 23)) {
            std::cerr << "nav.toml: [build].c_standard must be one of 90, 99, 11, 17, 23\n";
            return false;
        }
        b.c_standard = static_cast<int>(*v);
        any = true;
    }
    if (const toml::node* n = bt->get("linker_fragment")) {
        auto v = n->value<std::string>();
        if (!v || v->empty()) {
            std::cerr << "nav.toml: [build].linker_fragment must be a non-empty path string\n";
            return false;
        }
        b.linker_fragment = *v;
        any = true;
    }
    if (const toml::node* n = bt->get("libc")) {
        auto v = n->value<std::string>();
        if (!v || (*v != "none" && *v != "newlib")) {
            std::cerr << "nav.toml: [build].libc must be \"none\" or \"newlib\"\n";
            return false;
        }
        b.libc = *v;
        any = true;
    }
    if (const toml::node* n = bt->get("file_flags")) {
        const auto* arr = n->as_array();
        if (!arr) {
            std::cerr << "nav.toml: [build].file_flags must be an array of tables ([[build.file_flags]])\n";
            return false;
        }
        for (const auto& e : *arr) {
            const auto* t = e.as_table();
            if (!t) {
                std::cerr << "nav.toml: [build].file_flags must be an array of tables ([[build.file_flags]])\n";
                return false;
            }
            FileFlagRule r;
            if (!read_string_array(*t, "match", "[[build.file_flags]]", r.match) ||
                !read_string_array(*t, "flags", "[[build.file_flags]]", r.flags))
                return false;
            if (r.match.empty()) {
                std::cerr << "nav.toml: each [[build.file_flags]] needs a non-empty 'match'\n";
                return false;
            }
            b.file_flags.push_back(std::move(r));
        }
        any = true;
    }
    b.declared = any;
    return true;
}

} // namespace

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
    cfg.navhal_ref = tbl["navhal"]["ref"].value_or<std::string>("");
    cfg.navhal_git = tbl["navhal"]["git"].value_or<std::string>("");
    cfg.navhal_submodule = tbl["library"]["navhal_submodule"].value_or(false);
    cfg.hal_api_version = static_cast<int>(tbl["abi"]["hal_api_version"].value_or<int64_t>(0));

    if (!parse_build_settings(tbl, cfg.build)) return std::nullopt;

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
