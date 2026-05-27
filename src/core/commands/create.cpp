#include "nav/core/command.hpp"
#include "nav/core/project_name.hpp"
#include "nav/core/ui.hpp"
#include "nav/templates/embedded.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
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

// Per-run unique temp suffix so concurrent / repeated invocations don't collide.
std::string unique_suffix() {
    using namespace std::chrono;
    auto ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    return std::to_string(::getpid()) + "-" + std::to_string(ns);
}

} // namespace

namespace nav::core {

int CreateCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    ui::print_banner();

    // Trivial flag scan until the CLI11 restructure lands. Recognise --force/-f
    // and treat the first non-flag argument as the project name.
    bool force = false;
    std::string proj_name;
    for (const auto& a : args) {
        if (a == "--force" || a == "-f") {
            force = true;
        } else if (proj_name.empty()) {
            proj_name = a;
        }
    }
    if (proj_name.empty()) proj_name = "my-project";

    if (auto reason = nav::core::validate_project_name(proj_name); !reason.empty()) {
        ui::error("Invalid project name '" + proj_name + "': " + reason);
        return 1;
    }

    const fs::path final_path = fs::path(proj_name);
    std::error_code ec;

    if (fs::exists(final_path, ec)) {
        if (!force) {
            ui::error("Refusing to overwrite existing path '" + proj_name + "'. Pass --force to replace it.");
            return 1;
        }
        ui::warning("--force: removing existing '" + proj_name + "' before scaffolding.");
        fs::remove_all(final_path, ec);
        if (ec) {
            ui::error("Failed to remove existing target: " + ec.message());
            return 1;
        }
    }

    // Build the new project inside a sibling temp directory so a failure leaves
    // no half-initialised tree on disk. Same parent path → same filesystem →
    // the final rename is atomic.
    const fs::path tmp_path = fs::path(proj_name + ".nav-tmp-" + unique_suffix());

    auto cleanup_tmp = [&]() {
        std::error_code rmec;
        fs::remove_all(tmp_path, rmec);
    };

    ui::step("Scaffolding", "Creating root workspace: " + proj_name);

    const std::vector<std::string> subdirs = {"extern", "include", "src", "lib", "tests"};
    for (const auto& sub : subdirs) {
        fs::create_directories(tmp_path / sub, ec);
        if (ec) {
            ui::error("Failed to create subdirectory '" + sub + "': " + ec.message());
            cleanup_tmp();
            return 1;
        }
    }

    ui::step("Cloning", "Pulling dependency: NavHAL Framework");
    const std::string repo_url = "https://github.com/ragnar-vallhala/NavHAL.git";
    auto clone_res = ctx.execute(
        {"git", "clone", "--depth", "1", "--branch", "stable", "--progress", repo_url, "NavHAL"},
        (tmp_path / "extern").string());

    if (clone_res.exit_code != 0) {
        ui::error("Clone failed. Aborting project creation; no files were placed in the working directory.");
        std::cerr << clone_res.output << std::endl;
        cleanup_tmp();
        return clone_res.exit_code;
    }

    fs::remove_all(tmp_path / "extern" / "NavHAL" / "samples", ec);
    ui::info("Dependency cleaned (purged samples).");

    // Now that the network step has succeeded, lay down the local files.
    std::ofstream(tmp_path / "nav.toml")        << substitute(templates::kNavToml, proj_name);
    std::ofstream(tmp_path / ".config")         << templates::kDotConfig;
    std::ofstream(tmp_path / "src" / "main.c")  << templates::kMainC;
    std::ofstream(tmp_path / "CMakeLists.txt")  << substitute(templates::kCMakeLists, proj_name);
    ui::info("Default config files and starter code populated.");

    // Atomic move into the final location. If anything went wrong above we
    // already returned without touching final_path.
    fs::rename(tmp_path, final_path, ec);
    if (ec) {
        ui::error("Failed to finalise project at '" + proj_name + "': " + ec.message());
        cleanup_tmp();
        return 1;
    }

    ui::success("Complete! '" + proj_name + "' initialized successfully.");
    return 0;
}

} // namespace nav::core
