# Chapter 8: Building

Chapter 4 listed the files `nav build` writes and chapter 5 the `[build]` keys. This chapter follows one `nav build` from the first line of `BuildCommand::run` to the last, and then covers the neighbouring commands `nav regen` and `nav clean`, the template override `NAV_TEMPLATE_DIR`, and what the source says about Windows.

## Flags

`build` registers no CLI11 flags of its own (`register_flags` keeps its no-op default); `main.cpp` hands everything after the verb to `run`, which parses it by hand. The accepted forms are `--variant <name>`, `--variant=<name>`, and `-v` or `--verbose`. Anything else prints `Usage: nav build [--variant <name>] [-v|--verbose]` and exits 1. Note that the global `--verbose`/`-v` must come before the verb, while the build's own `-v` comes after it; they are parsed in different places.

```
nav build
nav build --variant hwtest
nav build -v
```

## The sequence

The order below is the order of the code, which differs a little from a tidy checklist.

1. **Project root.** `find_project_root` and `load_project_config`. With no `nav.toml` it stops with "Build halted! No nav.toml found in this directory or any parent." and the process changes directory to the root.
2. **Variant.** If `--variant` was given, a library is refused ("--variant applies to firmware projects; this is a library."), `apply_variant` folds the variant into the config, and a `kconfig` file named by the variant must exist. The build directory becomes `build/<name>`.
3. **Board check.** `[target].board` must be set and must be in the catalog; the build directory is created if absent and `nav-board.cmake` is written there.
4. **NavHAL sync.** `sync_project_navhal` restores `extern/NavHAL` from the commit pinned in `nav.lock` (pinning it on the first build). A comment in the source says a missing or dangling link is repaired here "rather than failing deep inside cmake".
5. **`.config` against the board.** If the `.config` (or the variant's) exists, it must select the board from `nav.toml`; otherwise the build stops and tells you to run `nav board use <id>`. This runs before dependency requirements are merged, so a stale file is reported and not patched.
6. **Template checks (firmware only).** `CMakeLists.txt` is read and tested for markers. An older generated file that lacks support for variants, `[build]` keys, `deps_opt`/`syscalls`/`linker_fragment_position`, or the app Kconfig makes the build stop with a message telling you to run `nav regen`.
7. **Dependencies.** `nav-deps.cmake` is written when `nav.lock` loads. If `nav.toml` has `[dependencies]`, the library graph is resolved, git pins are reconciled with `nav.lock`, `nav-deps-libs.cmake` is written, and dependency capability requirements are merged into `build/nav-effective.config`. Without dependencies, stale `nav-deps-libs.cmake` and `nav-deps-pre.cmake` are deleted. The HAL API version check follows.
8. **Effective config.** With no dependency merge, `.config` is still copied to `build/nav-effective.config`; the comment says this happens "on every build, so a write-back never carries over to the next one". `srctree` is set to the NavHAL checkout.
9. **App Kconfig.** Only if the project has a root `Kconfig`: `ensure_kconfig_python`, then `prepare_app_kconfig`, which writes `app_autoconf.h` in the build directory. Otherwise any stale `nav-app-kconfig.cmake` is removed. Chapter 6 covers this step.
10. **`nav-app.cmake`.** Planned after Kconfig, because `[build.kconfig.<SYMBOL>]` tables need the resolved config. Prints "App sources: N file(s)."
11. **Toolchain.** This check comes late. If the board's compiler is not found by `probe_tool`, nav prints "Cross compiler '<name>' not found — installing it." and calls `install_tools` for it. Only the board's compiler is checked here; the other tools are not probed by `build`.
12. **Configure.** `cmake -S . -B <build dir> -DPython3_EXECUTABLE=... -DNAV_APP_CONFIG=... -DNAV_NAVHAL_HAS_CONFIG_FILE=ON|OFF`, shown as "Configuring cmake".
13. **Compile.** `cmake --build <build dir> --parallel`, shown as "Compiling cmake --build".

Both cmake calls run with `silent` set unless `-v` was given. On failure the captured output is printed to stderr, followed by "Configure failed." or "Build failed.", and the cmake exit code is returned.

## Libraries and apps

`cfg->is_library()` changes three things in the code read here: `--variant` is refused, the template-marker checks and `nav-app.cmake` are skipped, and the template is `cmakelists_lib.txt.in`. That template builds a static archive from `src/*.c`, `src/*.cpp` and `src/*.S` (a `CONFIGURE_DEPENDS` glob) and, built standalone, sets up its own board and NavHAL context. It has no link, flash or size step. The rest of the sequence, including NavHAL sync and the board check, is shared. `nav regen` does not apply to libraries.

## Build directory per variant

`variant_build_dir` returns `build` for the base build and `build/<variant>` otherwise, so variants sit beside the base build and never touch it. Each directory gets its own `nav-board.cmake`, `nav-effective.config` and generated cmake files (see chapter 4). A variant may rename the firmware through `output`; `variant_config_path` points at the variant's `kconfig` file when it has one, else `.config`.

## Warnings and image size

Because output is captured, a successful quiet build would hide warnings. `extract_build_warnings` (`src/core/build_log.cpp`) scans the captured text. A line outside indentation containing `: warning: ` opens a block, as does the GNU ld notice "missing --end-group; added as last command line option" (kept as a one-line block). Following lines are appended as context until the next warning, an error line (`: error: ` or `: fatal error: `), a make or ninja progress line (`[ 12%]`, `[12/50]`) or a gcc banner such as "In file included from". Blocks are compared as whole text, so an identical warning repeated is printed once. The count is `total` for occurrences and `blocks` for distinct ones. nav prints each block and then, for example, "N compiler warning(s) (M occurrences). Run 'nav build -v' for the full output." The occurrence count is shown only when it differs from N. This runs only on success and only without `-v`.

Image size comes from the template, not from nav: for an app with a known size tool, `cmakelists.txt.in` adds a POST_BUILD step running `size` on the ELF. `build` looks in the captured output for a line containing `text`, `dec` and `filename`, prints "Image size:" and that header with the row below it. The final line is "Build complete. Artifacts in <build dir>/", where the directory is `build` or `build/<variant>`.

## Does a second build notice a `.config` edit?

The README says `nav build` "Rebuilds when `.config` changes", and chapter 6 could not find the implementation. The code shows this much. Every `nav build` re-copies (or re-merges) `.config` into `build/nav-effective.config` and runs `cmake -S . -B ...` again; it never skips configure. The template hands that file to NavHAL (`NAVHAL_CONFIG_FILE`), or for a NavHAL that predates that option copies it to `extern/NavHAL/.config` with `configure_file`. Nothing in `build.cpp` or `cmakelists.txt.in` compares timestamps or hashes of `.config`. Whether the compiler then rebuilds the affected objects depends on dependency tracking inside NavHAL's own CMake, which is not in this repository. So: configure always re-runs; that edited options reach the compiled output is plausible but unverified here.

## `nav regen`

`regen` works on firmware projects only ("'nav regen' only supports executable projects."), needs a project name, and then looks at `CMakeLists.txt`:

- Not recognised as nav-generated (`cmakelists_is_nav_generated_app`): "CMakeLists.txt was not generated by nav; leaving it alone."
- Has both the app-build and dep-opts markers: "CMakeLists.txt is already current (nothing to do)."
- Otherwise it renames the file to `CMakeLists.txt.old` and writes a fresh one. If `.old` exists it refuses: "CMakeLists.txt.old already exists; move it aside and re-run."

The "already current" test checks only the app-build and dep-opts markers, while `build` also checks for variant and app-Kconfig markers; the source does not say whether a file can pass one test and fail the other.

## `nav clean`

`clean` finds the root and runs `remove_all("build")`. That deletes every variant directory too, since they live under `build/`. With no `build/` it reports there is nothing to do and exits 0; if removal throws, it prints "Cache purge failed! File handle may be locked by another process." and exits 1. It takes no arguments and does not touch `extern/` or the NavHAL cache.

## Templates and `NAV_TEMPLATE_DIR`

Templates are baked into the binary (chapter 4). `template_text(file, embedded)` in `template_source.cpp` returns the baked text unless the environment variable `NAV_TEMPLATE_DIR` is set and non-empty and names a directory containing `file`; then that file is read from disk and nav prints "Using template <path> (NAV_TEMPLATE_DIR)". A file missing from the directory silently falls back to the baked copy. The header calls it a way for "a dev build of nav" to test template edits without a reinstall. It is used by `render_app_cmakelists` (so `nav regen`), `nav create`, and `nav board` for `dot_config.in`.

## Windows

The only Windows difference in `build` is the configure step: `-G Ninja` is appended on `_WIN32`, with a comment that Windows ships no `make`. The comment says Ninja is "provisioned above", but `build.cpp` installs only the board's compiler; where Ninja comes from is not shown in this file (`toolchain.cpp` does list a package name for `ninja`). `srctree` is set with `_putenv_s` instead of `setenv`.

## Sources

- `src/core/commands/build.cpp`: `BuildCommand::run`
- `src/core/commands/regen.cpp`: `RegenCommand::run`
- `src/core/commands/clean.cpp`: `CleanCommand::run`
- `src/core/build_log.cpp`: `extract_build_warnings`
- `src/core/appbuild.cpp`: `cmakelists_supports_*`, `cmakelists_is_nav_generated_app`, `render_app_cmakelists`
- `src/core/variant.cpp`: `variant_build_dir`, `variant_config_path`
- `src/core/template_source.cpp`, `include/nav/core/template_source.hpp`: `template_text`
- `templates/cmakelists.txt.in`, `templates/cmakelists_lib.txt.in`
- `src/cli/main.cpp`: command registration
- `tests/test_build_log.cpp`, `tests/test_appbuild.cpp`
- `README.md`: command table
