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
nav create my_project       # scaffold a project, pull NavHAL, generate .config
cd my_project
nav build                   # cross-compile with arm-none-eabi
nav upload                  # flash the connected board
nav monitor --baud 115200   # open the serial monitor
```

---

## Commands

| Command | Description |
| :--- | :--- |
| `nav create <name>` | Scaffold a project, pull NavHAL, and bootstrap `.config` and the build files. |
| `nav build` | Cross-compile with the `arm-none-eabi` toolchain. Rebuilds when `.config` changes. |
| `nav config` | Edit NavHAL Kconfig options through a menuconfig-style TUI over the project `.config`. |
| `nav upload` | Flash the firmware to the board and reset the target. |
| `nav monitor` | Open a serial monitor (auto-detects the port; `--baud` sets the rate). |
| `nav board` | Manage the board / toolchain registry. |
| `nav lib` | Work with library-type projects. |
| `nav check` | Verify the toolchain, package manager, and flasher are present and working. |
| `nav update` | Install missing local tools. |
| `nav clean` | Remove build artifacts. |

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
