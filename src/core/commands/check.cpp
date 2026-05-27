#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <iostream>
#include <string>

namespace nav::core {

int CheckCommand::run(IExecutionContext& ctx, const std::vector<std::string>& /*args*/) {
    ToolchainManager tm;
    int failed_critical = 0;

    ui::step("Validating [STAGE 1]", "System Environment Dependencies");
    auto system_reqs = tm.get_system_requirements();
    for (const auto& req : system_reqs) {
        auto res = tm.probe_tool(ctx, req);
        if (res.is_found) {
            std::cout << "  " << ui::GREEN() << "✔ " << ui::RESET() << ui::BOLD()
                      << res.tool_name << ui::RESET() << " -> Detected.\n";
        } else if (res.is_critical) {
            failed_critical++;
            std::cout << "  " << ui::RED() << "✖ " << ui::RESET() << ui::BOLD()
                      << res.tool_name << ui::RESET() << " -> "
                      << ui::RED() << "NOT FOUND (CRITICAL)\n" << ui::RESET();
        } else {
            std::cout << "  " << ui::YELLOW() << "⚠ " << ui::RESET() << ui::BOLD()
                      << res.tool_name << ui::RESET() << " -> Optional (Not Installed)\n";
        }
    }
    std::cout << std::endl;

    ui::step("Validating [STAGE 2]", "Target Specific Hardware Dependency");

    std::string board_id;
    auto root = find_project_root();
    if (!root) {
        ui::warning("No 'nav.toml' located in current scope. Project checks bypassed.");
    } else {
        auto cfg = load_project_config(*root);
        if (!cfg) {
            ui::warning("Failed to parse 'nav.toml'. Project checks bypassed.");
        } else {
            board_id = cfg->target_board;
        }

        if (board_id.empty()) {
            ui::warning("Resolved configuration is incomplete. Unable to extract target 'board' from toml.");
        } else {
            ui::info("Context resolved! Targeting hardware configuration: [" + board_id + "]");
            auto project_reqs = tm.get_project_requirements(board_id);
            for (const auto& req : project_reqs) {
                auto res = tm.probe_tool(ctx, req);
                if (res.is_found) {
                    std::cout << "  " << ui::GREEN() << "✔ " << ui::RESET() << ui::BOLD()
                              << res.tool_name << ui::RESET() << " -> " << res.version_info << std::endl;
                } else if (res.is_critical) {
                    failed_critical++;
                    std::cout << "  " << ui::RED() << "✖ " << ui::RESET() << ui::BOLD()
                              << res.tool_name << ui::RESET() << " -> "
                              << ui::RED() << "NOT FOUND (CRITICAL)\n" << ui::RESET();
                } else {
                    std::cout << "  " << ui::YELLOW() << "⚠ " << ui::RESET() << ui::BOLD()
                              << res.tool_name << ui::RESET() << " -> Optional (Not Installed)\n";
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
