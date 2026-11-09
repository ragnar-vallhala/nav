#include "nav/core/board.hpp"
#include "nav/core/board_config.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/kconfig_python.hpp"
#include "nav/core/navhal.hpp"
#include "nav/templates/embedded.hpp"
#include "nav/core/provision.hpp"
#include "nav/core/registry.hpp"
#include "nav/core/runner.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace nav::core {

namespace {

// The full tool set a board needs to be usable: compiler (to build) + flasher
// (to upload). Hosted targets may have neither.
std::vector<std::string> board_tools(const Board& b) {
    std::vector<std::string> t;
    if (!b.compiler.empty()) t.push_back(b.compiler);
    if (auto flasher = default_flash_binary(b, registry()); !flasher.empty()) t.push_back(flasher);
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
    print_array_field("runners",      board_runner_names(*board));
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
    if (const auto flasher = default_flash_binary(*board, registry()); !flasher.empty()) {
        auto res = tm.probe_tool(ctx, ToolRequirement{flasher, flasher, true});
        if (res.is_found) ui::tool_ok(flasher, res.version_info);
        else { ui::tool_missing_critical(flasher); ++missing_critical; }
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

// Point the project at another board: nav.toml [target] and a fresh .config
// from NavHAL's defconfig for it. The old .config is kept as .config.old.
int board_use(IExecutionContext& ctx, const std::string& id) {
    namespace fs = std::filesystem;
    if (id.empty()) { ui::error("Usage: nav board use <board-id>"); return 1; }
    auto root = find_project_root();
    if (!root) { ui::error("No nav.toml found in this directory or any parent."); return 1; }
    auto cfg = load_project_config(*root);
    if (!cfg) { ui::error("Failed to parse nav.toml."); return 1; }
    auto board = default_catalog(root).find(id);
    if (!board) {
        ui::error("Unknown board '" + id + "'. Run 'nav board list --all' to see available ids.");
        return 1;
    }

    const fs::path dotconfig = *root / ".config";
    const fs::path fresh = *root / ".config.nav-new";
    std::error_code ec;
    // A library has no .config unless its author added one to declare needs.
    const bool wants_config = !cfg->is_library() || fs::exists(dotconfig, ec);
    if (wants_config) {
        const fs::path navhal_dir = *root / "extern" / "NavHAL";
        if (!fs::exists(navhal_dir, ec)) {
            ui::warning("NavHAL missing at extern/NavHAL — restoring from the shared cache.");
            const fs::path src = ensure_navhal_cached(ctx, kDefaultNavhalRef);
            if (src.empty() || !link_navhal(ctx, src, navhal_dir)) {
                ui::error("Could not restore NavHAL. Check your network connection and retry.");
                return 1;
            }
        }
        const std::string python = ensure_kconfig_python(ctx);
        if (python.empty()) ui::warning("Writing an unexpanded .config; 'nav build' expands it.");
        const std::string why =
            seed_board_config(ctx, navhal_dir, navhal_board_name(*board), templates::kDotConfig, fresh, python);
        if (!why.empty()) {
            fs::remove(fresh, ec);
            ui::error("Could not write .config for board '" + id + "': " + why);
            return 1;
        }
    }

    std::ostringstream toml;
    toml << std::ifstream(*root / "nav.toml").rdbuf();
    std::ofstream(*root / "nav.toml", std::ios::trunc) << retarget_nav_toml(toml.str(), *board);

    if (wants_config) {
        if (fs::exists(dotconfig, ec)) {
            fs::rename(dotconfig, *root / ".config.old", ec);
            ui::info("Previous .config kept as .config.old.");
        }
        fs::rename(fresh, dotconfig, ec);
        if (ec) { ui::error("Could not move the new .config into place: " + ec.message()); return 1; }
    }

    ui::success("Project now targets " + board->id + ".");
    if (!cfg->dependencies.empty())
        ui::info("Dependency requirements are merged back into .config on the next 'nav build'.");
    const auto previous = default_catalog(root).find(cfg->target_board);
    if (fs::exists(*root / "build", ec) && previous && previous->compiler != board->compiler)
        ui::info("The compiler changed (" + previous->compiler + " -> " + board->compiler +
                 "): run 'nav clean' before building.");
    return 0;
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
    if (sub == "use")     return board_use(ctx, id);

    ui::error("Unknown subcommand 'board " + sub + "'. Use: list [--all] | info <id> | check <id> | install <id> | use <id>.");
    return 1;
}

} // namespace nav::core
