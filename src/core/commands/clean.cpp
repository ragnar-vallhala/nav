#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/ui.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace nav::core {

int CleanCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& /*args*/) {
    ui::step("Cleaning", "Analyzing intermediate cache status and target binary presence...");

    auto root = find_project_root();
    if (!root) {
        ui::error("Cleanup aborted! No nav.toml found in this directory or any parent.");
        return 1;
    }
    fs::current_path(*root);

    if (!fs::exists("build")) {
        ui::info("Workspace structure is already pristine. No 'build/' directory detected.");
        ui::success("System cleanly purged (nothing to do).");
        return 0;
    }

    try {
        ui::info("Purging cached object data and binary artifacts...");
        fs::remove_all("build");
        ui::success("Workspace flushed! Build context successfully reset to defaults.");
    } catch (const std::exception&) {
        ui::error("Cache purge failed! File handle may be locked by another process.");
        return 1;
    }
    return 0;
}

} // namespace nav::core
