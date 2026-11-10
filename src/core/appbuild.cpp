#include "nav/core/appbuild.hpp"
#include "nav/templates/embedded.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

std::string normalize(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.rfind("./", 0) == 0) s.erase(0, 2);
    return s;
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == '/') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool has_wild(const std::string& seg) {
    return seg.find_first_of("*?") != std::string::npos;
}

// '*' and '?' within a single path segment.
bool segment_match(const std::string& p, const std::string& s) {
    size_t pi = 0, si = 0, star = std::string::npos, mark = 0;
    while (si < s.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == s[si])) {
            ++pi; ++si;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = si;
        } else if (star != std::string::npos) {
            pi = star + 1;
            si = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

bool match_segments(const std::vector<std::string>& p, size_t pi,
                    const std::vector<std::string>& s, size_t si) {
    while (pi < p.size()) {
        if (p[pi] == "**") {
            while (pi < p.size() && p[pi] == "**") ++pi;
            if (pi == p.size()) return si < s.size(); // trailing ** needs >= 1 name
            for (size_t k = si; k <= s.size(); ++k)
                if (match_segments(p, pi, s, k)) return true;
            return false;
        }
        if (si >= s.size() || !segment_match(p[pi], s[si])) return false;
        ++pi; ++si;
    }
    return si == s.size();
}

// Files matching one pattern, walking only the pattern's literal directory
// prefix (so "src/**/*.c" never descends into extern/ or build/).
std::vector<std::string> expand_one(const fs::path& root, const std::string& pattern) {
    std::vector<std::string> out;
    const auto segs = split(normalize(pattern));
    if (segs.empty()) return out;

    size_t lit = 0;
    while (lit + 1 < segs.size() && !has_wild(segs[lit])) ++lit;
    fs::path base = root;
    std::string prefix;
    for (size_t i = 0; i < lit; ++i) {
        base /= segs[i];
        prefix += segs[i] + "/";
    }

    std::error_code ec;
    if (lit + 1 == segs.size() && !has_wild(segs[lit])) {
        // Plain file path.
        if (fs::is_regular_file(base / segs[lit], ec)) out.push_back(prefix + segs[lit]);
        return out;
    }
    if (!fs::is_directory(base, ec)) return out;

    // A pattern with no literal prefix walks the project root; keep it out of the
    // build tree, vendored sources (NavHAL in extern/) and dot-directories (.git).
    // A literal prefix such as extern/foo/ names such a directory on purpose.
    const bool from_root = lit == 0;
    for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (from_root && it->is_directory(fec)) {
            const std::string name = it->path().filename().generic_string();
            const bool top = it.depth() == 0;
            if ((!name.empty() && name[0] == '.') || (top && (name == "build" || name == "extern"))) {
                it.disable_recursion_pending();
                continue;
            }
        }
        if (!it->is_regular_file(fec)) continue;
        std::string rel = prefix + fs::relative(it->path(), base, fec).generic_string();
        if (glob_match(pattern, rel)) out.push_back(std::move(rel));
    }
    return out;
}

std::string abs_path(const fs::path& root, const std::string& rel) {
    return (root / rel).lexically_normal().generic_string();
}

std::string cq(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"' || c == '$') o += '\\';
        o += c;
    }
    return o + "\"";
}

void emit_list(std::ostringstream& os, const char* name, const std::vector<std::string>& v) {
    os << "set(" << name;
    for (const auto& e : v) os << "\n    " << cq(e);
    os << ")\n";
}

} // namespace

bool glob_match(const std::string& pattern, const std::string& path) {
    return match_segments(split(normalize(pattern)), 0, split(normalize(path)), 0);
}

bool kconfig_symbol_set(const std::map<std::string, std::string>& config, const std::string& symbol) {
    const std::string key = symbol.rfind("CONFIG_", 0) == 0 ? symbol : "CONFIG_" + symbol;
    auto it = config.find(key);
    return it != config.end() && it->second != "n";
}

std::optional<AppBuildPlan> plan_app_build(const ProjectConfig& cfg, std::string& error,
                                           const std::map<std::string, std::string>* config) {
    const BuildSettings& b = cfg.build;
    const fs::path root = cfg.root;

    // The base [build], then each matching [build.board.*] and [build.kconfig.*]
    // table, in that order. Everything is additive.
    struct Layer { std::string label; const BuildAdditions* add; };
    BuildAdditions base;
    base.sources = b.sources; base.exclude = b.exclude; base.include_dirs = b.include_dirs;
    base.defines = b.defines; base.c_flags = b.c_flags; base.libs = b.libs;
    base.file_flags = b.file_flags;
    std::vector<Layer> layers{{"[build]", &base}};
    auto add_layers = [&](const std::string& what, const std::map<std::string, BuildAdditions>& boards,
                          const std::map<std::string, BuildAdditions>& kconfigs) {
        if (auto it = boards.find(cfg.target_board); it != boards.end())
            layers.push_back({what + ".board." + cfg.target_board + "]", &it->second});
        for (const auto& [sym, add] : kconfigs)
            if (config && kconfig_symbol_set(*config, sym))
                layers.push_back({what + ".kconfig." + sym + "]", &add});
    };
    add_layers("[build", b.board, b.kconfig);
    // The variant, when one is selected, comes last.
    if (!b.variant.sources.empty() || !b.variant.exclude.empty() || !b.variant.include_dirs.empty() ||
        !b.variant.defines.empty() || !b.variant.c_flags.empty() || !b.variant.libs.empty() ||
        !b.variant.file_flags.empty() || !b.variant_board.empty() || !b.variant_kconfig.empty()) {
        layers.push_back({"[variant]", &b.variant});
        add_layers("[variant", b.variant_board, b.variant_kconfig);
    }

    // A layer's exclude drops sources from its own layer and every earlier one,
    // never from a later layer (so [build] can leave out a directory that a
    // board table then adds back by name).
    std::set<std::string> kept_set;
    for (size_t i = 0; i < layers.size(); ++i) {
        for (const auto& pat : layers[i].add->sources) {
            auto m = expand_one(root, pat);
            if (m.empty()) {
                error = layers[i].label + ".sources pattern '" + pat + "' matches no files";
                return std::nullopt;
            }
            for (const auto& f : m) {
                bool excluded = false;
                for (size_t j = i; j < layers.size() && !excluded; ++j)
                    excluded = std::any_of(layers[j].add->exclude.begin(), layers[j].add->exclude.end(),
                                           [&](const std::string& e) { return glob_match(e, f); });
                if (!excluded) kept_set.insert(f);
            }
        }
    }
    const std::vector<std::string> kept(kept_set.begin(), kept_set.end()); // sorted
    if (kept.empty()) {
        error = "[build].exclude removes every source file";
        return std::nullopt;
    }

    AppBuildPlan plan;
    for (const auto& f : kept) plan.sources.push_back(abs_path(root, f));
    auto append_unique = [](std::vector<std::string>& into, const std::string& v) {
        if (std::find(into.begin(), into.end(), v) == into.end()) into.push_back(v);
    };
    for (const auto& l : layers) {
        for (const auto& d : l.add->include_dirs) append_unique(plan.include_dirs, abs_path(root, normalize(d)));
        for (const auto& d : l.add->defines) append_unique(plan.defines, d);
        for (const auto& d : l.add->c_flags) plan.c_flags.push_back(d);
        for (const auto& d : l.add->libs) append_unique(plan.libs, d);
    }
    plan.c_standard = b.c_standard;
    plan.libc = b.libc;
    if (!b.syscalls.empty()) plan.syscalls = b.syscalls;
    plan.deps_opt = b.deps_opt;
    plan.linker_fragment_position = b.linker_fragment_position;
    plan.output = b.output;
    if (!b.linker_fragment.empty()) {
        std::error_code ec;
        const std::string abs_frag = abs_path(root, normalize(b.linker_fragment));
        if (!fs::is_regular_file(abs_frag, ec)) {
            error = "[build].linker_fragment '" + b.linker_fragment + "' does not exist";
            return std::nullopt;
        }
        plan.linker_fragment = abs_frag;
    }

    std::vector<FileFlagRule> rules;
    for (const auto& l : layers) rules.insert(rules.end(), l.add->file_flags.begin(), l.add->file_flags.end());
    for (const auto& rule : rules) {
        AppBuildPlan::FileFlags ff;
        ff.flags = rule.flags;
        for (const auto& f : kept) {
            if (std::any_of(rule.match.begin(), rule.match.end(),
                            [&](const std::string& m) { return glob_match(m, f); }))
                ff.files.push_back(abs_path(root, f));
        }
        if (ff.files.empty()) {
            std::string pats;
            for (const auto& m : rule.match) pats += (pats.empty() ? "'" : ", '") + m + "'";
            error = "a [[build.file_flags]] rule matching " + pats + " matches none of the build's sources";
            return std::nullopt;
        }
        plan.file_flags.push_back(std::move(ff));
    }
    return plan;
}

std::optional<std::vector<std::string>> plan_navhal_exclude(const ProjectConfig& cfg, const fs::path& navhal_root,
                                                            std::string& error) {
    std::vector<std::string> out;
    for (const auto& e : cfg.navhal_exclude) {
        const std::string rel = normalize(e);
        std::error_code ec;
        if (rel.empty() || fs::path(rel).is_absolute() || rel.find("..") != std::string::npos) {
            error = "[navhal].exclude '" + e + "' must be a path relative to the NavHAL root";
            return std::nullopt;
        }
        if (!fs::is_regular_file(navhal_root / rel, ec)) {
            error = "[navhal].exclude '" + e + "' does not exist in NavHAL (" + navhal_root.generic_string() + ")";
            return std::nullopt;
        }
        const std::string abs = abs_path(navhal_root, rel);
        if (std::find(out.begin(), out.end(), abs) == out.end()) out.push_back(abs);
    }
    return out;
}

std::string render_app_cmake(const AppBuildPlan& plan) {
    std::ostringstream os;
    os << "# Auto-generated by `nav build` from nav.toml [build].\n"
       << "# Do not edit; re-run `nav build` to regenerate.\n\n";
    emit_list(os, "NAV_APP_SOURCES", plan.sources);
    emit_list(os, "NAV_APP_INCLUDE_DIRS", plan.include_dirs);
    emit_list(os, "NAV_APP_DEFINES", plan.defines);
    emit_list(os, "NAV_APP_C_FLAGS", plan.c_flags);
    if (plan.c_standard) os << "set(NAV_APP_C_STANDARD " << *plan.c_standard << ")\n";
    else os << "set(NAV_APP_C_STANDARD)\n";
    os << "set(NAV_APP_LINKER_FRAGMENT" << (plan.linker_fragment.empty() ? "" : " " + cq(plan.linker_fragment)) << ")\n";
    os << "set(NAV_APP_OUTPUT" << (plan.output.empty() ? "" : " " + cq(plan.output)) << ")\n";
    os << "set(NAV_APP_LIBC " << cq(plan.libc) << ")\n";
    emit_list(os, "NAV_APP_LIBS", plan.libs);
    os << "set(NAV_APP_SYSCALLS " << cq(plan.syscalls) << ")\n";
    os << "set(NAV_APP_LINKER_FRAGMENT_POSITION " << cq(plan.linker_fragment_position) << ")\n";
    os << "set(NAV_APP_DEPS_OPT" << (plan.deps_opt.empty() ? "" : " " + cq(plan.deps_opt)) << ")\n";
    for (const auto& ff : plan.file_flags) {
        os << "\nset_property(SOURCE";
        for (const auto& f : ff.files) os << "\n    " << cq(f);
        os << "\n    APPEND PROPERTY COMPILE_OPTIONS";
        for (const auto& fl : ff.flags) os << "\n    " << cq(fl);
        os << ")\n";
    }
    if (!plan.navhal_exclude.empty()) {
        // NavHAL is already added when this file is included. Its sources live in
        // the `hal` library and in the `common` object library that `hal` pulls in
        // through $<TARGET_OBJECTS:common>, so filter both. Entries are compared by
        // real path: extern/NavHAL is a link to a pinned checkout.
        os << "\n";
        emit_list(os, "NAV_NAVHAL_EXCLUDE", plan.navhal_exclude);
        os << "set(_nav_hal_drop)\n"
           << "foreach(_f IN LISTS NAV_NAVHAL_EXCLUDE)\n"
           << "    get_filename_component(_r \"${_f}\" REALPATH)\n"
           << "    list(APPEND _nav_hal_drop \"${_r}\")\n"
           << "endforeach()\n"
           << "foreach(_t hal common)\n"
           << "    if(TARGET ${_t})\n"
           << "        get_target_property(_srcs ${_t} SOURCES)\n"
           << "        get_target_property(_sdir ${_t} SOURCE_DIR)\n"
           << "        set(_keep)\n"
           << "        foreach(_s IN LISTS _srcs)\n"
           << "            set(_drop FALSE)\n"
           << "            if(NOT _s MATCHES \"^\\\\$<\")\n"
           << "                get_filename_component(_rs \"${_s}\" REALPATH BASE_DIR \"${_sdir}\")\n"
           << "                if(_rs IN_LIST _nav_hal_drop)\n"
           << "                    set(_drop TRUE)\n"
           << "                endif()\n"
           << "            endif()\n"
           << "            if(NOT _drop)\n"
           << "                list(APPEND _keep \"${_s}\")\n"
           << "            endif()\n"
           << "        endforeach()\n"
           << "        set_property(TARGET ${_t} PROPERTY SOURCES \"${_keep}\")\n"
           << "    endif()\n"
           << "endforeach()\n";
    }
    return os.str();
}

bool cmakelists_supports_app_build(const std::string& t) {
    return t.find("nav-app.cmake") != std::string::npos;
}

bool cmakelists_supports_variants(const std::string& t) {
    return t.find("NAV_APP_OUTPUT") != std::string::npos && t.find("NAV_APP_CONFIG") != std::string::npos;
}

bool cmakelists_supports_app_kconfig(const std::string& t) {
    return t.find("nav-app-kconfig.cmake") != std::string::npos || t.find("NAV_APP_AUTOCONF") != std::string::npos;
}

bool cmakelists_supports_dep_opts(const std::string& t) {
    // NAV_LINK_STUBS_AFTER_GROUP marks the link line with the system libraries in
    // the dependency group and -lnosys after it; older templates mis-link syscalls.
    // NAV_ALIAS_SAFE marks the target walks that resolve ALIAS targets (an alias
    // such as navlink::navlink made set_property fail in older templates).
    return t.find("NAV_APP_LINKER_FRAGMENT_POSITION") != std::string::npos &&
           t.find("NAV_LINK_STUBS_AFTER_GROUP") != std::string::npos &&
           t.find("NAV_ALIAS_SAFE") != std::string::npos &&
           t.find("NAV_NAVHAL_CONFIG_FILE") != std::string::npos;
}

bool parse_navhal_version(const std::string& text, int& major, int& minor, int& patch) {
    auto field = [&](const char* name, int& out) {
        // `#define <name> <digits>`, or the same with a NAVHAL_ prefix.
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string hash, key, val;
            ls >> hash;
            if (hash == "#") ls >> hash; else if (hash.rfind("#", 0) == 0) hash.erase(0, 1);
            if (hash != "define") continue;
            ls >> key >> val;
            if (key != name && key != std::string("NAVHAL_") + name) continue;
            if (val.empty() || val.size() > 6 ||
                !std::all_of(val.begin(), val.end(), [](unsigned char c) { return std::isdigit(c); }))
                continue;
            out = std::stoi(val);
            return true;
        }
        return false;
    };
    return field("VERSION_MAJOR", major) && field("VERSION_MINOR", minor) && field("VERSION_PATCH", patch);
}

bool navhal_supports_config_file(const fs::path& navhal_dir) {
    std::ifstream in(navhal_dir / "include" / "navhal.h");
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    int ma = 0, mi = 0, pa = 0;
    if (!parse_navhal_version(ss.str(), ma, mi, pa)) return false;
    if (ma != 0) return ma > 0;
    return mi > 4 || (mi == 4 && pa >= 2);
}

bool cmakelists_is_nav_generated_app(const std::string& t) {
    return t.find("nav-board.cmake") != std::string::npos &&
           t.find("add_executable(${PROJECT_NAME}") != std::string::npos;
}

std::string render_app_cmakelists(const std::string& project_name) {
    std::string out(templates::kCMakeLists);
    const std::string token = "{{PROJECT_NAME}}";
    for (size_t pos = 0; (pos = out.find(token, pos)) != std::string::npos; pos += project_name.size())
        out.replace(pos, token.size(), project_name);
    return out;
}

} // namespace nav::core
