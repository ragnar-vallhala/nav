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

    if (!fs::exists("build")) {
        ui::error("Missing binary context. Please execute 'nav build' prior to upload attempts.");
        return 1;
    }

    ui::step("Flashing", "Engaging physical hardware link and transmitting payload...");
    auto upload_res = ctx.execute({"cmake", "--build", "build", "--target", "flash"});

    if (upload_res.exit_code == 0) {
        ui::success("Upload Finalized! Targeted hardware reset and broadcasting live firmware.");
    } else {
        ui::error("Hardware Interface Reported Fault! Please verify physical connections and ensure target board power.");
    }
    return upload_res.exit_code;
}

} // namespace nav::core
