#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/ui.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace nav::core {

int UploadCommand::run(IExecutionContext& ctx, const std::vector<std::string>& /*args*/) {
    ui::step("Uploading", "Initializing physical hardware deployment pipeline...");

    auto root = find_project_root();
    if (!root) {
        ui::error("Upload halted! No nav.toml found in this directory or any parent.");
        return 1;
    }
    fs::current_path(*root);

    // A library produces an archive, not a flashable firmware image.
    if (auto cfg = load_project_config(*root); cfg && cfg->is_library()) {
        ui::error("This is a library project — nothing to flash. Build it ('nav build') or "
                  "depend on it from a firmware project, then upload that.");
        return 1;
    }

    if (!fs::exists("build")) {
        ui::error("Missing binary context. Please execute 'nav build' prior to upload attempts.");
        return 1;
    }

    ui::step("Flashing", "Engaging physical hardware link and transmitting payload...");
    auto upload_res = ctx.execute({"cmake", "--build", "build", "--target", "flash"});

    if (upload_res.exit_code != 0) {
        ui::error("Hardware Interface Reported Fault! Please verify physical connections and ensure target board power.");
        return upload_res.exit_code;
    }

    // Some flashers leave the core halted after writing (e.g. st-flash on a
    // Nucleo whose NRST isn't wired to the debugger), so the freshly-flashed
    // image never starts. Boards that need it declare reset_after_flash +
    // reset_command in the registry (data/boards.json, user-overridable in
    // ~/.nav/boards.json) — nothing is hardcoded here.
    if (auto cfg = load_project_config(*root)) {
        auto catalog = default_catalog(root);
        if (auto board = catalog.find(cfg->target_board);
            board && board->reset_after_flash && !board->reset_command.empty()) {
            ui::step("Resetting", "Restarting the target on the new image...");
            auto rst = ctx.execute(board->reset_command, "", /*silent=*/true);
            if (rst.exit_code != 0)
                ui::warning("Flashed OK, but the reset failed — press the board's RESET button to start it.");
        }
    }

    ui::success("Upload Finalized! Targeted hardware reset and broadcasting live firmware.");
    return 0;
}

} // namespace nav::core
