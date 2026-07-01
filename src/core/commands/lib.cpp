#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/navconfig.hpp"
#include "nav/core/navhal.hpp"
#include "nav/core/project_name.hpp"
#include "nav/core/ui.hpp"

#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

// Heuristic: does `s` look like a git remote rather than a local path?
bool looks_like_git(const std::string& s) {
    return s.find("://") != std::string::npos
        || s.rfind("git@", 0) == 0
        || (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0);
}

// Infer a dependency name from its source when the user doesn't give one: the
// target library's own [project].name for a local path (falling back to the
// directory basename), or the repo slug for a git URL.
std::string infer_name(const fs::path& root, const std::string& source, bool is_git) {
    if (!is_git) {
        const fs::path dep = (root / source).lexically_normal();
        if (auto c = load_project_config(dep); c && !c->project_name.empty())
            return c->project_name;
    }
    std::string s = source;
    while (!s.empty() && (s.back() == '/' || s.back() == '\\')) s.pop_back();
    if (auto p = s.find_last_of("/\\"); p != std::string::npos) s = s.substr(p + 1);
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".git") == 0) s.resize(s.size() - 4);
    return s;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

std::vector<std::string> read_lines(const fs::path& p) {
    std::vector<std::string> lines;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

bool write_lines(const fs::path& p, const std::vector<std::string>& lines) {
    std::ofstream f(p, std::ios::trunc);
    if (!f) return false;
    for (const auto& l : lines) f << l << "\n";
    return f.good();
}

// Index of the active (uncommented) `[dependencies]` header, or -1 if absent.
int dependencies_header(const std::vector<std::string>& lines) {
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        if (t == "[dependencies]") return static_cast<int>(i);
    }
    return -1;
}

int lib_list(const fs::path& root) {
    auto cfg = load_project_config(root);
    if (!cfg) { ui::error("Failed to parse nav.toml."); return 1; }
    if (cfg->dependencies.empty()) {
        ui::info("No library dependencies declared. Add one with 'nav lib add <name> <path|git-url>'.");
        return 0;
    }
    ui::step("Dependencies", cfg->project_name);
    for (const auto& d : cfg->dependencies) {
        std::string opts;
        if (!d.options.empty()) {
            opts = "  [";
            for (size_t i = 0; i < d.options.size(); ++i) { if (i) opts += ", "; opts += d.options[i]; }
            opts += "]";
        }
        if (d.is_git())
            ui::info("  " + d.name + "  (git: " + d.git + (d.ref.empty() ? "" : " @ " + d.ref) + ")" + opts);
        else
            ui::info("  " + d.name + "  (path: " + d.path + ")" + opts);
    }
    return 0;
}

int lib_add(IExecutionContext& ctx, const fs::path& root, std::string name,
            const std::string& source, const std::string& ref, bool force_git,
            bool force_path, const std::vector<std::string>& options) {
    if (source.empty()) {
        ui::error("Usage: nav lib add <path|git-url> [name] [--ref <ref>]");
        return 1;
    }

    const bool is_git = force_git || (!force_path && looks_like_git(source));

    // Name is optional — infer it from the source when omitted.
    if (name.empty()) name = infer_name(root, source, is_git);
    if (name.empty()) {
        ui::error("Could not infer a dependency name from '" + source + "'. Pass one: nav lib add <name> " + source);
        return 1;
    }
    if (auto reason = validate_project_name(name); !reason.empty()) {
        ui::error("Invalid dependency name '" + name + "': " + reason);
        return 1;
    }

    if (auto cfg = load_project_config(root)) {
        for (const auto& d : cfg->dependencies) {
            if (d.name == name) {
                ui::error("Dependency '" + name + "' already exists. Remove it first with 'nav lib remove "
                          + name + "'.");
                return 1;
            }
        }
    } else {
        ui::error("Failed to parse nav.toml.");
        return 1;
    }

    // Build the inline-table entry.
    std::string entry = name + " = { ";
    if (is_git) {
        entry += "git = \"" + source + "\"";
        if (!ref.empty()) entry += ", ref = \"" + ref + "\"";
    } else {
        entry += "path = \"" + source + "\"";
    }
    if (!options.empty()) {
        entry += ", options = [";
        for (size_t i = 0; i < options.size(); ++i) {
            if (i) entry += ", ";
            entry += "\"" + options[i] + "\"";
        }
        entry += "]";
    }
    entry += " }";

    auto lines = read_lines(root / "nav.toml");
    int hdr = dependencies_header(lines);
    if (hdr >= 0) {
        lines.insert(lines.begin() + hdr + 1, entry);
    } else {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
        lines.push_back("[dependencies]");
        lines.push_back(entry);
    }
    if (!write_lines(root / "nav.toml", lines)) {
        ui::error("Failed to write nav.toml.");
        return 1;
    }

    // Resolve where the dependency lives on disk so we can union its NavHAL
    // capability requirements into the app's .config *now* — the app config is
    // the single authority and must already satisfy every dep at build time.
    // Path deps live in-tree; git deps are cloned into the shared cache on add
    // (not deferred to build) so their shipped .config is available here.
    std::error_code ec;
    fs::path dep_dir;
    if (is_git) {
        ui::step("Fetching", source + (ref.empty() ? "" : " @ " + ref));
        dep_dir = ensure_git_cached(ctx, source, ref);
        if (dep_dir.empty()) {
            ui::warning("Added, but could not fetch " + source +
                        " to union its capabilities. They'll be checked on the next 'nav build'.");
            ui::success("Added dependency '" + name + "'.");
            return 0;
        }
    } else {
        dep_dir = (root / source).lexically_normal();
    }

    if (!fs::exists(dep_dir / "nav.toml", ec)) {
        ui::warning("Added, but no nav.toml found at " + dep_dir.string() + " yet.");
    } else if (auto dcfg = load_project_config(dep_dir); dcfg && !dcfg->is_library()) {
        ui::warning("Added, but '" + source + "' is not a library project (type != \"library\").");
    }

    // Union the library's NavHAL capability requirements into the app's .config.
    // Append only what's missing — the user's existing values win (final
    // authority); differing values are surfaced as conflicts, not overwritten.
    const fs::path app_cfg = root / ".config";
    if (auto lc = find_navhal_config(dep_dir); lc && fs::exists(app_cfg, ec)) {
        const auto diff = diff_requirements(parse_kconfig(app_cfg), parse_kconfig(*lc));
        if (!diff.missing.empty()) {
            std::ofstream f(app_cfg, std::ios::app);
            f << "\n# Required by '" << name << "' (added by nav lib add)\n";
            for (const auto& a : diff.missing) f << a << "\n";
            ui::info("Unioned " + std::to_string(diff.missing.size()) +
                     " capability requirement(s) from '" + name + "' into .config.");
        }
        for (const auto& c : diff.conflicts)
            ui::warning(".config conflict: " + c.key + " (have " + c.have + ", " + name +
                        " needs " + c.need + ") — kept your value; edit if required.");
    }

    ui::success("Added dependency '" + name + "'. It will be built on the next 'nav build'.");
    return 0;
}

int lib_remove(const fs::path& root, const std::string& name) {
    if (name.empty()) { ui::error("Usage: nav lib remove <name>"); return 1; }

    auto lines = read_lines(root / "nav.toml");
    bool removed = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string t = trim(lines[i]);
        // Match "<name> =" or "<name>=" at the start of a dependency line.
        if (t.rfind(name, 0) == 0) {
            std::string rest = trim(t.substr(name.size()));
            if (!rest.empty() && rest.front() == '=') {
                lines.erase(lines.begin() + i);
                removed = true;
                break;
            }
        }
    }
    if (!removed) {
        ui::error("No dependency named '" + name + "' found in nav.toml.");
        return 1;
    }
    if (!write_lines(root / "nav.toml", lines)) {
        ui::error("Failed to write nav.toml.");
        return 1;
    }
    ui::success("Removed dependency '" + name + "'.");
    return 0;
}

} // namespace

int LibCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    auto root = find_project_root();
    if (!root) {
        ui::error("No nav.toml found in this directory or any parent.");
        return 1;
    }

    std::string ref, flag_source, flag_name;
    bool force_git = false, force_path = false;
    std::vector<std::string> options;
    std::vector<std::string> pos;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--ref" && i + 1 < args.size()) { ref = args[++i]; }
        else if (a == "--name" && i + 1 < args.size()) { flag_name = args[++i]; }
        else if (a == "--git" && i + 1 < args.size()) { flag_source = args[++i]; force_git = true; }
        else if (a == "--path" && i + 1 < args.size()) { flag_source = args[++i]; force_path = true; }
        else if ((a == "--option" || a == "-D") && i + 1 < args.size()) { options.push_back(args[++i]); }
        else pos.push_back(a);
    }
    const std::string sub = pos.empty() ? "" : pos[0];
    const std::vector<std::string> rest(pos.begin() + (pos.empty() ? 0 : 1), pos.end());

    if (sub.empty() || sub == "list") return lib_list(*root);

    if (sub == "add") {
        std::string source = flag_source;
        std::string name = flag_name;
        if (source.empty()) {
            // `add <name> <source>` or, with the name inferred, `add <source>`.
            if (rest.size() >= 2) { if (name.empty()) name = rest[0]; source = rest[1]; }
            else if (rest.size() == 1) { source = rest[0]; }
        } else if (name.empty() && !rest.empty()) {
            name = rest[0];
        }
        return lib_add(ctx, *root, name, source, ref, force_git, force_path, options);
    }

    if (sub == "remove" || sub == "rm") {
        const std::string name = !flag_name.empty() ? flag_name : (rest.empty() ? "" : rest[0]);
        return lib_remove(*root, name);
    }

    ui::error("Unknown subcommand 'lib " + sub + "'. Use: add <path|git-url> [name] [--ref r] | remove <name> | list.");
    return 1;
}

} // namespace nav::core
