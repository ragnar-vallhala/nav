#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace nav::core {

int UpdateCommand::run(IExecutionContext& ctx, const std::vector<std::string>& /*args*/) {
    ui::step("Updating", "Synchronizing ecosystem dependencies and validation metrics...");

    auto root = find_project_root();
    if (!root) {
        ui::error("Update halted! No nav.toml found in this directory or any parent.");
        return 1;
    }
    fs::current_path(*root);

    if (!fs::exists("extern/NavHAL")) {
        ui::step("Cloning", "Core Dependency 'NavHAL' missing! Pulling secure vector...");
        auto clone_res = ctx.execute({
            "git", "clone", "--branch", "stable", "--progress",
            "https://github.com/ragnar-vallhala/NavHAL.git", "extern/NavHAL"
        });
        if (clone_res.exit_code == 0) {
            fs::remove_all("extern/NavHAL/samples");
            ui::success("Successfully tethered NavHAL framework into local workspace.");
        } else {
            ui::error("Network fault! Remote repository fetch failed.");
            return 1;
        }
    } else {
        ui::info("Framework dependency present and secured.");
    }

    std::string board_id;
    if (auto cfg = load_project_config(*root)) {
        board_id = cfg->target_board;
    }

    ToolchainManager tm;
    std::vector<std::string> missing_bins;

    for (const auto& req : tm.get_system_requirements()) {
        if (req.binary_name == "apt" || req.binary_name == "dnf" ||
            req.binary_name == "pacman" || req.binary_name == "brew") continue;
        if (!tm.probe_tool(ctx, req).is_found) missing_bins.push_back(req.binary_name);
    }

    if (!board_id.empty()) {
        auto catalog = default_catalog(root);
        if (auto board = catalog.find(board_id)) {
            for (const auto& req : tm.get_project_requirements(*board)) {
                if (!tm.probe_tool(ctx, req).is_found) missing_bins.push_back(req.binary_name);
            }
        } else {
            ui::warning("Board id '" + board_id + "' not in catalog; project toolchain probes skipped.");
        }
    }

    if (missing_bins.empty()) {
        ui::success("Verification absolute! All local toolchains verified and active.");
        return 0;
    }

    ui::step("Provisioning", "Autodiscovering package paths for missing system dependencies...");

    auto pm = detect_package_manager();
    if (!pm) {
        ui::warning("No supported package manager detected. Install manually: ");
        for (const auto& bin : missing_bins) ui::warning("  - " + bin);
        return 1;
    }

    std::vector<std::string> packages_to_install;
    std::vector<std::string> unmapped;
    for (const auto& bin : missing_bins) {
        if (auto pkg = map_binary_to_package(*pm, bin)) {
            packages_to_install.push_back(*pkg);
        } else {
            unmapped.push_back(bin);
        }
    }

    for (const auto& bin : unmapped) {
        ui::warning("No package mapping for '" + bin + "' on this distribution. Install it manually.");
    }

    if (packages_to_install.empty()) {
        return unmapped.empty() ? 0 : 1;
    }

    ui::info("Required system components: " + std::to_string(packages_to_install.size()));

    auto install_cmd = build_install_command(*pm, packages_to_install);

    ui::step("Installing", "Invoking system package manager elevation sequence...");
    auto ins_res = ctx.execute(install_cmd);

    if (ins_res.exit_code == 0) {
        ui::success("Environment secured! Total system state synchronized.");
    } else {
        ui::error("Installation execution fault. Please run manually via shell.");
    }

    // Re-surface unmapped binaries after install spam, and OR them into the
    // exit code so partial success surfaces as failure.
    if (!unmapped.empty()) {
        ui::warning("The following binaries have no package mapping on this distribution and were NOT installed:");
        for (const auto& bin : unmapped) ui::warning("  - " + bin);
        ui::warning("Install them manually before continuing.");
    }

    if (ins_res.exit_code != 0 || !unmapped.empty()) {
        return 1;
    }
    return 0;
}

} // namespace nav::core
