# Chapter 1: What nav is

## Purpose

`nav` is a command-line tool for NavHAL-based Cortex-M firmware projects. According to the README, it scaffolds a project, resolves dependencies from git, cross-compiles with the ARM toolchain, flashes the result to hardware, and opens a serial monitor. The stated aim is that a normal edit / build / flash / debug loop is a few short commands.

The README lists these features:

- project scaffolding with NavHAL pulled in as a dependency;
- cross-compilation for Cortex-M targets through `arm-none-eabi-gcc`;
- Kconfig-based configuration, where `nav config` opens a menuconfig-style TUI over the project `.config`;
- flashing to hardware (for example with `st-flash`), with a target reset after the flash, plus a built-in serial monitor;
- toolchain and environment checks, with best-effort installation of missing tools;
- a JSON board and toolchain registry.

`nav` is written in C++, ships as a Debian `.deb`, and builds on Linux and Windows. It pulls NavHAL and other dependencies directly from GitHub through git, and no package registry is required. Earlier versions used a self-hosted package registry; the README says it was removed in favor of the git-based approach. Documents in `docs/` that describe that registry (`plan.md`, `mvp.md`, `auth.md`) therefore do not describe current behaviour.

## Who it is for

The README names its audience only through its first sentence: people working on NavHAL-based Cortex-M firmware projects. It does not describe any further target user, experience level, or team size, so this chapter does not either. The license is Apache 2.0, with copyright held by NAVRobotec Pvt Ltd.

## Two project types: app and library

`nav` knows two kinds of project. `docs/LIBRARY_ABI.md` defines them in its vocabulary table:

- An **app** is a firmware project. It owns `main()`, produces a flashable binary, and depends on zero or more libraries.
- A **library** is a reusable unit of firmware code, hosted as a Git repository identified by its clone URL. It produces a static archive that an app (or another library) consumes, and it has no `main()`.

The kind is set by the `type` field in the `[project]` table of `nav.toml`. The value `"executable"` means an app and `"library"` means a library. `LIBRARY_ABI.md` states these are the only two kinds, and that "module" and "package" are not separate terms but mean "library".

Both are created with `nav create`. Without a flag it scaffolds an app; with `--lib` (or `--library`) it scaffolds a library. In the source, the default project name is `my-project` for an app and `my-library` for a library. The two scaffolds differ as follows:

| | App | Library |
| :--- | :--- | :--- |
| `type` in `nav.toml` | `"executable"` | `"library"` |
| Generated files | `nav.toml`, `.config`, `src/main.c`, `CMakeLists.txt` | `nav.toml`, `include/<name>.h`, `src/<name>.c`, `CMakeLists.txt` |
| Output | firmware | static archive |

Both templates carry the same `[target]` table (`arch`, `vendor`, `board`) and `[build]` with `backend = "cmake"`. Both include a commented-out `[dependencies]` table. In it a library is referenced by a local path or a git URL, for example `mylib = { path = "../mylib" }`.

The library template notes that `[target]` is the board used when the library is built standalone with `nav build`. When a firmware project depends on the library, the library is rebuilt for that project's board. Some options apply only to apps: `nav build --variant` stops with the message "--variant applies to firmware projects; this is a library."

## The edit-build-flash loop

The README's quick start is the loop in its shortest form:

```bash
nav create my_project --board nucleo_f401re   # scaffold, pull NavHAL, write .config for the board
cd my_project
nav build                   # cross-compile with arm-none-eabi
nav upload                  # flash the connected board
nav monitor --baud 115200   # open the serial monitor
```

`nav create` is run once. The remaining three commands make up the repeated part:

- `nav build` cross-compiles with the `arm-none-eabi` toolchain. It rebuilds when `.config` changes.
- `nav upload` flashes the firmware using the board's runner (`st-flash`, `openocd` or `avrdude`).
- `nav monitor` opens a serial monitor. It auto-detects the port, and `--baud` sets the rate.

Editing happens in between: source files, `nav.toml`, or `.config`. To change NavHAL options, `nav config` opens the menuconfig-style interface over `.config`; after saving, the tool tells you to run `nav build` to apply the change.

Other commands in the README's table support the loop without being part of every pass:

- `nav check` verifies that the toolchain, package manager and flasher are present and working.
- `nav update` installs missing local tools. Inside a project it also re-resolves NavHAL's `[navhal]` ref and every locked library ref.
- `nav board` manages the board and toolchain registry, and `nav board use <id>` switches a project's board.
- `nav lib` works with library dependencies (`add`, `remove`, `list`, `tree`).
- `nav regen` rewrites a nav-generated `CMakeLists.txt` from the current template.
- `nav clean` removes build artifacts.

An app is described by `nav.toml`, `.config` and its sources, and nav generates the CMake from them. Later chapters cover those files in detail. The repo does not state a recommended editor, debugger, or how `nav` fits into continuous integration beyond what the README shows, so none is described here.

## Sources

- `README.md`: intro paragraph; Features; Quick start; Commands table; Dependencies; License; "App sources and flags" (first paragraph).
- `templates/nav.toml.in`: whole file (lines 1-20).
- `templates/nav_lib.toml.in`: whole file (lines 1-23).
- `docs/LIBRARY_ABI.md`: section 1 "Vocabulary" (lines 17-26) and the opening of section 2 (lines 30-36).
- `src/core/commands/create.cpp`: flag scan (lines 62-90), scaffold branches for library and app (lines 180-205).
- `src/core/commands/build.cpp`: `--variant` rejection for libraries (lines 60-62).
- `src/core/commands/config.cpp`: final success message (line 141).
- `src/core/commands/upload.cpp`: usage string (lines 33-35).
- `src/core/commands/monitor.cpp`: port auto-detection messages (lines 52-63).
