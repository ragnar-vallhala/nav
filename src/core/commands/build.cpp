#include "nav/core/board.hpp"
#include "nav/core/cache.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/deps.hpp"
#include "nav/core/http_index.hpp"
#include "nav/core/install.hpp"
#include "nav/core/lockfile.hpp"
#include "nav/core/platform.hpp"
#include "nav/core/ui.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace nav::core {

int BuildCommand::run(IExecutionContext& ctx, const std::vector<std::string>& /*args*/) {
    ui::step("Building", "Scanning project space and verifying build definitions...");

    auto root = find_project_root();
    if (!root) {
        ui::error("Build halted! No nav.toml found in this directory or any parent.");
        return 1;
    }
    fs::current_path(*root);

    auto cfg = load_project_config(*root);
    if (!cfg) {
        ui::error("Failed to parse 'nav.toml'.");
        return 1;
    }
    if (cfg->target_board.empty()) {
        ui::error("'nav.toml' has no [target].board entry. Set one (see 'nav board list') and re-run.");
        return 1;
    }

    auto catalog = default_catalog(root);
    auto board = catalog.find(cfg->target_board);
    if (!board) {
        ui::error("Board id '" + cfg->target_board + "' not in catalog. Run 'nav board list' to see available ids.");
        return 1;
    }

    if (!fs::exists("build")) {
        fs::create_directory("build");
        ui::info("Created pristine build workspace sub-node.");
    }

    const fs::path board_cmake = fs::path("build") / "nav-board.cmake";
    if (!write_board_cmake(*board, board_cmake)) {
        ui::error("Failed to write " + board_cmake.string() + ".");
        return 1;
    }
    ui::info("Resolved board: " + board->id + " (" + board->arch + ")");

    // Registry dependencies: ensure everything pinned in nav.lock is cached,
    // then emit build/nav-deps.cmake so the generated CMake picks up library
    // include/link inputs and any registry-provided toolchain compiler.
    if (auto lock = load_lockfile(*root / "nav.lock")) {
        const std::string platform = host_platform_key(ctx);
        if (!lock->packages.empty()) {
            ui::step("Dependencies", "Ensuring locked packages are cached...");
            auto outcomes = ensure_locked_present(ctx, cache_root(), default_registry_url(),
                                                  *lock, "", /*skip_cached=*/true, platform);
            report_ensure_outcomes(outcomes);
            if (!all_present(outcomes)) {
                ui::error("Missing dependencies. Run 'nav fetch' once the registry is reachable.");
                return 1;
            }
        }
        const fs::path deps_cmake = fs::path("build") / "nav-deps.cmake";
        if (!write_deps_cmake(*lock, cache_root(), platform, board->compiler, deps_cmake)) {
            ui::error("Failed to write " + deps_cmake.string() + ".");
            return 1;
        }
    }

    ui::step("Configuring", "Initializing dynamic build system generator (CMake)...");
    auto conf_res = ctx.execute({"cmake", "-S", ".", "-B", "build"});
    if (conf_res.exit_code != 0) {
        ui::error("Configuration sequence failed. Terminating build pipe.");
        return conf_res.exit_code;
    }

    ui::step("Compiling", "Engaging native system builders...");
    auto build_res = ctx.execute({"cmake", "--build", "build", "--parallel"});
    if (build_res.exit_code == 0) {
        ui::success("Compilation Successful! Binary artifacts successfully established in build/");
    } else {
        ui::error("Builder reported non-recoverable compilation failure.");
    }
    return build_res.exit_code;
}

} // namespace nav::core
