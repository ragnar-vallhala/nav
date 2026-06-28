#include "nav/core/board.hpp"
#include "nav/core/cache.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/deps.hpp"
#include "nav/core/lockfile.hpp"
#include "nav/core/platform.hpp"
#include "nav/core/ui.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>

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

    // Locked dependencies: emit build/nav-deps.cmake from nav.lock so the
    // generated CMake picks up any cached library include/link inputs and the
    // pinned toolchain compiler. Populating the cache is out of scope here — the
    // registry fetch path has been removed; a git-based fetcher will replace it.
    if (auto lock = load_lockfile(*root / "nav.lock")) {
        const std::string platform = host_platform_key(ctx);
        const fs::path deps_cmake = fs::path("build") / "nav-deps.cmake";
        if (!write_deps_cmake(*lock, cache_root(), platform, board->compiler, deps_cmake)) {
            ui::error("Failed to write " + deps_cmake.string() + ".");
            return 1;
        }
    }

    // NavHAL's Kconfig (extern/NavHAL/Kconfig) uses relative `source` globs that
    // kconfiglib resolves against $srctree (default: cwd). NavHAL standalone runs
    // cmake from its own root so cwd happens to match; we drive cmake from the
    // project root, so point $srctree at the NavHAL clone explicitly. Child
    // processes (cmake -> kconfig.py) inherit it.
    const fs::path navhal_dir = *root / "extern" / "NavHAL";
    if (fs::exists(navhal_dir)) {
        ::setenv("srctree", navhal_dir.string().c_str(), /*overwrite=*/1);
    }

    // Run cmake quietly (silent=true captures output without streaming it), so a
    // successful build stays terse like a container build. On failure we replay
    // the captured log so the diagnostics aren't lost.
    ui::step("Configuring", "cmake");
    auto conf_res = ctx.execute({"cmake", "-S", ".", "-B", "build"}, "", /*silent=*/true);
    if (conf_res.exit_code != 0) {
        std::cerr << conf_res.output << std::endl;
        ui::error("Configure failed.");
        return conf_res.exit_code;
    }

    ui::step("Compiling", "cmake --build");
    auto build_res = ctx.execute({"cmake", "--build", "build", "--parallel"}, "", /*silent=*/true);
    if (build_res.exit_code == 0) {
        ui::success("Build complete. Artifacts in build/");
    } else {
        std::cerr << build_res.output << std::endl;
        ui::error("Build failed.");
    }
    return build_res.exit_code;
}

} // namespace nav::core
