# Chapter 6: Configuration with Kconfig

A nav firmware project is configured by a single file in the project root, `.config`. The README describes an app as "`nav.toml` + `.config` + sources". The file uses Kconfig syntax (`CONFIG_NAME=y` lines). NavHAL owns the Kconfig tree that gives those lines meaning, and nav's job is to create the file, let you edit it, check it against the board, and hand a derived copy to the build. This chapter follows that path.

## Where .config comes from

`nav create <name> [--board <id>]` writes the first `.config`. The README says it is "seeded from NavHAL's defconfig for the board (default `nucleo_f401re`)". The work is done by `seed_board_config()` in `src/core/board_config.cpp`, which does the following:

1. It scans NavHAL's `src/board/*/Kconfig` for a line of the form `default "<name>" if BOARD_<SYM>`. That line maps a board name to its Kconfig symbol.
2. It searches `cmake/defconfigs/` in the NavHAL checkout for the first `.defconfig` file, in sorted order, that sets that board symbol to `y`. If none exists it fails with a message saying NavHAL ships no defconfig selecting the symbol.
3. It writes the defconfig to the output path, then a `# nav starter set` comment, then nav's starter options.
4. If a Python with kconfiglib is available, it loads the fragment into kconfiglib, lets Kconfig fill in every default, and writes the full file back with a header reading "Seeded by nav for board ... from ...". The header also says "Edit with 'nav config'." The code comments call this the same expansion as `make <board>_defconfig`.
5. It re-reads the expanded file and checks that it selects exactly the requested board.

If no Python is available the file is left unexpanded, and `nav create` warns "Writing an unexpanded .config; 'nav build' expands it."

The starter options come from `templates/dot_config.in`. Its own comment says they are added "on top of the board's NavHAL defconfig; a board that lacks one of them ignores it". The file turns on `CONFIG_USE_FPU` and a set of drivers (`CONFIG_DRV_CLOCK`, `CONFIG_DRV_INTERRUPT`, `CONFIG_DRV_GPIO`, `CONFIG_DRV_TIMER`, `CONFIG_DRV_UART`, `CONFIG_DRV_DMA`, `CONFIG_DRV_CRC`, `CONFIG_DRV_DWT`, `CONFIG_DRV_FPU`, `CONFIG_DRV_I2C`, `CONFIG_DRV_SPI`, `CONFIG_DRV_PWM`, `CONFIG_DRV_SDIO`, `CONFIG_DRV_FLASH`), plus `CONFIG_SAMPLE_NONE=y`. `nav board use` seeds from the same template.

## Editing with nav config

`nav config` is implemented in `src/core/commands/config.cpp`. It finds the project root, makes sure `extern/NavHAL` is provisioned (restoring the link from the shared cache as `nav build` does), and checks that `extern/NavHAL/Kconfig` exists. It then runs kconfiglib's `menuconfig` module as `<python> -m menuconfig <Kconfig>`, started with `std::system` so the curses interface owns the terminal.

`srctree` is set to the NavHAL directory, so the tree's `source` globs resolve there, and `KCONFIG_CONFIG` is set to the project's `.config`, so edits land in the project. If `MENUCONFIG_STYLE` is unset, nav sets a theme based on the terminal's default colours. The on-screen hint reads "Arrow keys / Enter to navigate, Space to toggle, ? for help, Q to save & quit."

If the project has a root `Kconfig` file (`app_has_kconfig()` tests for that), `nav config` instead opens the merged tree in `build/nav-Kconfig`, described below.

After a save, `nav config` compares the requirements of each path dependency that has a NavHAL config file with the saved `.config` (git dependencies are skipped; the code comment says they are validated at build time). It prints "Saved, but a dependency now needs capabilities your .config lacks:" followed by one line per missing item. It finishes with "Configuration saved to .config. Run 'nav build' to apply it."

The README says `nav build` "Rebuilds when `.config` changes". I did not locate where that is implemented; the code does show that every `nav build` rewrites the derived config described below.

## What nav build does with .config

Before dependency requirements are considered, `nav build` checks the board. In `src/core/commands/build.cpp` it calls `check_config_board()` with the board from `nav.toml` and the parsed `.config` (or the variant's config file, via `variant_config_path()`). The check works as follows:

It passes only if exactly one board symbol is `y` and it is the `nav.toml` board. Otherwise it reports ".config selects no board", ".config selects" followed by the list, or that NavHAL has no board of that name.

On failure the build prints "nav.toml targets board '<id>', but <reason>." and then "Regenerate .config for it with 'nav board use <id>' (the old file is kept as .config.old)." The README's `nav board` row says the same thing. `nav board use` seeds a fresh file, rewrites `[target]` in `nav.toml`, and renames the old `.config` to `.config.old`. For a library project it only touches `.config` if one exists. The check runs before dependency requirements are merged, so a stale `.config` is reported, not patched.

### nav-effective.config

The build never writes the tracked `.config`. Instead it writes `nav-effective.config` into the build directory. There are two paths, both in `build.cpp`:

- If dependencies declare NavHAL requirements that `.config` lacks, they are appended to a copy of `.config` under the comment "Added by nav build from dependency requirements". The build prints how many were merged and that `.config` is not modified. If the app explicitly sets a different value, or marks the symbol "is not set", the app wins and a warning names the dependency and symbol. The one hard error is a board-choice symbol the board does not select, which ends with "Switch board with 'nav board use <id>', and rebuild."
- Otherwise the file is a plain copy of `.config`, overwritten on every build, so anything NavHAL writes back does not carry over.

The configure step receives `-DNAV_APP_CONFIG=` pointing at this file. NavHAL takes it by path (`-DNAVHAL_CONFIG_FILE`, NavHAL 0.4.2 or newer, per the README); with an older pinned NavHAL, nav prints a note and the project shares `extern/NavHAL/.config`.

## An application Kconfig

A project that ships its own root `Kconfig` gets a further step in `prepare_app_kconfig()`. `render_merged_kconfig()` writes `build/nav-Kconfig` with `mainmenu "<project> configuration"`, then `source` lines for NavHAL and for each dependency that has a `Kconfig`. The app's file is last, wrapped in `menu "Application options"`; `tests/test_appkconfig.cpp` checks this ordering. A generated script, `nav-kconfig.py`, loads the config into kconfiglib with `warn_assign_undef` on, so an unknown `CONFIG_X` is reported and not dropped. Undefined-symbol and invalid-value warnings become errors, and the build fails with ".config does not match the Kconfig tree:". Symbols under "Application options" are written to `app_autoconf.h` and `nav-app-kconfig.cmake`; the resolved config is `build/nav.config`.

## Board- and Kconfig-conditional build settings

The README section "Board- and Kconfig-conditional settings" describes `[build.board.<board>]` and `[build.kconfig.<SYMBOL>]` tables in `nav.toml`. They take the same keys as `[build]`. The Kconfig tables read the resolved `build/nav.config`, or the app `.config` when the project has no Kconfig. In `build.cpp` the planner receives that file's parsed contents (`plan_app_build`) after the app Kconfig step, which matches the README.

## The Python side

Every kconfiglib use (`nav config`, the seed expansion, the app Kconfig step, and NavHAL's own `tools/kconfig.py` during CMake configure) goes through `src/core/kconfig_python.cpp`.

`find_kconfig_python()` probes interpreters by running `import kconfiglib`. It tries the venv's interpreter first, if it exists, then `python3`; on Windows it tries `python3` and then `python`. `ensure_kconfig_python()` returns the first that works. Otherwise it creates a venv and installs the pinned requirement, `kconfiglib==14.1.0` (`kKconfiglibRequirement` in `include/nav/core/kconfig_python.hpp`). It removes any half-made venv first, and if creating the venv or installing fails it deletes the venv and prints an error (the venv error suggests installing `python3-venv` on Debian/Ubuntu).

The venv location is `kconfig_venv_dir()`, which returns `nav_home_base() / "python"`. In `src/core/cache.cpp`, `nav_home_base()` is `$NAV_HOME` if set and non-empty, else `$HOME/.nav`, else (Windows) `%USERPROFILE%\.nav`, else a relative `.nav`. So by default the venv is `~/.nav/python`. `nav build` calls `ensure_kconfig_python()` before cmake, so the install happens on first use.

## Library Kconfig fragments

`docs/LIBRARY_ABI.md` section 6 is part of a "v1 (draft)" contract. It says a library's `Kconfig` is appended under a generated `menu "<library-name>"` block, with symbols prefixed by the uppercased library slug and a list of validation rules. The code differs. `render_merged_kconfig()` adds each dependency's `Kconfig` as a bare `source` line with no per-library `menu`, and I found no enforcement of the prefix, `mainmenu`, `source` or `select DRV_*` rules in `src`. Treat section 6 as intent, not current behaviour.

## Sources

- `src/core/commands/config.cpp`: `ConfigCommand::run`
- `src/core/appkconfig.cpp`: `app_has_kconfig`, `render_merged_kconfig`, `write_merged_kconfig`, `kconfig_validator_script`, `prepare_app_kconfig`
- `src/core/kconfig_python.cpp`: `kconfig_venv_dir`, `find_kconfig_python`, `ensure_kconfig_python`
- `include/nav/core/kconfig_python.hpp`: `kKconfiglibRequirement`
- `src/core/cache.cpp`: `nav_home_base`
- `src/core/board_config.cpp`: `scan_navhal_targets`, `find_board_defconfig`, `check_config_board`, `seed_board_config`
- `src/core/commands/build.cpp`: board check, dependency merge, `nav-effective.config`, app Kconfig step, cmake configure arguments
- `src/core/commands/create.cpp`, `src/core/commands/board.cpp`: `.config` seeding, `board_use`
- `templates/dot_config.in`, `templates/cmakelists.txt.in`
- `README.md`: Features, Commands, "Board- and Kconfig-conditional settings", NavHAL config-by-path paragraph
- `docs/LIBRARY_ABI.md`: section 6 (v1 draft)
- `tests/test_appkconfig.cpp`: `MergedKconfigSourcesNavhalDepsThenAppInMenu`
