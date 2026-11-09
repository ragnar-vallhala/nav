# nav — build tooling for NavHAL firmware

`nav` is a command-line tool for NavHAL-based Cortex-M firmware projects. It scaffolds a
project, resolves dependencies from git, cross-compiles with the ARM toolchain, flashes the
result to hardware, and opens a serial monitor — so a normal edit / build / flash / debug loop
is a few short commands.

Written in C++. Dependencies are pulled directly from GitHub via git; no package registry is
required.

---

## Features

- Project scaffolding with NavHAL pulled in as a dependency
- Cross-compilation for Cortex-M targets via `arm-none-eabi-gcc`
- Kconfig-based configuration — `nav config` opens a menuconfig-style TUI over the project `.config`
- Flashing to hardware (e.g. `st-flash`) with a post-flash target reset, plus a built-in serial monitor
- Toolchain/environment checks and best-effort installation of missing tools
- JSON board and toolchain registry
- Ships as a Debian `.deb`; builds on Linux and Windows

---

## Quick start

```bash
nav create my_project --board nucleo_f401re   # scaffold, pull NavHAL, write .config for the board
cd my_project
nav build                   # cross-compile with arm-none-eabi
nav upload                  # flash the connected board
nav monitor --baud 115200   # open the serial monitor
```

---

## Commands

| Command | Description |
| :--- | :--- |
| `nav create <name> [--board <id>]` | Scaffold a project, pull NavHAL, and write the build files and a `.config` seeded from NavHAL's defconfig for the board (default `nucleo_f401re`). |
| `nav build` | Cross-compile with the `arm-none-eabi` toolchain. Rebuilds when `.config` changes. |
| `nav config` | Edit NavHAL Kconfig options through a menuconfig-style TUI over the project `.config`. |
| `nav upload` | Flash the firmware with the board's runner (`st-flash`, `openocd`, `avrdude`). `--runner <name>` picks another, `--port <dev>` sets the serial port (auto-detected when one is attached), `--set <name>=<value>` fills a runner variable, `--dry-run` prints the commands, `--list` shows the board's runners. |
| `nav monitor` | Open a serial monitor (auto-detects the port; `--baud` sets the rate). |
| `nav board` | Manage the board / toolchain registry. `nav board use <id>` switches the project's board: it updates `[target]` in `nav.toml` and regenerates `.config` (the old one is kept as `.config.old`). `nav build` stops if `.config` selects a different board from `nav.toml`. |
| `nav lib` | Work with library dependencies: `add`, `remove`, `list`, and `tree`, which prints the whole dependency graph and marks conflicts (one library requested from two sources, reported with the chain of projects behind each request). `[overrides]` in `nav.toml` picks one source for the whole project. |
| `nav check` | Verify the toolchain, package manager, and flasher are present and working. |
| `nav update` | Install missing local tools. |
| `nav regen` | Rewrite a nav-generated `CMakeLists.txt` from the current template (old one kept as `CMakeLists.txt.old`); needed before an older project can use `[build]` keys. |
| `nav clean` | Remove build artifacts. |

### App sources and flags (`[build]` in `nav.toml`)
An app is `nav.toml` + `.config` + sources; nav generates the CMake. Without any of these keys the app is `src/main.c` plus `include/`.
```toml
[build]
sources      = ["src/**/*.c"]           # globs from the project root (default ["src/main.c"])
exclude      = ["src/driver/sensor/**"] # globs removed from sources
include_dirs = ["include", "board/x"]   # default ["include"]
defines      = ["CORTEX_M4=1"]
c_flags      = ["-O2", "-Wall"]
c_standard   = 11                       # optional: 90, 99, 11, 17, 23
linker_fragment = "app.ld"              # extra linker script, added to NavHAL's board script
libc         = "newlib"                 # default "none" (freestanding, -nostdlib)
libs         = ["m"]                    # extra -l libraries

[[build.file_flags]]                    # extra flags for matching sources, in order
match = ["src/est/*.c", "src/control/**/*.c"]
flags = ["-Wextra", "-Wconversion", "-Werror"]
```
`*` and `?` stay inside a path segment, `**` spans directories. `nav build` expands the globs (sorted) into `build/nav-app.cmake` each time, and fails if a `sources` pattern or a `file_flags` rule matches nothing. `linker_fragment` should use `INSERT AFTER .text;`; `libc = "newlib"` links `-lc -lm -lnosys -lgcc`. NavHAL and dependency headers reach the app as system includes, so they never trip the app's `-Werror`. After linking, nav prints the image size.

These keys need a `CMakeLists.txt` from the current template. A project made by an older nav keeps building as before, but if `nav.toml` uses `[build]` keys, `nav build` says so; run `nav regen` to rewrite `CMakeLists.txt` (the old one is kept as `CMakeLists.txt.old`).

### `nav regen`
Rewrites a nav-generated `CMakeLists.txt` from the current template, after saving the old one as `CMakeLists.txt.old`. It refuses a `CMakeLists.txt` that nav did not generate.

### Flash runners

A runner is a tool plus a command template, defined under `"runners"` in
`data/boards.json` (extend or override it in `~/.nav/boards.json`):

```json
"openocd": {
  "tool": "openocd",
  "image": "elf",
  "args": ["-f", "{interface}", "-f", "{target}", "-c", "program \"{elf}\" verify reset exit"],
  "defaults": { "interface": "interface/stlink.cfg" }
}
```

`image` is the file the command flashes (`elf`, `bin` or `hex`; nav runs the
board's objcopy to make `bin`/`hex`). A board names its default `runner` and
gives each supported runner the variables it needs:

```json
"runner": "st-flash",
"runners": { "st-flash": {}, "openocd": { "target": "target/stm32f4x.cfg" } }
```

Built-in variables are `{elf}`, `{bin}`, `{hex}`, `{address}` (the board's
`flash_address`) and `{port}`. Command-line values (`--port`, `--set`) win over
the board's, which win over the runner's `defaults`.

---

## Install

Download the `.deb` from the [GitHub Releases](../../releases) page and install it:

```bash
sudo dpkg -i nav-X.X.X-Linux.deb
```

Or build from source:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

---

## Dependencies

`nav` pulls NavHAL and other dependencies directly from GitHub via git. Earlier versions used a
self-hosted package registry; it was removed in favor of the simpler git-based approach.

---

## License

Apache License 2.0 — see [`LICENSE`](LICENSE). Copyright 2026 NAVRobotec Pvt Ltd.
