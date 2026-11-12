# Chapter 4: Architecture

nav is a C++ program built from three parts: a thin entry point in `src/cli/main.cpp`, a static library of core modules (`nav_core`, sources under `src/core/`), and two JSON registry files in `data/` that are baked into the binary at build time. This chapter describes how those parts fit together.

## Commands

Every command verb is a class derived from `ICommand`, declared in `include/nav/core/command.hpp`. The interface has three members: `run(IExecutionContext& ctx, const std::vector<std::string>& args)`, `help_text()`, and an optional `register_flags(CLI::App&)` hook that defaults to a no-op. The header's comment says the default exists so that "existing commands keep working while we migrate them piecemeal".

`main.cpp` creates one instance of each class and stores it in a `std::map` keyed by verb name. For each entry it adds a CLI11 subcommand using the class's help text, calls `prefix_command()` on it, and calls `register_flags`. Because of `prefix_command()`, everything after the verb is collected raw and passed to `run` as `args`; the source comment notes that some verbs still parse their own argv, and that global flags (`--color`, `--no-color`, `--verbose`/`-v`, `--quiet`/`-q`, `--cwd`) must come before the verb. After parsing, `main` builds a `HostExecutionContext` and calls `run` on the selected command.

All eleven classes are declared in `command.hpp`; each `run` is defined in its own file in `src/core/commands/`:

| Verb | Class | Implementation |
|---|---|---|
| `create` | `CreateCommand` | `src/core/commands/create.cpp` |
| `build` | `BuildCommand` | `src/core/commands/build.cpp` |
| `regen` | `RegenCommand` | `src/core/commands/regen.cpp` |
| `upload` | `UploadCommand` | `src/core/commands/upload.cpp` |
| `monitor` | `MonitorCommand` | `src/core/commands/monitor.cpp` |
| `clean` | `CleanCommand` | `src/core/commands/clean.cpp` |
| `check` | `CheckCommand` | `src/core/commands/check.cpp` |
| `update` | `UpdateCommand` | `src/core/commands/update.cpp` |
| `board` | `BoardCommand` | `src/core/commands/board.cpp` |
| `lib` | `LibCommand` | `src/core/commands/lib.cpp` |
| `config` | `ConfigCommand` | `src/core/commands/config.cpp` |

## Core modules

The headers are in `include/nav/core/` and the implementations in `src/core/`. The modules fall into these groups.

**Project description.** `config` defines `ProjectConfig` (the parsed `nav.toml`), `Dependency`, and the `[build]` structures, and provides `find_project_root` and `load_project_config`. `variant` applies `[variant.<name>]` tables on top of `[build]` and decides where a variant builds (`build`, or `build/<variant>`). `project_name` validates project names.

**Registry and boards.** `registry` loads the registry (described below). `board` defines the `Board` struct and `BoardCatalog`, and renders `build/nav-board.cmake`. `runner` resolves flash runners: the default runner for a board, the variables that fill a command template, and the matching `objcopy`. `probe` finds ST-Link debug probes (through sysfs, on Linux only) and applies the probe-selection precedence; `serial` lists `ttyACM` and `ttyUSB` devices.

**Tools and provisioning.** `toolchain` holds `ToolchainManager`, which builds the lists of required tools and probes them through an execution context, plus package-manager detection. `provision` installs tools, either through a detected system package manager or by nav's own portable download into `<nav home>/toolchains`, and `inject_toolchains_into_path` (called early in `main`) prepends those directories to `PATH`. `platform` derives a host platform key from `uname` output.

**NavHAL and Kconfig.** `navhal` resolves a project's `[navhal]` git URL and ref to an immutable per-commit checkout in the shared cache. `cache` defines the layout under the nav home directory. `lockfile` reads and writes `nav.lock`, which pins git libraries and the NavHAL commit. `board_config` ties the project's `.config` to the board named in `nav.toml`. `navconfig` parses `.config` files and merges dependency requirements into the app's view of the config. `appkconfig` builds a merged Kconfig tree when an app ships its own `Kconfig`, and `kconfig_python` finds or creates a Python that can import kconfiglib.

**Dependencies and build planning.** `libdeps` resolves the library dependency graph; `appbuild` expands `[build]` globs into a build plan and renders `build/nav-app.cmake`; `deps` renders `nav-deps.cmake` from the lockfile and the package cache; `build_log` deduplicates compiler warnings in captured build output. `semver` implements SemVer 2.0.0 parsing and comparison. `ui` provides the colored output helpers.

`index` (a package-index parser) is also compiled into `nav_core`; `lockfile` and `deps` use its types. Its role beyond that is not described here.

## The execution context

Commands never spawn processes directly. They call `IExecutionContext::execute`, declared in `include/nav/core/execution_context.hpp`:

```cpp
virtual CommandResult execute(const std::vector<std::string>& cmd,
                              const std::string& working_dir = "",
                              bool silent = false,
                              std::chrono::milliseconds timeout = kNoTimeout) = 0;
```

The command is an argv vector. `CommandResult` holds an `exit_code` and an `output` string, which is the child's stdout and stderr merged onto one pipe. `kNoTimeout` is zero, meaning wait for the child; the header says long-running calls such as build, clone and install should use it and short probes should pass a real deadline.

`HostExecutionContext` is the production implementation. It forwards to `run_raw_command` in `src/core/execution_context.cpp`, which has separate Windows and POSIX paths. The POSIX path creates a pipe, forks, duplicates the pipe onto the child's stdout and stderr, optionally changes directory, and calls `execvp`. It enforces the timeout with a poll deadline. With `silent` false the output is echoed to stdout as it arrives; with `silent` true it is only captured, capped at 1 MiB (`kSilentCap`) with a truncation marker appended.

The test double is `MockExecutionContext` in `tests/mock_execution_context.hpp`. It records every call (argv, working directory, `silent`, `timeout`) in a `calls` vector, then either invokes a user-supplied `handler` or returns `scripted_result`, which defaults to exit code 0 and empty output. Tests can therefore check the commands nav would run without running them.

## Generated CMake

nav produces CMake in two ways: project files written from templates, and per-build files written by `nav build`.

**Templates.** The sources are in `templates/` (`cmakelists.txt.in`, `cmakelists_lib.txt.in`, `nav.toml.in`, `nav_lib.toml.in`, `dot_config.in`, `main.c.in`, `lib_header.h.in`, `lib_source.c.in`). The top-level `CMakeLists.txt` reads each into a variable and `templates/embedded.hpp.in` is configured into a generated header that holds them as raw string literals in the `nav::templates` namespace. `nav create` substitutes `{{PROJECT_NAME}}`, `{{ARCH}}`, `{{VENDOR}}` and `{{BOARD}}` into the templates with a plain find-and-replace (`substitute` in `create.cpp`). `nav regen` calls `render_app_cmakelists`, which replaces only `{{PROJECT_NAME}}`, and refuses to overwrite a `CMakeLists.txt` that nav did not generate.

**Per-build files.** `BuildCommand::run` writes several files into the build directory before invoking CMake: `nav-board.cmake` (from the `Board`, whose header comment says "Auto-generated by `nav build` from the board registry. Do not edit."), `nav-app.cmake` (from `[build]`), `nav-deps.cmake` (from `nav.lock`, when it exists), and `nav-deps-libs.cmake` and `nav-deps-pre.cmake` for library dependencies. When the app has a root `Kconfig`, `nav-app-kconfig.cmake` is written as well. The generated `CMakeLists.txt` includes these files; `cmakelists_supports_app_build` detects an older file by checking whether it mentions `nav-app.cmake`. `build` then runs `cmake -S . -B <build dir>` through the execution context (adding `-G Ninja` on Windows only) and `cmake --build <build dir> --parallel`.

## The registry data files

The registry is two JSON files in `data/`, each with `"schema": 1`.

`data/toolchain.json` holds the four core, board-independent tools. Its `"core"` array is `["cmake", "git", "ninja", "python"]`, and its `"tools"` object gives each one a `binary`, a pinned `version`, per-package-manager `packages`, and a Windows `download`. `ToolchainManager::get_system_requirements` reads `core_tool_ids()` from the registry, with a comment saying this keeps the list in sync with what `nav update` can provision.

`data/boards.json` holds everything board-specific, in three objects. `"tools"` has the compiler and flasher tools: `arm-none-eabi-gcc`, `st-flash`, `avr-gcc`, `openocd` and `avrdude`, with versions and packages in the same shape as above. `"runners"` defines `st-flash`, `openocd` and `avrdude` as a tool, an image type and an argument template. `"boards"` defines `nucleo_f401re`, `navixsmf401re`, `navixdev` and `atmega328p`, each with architecture, compiler, flash tool, default runner, compile and link flags, and supported frameworks.

`registry.cpp` merges these in several steps. The baked defaults come from `nav::config::kBakedToolchainJson` and `kBakedBoardsJson`, which CMake generates from the two files. On first run nav writes them to `~/.nav/` if absent, then merges any user content over the defaults. Board entries merge field by field, so keys a user file omits keep the baked values. The loader for `boards.json` also reads its `"tools"` and `"runners"` objects, so the tool table is the union of both files.

## Sources

- `include/nav/core/command.hpp`: `ICommand` and the eleven command classes.
- `src/cli/main.cpp`: command map, `prefix_command()` registration, `HostExecutionContext` creation.
- `src/core/commands/*.cpp`: the `run` definitions in the table (`regen.cpp` for the `render_app_cmakelists` call).
- `include/nav/core/execution_context.hpp`, `src/core/execution_context.cpp`: `IExecutionContext`, `CommandResult`, `HostExecutionContext`, `run_raw_command`.
- `tests/mock_execution_context.hpp`: `MockExecutionContext`, `ExecutedCall`.
- `include/nav/core/registry.hpp`, `src/core/registry.cpp`: `Registry`, `build_registry`, `load_boards`, `load_toolchain`, `merge_json`.
- `include/nav/core/board.hpp`, `src/core/board.cpp`: `Board`, `render_board_cmake`, `write_board_cmake`.
- `include/nav/core/toolchain.hpp`, `src/core/toolchain.cpp`: `ToolchainManager::get_system_requirements`.
- `include/nav/core/` headers for `config`, `variant`, `project_name`, `runner`, `probe`, `serial`, `provision`, `platform`, `navhal`, `cache`, `lockfile`, `board_config`, `navconfig`, `appkconfig`, `kconfig_python`, `libdeps`, `appbuild`, `deps`, `build_log`, `semver`, `index`, `ui` (header comments).
- `src/core/appbuild.cpp`: `render_app_cmakelists`, `cmakelists_supports_app_build`.
- `src/core/commands/build.cpp`: `BuildCommand::run` (generated files, configure and build calls).
- `src/core/commands/create.cpp`: `substitute`, template use.
- `CMakeLists.txt`: embedded templates and embedded registry sections.
- `templates/embedded.hpp.in`, `templates/embedded_config.hpp.in`.
- `data/toolchain.json`, `data/boards.json`.
