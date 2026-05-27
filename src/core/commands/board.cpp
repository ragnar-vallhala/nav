#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/ui.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace nav::core {

namespace {

int print_list() {
    auto cat = default_catalog(find_project_root());
    auto boards = cat.list();
    if (boards.empty()) {
        ui::warning("No boards found. Searched NAV_BOARD_PATH, exe-relative share/nav/boards, /usr/share/nav/boards.");
        return 0;
    }
    std::sort(boards.begin(), boards.end(),
              [](const Board& a, const Board& b) { return a.id < b.id; });

    std::size_t id_width = 0;
    for (const auto& b : boards) id_width = std::max(id_width, b.id.size());

    std::cout << ui::BOLD() << "ID";
    for (std::size_t i = 2; i < id_width + 2; ++i) std::cout << ' ';
    std::cout << "  NAME" << ui::RESET() << "\n";

    for (const auto& b : boards) {
        std::cout << b.id;
        for (std::size_t i = b.id.size(); i < id_width + 2; ++i) std::cout << ' ';
        std::cout << "  " << b.name << "\n";
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

int print_info(const std::string& id) {
    if (id.empty()) {
        ui::error("Usage: nav board info <board-id>");
        return 1;
    }
    auto cat = default_catalog(find_project_root());
    auto board = cat.find(id);
    if (!board) {
        ui::error("Unknown board '" + id + "'. Run 'nav board list' to see available boards.");
        return 1;
    }
    std::cout << ui::BOLD() << board->name << ui::RESET()
              << " (" << board->id << ")\n";
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

} // namespace

int BoardCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "list") {
        return print_list();
    }
    if (args[0] == "info") {
        return print_info(args.size() > 1 ? args[1] : "");
    }
    ui::error("Unknown subcommand 'board " + args[0] + "'. Use 'list' or 'info <id>'.");
    return 1;
}

} // namespace nav::core
