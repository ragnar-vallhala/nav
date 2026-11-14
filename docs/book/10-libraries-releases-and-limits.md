# Chapter 10: Libraries, releases and limits

This last chapter covers three things: what nav means by a library, how nav itself is released and tested, and where the tool stops. Dependency resolution and pinning are in chapter 7, and building is in chapter 8. Neither is repeated here.

## Libraries

A library is a project whose `nav.toml` has `[project].type = "library"`. As chapter 5 notes, only the exact string `"library"` counts. A library has no `main()`, builds a static archive, and is consumed through another project's `[dependencies]`.

### Scaffolding

`nav create --lib <name>` creates one. `--library` is accepted as a synonym, and both are matched in `CreateCommand::run` in `create.cpp`. With no name the default is `my-library`. The board defaults to `nucleo_f401re`, as for a firmware project. NavHAL is still resolved and linked into `extern/`, and `nav.lock` is still written. Then these files are laid down and the command prints "Library scaffold and starter code populated.":

| File | Source template | Content |
|---|---|---|
| `nav.toml` | `nav_lib.toml.in` | `[project]` with version `0.1.0` and `type = "library"`, `[target]` (used only when building standalone), `[build] backend = "cmake"`, a commented `[dependencies]` example, then a `[navhal]` block |
| `include/<name>.h` | `lib_header.h.in` | an `extern "C"` header declaring `int nav_lib_version(void)` |
| `src/<name>.c` | `lib_source.c.in` | defines it, returning 1 |
| `CMakeLists.txt` | `cmakelists_lib.txt.in` | see below |

No `.config` is written for a library. The scaffold has no `[abi]` or `[library]` table.

The generated `CMakeLists.txt` works in two ways, and decides by comparing `CMAKE_SOURCE_DIR` with its own directory. Standalone (`nav build` inside the library), it sets up the board, compiler and dependency context and calls `project()`. As a dependency, a consumer `add_subdirectory()`s it and it inherits the consumer's compiler, flags and NavHAL include path. It globs `src/*.c`, `src/*.cpp` and `src/*.S` recursively, builds `add_library(<name> STATIC ...)`, and exports `include/` as PUBLIC. `nav build --variant` on a library is refused with "--variant applies to firmware projects; this is a library."

### The `[library]` and `[abi]` tables

`config.cpp` reads two library-related values and nothing else from these tables:

- `[library].navhal_submodule` (boolean, default false). If any resolved library sets it, `nav build` writes `nav-deps-pre.cmake` defining `SUBMODULE` and prints "A dependency owns the CPU vectors — building NavHAL with -DSUBMODULE." NavHAL then does not emit its own SysTick, PendSV, SVCall and HardFault handlers.
- `[abi].hal_api_version` (integer, default 0).

The check in `build.cpp` reads `HAL_API_VERSION` from `extern/NavHAL/include/navhal.h` (`read_hal_api_version`) and compares it with the project and with every resolved library. A declared value of 0 (or absent) is skipped. A mismatch ends the build with this header, followed by one indented line per offender:

- `HAL API version mismatch:`
- `<name> targets hal_api_version = <want>, NavHAL provides <have>`
- `<name> declares hal_api_version = <want>, but extern/NavHAL/include/navhal.h defines no HAL_API_VERSION`

### The Library ABI document

`docs/LIBRARY_ABI.md` is headed "v1 (draft)", and says it must stay byte-identical with a copy in the NavHAL repository (that copy is not in this repository, so the match is not checked here). It defines: vocabulary (library, app, HAL contract, target triple); a manifest with required fields; file layout; capability requirements (`[requires].caps`); a build interface; an optional Kconfig fragment; lifecycle (`<library>_init` and `_deinit`); naming (symbols prefixed with the library slug); semver versioning; dependency, override, lockfile and bundled-submodule rules; library-versus-app differences; distribution (GitHub as host, release is a tag, source only, no server); validation rules; and open questions for v2. The following table checks each rule against `src/`.

| Rule in the document | In the code |
|---|---|
| `[project].type`, `[target]`, `[dependencies]` read | Enforced (chapter 5) |
| `[abi].hal_api_version` equals NavHAL's, when declared | Enforced at `nav build`. The document also says "at `nav add` time"; no such check was found in `lib.cpp` |
| `[library].navhal_submodule` | Enforced, as above |
| A git ref tagged `v*` must match `[project].version` | Enforced in `libdeps.cpp`: "A release tag must match it." |
| Empty `ref` warns | Enforced: "...is not reproducible. Pin a release tag: ref = \"vX.Y.Z\"." |
| `[overrides]`, conflict reporting, `nav lib tree`, bundled-submodule warnings | Present in `libdeps.cpp` and `lib.cpp` (chapter 7) |
| `[abi].abi_version`, `license` | not found in src |
| `[requires].caps` and optional caps, unknown-cap check | not found in src |
| `[dependencies.optional]` | not found in src |
| `nm` check of exported symbols, no redefinition of `NAVHAL_*`, `cmake/extras.cmake` and `[build].kconfig` hooks | not found in src |
| A library defining `main()` is rejected | not found in src |
| Kconfig: `menu "<library-name>"` wrapper, uppercase slug prefix, no `mainmenu`, no outside `source`, no `select DRV_*` | not found in src. `render_merged_kconfig` writes a bare `source` line per dependency (chapter 6) |

Three places where the document and the code disagree. The document says libraries do not ship a `CMakeLists.txt` and nav generates one; the scaffold ships one and nav does not generate it. The document says nav clones with `--recurse-sublibraries`; the code passes `--recurse-submodules --shallow-submodules`. The document says `ref` may be a commit; see the limits below. It also still uses `nav add`, while the command is `nav lib add`.

`docs/vaios-as-library.md` is a plan for using the vaios RTOS as a library, and its Tier A and B descriptions match the code for `navhal_submodule`, `options` and `nav-deps-pre.cmake`. Its recap says nav clones git dependencies with "no `--recurse-submodules`"; the current code does pass it, so that sentence is stale.

## Releases and testing

### Version and packaging

The version is the `project(Nav VERSION 1.3.1 ...)` line in the top-level `CMakeLists.txt`. It reaches the binary as `NAV_VERSION` and the CPack version. CPack metadata is shared; on Windows the generators are `NSIS;ZIP`, otherwise `DEB`. The `.deb` depends on `git, cmake, python3, build-essential`, plus `libc6` and similar lines from `dpkg-shlibdeps`. No `CPACK_PACKAGE_FILE_NAME` is set, so file names are CPack's defaults. The workflow expects `nav-<version>-Linux.deb`, `nav-<version>-win64.exe` and `nav-<version>-win64.zip`.

Local helpers: `scripts/build_deb.sh` configures, builds and runs `cpack -G DEB`. `scripts/install.sh` builds with tests off and installs `bin/nav` and `share/nav/boards` under a prefix (`/usr/local` by default, `--prefix` to change). `scripts/release.sh <version>` adds a `v` if missing, runs `git tag` and then `git push origin <tag>`. It does not compare the tag with the CMake version; the workflow checks only that file names carry the tag's version.

### The release workflow

`release.yml` triggers on a pushed `v*` tag, on `workflow_dispatch`, and on pull requests touching `release.yml` or `scripts/windows/**`. Only the tag push publishes. The other two are dry runs. It has three jobs.

1. `linux` builds Release on `ubuntu-22.04`, with a comment that a newer image produced a package that would not start on 24.04, runs `cpack -G DEB`, and uploads `dist-linux`.
2. `windows` runs `scripts/windows/ci-build.ps1` on `windows-latest`. The script downloads WinLibs MinGW-w64, NSIS and Ninja, reuses CMake and Git, builds a static `nav.exe`, runs `cpack -G 'NSIS;ZIP'`, and fails if no artifact over 100000 bytes appears. It uploads `dist-windows`.
3. `release` needs both. It requires exactly one `.deb`, one `win64.exe` and one `win64.zip`. On a tag push every `nav-*` file name must contain the tag version. It writes `SHA256SUMS` with `sha256sum nav-*`, verifies it with `sha256sum -c`, and uploads a `release-assets` workflow artifact. Only on a tag push does it publish: if `gh release view` finds the tag, it uploads with `--clobber`; otherwise it runs `gh release create ... --notes-file header.md --generate-notes --verify-tag`. The final step, "Mirror to get.navrobotec.in (not enabled)", only echoes "mirror upload not enabled; waiting for the publish contract". It uploads nothing.

A release carries four assets: `nav-<version>-Linux.deb`, `nav-<version>-win64.exe`, `nav-<version>-win64.zip` and `SHA256SUMS`. Verify a download by placing it beside `SHA256SUMS` and running `sha256sum -c SHA256SUMS`. On Windows the release notes suggest `Get-FileHash -Algorithm SHA256 <file>`, compared by eye. The checksum file sits next to the files it covers, so it detects a damaged download, not a tampered release.

### Tests

`ci.yml` runs on pushes and pull requests to `main` and `dev`, in three Linux jobs:

- `core` configures with `-DNAV_BUILD_TESTS=ON`, builds, runs `ctest`, then runs `nav --version`.
- `deb-floor` builds the `.deb` on `ubuntu-22.04`, requires a `Depends:` line naming `libc6`, and fails if the highest `GLIBC_` symbol in `build/nav` is above 2.35.
- `new-app-build` installs `gcc-arm-none-eabi` and Python, builds nav, and runs `scripts/e2e-new-app.sh build/nav`. That script creates an app in a throwaway `NAV_HOME`, builds it, and checks the ELF is ARM. It needs network. A second step sets `NAV_E2E_NAVHAL_REF=no-such-ref-xyz` and requires exit status 1, so the check can fail.

`tests/CMakeLists.txt` lists 30 `.cpp` files in the `nav_tests` GoogleTest executable (GoogleTest 1.14.0, fetched at configure time). Areas covered: configuration and `nav.toml` loading (`test_config`, `test_variant`, `test_appbuild`), Kconfig (`test_appkconfig`, `test_kconfig_python`, `test_board_config`, `test_navconfig`), dependencies and pinning (`test_deps`, `test_libdeps`, `test_depgraph`, `test_lockfile`, `test_navhal_pin`, `test_semver`, `test_cache`), boards and flashing (`test_board`, `test_runner`, `test_probe`, `test_serial`), and the project commands, templates and output (`test_create`, `test_update`, `test_template_source`, `test_ui`, `test_platform`, `test_toolchain`, `test_execution_context`). On Unix one more CTest, `TemplateAliasTargets`, runs `scripts/check-template-alias.sh`, which splices part of the app template into a throwaway CMake project and configures it. Individual test cases are not counted here.

## Known limits

- **No macOS asset or build.** The release workflow builds Linux and Windows only, and CI is Linux only. `toolchain.cpp` contains a `__APPLE__` branch that looks for `brew`, but nothing builds or tests it. The README says nav "builds on Linux and Windows".
- **Windows tests.** `ci.yml` has no Windows job. Windows is built only by the `windows` job in `release.yml`, on tags, manual runs and pull requests touching that workflow or `scripts/windows/**`. `ci-build.ps1` does not pass `NAV_BUILD_TESTS`, which defaults to OFF, and does not run `ctest`. `ci.yml` itself says Windows is "not covered until the Win11 runner of todo #293 exists". So no CI job runs the unit tests on Windows.
- **Library git refs versus the NavHAL ref.** For a library git dependency, `ensure_git_cached` calls `clone_into_cache`, which runs `git clone --depth 1 --branch <ref>` (and, when `ref` is empty, clones the default branch). `--branch` takes a branch or tag name, so a commit hash will not work as a library `ref`, whatever `LIBRARY_ABI.md` says. The `[navhal]` ref is different. `fetch_ref_into` tests it with `looks_like_commit` (7 to 64 hex characters). A tag or branch is shallow-cloned the same way, but a commit is fetched exactly (for a full 40 or 64 character SHA), falling back to a full clone and `checkout --detach`. So `[navhal].ref` may be a tag, branch or commit; a library `ref` may be a tag or branch only.
- **"Rebuilds when `.config` changes".** The README says this for `nav build`. Chapters 6 and 8 found no mechanism beyond configure re-running on each build and `nav-effective.config` being rewritten every time.
- **`[build] backend`** is read into `build_backend` and used nowhere else in `src` or `include`, though the ABI document lists it as "implemented".
- **`[target].arch` and `.vendor`** are not checked by `nav build`; only `board` is (chapter 5).
- **Flashing** depends on external tools. The README names `st-flash`, `openocd` and `avrdude` as runners. Nothing in `src` or the README mentions `dfu-util`, and nav links no flashing library.
- **No registry.** `docs/auth.md`, `docs/mvp.md`, `docs/plan.md` and the package-index code (`src/core/index.cpp`, `tests/test_index.cpp`) belong to a removed package-registry design; they are history, not behaviour, as the README's Dependencies section says.
- **README sections.** The README has no limits or known-issues heading, so nothing more was added.

## Sources

- `src/core/commands/create.cpp`: `CreateCommand::run`
- `src/core/commands/build.cpp`: HAL contract check, `--variant` refusal, `nav-deps-pre.cmake`
- `src/core/commands/lib.cpp`: `nav lib` subcommands
- `src/core/config.cpp`: `load_project_config`
- `src/core/libdeps.cpp`: `version_from_tag`, `read_bundled`, `Walker`, `check_bundled_copies`
- `src/core/navhal.cpp`: `clone_into_cache`, `ensure_git_cached`, `fetch_ref_into`, `looks_like_commit`, `read_hal_api_version`
- `src/core/appkconfig.cpp`: `render_merged_kconfig`
- `src/core/toolchain.cpp`, `src/core/index.cpp`
- `templates/nav_lib.toml.in`, `cmakelists_lib.txt.in`, `lib_header.h.in`, `lib_source.c.in`
- `docs/LIBRARY_ABI.md`, `docs/vaios-as-library.md`, `docs/auth.md`, `docs/mvp.md`, `docs/plan.md`
- `CMakeLists.txt`, `tests/CMakeLists.txt`
- `.github/workflows/ci.yml`, `.github/workflows/release.yml`
- `scripts/release.sh`, `build_deb.sh`, `install.sh`, `e2e-new-app.sh`, `check-template-alias.sh`, `scripts/windows/ci-build.ps1`
- `README.md`: Features, Commands, Pinning NavHAL, Install, Dependencies
