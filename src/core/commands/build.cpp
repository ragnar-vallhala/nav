#include "nav/core/board.hpp"
#include "nav/core/board_config.hpp"
#include "nav/core/cache.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/deps.hpp"
#include "nav/core/kconfig_python.hpp"
#include "nav/core/libdeps.hpp"
#include "nav/core/lockfile.hpp"
#include "nav/core/navconfig.hpp"
#include "nav/core/navhal.hpp"
#include "nav/core/platform.hpp"
#include "nav/core/provision.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

    // NavHAL's Kconfig (extern/NavHAL/Kconfig) uses relative `source` globs that
    // kconfiglib resolves against $srctree (default: cwd). NavHAL standalone runs
    // cmake from its own root so cwd happens to match; we drive cmake from the
    // project root, so point $srctree at the NavHAL clone explicitly. Child
    // processes (cmake -> kconfig.py) inherit it.
    const fs::path navhal_dir = *root / "extern" / "NavHAL";
    // The project links extern/NavHAL into the shared cache; if that link is
    // missing or dangling (cache cleared, fresh checkout), restore it before
    // configuring rather than failing deep inside cmake.
    if (!fs::exists(navhal_dir)) {
        ui::warning("NavHAL missing at extern/NavHAL — restoring from the shared cache.");
        const fs::path navhal_src = ensure_navhal_cached(ctx, kDefaultNavhalRef);
        if (navhal_src.empty() || !link_navhal(ctx, navhal_src, navhal_dir)) {
            ui::error("Could not restore NavHAL. Check your network connection and retry.");
            return 1;
        }
    }
    // The board in nav.toml decides; .config must select that board and no
    // other. Checked before dependency requirements are merged in, so a stale
    // .config is reported instead of patched.
    const NavhalTargets targets = scan_navhal_targets(navhal_dir);
    const fs::path app_config = *root / ".config";
    if (fs::exists(app_config)) {
        const std::string why = check_config_board(targets, navhal_board_name(*board), parse_kconfig(app_config));
        if (!why.empty()) {
            ui::error("nav.toml targets board '" + board->id + "', but " + why + ".");
            ui::error("Regenerate .config for it with 'nav board use " + board->id +
                      "' (the old file is kept as .config.old).");
            return 1;
        }
    }


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

    // Nav library dependencies (nav.toml [dependencies]): resolve the transitive
    // graph (local paths + git clones into the shared cache) and emit
    // build/nav-deps-libs.cmake so the generated CMake add_subdirectory()s and
    // links each library against this project's board context.
    const fs::path libdeps_cmake = fs::path("build") / "nav-deps-libs.cmake";
    // Hooks that must run before NavHAL is added (e.g. -DSUBMODULE so a
    // vector-owning library like vaios takes the SysTick/PendSV handlers).
    const fs::path libdeps_pre_cmake = fs::path("build") / "nav-deps-pre.cmake";
    std::vector<ResolvedLib> resolved; // kept for the HAL API check below
    if (!cfg->dependencies.empty()) {
        std::vector<std::string> direct;
        auto libs = resolve_library_deps(ctx, *root, *cfg, &direct);
        if (!libs) {
            ui::error("Could not resolve library dependencies. See errors above.");
            return 1;
        }

        // Pin every git library to the commit its ref resolved to (nav.lock
        // "git"), and refuse a ref that now resolves somewhere else.
        const fs::path lock_path = *root / "nav.lock";
        Lockfile lock;
        if (fs::exists(lock_path)) {
            auto loaded = load_lockfile(lock_path);
            if (!loaded) {
                ui::error("nav.lock exists but could not be parsed; fix or remove it and rebuild.");
                return 1;
            }
            lock = std::move(*loaded);
        }
        auto pins = reconcile_git_lock(lock, *libs);
        for (const auto& n : pins.notes) ui::info("nav.lock: " + n);
        if (!pins.errors.empty()) {
            ui::error("A pinned git dependency no longer matches nav.lock:");
            for (const auto& e : pins.errors) ui::error("  " + e);
            ui::error("The ref moved upstream, or the shared cache holds another commit. If the new");
            ui::error("commit is intended, delete that entry from nav.lock and rebuild; otherwise");
            ui::error("pin a ref that still points at the locked commit.");
            return 1;
        }
        if (pins.changed && !save_lockfile(lock, lock_path)) {
            ui::error("Failed to write nav.lock.");
            return 1;
        }
        // A library may carry its own copy of something the build provides
        // once (NavHAL, or another library) as a git submodule. Only one copy
        // is built; say so when the bundled copy is a different commit.
        for (const auto& w : check_bundled_copies(*libs, kNavhalRepoUrl, git_head_commit(ctx, navhal_dir)))
            ui::warning(w);
        if (!write_libdeps_cmake(*libs, direct, libdeps_cmake)) {
            ui::error("Failed to write " + libdeps_cmake.string() + ".");
            return 1;
        }

        // Capability check: the app's .config is the single authority that
        // configures NavHAL. Every dependency that ships a NavHAL config
        // declares the capabilities it needs; the app .config must satisfy them
        // all. They are unioned in at `nav lib add`, but self-heal here too so a
        // dep added before this existed (or a hand-edited .config) still builds:
        // append any purely-missing capabilities, and hard-error only on a
        // genuine value conflict (the app sets a key to something a dep can't use).
        if (fs::exists(app_config)) {
            auto have = parse_kconfig(app_config);
            std::vector<std::string> missing, conflicts;
            for (const auto& lib : *libs) {
                auto lc = find_navhal_config(lib.src);
                if (!lc) continue;
                auto diff = diff_requirements(have, parse_kconfig(*lc));
                hold_target_choices(diff, targets.choice_symbols);
                for (const auto& m : diff.missing) {
                    missing.push_back(m);
                    have[m.substr(0, m.find('='))] = m.substr(m.find('=') + 1); // seen now
                }
                for (const auto& c : diff.conflicts) {
                    if (targets.choice_symbols.count(c.key))
                        conflicts.push_back(lib.name + " is configured for " + c.key +
                                            ", which board '" + board->id + "' does not select");
                    else
                        conflicts.push_back(lib.name + " needs " + c.key + "=" + c.need +
                                            " but .config has " + c.have);
                }
            }
            if (!conflicts.empty()) {
                ui::error("Your .config conflicts with capabilities required by dependencies:");
                for (const auto& c : conflicts) ui::error("  " + c);
                ui::error("Reconcile the value(s) in .config (nav config), or switch board with");
                ui::error("'nav board use <id>', and rebuild.");
                return 1;
            }
            if (!missing.empty()) {
                std::ofstream f(app_config, std::ios::app);
                f << "\n# Unioned by nav build from dependency requirements\n";
                for (const auto& m : missing) f << m << "\n";
                ui::info("Unioned " + std::to_string(missing.size()) +
                         " capability requirement(s) from dependencies into .config.");
            }
        }

        std::error_code rmec;
        const bool owns_vectors =
            std::any_of(libs->begin(), libs->end(), [](const ResolvedLib& l) { return l.navhal_submodule; });
        if (owns_vectors) {
            std::ofstream pre(libdeps_pre_cmake);
            pre << "# Auto-generated by `nav build`: a dependency owns the CPU vectors.\n"
                << "# Build NavHAL as a submodule so it cedes SysTick/PendSV/SVCall/HardFault.\n"
                << "add_compile_definitions(SUBMODULE)\n";
            ui::info("A dependency owns the CPU vectors — building NavHAL with -DSUBMODULE.");
        } else {
            fs::remove(libdeps_pre_cmake, rmec);
        }
        ui::info("Linked " + std::to_string(libs->size()) + " library dependency(ies).");
        resolved = std::move(*libs);
    } else {
        // No deps now — clear any stale graph from an earlier build.
        std::error_code rmec;
        fs::remove(libdeps_cmake, rmec);
        fs::remove(libdeps_pre_cmake, rmec);
    }

    // HAL contract check: a project or library that declares
    // [abi].hal_api_version must match the NavHAL it is built against.
    {
        std::vector<std::string> mismatches;
        const auto have = read_hal_api_version(navhal_dir);
        auto check = [&](const std::string& who, int want) {
            if (want == 0) return;
            if (!have) {
                mismatches.push_back(who + " declares hal_api_version = " + std::to_string(want) +
                                     ", but extern/NavHAL/include/navhal.h defines no HAL_API_VERSION");
            } else if (*have != want) {
                mismatches.push_back(who + " targets hal_api_version = " + std::to_string(want) +
                                     ", NavHAL provides " + std::to_string(*have));
            }
        };
        check(cfg->project_name.empty() ? "this project" : cfg->project_name, cfg->hal_api_version);
        for (const auto& lib : resolved) check(lib.name, lib.hal_api_version);
        if (!mismatches.empty()) {
            ui::error("HAL API version mismatch:");
            for (const auto& m : mismatches) ui::error("  " + m);
            return 1;
        }
    }

    if (fs::exists(navhal_dir)) {
#ifdef _WIN32
        ::_putenv_s("srctree", navhal_dir.string().c_str());
#else
        ::setenv("srctree", navhal_dir.string().c_str(), /*overwrite=*/1);
#endif
    }

    // Run cmake quietly (silent=true captures output without streaming it), so a
    // successful build stays terse like a container build. On failure we replay
    // the captured log so the diagnostics aren't lost.
    // Self-heal the board's cross compiler: if it isn't on PATH, install it
    // (package manager or nav's downloader) so a freshly-created project builds
    // without a separate manual toolchain step. No-op once present.
    if (!board->compiler.empty()) {
        ToolchainManager tm;
        ToolRequirement req{board->compiler, board->compiler, true};
        if (!tm.probe_tool(ctx, req).is_found) {
            ui::info("Cross compiler '" + board->compiler + "' not found — installing it.");
            install_tools(ctx, {board->compiler});
        }
    }

    // NavHAL's Kconfig step (tools/kconfig.py) imports kconfiglib and runs
    // under ${Python3_EXECUTABLE}. Hand CMake an interpreter that has it --
    // nav's own venv if the system Python lacks it -- rather than letting
    // find_package(Python3) pick one that fails deep inside configure.
    const std::string kconfig_python = ensure_kconfig_python(ctx);
    if (kconfig_python.empty()) return 1;

    // Windows ships no `make`, so drive cmake through Ninja (provisioned above).
    std::vector<std::string> configure_cmd = {"cmake", "-S", ".", "-B", "build",
                                              "-DPython3_EXECUTABLE=" + kconfig_python};
#ifdef _WIN32
    configure_cmd.push_back("-G");
    configure_cmd.push_back("Ninja");
#endif

    ui::step("Configuring", "cmake");
    auto conf_res = ctx.execute(configure_cmd, "", /*silent=*/true);
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
