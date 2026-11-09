#include "nav/core/config.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <iostream>
#include <regex>

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

bool parse_file_flags(const toml::node& n, const std::string& where, std::vector<FileFlagRule>& out) {
    const auto* arr = n.as_array();
    if (!arr) {
        std::cerr << "nav.toml: " << where << ".file_flags must be an array of tables ([[" << where.substr(1, where.size() - 2)
                  << ".file_flags]])\n";
        return false;
    }
    for (const auto& e : *arr) {
        const auto* t = e.as_table();
        if (!t) {
            std::cerr << "nav.toml: " << where << ".file_flags must be an array of tables\n";
            return false;
        }
        FileFlagRule r;
        if (!read_string_array(*t, "match", "[[" + where.substr(1, where.size() - 2) + ".file_flags]]", r.match) ||
            !read_string_array(*t, "flags", "[[" + where.substr(1, where.size() - 2) + ".file_flags]]", r.flags))
            return false;
        if (r.match.empty()) {
            std::cerr << "nav.toml: each [[" << where.substr(1, where.size() - 2) << ".file_flags]] needs a non-empty 'match'\n";
            return false;
        }
        out.push_back(std::move(r));
    }
    return true;
}

// The additive [build]-style keys of `t` into `a`. `extra_known` lists keys the
// caller handles itself (not an error here). Unknown keys are an error.
bool parse_additions(const toml::table& t, const std::string& where, BuildAdditions& a,
                     std::initializer_list<std::string_view> extra_known) {
    static const std::string_view kKeys[] = {"sources", "exclude", "include_dirs", "defines",
                                             "c_flags", "libs", "file_flags"};
    for (const auto& [k, v] : t) {
        (void)v;
        bool ok = false;
        for (auto n : kKeys) ok = ok || k.str() == n;
        for (auto n : extra_known) ok = ok || k.str() == n;
        if (!ok) {
            std::cerr << "nav.toml: unknown key " << where << "." << k.str()
                      << " (known: sources, exclude, include_dirs, defines, c_flags, file_flags, libs)\n";
            return false;
        }
    }
    if (!read_string_array(t, "sources", where, a.sources) || !read_string_array(t, "exclude", where, a.exclude) ||
        !read_string_array(t, "include_dirs", where, a.include_dirs) ||
        !read_string_array(t, "defines", where, a.defines) || !read_string_array(t, "c_flags", where, a.c_flags) ||
        !read_string_array(t, "libs", where, a.libs))
        return false;
    if (const toml::node* n = t.get("file_flags"))
        if (!parse_file_flags(*n, where, a.file_flags)) return false;
    return true;
}

// [<prefix>.board.<b>] and [<prefix>.kconfig.<SYM>] sub-tables of `parent`.
bool parse_conditionals(const toml::table& parent, const std::string& prefix,
                        std::map<std::string, BuildAdditions>& board,
                        std::map<std::string, BuildAdditions>& kconfig, bool* present,
                        const char* kconfig_key = "kconfig") {
    for (const char* kind : {"board", kconfig_key}) {
        if (std::string(kind) == "board" && std::string(kconfig_key) != "kconfig") continue;  // parsed once
        const toml::node* n = parent.get(kind);
        if (!n) continue;
        // A variant's `kconfig = "<file>"` is its alternative .config, not a table.
        if (!n->is_table() && std::string(kind) == "kconfig" && prefix.rfind("variant.", 0) == 0) continue;
        const std::string where = prefix + "." + kind;
        const auto* tbl = n->as_table();
        if (!tbl) {
            std::cerr << "nav.toml: [" << where << "] must hold tables ([" << where << ".<name>])\n";
            return false;
        }
        for (const auto& [name, node] : *tbl) {
            const std::string w = "[" + where + "." + std::string(name.str()) + "]";
            const auto* t = node.as_table();
            if (!t) {
                std::cerr << "nav.toml: " << w << " must be a table\n";
                return false;
            }
            BuildAdditions a;
            if (!parse_additions(*t, w, a, {})) return false;
            auto& dst = (std::string(kind) == "board" ? board : kconfig)[std::string(name.str())];
            auto cat = [](auto& d, const auto& s) { d.insert(d.end(), s.begin(), s.end()); };
            cat(dst.sources, a.sources); cat(dst.exclude, a.exclude); cat(dst.include_dirs, a.include_dirs);
            cat(dst.defines, a.defines); cat(dst.c_flags, a.c_flags); cat(dst.libs, a.libs);
            cat(dst.file_flags, a.file_flags);
            if (present) *present = true;
        }
    }
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
    if (const toml::node* n = bt->get("syscalls")) {
        auto v = n->value<std::string>();
        if (!v || (*v != "nosys" && *v != "none")) {
            std::cerr << "nav.toml: [build].syscalls must be \"nosys\" or \"none\"\n";
            return false;
        }
        b.syscalls = *v;
        any = true;
    }
    if (const toml::node* n = bt->get("deps_opt")) {
        auto v = n->value<std::string>();
        static const char* const kOpt[] = {"O0", "O1", "O2", "O3", "Os", "Og", "Oz"};
        if (!v || std::find(std::begin(kOpt), std::end(kOpt), *v) == std::end(kOpt)) {
            std::cerr << "nav.toml: [build].deps_opt must be one of O0 O1 O2 O3 Os Og Oz\n";
            return false;
        }
        b.deps_opt = *v;
        any = true;
    }
    if (const toml::node* n = bt->get("linker_fragment_position")) {
        auto v = n->value<std::string>();
        if (!v || (*v != "before" && *v != "after")) {
            std::cerr << "nav.toml: [build].linker_fragment_position must be \"before\" or \"after\"\n";
            return false;
        }
        b.linker_fragment_position = *v;
        any = true;
    }
    if (const toml::node* n = bt->get("file_flags")) {
        if (!parse_file_flags(*n, "[build]", b.file_flags)) return false;
        any = true;
    }
    {
        bool present = false;
        if (!parse_conditionals(*bt, "build", b.board, b.kconfig, &present)) return false;
        any = any || present;
    }
    b.declared = any;
    return true;
}

bool read_string(const toml::table& tbl, std::string_view key, const std::string& where, std::string& out) {
    const toml::node* n = tbl.get(key);
    if (!n) return true;
    auto v = n->value<std::string>();
    if (!v) {
        std::cerr << "nav.toml: " << where << "." << key << " must be a string\n";
        return false;
    }
    out = *v;
    return true;
}

bool parse_upload_settings(const toml::table& tbl, UploadSettings& u) {
    const toml::node* node = tbl.get("upload");
    if (!node) return true;
    const auto* ut = node->as_table();
    if (!ut) {
        std::cerr << "nav.toml: [upload] must be a table\n";
        return false;
    }
    for (const auto& [k, v] : *ut) {
        (void)v;
        if (k.str() != "probe" && k.str() != "connect_under_reset") {
            std::cerr << "nav.toml: unknown key [upload]." << k.str() << " (known: probe, connect_under_reset)\n";
            return false;
        }
    }
    if (!read_string(*ut, "probe", "[upload]", u.probe)) return false;
    if (const toml::node* n = ut->get("connect_under_reset")) {
        auto v = n->value<bool>();
        if (!v) {
            std::cerr << "nav.toml: [upload].connect_under_reset must be true or false\n";
            return false;
        }
        u.connect_under_reset = *v;
    }
    return true;
}

// [navhal] exclude = ["src/utils/freestanding.c", ...]
bool parse_navhal_exclude(const toml::table& tbl, std::vector<std::string>& out) {
    const toml::node* node = tbl.get("navhal");
    const auto* nt = node ? node->as_table() : nullptr;
    if (!nt) return true;
    if (!read_string_array(*nt, "exclude", "[navhal]", out)) return false;
    for (const auto& e : out) {
        if (e.empty()) {
            std::cerr << "nav.toml: [navhal].exclude holds an empty path\n";
            return false;
        }
    }
    return true;
}

bool parse_variants(const toml::table& tbl, std::vector<Variant>& out) {
    const toml::node* node = tbl.get("variant");
    if (!node) return true;
    const auto* vt = node->as_table();
    if (!vt) {
        std::cerr << "nav.toml: [variant] must hold tables ([variant.<name>])\n";
        return false;
    }
    for (const auto& [name_key, vnode] : *vt) {
        Variant v;
        v.name = std::string(name_key.str());
        const std::string where = "[variant." + v.name + "]";
        // The name is a directory under build/; keep it clear of cmake's own.
        static const std::regex name_re("[A-Za-z0-9][A-Za-z0-9_-]*");
        if (!std::regex_match(v.name, name_re) || v.name == "deps" || v.name == "extern" ||
            v.name == "CMakeFiles" || v.name == "Testing") {
            std::cerr << "nav.toml: " << where << ": variant names use letters, digits, '_' and '-', and may not be "
                      << "deps, extern, CMakeFiles or Testing (they become build/<name>/)\n";
            return false;
        }
        const auto* t = vnode.as_table();
        if (!t) {
            std::cerr << "nav.toml: " << where << " must be a table\n";
            return false;
        }
        if (!parse_additions(*t, where, v.add,
                             {"sources_add", "sources_remove", "output", "kconfig", "kconfig_when", "dependencies",
                              "board"}))
            return false;
        // sources_add / sources_remove are the older names for sources / exclude.
        if (!read_string_array(*t, "sources_add", where, v.sources_add) ||
            !read_string_array(*t, "sources_remove", where, v.sources_remove) ||
            !read_string(*t, "output", where, v.output))
            return false;
        v.defines = v.add.defines;
        if (const toml::node* kn = t->get("kconfig"); kn && !kn->is_table()) {
            if (!read_string(*t, "kconfig", where, v.kconfig)) return false;
        }
        if (!parse_conditionals(*t, "variant." + v.name, v.board, v.kconfig_when, nullptr, "kconfig")) return false;
        if (!parse_conditionals(*t, "variant." + v.name, v.board, v.kconfig_when, nullptr, "kconfig_when"))
            return false;
        if (const toml::node* d = t->get("dependencies")) {
            const auto* dt = d->as_table();
            if (!dt) {
                std::cerr << "nav.toml: " << where << ".dependencies must be a table\n";
                return false;
            }
            for (const auto& [lib, lnode] : *dt) {
                const std::string lwhere = where + ".dependencies." + std::string(lib.str());
                const auto* lt = lnode.as_table();
                if (!lt) {
                    std::cerr << "nav.toml: " << lwhere << " must be a table with an options array\n";
                    return false;
                }
                std::vector<std::string> opts;
                if (!read_string_array(*lt, "options", lwhere, opts)) return false;
                v.dep_options[std::string(lib.str())] = std::move(opts);
            }
        }
        out.push_back(std::move(v));
    }
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
    if (!parse_navhal_exclude(tbl, cfg.navhal_exclude)) return std::nullopt;
    cfg.navhal_submodule = tbl["library"]["navhal_submodule"].value_or(false);
    cfg.hal_api_version = static_cast<int>(tbl["abi"]["hal_api_version"].value_or<int64_t>(0));

    if (!parse_build_settings(tbl, cfg.build)) return std::nullopt;
    if (!parse_upload_settings(tbl, cfg.upload)) return std::nullopt;
    if (!parse_variants(tbl, cfg.variants)) return std::nullopt;
    if (!cfg.navhal_exclude.empty()) cfg.build.declared = true;  // needs the [build]-aware CMakeLists.txt

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
