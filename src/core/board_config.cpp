#include "nav/core/board_config.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::vector<fs::path> sorted_entries(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

void add_choice_members(const fs::path& choice_file, std::set<std::string>& into) {
    std::error_code ec;
    if (!fs::is_regular_file(choice_file, ec)) return;
    static const std::regex member(R"(^\s*config\s+(\w+))", std::regex::multiline);
    const std::string text = read_file(choice_file);
    for (std::sregex_iterator it(text.begin(), text.end(), member), end; it != end; ++it)
        into.insert("CONFIG_" + (*it)[1].str());
}

} // namespace

NavhalTargets scan_navhal_targets(const fs::path& navhal_dir) {
    NavhalTargets t;
    const fs::path src = navhal_dir / "src";
    for (const auto& arch : sorted_entries(src / "arch")) add_choice_members(arch / "Kconfig.choice", t.choice_symbols);
    for (const auto& vendor : sorted_entries(src / "vendor")) {
        add_choice_members(vendor / "Kconfig.choice", t.choice_symbols);
        for (const auto& family : sorted_entries(vendor / "family"))
            add_choice_members(family / "Kconfig.choice", t.choice_symbols);
    }
    // `config BOARD / default "<name>" if BOARD_<SYM>` names the board's symbol.
    static const std::regex identity(R"re(default\s+"([^"]+)"\s+if\s+(BOARD_\w+))re");
    for (const auto& board : sorted_entries(src / "board")) {
        add_choice_members(board / "Kconfig.choice", t.choice_symbols);
        const std::string text = read_file(board / "Kconfig");
        std::smatch m;
        if (std::regex_search(text, m, identity)) {
            const std::string sym = "CONFIG_" + m[2].str();
            t.board_symbol[m[1].str()] = sym;
            t.board_name[sym] = m[1].str();
        }
    }
    return t;
}

std::optional<fs::path> find_board_defconfig(const fs::path& navhal_dir, const std::string& board_symbol) {
    for (const auto& p : sorted_entries(navhal_dir / "cmake" / "defconfigs")) {
        if (p.extension() != ".defconfig") continue;
        const auto cfg = parse_kconfig(p);
        if (auto it = cfg.find(board_symbol); it != cfg.end() && it->second == "y") return p;
    }
    return std::nullopt;
}

std::string check_config_board(const NavhalTargets& targets, const std::string& navhal_board,
                               const std::map<std::string, std::string>& have) {
    const auto want = targets.board_symbol.find(navhal_board);
    if (want == targets.board_symbol.end())
        return "NavHAL has no board named '" + navhal_board + "' (no src/board/*/Kconfig declares it)";

    std::vector<std::string> selected;
    for (const auto& [sym, name] : targets.board_name) {
        if (auto it = have.find(sym); it != have.end() && it->second == "y") selected.push_back(name);
    }
    if (selected.size() == 1 && selected[0] == navhal_board) return "";
    if (selected.empty()) return ".config selects no board";
    std::string list;
    for (const auto& s : selected) list += (list.empty() ? "" : ", ") + s;
    return ".config selects " + list;
}

void hold_target_choices(RequirementDiff& diff, const std::set<std::string>& choice_symbols) {
    std::vector<std::string> keep;
    for (const auto& kv : diff.missing) {
        const auto key = kv.substr(0, kv.find('='));
        if (choice_symbols.count(key)) diff.conflicts.push_back({key, "(not set)", kv.substr(kv.find('=') + 1)});
        else keep.push_back(kv);
    }
    diff.missing = std::move(keep);
    std::sort(diff.conflicts.begin(), diff.conflicts.end(),
              [](const auto& a, const auto& b) { return a.key < b.key; });
}

std::string seed_board_config(IExecutionContext& ctx, const fs::path& navhal_dir, const std::string& navhal_board,
                              std::string_view starter, const fs::path& out, const std::string& python) {
    const auto targets = scan_navhal_targets(navhal_dir);
    const auto sym = targets.board_symbol.find(navhal_board);
    if (sym == targets.board_symbol.end())
        return "NavHAL has no board named '" + navhal_board + "'";
    const auto defconfig = find_board_defconfig(navhal_dir, sym->second);
    if (!defconfig)
        return "NavHAL ships no defconfig selecting " + sym->second + " (looked in cmake/defconfigs/)";

    {
        std::ofstream f(out, std::ios::trunc);
        if (!f) return "cannot write " + out.string();
        f << read_file(*defconfig) << "\n# nav starter set\n" << starter;
        if (!f.good()) return "cannot write " + out.string();
    }
    if (python.empty()) return "";

    // Same expansion as `make <board>_defconfig`: load the fragment, let Kconfig
    // fill every default, write the full file back.
    static const char* kExpand =
        "import os, sys\n"
        "os.environ['srctree'] = sys.argv[3]\n"
        "import kconfiglib\n"
        "k = kconfiglib.Kconfig(sys.argv[1], warn=False)\n"
        "k.load_config(sys.argv[2])\n"
        "k.write_config(sys.argv[2], header=sys.argv[4], save_old=False)\n";
    const std::string header = "# Seeded by nav for board " + navhal_board + " from " +
                               defconfig->filename().string() + ".\n# Edit with 'nav config'.\n";
    const fs::path abs_navhal = fs::absolute(navhal_dir);
    auto r = ctx.execute({python, "-c", kExpand, (abs_navhal / "Kconfig").string(), fs::absolute(out).string(),
                          abs_navhal.string(), header},
                         abs_navhal.string(), /*silent=*/true);
    if (r.exit_code != 0) return "Kconfig could not expand the board config: " + r.output;

    if (auto why = check_config_board(targets, navhal_board, parse_kconfig(out)); !why.empty())
        return "the expanded config does not select " + navhal_board + " (" + why + ")";
    return "";
}

std::string retarget_nav_toml(const std::string& text, const Board& board) {
    const std::vector<std::pair<std::string, std::string>> want = {
        {"arch", board.arch}, {"vendor", board.vendor}, {"board", board.id}};
    auto entry = [](const std::string& k, const std::string& v) { return k + " = \"" + v + "\""; };

    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) lines.push_back(l);
    }
    auto trimmed = [](const std::string& l) {
        const auto b = l.find_first_not_of(" \t\r");
        return b == std::string::npos ? std::string{} : l.substr(b, l.find_last_not_of(" \t\r") - b + 1);
    };

    size_t header = lines.size();
    for (size_t i = 0; i < lines.size(); ++i)
        if (trimmed(lines[i]) == "[target]") { header = i; break; }

    if (header == lines.size()) {
        if (!lines.empty() && !trimmed(lines.back()).empty()) lines.push_back("");
        lines.push_back("[target]");
        for (const auto& [k, v] : want) lines.push_back(entry(k, v));
    } else {
        std::set<std::string> seen;
        size_t end = header + 1;
        static const std::regex key_line(R"(^\s*([A-Za-z_]+)\s*=)");
        for (; end < lines.size(); ++end) {
            const std::string t = trimmed(lines[end]);
            if (!t.empty() && t[0] == '[') break;
            std::smatch m;
            if (!std::regex_search(lines[end], m, key_line)) continue;
            for (const auto& [k, v] : want)
                if (m[1].str() == k) { lines[end] = entry(k, v); seen.insert(k); }
        }
        std::vector<std::string> add;
        for (const auto& [k, v] : want)
            if (!seen.count(k)) add.push_back(entry(k, v));
        lines.insert(lines.begin() + static_cast<long>(header) + 1, add.begin(), add.end());
    }

    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

} // namespace nav::core
