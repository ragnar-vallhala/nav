#include "nav/core/appbuild.hpp"
#include "nav/core/appkconfig.hpp"
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
#include "nav/core/variant.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace nav::core {

int BuildCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    std::string variant_name;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--variant" && i + 1 < args.size()) variant_name = args[++i];
        else if (args[i].rfind("--variant=", 0) == 0) variant_name = args[i].substr(10);
        else { ui::error("Usage: nav build [--variant <name>]"); return 1; }
    }
    ui::step("Building", variant_name.empty() ? "Scanning project space and verifying build definitions..."
                                              : "Variant '" + variant_name + "': verifying build definitions...");

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
    // A variant folds into [build] and builds in build/<name>/, beside the base build.
    const fs::path bdir = variant_build_dir(variant_name);
    const Variant* variant = nullptr;
    if (!variant_name.empty()) {
        if (cfg->is_library()) {
            ui::error("--variant applies to firmware projects; this is a library.");
            return 1;
        }
        std::string err;
        auto eff = apply_variant(*cfg, variant_name, err);
        if (!eff) {
            ui::error("nav.toml: " + err + ".");
            return 1;
        }
        *cfg = std::move(*eff);
        variant = find_variant(*cfg, variant_name);
        std::error_code vec;
        if (!variant->kconfig.empty() && !fs::is_regular_file(variant_config_path(*cfg, variant_name), vec)) {
            ui::error("[variant." + variant_name + "].kconfig '" + variant->kconfig + "' does not exist.");
            return 1;
        }
        ui::info("Variant: " + variant_name + " -> " + bdir.generic_string() + "/");
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

    if (!fs::exists(bdir)) {
        fs::create_directories(bdir);
        ui::info("Created pristine build workspace sub-node.");
    }

    const fs::path board_cmake = bdir / "nav-board.cmake";
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
    // extern/NavHAL links to the checkout of the commit nav.lock pins for the
    // project's [navhal] ref (pinning it on the first build). A missing or
    // dangling link (cache cleared, fresh checkout) is restored here rather
    // than failing deep inside cmake.
    if (sync_project_navhal(ctx, *root, *cfg).empty()) {
        ui::error("Could not provision NavHAL. See errors above.");
        return 1;
    }
    // The board in nav.toml decides; .config must select that board and no
    // other. Checked before dependency requirements are merged in, so a stale
    // .config is reported instead of patched.
    const NavhalTargets targets = scan_navhal_targets(navhal_dir);
    const fs::path app_config = variant_config_path(*cfg, variant_name);
    if (fs::exists(app_config)) {
        const std::string why = check_config_board(targets, navhal_board_name(*board), parse_kconfig(app_config));
        if (!why.empty()) {
            ui::error("nav.toml targets board '" + board->id + "', but " + why + ".");
            ui::error("Regenerate .config for it with 'nav board use " + board->id +
                      "' (the old file is kept as .config.old).");
            return 1;
        }
    }


    // App sources/flags from nav.toml [build]: expand the globs into
    // build/nav-app.cmake. Only a CMakeLists.txt generated from a template that
    // includes that file can honour it; an older one keeps building src/main.c
    // unless nav.toml asks for more, in which case say how to regenerate rather
    // than silently ignoring the settings.
    const fs::path app_cmake = bdir / "nav-app.cmake";
    if (!cfg->is_library()) {
        std::string lists_text;
        if (std::ifstream in{"CMakeLists.txt"}) {
            std::ostringstream ss;
            ss << in.rdbuf();
            lists_text = ss.str();
        }
        const bool new_template = cmakelists_supports_app_build(lists_text);
        if (variant && !cmakelists_supports_variants(lists_text)) {
            ui::error("CMakeLists.txt was generated by an older nav and does not support build variants. "
                      "Run 'nav regen' to update it (the old one is kept as CMakeLists.txt.old).");
            return 1;
        }
        if (cfg->build.declared && !new_template) {
            ui::error("nav.toml sets [build] keys, but CMakeLists.txt was generated by an older nav "
                      "and ignores them. Run 'nav regen' to update it (the old one is kept as CMakeLists.txt.old).");
            return 1;
        }
        if (app_has_kconfig(*root) && !cmakelists_supports_app_kconfig(lists_text)) {
            ui::error("The project has a Kconfig, but CMakeLists.txt was generated by an older nav "
                      "and does not include its generated config. Run 'nav regen' to update it "
                      "(the old one is kept as CMakeLists.txt.old).");
            return 1;
        }
        std::error_code rmec;
        if (new_template) {
            std::string err;
            auto plan = plan_app_build(*cfg, err);
            if (!plan) {
                ui::error("nav.toml: " + err + ".");
                return 1;
            }
            std::ofstream f(app_cmake);
            f << render_app_cmake(*plan);
            if (!f.good()) {
                ui::error("Failed to write " + app_cmake.string() + ".");
                return 1;
            }
            ui::info("App sources: " + std::to_string(plan->sources.size()) + " file(s).");
        } else {
            fs::remove(app_cmake, rmec);
        }
    }

    // Locked dependencies: emit build/nav-deps.cmake from nav.lock so the
    // generated CMake picks up any cached library include/link inputs and the
    // pinned toolchain compiler. Populating the cache is out of scope here — the
    // registry fetch path has been removed; a git-based fetcher will replace it.
    if (auto lock = load_lockfile(*root / "nav.lock")) {
        const std::string platform = host_platform_key(ctx);
        const fs::path deps_cmake = bdir / "nav-deps.cmake";
        if (!write_deps_cmake(*lock, cache_root(), platform, board->compiler, deps_cmake)) {
            ui::error("Failed to write " + deps_cmake.string() + ".");
            return 1;
        }
    }

    // Nav library dependencies (nav.toml [dependencies]): resolve the transitive
    // graph (local paths + git clones into the shared cache) and emit
    // build/nav-deps-libs.cmake so the generated CMake add_subdirectory()s and
    // links each library against this project's board context.
    const fs::path libdeps_cmake = bdir / "nav-deps-libs.cmake";
    // Hooks that must run before NavHAL is added (e.g. -DSUBMODULE so a
    // vector-owning library like vaios takes the SysTick/PendSV handlers).
    const fs::path libdeps_pre_cmake = bdir / "nav-deps-pre.cmake";
    std::vector<ResolvedLib> resolved; // kept for the HAL API check below
    // App-level Kconfig (appkconfig.hpp): opt-in by shipping a root Kconfig.
    const bool app_kconfig = app_has_kconfig(*root);
    std::vector<ResolvedLib> app_libs;
    if (!cfg->dependencies.empty()) {
        std::vector<std::string> direct;
        auto libs = resolve_library_deps(ctx, *root, *cfg, &direct);
        if (!libs) {
            ui::error("Could not resolve library dependencies. See errors above.");
            return 1;
        }
        if (variant) {
            std::string verr;
            if (!apply_variant_options(*libs, *variant, verr)) {
                ui::error("nav.toml: " + verr + ".");
                return 1;
            }
            for (const auto& [lib, opts] : variant->dep_options)
                for (const auto& o : opts) ui::info("Variant option for " + lib + ": " + o);
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
            ui::error("commit is intended, run 'nav update' (or delete that entry from nav.lock) and");
            ui::error("rebuild; otherwise pin a ref that still points at the locked commit.");
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
        if (!write_libdeps_cmake(*libs, direct, libdeps_cmake, app_kconfig)) {
            ui::error("Failed to write " + libdeps_cmake.string() + ".");
            return 1;
        }

        app_libs = *libs;

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
                update_config_file(app_config, missing, "# Unioned by nav build from dependency requirements");
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
        if (variant && !variant->dep_options.empty()) {
            ui::error("nav.toml: [variant." + variant_name + ".dependencies." + variant->dep_options.begin()->first +
                      "]: this project has no such dependency.");
            return 1;
        }
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

    // App-level Kconfig: merge NavHAL's, the dependencies' and the app's Kconfig,
    // validate .config against it, and generate app_autoconf.h + nav.config.
    // Without a root Kconfig nothing here runs and any stale output is removed.
    {
        std::error_code kec;
        const fs::path app_cmake = bdir / "nav-app-kconfig.cmake";
        if (!app_kconfig) {
            fs::remove(app_cmake, kec);
        } else {
            std::string err;
            std::vector<std::string> warns;
            // Same interpreter as NavHAL's Kconfig step: one that has kconfiglib.
            const std::string py = ensure_kconfig_python(ctx);
            if (py.empty()) return 1;
            if (!prepare_app_kconfig(ctx, *root, *cfg, app_libs, navhal_dir, &err, &warns, py,
                                    *root / bdir, app_config)) {
                ui::error("App Kconfig: " + err);
                return 1;
            }
            for (const auto& w : warns) ui::warning(w);
            ui::info("App Kconfig merged (NavHAL + " + std::to_string(app_libs.size()) +
                     " dependency(ies) + app); wrote " + (bdir / "app_autoconf.h").generic_string() + ".");
        }
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
    // NAV_APP_CONFIG: the variant's alternative .config (empty = the project's).
    std::vector<std::string> configure_cmd = {
        "cmake", "-S", ".", "-B", bdir.generic_string(), "-DPython3_EXECUTABLE=" + kconfig_python,
        "-DNAV_APP_CONFIG=" + (variant && !variant->kconfig.empty() ? app_config.generic_string() : std::string{})};
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
    auto build_res = ctx.execute({"cmake", "--build", bdir.generic_string(), "--parallel"}, "", /*silent=*/true);
    if (build_res.exit_code == 0) {
        // The template's POST_BUILD step runs `size` on the ELF; the build is
        // captured silently, so surface that table (header + its row).
        {
            std::istringstream in(build_res.output);
            std::string line;
            while (std::getline(in, line)) {
                if (line.find("text") != std::string::npos && line.find("dec") != std::string::npos &&
                    line.find("filename") != std::string::npos) {
                    std::string row;
                    std::getline(in, row);
                    ui::info("Image size:");
                    std::cout << line << "\n" << row << "\n";
                    break;
                }
            }
        }
        ui::success("Build complete. Artifacts in " + bdir.generic_string() + "/");
    } else {
        std::cerr << build_res.output << std::endl;
        ui::error("Build failed.");
    }
    return build_res.exit_code;
}

} // namespace nav::core
