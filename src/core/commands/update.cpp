#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/navhal.hpp"
#include "nav/core/provision.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

bool is_pkg_manager_probe(const std::string& bin) {
    return bin == "apt" || bin == "dnf" || bin == "pacman" || bin == "brew" ||
           bin == "winget" || bin == "choco" || bin == "scoop";
}

} // namespace

int UpdateCommand::run(IExecutionContext& ctx, const std::vector<std::string>& /*args*/) {
    ui::step("Updating", "Synchronizing toolchain and dependencies...");

    // A project is optional: with one we can also refresh NavHAL and the board's
    // cross compiler; without one we still bootstrap the base system toolchain so
    // a bare machine can reach the point of running `nav create`.
    auto root = find_project_root();
    if (root) {
        fs::current_path(*root);
        if (!fs::exists("extern/NavHAL")) {
            ui::step("Linking", "NavHAL missing — restoring from the shared cache...");
            const fs::path central = ensure_navhal_cached(ctx, kDefaultNavhalRef);
            if (!central.empty() && link_navhal(ctx, central, *root / "extern" / "NavHAL"))
                ui::success("NavHAL framework linked into the workspace.");
            else
                ui::warning("Could not provision NavHAL; continuing with toolchain checks.");
        } else {
            ui::info("Framework dependency present and secured.");
        }
    } else {
        ui::info("No project in scope — bootstrapping the base system toolchain.");
    }

    std::string board_id;
    if (root) {
        if (auto cfg = load_project_config(*root)) board_id = cfg->target_board;
    }

    ToolchainManager tm;
    std::vector<std::string> missing_bins;

    for (const auto& req : tm.get_system_requirements()) {
        if (is_pkg_manager_probe(req.binary_name)) continue;
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

    int rc = install_tools(ctx, missing_bins);
    if (rc == 0) ui::success("Toolchain synchronized.");
    return rc;
}

} // namespace nav::core
