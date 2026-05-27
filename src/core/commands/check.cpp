#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <iostream>
#include <string>

namespace nav::core {

namespace {

// Returns 1 if the probe was a critical miss (for caller's failed_critical tally).
int render_probe(const ProbeResult& res, const std::string& ok_detail) {
    if (res.is_found) {
        ui::tool_ok(res.tool_name, ok_detail);
        return 0;
    }
    if (res.is_critical) {
        ui::tool_missing_critical(res.tool_name);
        return 1;
    }
    ui::tool_missing_optional(res.tool_name);
    return 0;
}

} // namespace

int CheckCommand::run(IExecutionContext& ctx, const std::vector<std::string>& /*args*/) {
    ToolchainManager tm;
    int failed_critical = 0;

    ui::step("Validating [STAGE 1]", "System Environment Dependencies");
    for (const auto& req : tm.get_system_requirements()) {
        auto res = tm.probe_tool(ctx, req);
        failed_critical += render_probe(res, "Detected.");
    }
    std::cout << std::endl;

    ui::step("Validating [STAGE 2]", "Target Specific Hardware Dependency");

    auto root = find_project_root();
    if (!root) {
        ui::warning("No 'nav.toml' located in current scope. Project checks bypassed.");
    } else {
        auto cfg = load_project_config(*root);
        if (!cfg) {
            ui::warning("Failed to parse 'nav.toml'. Project checks bypassed.");
        } else if (cfg->target_board.empty()) {
            ui::warning("Resolved configuration is incomplete. Unable to extract target 'board' from toml.");
        } else {
            const std::string& board_id = cfg->target_board;
            ui::info("Context resolved! Targeting hardware configuration: [" + board_id + "]");
            auto catalog = default_catalog(root);
            auto board = catalog.find(board_id);
            if (!board) {
                ui::warning("Board id '" + board_id + "' not in catalog. Run 'nav board list' to see available ids; project checks skipped.");
            } else {
                for (const auto& req : tm.get_project_requirements(*board)) {
                    auto res = tm.probe_tool(ctx, req);
                    failed_critical += render_probe(res, res.version_info);
                }
            }
        }
    }

    std::cout << std::endl;

    if (failed_critical > 0) {
        ui::error("Comprehensive assessment complete. Total Critical Defects: "
                  + std::to_string(failed_critical));
        return 1;
    }

    ui::success("All verified verification thresholds passed. System state secure.");
    return 0;
}

} // namespace nav::core
