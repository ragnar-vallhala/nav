#pragma once

#include "nav/core/board.hpp"
#include "nav/core/registry.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nav::core {

// Flash runners: how `nav upload` gets firmware onto a board. A runner
// (RunnerDef, from the "runners" table in boards.json) is a tool plus a command
// template; a board names its default runner and the variables each supported
// runner needs. Variables, lowest precedence first:
//   1. the runner's "defaults"
//   2. the board's "runners": { "<runner>": { ... } } entry
//   3. built-ins: elf, bin, hex (image paths), address (board flash_address)
//   4. values given on the command line (--port, --set key=value)

// The runner `nav upload` uses when none is named: the board's "runner", else
// its flash_tool, else the first entry of its "runners". Empty if none.
std::string default_runner(const Board& board);

// The binary the default runner invokes (what `nav board install/check` need
// for uploading); falls back to flash_tool when the runner isn't defined.
std::string default_flash_binary(const Board& board, const Registry& reg);

// Every runner the board declares, default first.
std::vector<std::string> board_runner_names(const Board& board);

// True when the runner's command uses {var}.
bool runner_uses(const RunnerDef& runner, const std::string& var);

// The objcopy that matches the board's compiler: arm-none-eabi-gcc ->
// arm-none-eabi-objcopy, avr-gcc -> avr-objcopy; plain "objcopy" otherwise.
std::string objcopy_for(const Board& board);

// `tpl` with every {name} replaced from `vars`; names with no value are added
// to `missing` and left as written.
std::string expand_placeholders(const std::string& tpl, const std::map<std::string, std::string>& vars,
                                std::set<std::string>& missing);

struct FlashStep {
    std::string what;              // short label for the UI
    std::vector<std::string> argv;
};

struct FlashPlan {
    std::string runner;            // runner id chosen
    std::string tool;              // its registry tool id (to probe / install)
    std::vector<FlashStep> steps;  // image conversion (if needed), then the flash itself
    std::string error;             // non-empty: nothing can run, this says why
};

// Plan flashing `elf` to `board` with `runner_id` (empty: the board's default).
// `cli_vars` holds values given on the command line.
FlashPlan plan_flash(const Board& board, const Registry& reg, const std::string& runner_id,
                     const std::filesystem::path& elf, const std::map<std::string, std::string>& cli_vars);

} // namespace nav::core
