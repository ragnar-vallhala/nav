#include "nav/core/command.hpp"
#include "nav/core/ui.hpp"
#include "nav/templates/embedded.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string substitute(std::string_view tpl, const std::string& project_name) {
    std::string out(tpl);
    const std::string token = "{{PROJECT_NAME}}";
    size_t pos = 0;
    while ((pos = out.find(token, pos)) != std::string::npos) {
        out.replace(pos, token.length(), project_name);
        pos += project_name.length();
    }
    return out;
}

} // namespace

namespace nav::core {

int CreateCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    ui::print_banner();

    std::string proj_name = args.empty() ? "my-project" : args[0];

    ui::step("Scaffolding", "Creating root workspace workspace: " + proj_name);

    const std::vector<std::string> subdirs = {"extern", "include", "src", "lib", "tests"};
    for (const auto& sub : subdirs) {
        fs::create_directories(proj_name + "/" + sub);
    }
    ui::info("Folder structure created locally.");

    std::ofstream(proj_name + "/nav.toml")        << substitute(templates::kNavToml, proj_name);
    std::ofstream(proj_name + "/.config")         << templates::kDotConfig;
    std::ofstream(proj_name + "/src/main.c")      << templates::kMainC;
    ui::info("Default config files and starter code populated.");

    std::ofstream(proj_name + "/CMakeLists.txt")  << substitute(templates::kCMakeLists, proj_name);
    ui::info("Root build definitions (CMakeLists.txt) successfully orchestrated.");

    const std::string extern_path = proj_name + "/extern";
    const std::string repo_url = "https://github.com/ragnar-vallhala/NavHAL.git";

    ui::step("Cloning", "Pulling dependency: NavHAL Framework");
    auto result = ctx.execute(
        {"git", "clone", "--depth", "1", "--branch", "stable", "--progress", repo_url, "NavHAL"},
        extern_path);

    if (result.exit_code == 0) {
        fs::remove_all(extern_path + "/NavHAL/samples");
        ui::info("Dependencies cleaned (Purged samples).");
        ui::success("Complete! '" + proj_name + "' initialized successfully.");
    } else {
        ui::error("Clone sequence collapsed. Traceback dump:");
        std::cerr << result.output << std::endl;
    }

    return result.exit_code;
}

} // namespace nav::core
