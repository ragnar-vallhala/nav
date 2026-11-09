#include "nav/core/appbuild.hpp"
#include "nav/templates/embedded.hpp"

#include <algorithm>
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

    for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
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

std::optional<AppBuildPlan> plan_app_build(const ProjectConfig& cfg, std::string& error) {
    const BuildSettings& b = cfg.build;
    const fs::path root = cfg.root;

    std::set<std::string> found;
    for (const auto& pat : b.sources) {
        auto m = expand_one(root, pat);
        if (m.empty()) {
            error = "[build].sources pattern '" + pat + "' matches no files";
            return std::nullopt;
        }
        found.insert(m.begin(), m.end());
    }
    std::vector<std::string> kept;
    for (const auto& f : found) {
        bool excluded = std::any_of(b.exclude.begin(), b.exclude.end(),
                                    [&](const std::string& e) { return glob_match(e, f); });
        if (!excluded) kept.push_back(f); // std::set iteration is sorted
    }
    if (kept.empty()) {
        error = "[build].exclude removes every source file";
        return std::nullopt;
    }

    AppBuildPlan plan;
    for (const auto& f : kept) plan.sources.push_back(abs_path(root, f));
    for (const auto& d : b.include_dirs) plan.include_dirs.push_back(abs_path(root, normalize(d)));
    plan.defines = b.defines;
    plan.c_flags = b.c_flags;
    plan.c_standard = b.c_standard;
    plan.libc = b.libc;
    plan.libs = b.libs;
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

    for (const auto& rule : b.file_flags) {
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
            error = "[[build.file_flags]] match " + pats + " matches none of the build's sources";
            return std::nullopt;
        }
        plan.file_flags.push_back(std::move(ff));
    }
    return plan;
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
    for (const auto& ff : plan.file_flags) {
        os << "\nset_property(SOURCE";
        for (const auto& f : ff.files) os << "\n    " << cq(f);
        os << "\n    APPEND PROPERTY COMPILE_OPTIONS";
        for (const auto& fl : ff.flags) os << "\n    " << cq(fl);
        os << ")\n";
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
