#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/kconfig_python.hpp"
#include "nav/core/lockfile.hpp"
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

// Re-fetch every git library pinned in nav.lock (a cached branch ref never
// moves on its own) and rewrite its pin to the commit the ref now resolves to.
void refresh_locked_libraries(IExecutionContext& ctx, const fs::path& root) {
    const fs::path lock_path = root / "nav.lock";
    auto lock = load_lockfile(lock_path);
    if (!lock || lock->git.empty()) return;
    bool changed = false;
    for (auto& g : lock->git) {
        ui::step("Resolving", "library " + g.name + " (" + g.url + (g.ref.empty() ? "" : " @ " + g.ref) + ")");
        const std::string commit = refresh_git_cache(ctx, g.url, g.ref);
        if (commit.empty()) {
            ui::warning("Could not refresh " + g.name + "; keeping its pin.");
            continue;
        }
        if (commit != g.commit) {
            ui::info("nav.lock: " + g.name + " " + g.commit.substr(0, 12) + " -> " + commit.substr(0, 12));
            g.commit = commit;
            changed = true;
        }
    }
    if (changed && !save_lockfile(*lock, lock_path)) ui::warning("Failed to write nav.lock.");
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
        if (auto cfg = load_project_config(*root)) {
            // Re-resolve NavHAL's ref from upstream (a branch may have moved),
            // rewrite its nav.lock pin and point extern/NavHAL at it.
            const NavhalSpec spec = navhal_spec(*cfg);
            ui::step("Resolving", "NavHAL " + spec.ref + " from " + spec.git);
            if (!sync_project_navhal(ctx, *root, *cfg, /*refresh=*/true).empty())
                ui::success("NavHAL " + spec.ref + " resolved and locked.");
            else
                ui::warning("Could not provision NavHAL; continuing with toolchain checks.");
            refresh_locked_libraries(ctx, *root);
        } else {
            ui::warning("Could not parse nav.toml; skipping the NavHAL and library refresh.");
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

    int rc = missing_bins.empty() ? 0 : install_tools(ctx, missing_bins);

    // After the tools: kconfiglib needs a Python to install into.
    if (ensure_kconfig_python(ctx).empty()) rc = rc ? rc : 1;

    if (rc == 0)
        ui::success(missing_bins.empty() ? "Verification absolute! All local toolchains verified and active."
                                         : "Toolchain synchronized.");
    return rc;
}

} // namespace nav::core
