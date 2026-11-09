#pragma once

#include "nav/core/board.hpp"
#include "nav/core/execution_context.hpp"
#include "nav/core/navconfig.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace nav::core {

// Ties a project's .config to the board named in nav.toml. NavHAL describes each
// board in src/board/<name>/Kconfig (`default "<name>" if BOARD_<SYM>`) and ships
// a defconfig per board in cmake/defconfigs/ that selects its arch, vendor,
// family and board. nav reads both rather than guessing symbol names.

// The board's directory name in NavHAL (src/board/<name>/); the catalog id when
// the catalog entry doesn't say.
inline std::string navhal_board_name(const Board& b) { return b.navhal_board.empty() ? b.id : b.navhal_board; }

struct NavhalTargets {
    std::map<std::string, std::string> board_symbol; // NavHAL board name -> CONFIG_BOARD_<SYM>
    std::map<std::string, std::string> board_name;   // CONFIG_BOARD_<SYM> -> NavHAL board name
    // Every arch / vendor / family / board choice member (CONFIG_ prefixed). A
    // project picks exactly one of each; a dependency never adds one.
    std::set<std::string> choice_symbols;
};

NavhalTargets scan_navhal_targets(const std::filesystem::path& navhal_dir);

// The defconfig in <navhal>/cmake/defconfigs/ that selects `board_symbol`.
std::optional<std::filesystem::path> find_board_defconfig(const std::filesystem::path& navhal_dir,
                                                          const std::string& board_symbol);

// Empty when the .config selects exactly the board `navhal_board`; otherwise a
// one-line description of what it selects instead.
std::string check_config_board(const NavhalTargets& targets, const std::string& navhal_board,
                               const std::map<std::string, std::string>& have);

// A dependency may not select a target: a missing choice member it requires
// becomes a conflict instead of something to append to the app's .config.
void hold_target_choices(RequirementDiff& diff, const std::set<std::string>& choice_symbols);

// Write a fresh .config for `navhal_board` to `out`: the board's defconfig plus
// `starter` (extra symbols, ignored where the board lacks them). With `python`
// (one that imports kconfiglib) the result is expanded with Kconfig defaults,
// like `make <board>_defconfig`; without it the unexpanded fragment is written
// and NavHAL's configure step expands it. Returns empty on success, else why not.
std::string seed_board_config(IExecutionContext& ctx, const std::filesystem::path& navhal_dir,
                              const std::string& navhal_board, std::string_view starter,
                              const std::filesystem::path& out, const std::string& python);

// `text` (a nav.toml) with [target] arch / vendor / board set to `board`'s.
// Other lines, comments and order are kept; a missing key or section is added.
std::string retarget_nav_toml(const std::string& text, const Board& board);

} // namespace nav::core
