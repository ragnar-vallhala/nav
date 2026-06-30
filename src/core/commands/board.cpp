#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/provision.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

namespace nav::core {

namespace {

// The full tool set a board needs to be usable: compiler (to build) + flasher
// (to upload). Hosted targets may have neither.
std::vector<std::string> board_tools(const Board& b) {
    std::vector<std::string> t;
    if (!b.compiler.empty()) t.push_back(b.compiler);
    if (!b.flash_tool.empty()) t.push_back(b.flash_tool);
    return t;
}

// A board's toolchain is "installed" only when ALL its tools (compiler AND
// flasher) resolve on PATH — including nav-provisioned ones (main() injects).
// Otherwise a board would show "installed" yet fail to upload.
bool toolchain_installed(IExecutionContext& ctx, const Board& b) {
    ToolchainManager tm;
    for (const auto& bin : board_tools(b)) {
        if (!tm.probe_tool(ctx, ToolRequirement{bin, bin, true}).is_found) return false;
    }
    return true;
}

int print_list(IExecutionContext& ctx, bool all) {
    auto cat = default_catalog(find_project_root());
    auto boards = cat.list();
    std::sort(boards.begin(), boards.end(),
              [](const Board& a, const Board& b) { return a.id < b.id; });

    // Default view: only boards whose toolchain is installed. --all: everything.
    std::vector<Board> shown;
    for (auto& b : boards) {
        if (all || toolchain_installed(ctx, b)) shown.push_back(b);
    }

    if (shown.empty()) {
        if (all) {
            ui::warning("No boards found. Searched NAV_BOARD_PATH, exe-relative share/nav/boards.");
        } else {
            ui::info("No board toolchains installed yet.");
            ui::info("See supported boards:   nav board --all");
            ui::info("Install one's toolchain: nav board install <id>");
        }
        return 0;
    }

    std::size_t id_width = 2;
    for (const auto& b : shown) id_width = std::max(id_width, b.id.size());

    std::cout << ui::BOLD() << "ID";
    for (std::size_t i = 2; i < id_width + 2; ++i) std::cout << ' ';
    std::cout << "  NAME";
    if (all) std::cout << "                                  TOOLCHAIN";
    std::cout << ui::RESET() << "\n";

    for (const auto& b : shown) {
        std::cout << b.id;
        for (std::size_t i = b.id.size(); i < id_width + 2; ++i) std::cout << ' ';
        std::cout << "  " << b.name;
        if (all) {
            // pad NAME column to a fixed width, then status
            for (std::size_t i = b.name.size(); i < 34; ++i) std::cout << ' ';
            bool ok = toolchain_installed(ctx, b);
            std::cout << (ok ? ui::GREEN() + "installed" + ui::RESET()
                             : ui::YELLOW() + "not installed" + ui::RESET());
        }
        std::cout << "\n";
    }
    return 0;
}

void print_str_field(const std::string& label, const std::string& value) {
    if (value.empty()) return;
    std::cout << "  " << ui::BOLD() << label << ui::RESET() << ": " << value << "\n";
}

void print_array_field(const std::string& label, const std::vector<std::string>& items) {
    if (items.empty()) return;
    std::cout << "  " << ui::BOLD() << label << ui::RESET() << ":";
    for (const auto& it : items) std::cout << " " << it;
    std::cout << "\n";
}

std::optional<Board> resolve(const std::string& id) {
    if (id.empty()) return std::nullopt;
    return default_catalog(find_project_root()).find(id);
}

int print_info(const std::string& id) {
    if (id.empty()) { ui::error("Usage: nav board info <board-id>"); return 1; }
    auto board = resolve(id);
    if (!board) {
        ui::error("Unknown board '" + id + "'. Run 'nav board --all' to see supported boards.");
        return 1;
    }
    std::cout << ui::BOLD() << board->name << ui::RESET() << " (" << board->id << ")\n";
    print_str_field("arch",     board->arch);
    print_str_field("vendor",   board->vendor);
    print_str_field("mcu",      board->mcu);
    print_str_field("compiler", board->compiler);
    print_array_field("compile_flags", board->compile_flags);
    print_array_field("link_flags",    board->link_flags);
    print_str_field("flash_tool",     board->flash_tool);
    print_str_field("flash_address",  board->flash_address);
    print_str_field("default_framework", board->default_framework);
    print_array_field("supported_frameworks", board->supported_frameworks);
    return 0;
}

int board_check(IExecutionContext& ctx, const std::string& id) {
    if (id.empty()) { ui::error("Usage: nav board check <board-id>"); return 1; }
    auto board = resolve(id);
    if (!board) {
        ui::error("Unknown board '" + id + "'. Run 'nav board --all' to see supported boards.");
        return 1;
    }
    ui::step("Checking", "Toolchain for " + board->id + " (" + board->name + ")");

    ToolchainManager tm;
    int missing_critical = 0;

    // Cross compiler — required.
    if (board->compiler.empty()) {
        ui::tool_ok("Compiler", "none required (hosted target)");
    } else {
        auto res = tm.probe_tool(ctx, ToolRequirement{board->compiler, board->compiler, true});
        if (res.is_found) ui::tool_ok(board->compiler, res.version_info);
        else { ui::tool_missing_critical(board->compiler); ++missing_critical; }
    }

    // Flasher — required for `nav upload`, so it counts toward readiness.
    if (!board->flash_tool.empty()) {
        auto res = tm.probe_tool(ctx, ToolRequirement{board->flash_tool, board->flash_tool, true});
        if (res.is_found) ui::tool_ok(board->flash_tool, res.version_info);
        else { ui::tool_missing_critical(board->flash_tool); ++missing_critical; }
    }

    std::cout << "\n";
    if (missing_critical > 0) {
        ui::error("Toolchain incomplete. Run 'nav board install " + board->id + "'.");
        return 1;
    }
    ui::success("Toolchain ready for " + board->id + " (compiler + flasher).");
    return 0;
}

int board_install(IExecutionContext& ctx, const std::string& id) {
    if (id.empty()) { ui::error("Usage: nav board install <board-id>"); return 1; }
    auto board = resolve(id);
    if (!board) {
        ui::error("Unknown board '" + id + "'. Run 'nav board --all' to see supported boards.");
        return 1;
    }
    auto tools = board_tools(*board);
    if (tools.empty()) {
        ui::success("'" + board->id + "' is a hosted target — no toolchain needed.");
        return 0;
    }

    if (toolchain_installed(ctx, *board)) {
        ui::success("Toolchain for '" + board->id + "' is already installed (compiler + flasher).");
        return 0;
    }

    ui::step("Installing", "Toolchain for " + board->id + ": " + [&] {
        std::string s; for (size_t i = 0; i < tools.size(); ++i) { if (i) s += ", "; s += tools[i]; } return s; }());
    int rc = install_tools(ctx, tools);
    if (rc != 0) {
        ui::error("Could not install the full toolchain for '" + board->id + "'.");
        return rc;
    }

    if (toolchain_installed(ctx, *board)) {
        ui::success("Toolchain ready for " + board->id + " (compiler + flasher). Build with 'nav build'.");
        return 0;
    }
    ui::warning("Install completed but some tools still aren't on PATH. Open a new terminal and retry 'nav board check " + board->id + "'.");
    return 1;
}

} // namespace

int BoardCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    bool all = false;
    std::vector<std::string> pos;
    for (const auto& a : args) {
        if (a == "--all" || a == "-a") all = true;
        else pos.push_back(a);
    }

    const std::string sub = pos.empty() ? "list" : pos[0];
    const std::string id  = pos.size() > 1 ? pos[1] : "";

    if (sub == "list")    return print_list(ctx, all);
    if (sub == "info")    return print_info(id);
    if (sub == "check")   return board_check(ctx, id);
    if (sub == "install") return board_install(ctx, id);

    ui::error("Unknown subcommand 'board " + sub + "'. Use: list [--all] | info <id> | check <id> | install <id>.");
    return 1;
}

} // namespace nav::core
