#include "nav/core/board.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cstdlib>
#include <system_error>

#include <unistd.h>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

template <typename View>
std::vector<std::string> read_string_array(const View& node) {
    std::vector<std::string> out;
    if (auto arr = node.as_array()) {
        for (const auto& el : *arr) {
            if (auto s = el.template value<std::string>()) out.push_back(*s);
        }
    }
    return out;
}

fs::path exe_path() {
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p;
    return {};
}

void add_path_env(BoardCatalog& cat) {
    const char* env = std::getenv("NAV_BOARD_PATH");
    if (!env || env[0] == '\0') return;
    std::string s(env);
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find(':', start);
        if (end == std::string::npos) end = s.size();
        if (end > start) cat.add_search_path(fs::path(s.substr(start, end - start)));
        if (end == s.size()) break;
        start = end + 1;
    }
}

} // namespace

std::optional<Board> parse_board_file(const fs::path& path) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error&) {
        return std::nullopt;
    }

    Board b;
    b.id     = tbl["id"].value_or<std::string>("");
    b.name   = tbl["name"].value_or<std::string>("");
    b.arch   = tbl["arch"].value_or<std::string>("");
    b.vendor = tbl["vendor"].value_or<std::string>("");
    b.mcu    = tbl["mcu"].value_or<std::string>("");

    if (b.id.empty() || b.arch.empty()) {
        // Required fields missing — refuse to load.
        return std::nullopt;
    }

    b.compile_flags = read_string_array(tbl["cpu"]["compile_flags"]);
    b.link_flags    = read_string_array(tbl["cpu"]["link_flags"]);

    b.compiler      = tbl["toolchain"]["compiler"].value_or<std::string>("");
    b.flash_tool    = tbl["flashing"]["tool"].value_or<std::string>("");
    b.flash_address = tbl["flashing"]["address"].value_or<std::string>("");

    b.default_framework    = tbl["framework"]["default"].value_or<std::string>("");
    b.supported_frameworks = read_string_array(tbl["framework"]["supported"]);

    return b;
}

void BoardCatalog::add_search_path(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".toml") continue;

        auto board = parse_board_file(entry.path());
        if (!board) continue;

        const bool already_have = std::any_of(boards_.begin(), boards_.end(),
            [&](const Board& b) { return b.id == board->id; });
        if (already_have) continue;

        boards_.push_back(std::move(*board));
    }
}

std::optional<Board> BoardCatalog::find(const std::string& id) const {
    for (const auto& b : boards_) {
        if (b.id == id) return b;
    }
    return std::nullopt;
}

BoardCatalog default_catalog(const std::optional<fs::path>& project_root) {
    BoardCatalog cat;

    add_path_env(cat);

    if (project_root) {
        cat.add_search_path(*project_root / ".nav" / "boards");
    }

    if (fs::path exe = exe_path(); !exe.empty()) {
        const fs::path bin_dir = exe.parent_path();
        // Installed layout: <prefix>/bin/nav  →  <prefix>/share/nav/boards
        cat.add_search_path(bin_dir / ".." / "share" / "nav" / "boards");
        // In-tree build layout: <repo>/build/nav  →  <repo>/share/nav/boards
        cat.add_search_path(bin_dir / ".." / "share" / "nav" / "boards");
        // Out-of-tree build under <repo>/build/Release/nav etc.
        cat.add_search_path(bin_dir / ".." / ".." / "share" / "nav" / "boards");
    }

    cat.add_search_path("/usr/share/nav/boards");
    cat.add_search_path("/usr/local/share/nav/boards");

    return cat;
}

} // namespace nav::core
