#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nav::core {

struct Board {
    std::string id;
    std::string name;
    std::string arch;
    std::string vendor;
    std::string mcu;

    // NavHAL's board directory name (src/board/<navhal_board>/) used for the
    // linker script path. Often equals id, but not always (id "atmega328p").
    std::string navhal_board;

    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;

    // Toolchain compiler binary (e.g. "arm-none-eabi-gcc"). Empty for hosted
    // targets where the system compiler is fine.
    std::string compiler;

    // Flashing. Optional; empty fields mean "no built-in support" (a board may
    // still flash through a custom CMake target).
    std::string flash_tool;
    std::string flash_address;

    // Post-flash reset. Some flashers leave the core halted after writing (e.g.
    // st-flash on a Nucleo whose NRST isn't wired to the debugger), so the new
    // image never starts until the board is reset. When reset_after_flash is
    // true, `nav upload` runs reset_command (argv) after a successful flash.
    // Both come from the board registry (data/boards.json, user-overridable in
    // ~/.nav/boards.json), so no reset behaviour is hardcoded.
    bool reset_after_flash = false;
    std::vector<std::string> reset_command;

    std::string default_framework;
    std::vector<std::string> supported_frameworks;
};

class BoardCatalog {
public:
    void add(Board b) { boards_.push_back(std::move(b)); }

    std::optional<Board> find(const std::string& id) const;
    std::vector<Board> list() const { return boards_; }

private:
    std::vector<Board> boards_;
};

// Build a catalog from the registry (data/boards.json, baked + the user's
// ~/.nav/boards.json). `project_root` is accepted for API compatibility but no
// longer used (board overrides now live in the user's boards.json).
BoardCatalog default_catalog(const std::optional<std::filesystem::path>& project_root = std::nullopt);

// Render the board as CMake `set()` directives for inclusion at configure
// time. Variables exposed (NAV_BOARD_ prefix to avoid collision with NavHAL's
// BOARD/FLASHER/FLASH_ADDRESS): ID, NAME, ARCH, VENDOR, MCU, COMPILER,
// COMPILE_FLAGS, LINK_FLAGS, FLASH_TOOL, FLASH_ADDRESS. Lists use CMake
// semicolon notation.
std::string render_board_cmake(const Board& board);

// Write render_board_cmake() output to `out_path`, creating parent dirs as
// needed. Returns true on success.
bool write_board_cmake(const Board& board, const std::filesystem::path& out_path);

} // namespace nav::core
