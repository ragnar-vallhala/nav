#include "nav/core/board.hpp"
#include "nav/core/board_config.hpp"
#include "nav/core/command.hpp"
#include "nav/core/kconfig_python.hpp"
#include "nav/core/navhal.hpp"
#include "nav/core/project_name.hpp"
#include "nav/core/ui.hpp"
#include "nav/templates/embedded.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string substitute(std::string_view tpl, const std::vector<std::pair<std::string, std::string>>& vars) {
    std::string out(tpl);
    for (const auto& [name, value] : vars) {
        const std::string token = "{{" + name + "}}";
        size_t pos = 0;
        while ((pos = out.find(token, pos)) != std::string::npos) {
            out.replace(pos, token.length(), value);
            pos += value.length();
        }
    }
    return out;
}

constexpr const char* kDefaultBoard = "nucleo_f401re";

// Per-run unique temp suffix so concurrent / repeated invocations don't collide.
std::string unique_suffix() {
    using namespace std::chrono;
    auto ns = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
#ifdef _WIN32
    auto pid = ::_getpid();
#else
    auto pid = ::getpid();
#endif
    return std::to_string(pid) + "-" + std::to_string(ns);
}

} // namespace

namespace nav::core {

int CreateCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    ui::print_banner();

    // Trivial flag scan until the CLI11 restructure lands. Recognise --force/-f
    // and treat the first non-flag argument as the project name.
    bool force = false;
    bool as_library = false;
    std::string proj_name;
    std::string board_id = kDefaultBoard;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == "--force" || a == "-f") {
            force = true;
        } else if (a == "--lib" || a == "--library") {
            as_library = true;
        } else if (a == "--board" || a == "-b") {
            if (i + 1 >= args.size()) {
                ui::error("--board needs a board id (see 'nav board list --all').");
                return 1;
            }
            board_id = args[++i];
        } else if (a.rfind("--board=", 0) == 0) {
            board_id = a.substr(8);
        } else if (proj_name.empty()) {
            proj_name = a;
        }
    }
    if (proj_name.empty()) proj_name = as_library ? "my-library" : "my-project";

    if (auto reason = nav::core::validate_project_name(proj_name); !reason.empty()) {
        ui::error("Invalid project name '" + proj_name + "': " + reason);
        return 1;
    }

    const auto board = default_catalog().find(board_id);
    if (!board) {
        ui::error("Unknown board '" + board_id + "'. Run 'nav board list --all' to see available ids.");
        return 1;
    }
    const std::vector<std::pair<std::string, std::string>> vars = {
        {"PROJECT_NAME", proj_name}, {"ARCH", board->arch}, {"VENDOR", board->vendor}, {"BOARD", board->id}};

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

    // Fetch NavHAL into the shared per-user cache (cloned at most once per ref,
    // then reused by every project) and link it into the project rather than
    // re-cloning a full copy here.
    ui::step("Linking", "Resolving NavHAL " + std::string(kDefaultNavhalRef) + " from the shared cache");
    const fs::path navhal_src = ensure_navhal_cached(ctx, kDefaultNavhalRef);
    if (navhal_src.empty()) {
        ui::error("Could not provision NavHAL. Aborting; no files were placed in the working directory.");
        cleanup_tmp();
        return 1;
    }
    if (!link_navhal(ctx, navhal_src, tmp_path / "extern" / "NavHAL")) {
        ui::error("Failed to link NavHAL into the project.");
        cleanup_tmp();
        return 1;
    }
    ui::info("NavHAL ready (cache: " + navhal_src.string() + ").");

    // Now that the network step has succeeded, lay down the local files. A
    // library project gets a static-archive CMake + exported header/source and
    // no main(); a firmware project gets the executable scaffold + .config.
    if (as_library) {
        std::ofstream(tmp_path / "nav.toml")                     << substitute(templates::kNavTomlLib, vars);
        std::ofstream(tmp_path / "include" / (proj_name + ".h")) << substitute(templates::kLibHeader, vars);
        std::ofstream(tmp_path / "src" / (proj_name + ".c"))     << substitute(templates::kLibSource, vars);
        std::ofstream(tmp_path / "CMakeLists.txt")               << substitute(templates::kCMakeListsLib, vars);
        ui::info("Library scaffold and starter code populated.");
    } else {
        // .config comes from NavHAL's defconfig for the chosen board, so it
        // selects the board nav.toml names.
        const std::string python = ensure_kconfig_python(ctx);
        if (python.empty()) ui::warning("Writing an unexpanded .config; 'nav build' expands it.");
        const std::string why = seed_board_config(ctx, tmp_path / "extern" / "NavHAL", navhal_board_name(*board),
                                                  templates::kDotConfig, tmp_path / ".config", python);
        if (!why.empty()) {
            ui::error("Could not write .config for board '" + board->id + "': " + why);
            cleanup_tmp();
            return 1;
        }
        std::ofstream(tmp_path / "nav.toml")        << substitute(templates::kNavToml, vars);
        std::ofstream(tmp_path / "src" / "main.c")  << templates::kMainC;
        std::ofstream(tmp_path / "CMakeLists.txt")  << substitute(templates::kCMakeLists, vars);
        ui::info("Default config files and starter code populated.");
    }

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
