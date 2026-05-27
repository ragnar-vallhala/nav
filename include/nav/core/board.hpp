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

    std::vector<std::string> compile_flags;
    std::vector<std::string> link_flags;

    // Toolchain compiler binary (e.g. "arm-none-eabi-gcc"). Empty for hosted
    // targets where the system compiler is fine.
    std::string compiler;

    // Flashing. Optional; empty fields mean "no built-in support" (a board may
    // still flash through a custom CMake target).
    std::string flash_tool;
    std::string flash_address;

    std::string default_framework;
    std::vector<std::string> supported_frameworks;
};

class BoardCatalog {
public:
    // Scan `dir` for *.toml entries and merge them in. First add wins on id
    // collisions, so callers should add the highest-priority path first.
    void add_search_path(const std::filesystem::path& dir);

    std::optional<Board> find(const std::string& id) const;
    std::vector<Board> list() const { return boards_; }

private:
    std::vector<Board> boards_;
};

// Build a catalog populated with the standard search order, highest-priority
// first:
//   1. each colon-separated entry of $NAV_BOARD_PATH
//   2. <project_root>/.nav/boards (when given)
//   3. <exe_dir>/../share/nav/boards (the location an installed nav ships to)
//   4. <exe_dir>/../../share/nav/boards (in-tree build layout)
//   5. /usr/share/nav/boards
BoardCatalog default_catalog(const std::optional<std::filesystem::path>& project_root = std::nullopt);

// Parse a single board TOML file. Returns nullopt on parse failure or when a
// required field (id, arch) is missing.
std::optional<Board> parse_board_file(const std::filesystem::path& path);

} // namespace nav::core
