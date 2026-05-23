#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
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

    if (!fs::exists("build")) {
        fs::create_directory("build");
        ui::info("Created pristine build workspace sub-node.");
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
