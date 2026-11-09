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
| `nav build` | Cross-compile with the `arm-none-eabi` toolchain. Rebuilds when `.config` changes. `--variant <name>` builds a `[variant.<name>]` image in `build/<name>/`. |
| `nav config` | Edit NavHAL Kconfig options through a menuconfig-style TUI over the project `.config`. |
| `nav upload` | Flash the firmware with the board's runner (`st-flash`, `openocd`, `avrdude`). `--runner <name>` picks another, `--port <dev>` sets the serial port (auto-detected when one is attached), `--set <name>=<value>` fills a runner variable, `--dry-run` prints the commands, `--list` shows the board's runners. `--probe <serial\|usb-path>`, `--connect-under-reset` and `--list-probes` pick the debug probe (see [Probe selection](#probe-selection)). `--variant <name>` flashes that variant's build. |
| `nav monitor` | Open a serial monitor (auto-detects the port; `--baud` sets the rate). |
| `nav board` | Manage the board / toolchain registry. `nav board use <id>` switches the project's board: it updates `[target]` in `nav.toml` and regenerates `.config` (the old one is kept as `.config.old`). `nav build` stops if `.config` selects a different board from `nav.toml`. |
| `nav lib` | Work with library dependencies: `add`, `remove`, `list`, and `tree`, which prints the whole dependency graph and marks conflicts (one library requested from two sources, reported with the chain of projects behind each request). `[overrides]` in `nav.toml` picks one source for the whole project. |
| `nav check` | Verify the toolchain, package manager, and flasher are present and working. |
| `nav update` | Install missing local tools. In a project it also re-resolves NavHAL's `[navhal]` ref and every locked library ref from upstream and rewrites their `nav.lock` pins. |
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
linker_fragment_position = "before"     # "before" (default) or "after" the board's -T
libc         = "newlib"                 # default "none" (freestanding, -nostdlib)
syscalls     = "nosys"                  # with newlib: "nosys" (default) or "none" (no -lnosys)
libs         = ["m"]                    # extra -l libraries
deps_opt     = "Os"                     # -O level for NavHAL and every dependency (O0 O1 O2 O3 Os Og Oz)

[[build.file_flags]]                    # extra flags for matching sources, in order
match = ["src/est/*.c", "src/control/**/*.c"]
flags = ["-Wextra", "-Wconversion", "-Werror"]
```
`*` and `?` stay inside a path segment, `**` spans directories. A `sources` glob that starts with a wildcard (such as `**/*.c`) walks the project root but skips `build/`, `extern/` and dot-directories like `.git`; a literal prefix such as `extern/foo/*.c` can still reach into them. `nav build` expands the globs (sorted) into `build/nav-app.cmake` each time, and fails if a `sources` pattern or a `file_flags` rule matches nothing. `linker_fragment` is a second `-T`. With the default `linker_fragment_position = "before"` it comes before the board's `-T`, which is what an `INSERT AFTER .text;` fragment needs (GNU ld only resolves the insert point when the fragment is read first). With `"after"` it comes after the board script, so the fragment can name `MEMORY` regions the board script defines; an `INSERT` fragment then fails with ".text not found for insert". `libc = "newlib"` drops `-nostdlib` and links `-lc -lm`, your `libs` and `-lgcc` in the same link group as NavHAL and every dependency archive, so ld rescans the dependencies when libc needs `_sbrk`/`_write`/...; `-lnosys` comes after that group and only supplies syscalls no dependency defines. `syscalls = "none"` leaves out `-lnosys` (and `--specs=nosys.specs`, which would add it), so a library or the app must provide the syscalls. `deps_opt` adds `-<level>` to NavHAL and every dependency library target, after the build-type and library defaults, so it is the last `-O` on their compile lines and wins; unset leaves them alone (the app's own flags stay in `c_flags`). If a dependency declares `[library] navhal_submodule = true`, nav builds NavHAL with `-DSUBMODULE`; the define is applied to NavHAL's targets only, never to the app or other libraries. Settings that need the newer CMake template (`deps_opt`, `syscalls = "none"`, `linker_fragment_position = "after"`) ask you to run `nav regen` on an older project. NavHAL and dependency headers reach the app as system includes, so they never trip the app's `-Werror`. After linking, nav prints the image size.

These keys need a `CMakeLists.txt` from the current template. A project made by an older nav keeps building as before, but if `nav.toml` uses `[build]` keys, `nav build` says so; run `nav regen` to rewrite `CMakeLists.txt` (the old one is kept as `CMakeLists.txt.old`).

#### Board- and Kconfig-conditional settings

`[build.board.<board>]` and `[build.kconfig.<SYMBOL>]` take the same keys as `[build]` (`sources`, `exclude`, `include_dirs`, `defines`, `c_flags`, `libs`, `[[...file_flags]]`) and add to it:
```toml
[build.board.navixdev]                  # when [target].board = "navixdev"
sources      = ["src/sensors/dev/*.c"]
include_dirs = ["board/navixdev"]

[build.kconfig.USE_BARO]                # when CONFIG_USE_BARO is y (or set to any value but n)
sources = ["src/baro/*.c"]
defines = ["HAS_BARO=1"]
[[build.kconfig.USE_BARO.file_flags]]
match = ["src/baro/*.c"]
flags = ["-O3"]
```
Layers apply in the order `[build]`, the matching board table, the matching Kconfig tables (by symbol name), then the selected variant's. A layer's `exclude` removes sources from its own layer and earlier ones, so `[build]` can exclude `src/sensors/**` and a board table adds its own back. The Kconfig tables read the resolved `build/nav.config` (the app `.config` when the project has no Kconfig); `nav build` resolves it before it writes `build/nav-app.cmake`. An unknown key in these tables is an error.

### Build variants (`[variant.<name>]`)
A variant is a second image of the same project, such as a hardware-test firmware beside the production one:
```toml
[variant.hwtest]
sources_add    = ["tests/onboard/**/*.c"]   # joined to [build].sources
sources_remove = ["src/main.c"]             # joined to [build].exclude
defines        = ["VAYU_HW_TEST=1"]         # appended to [build].defines
output         = "hwtest"                   # ELF/bin name; default = project name
kconfig        = ".config.hwtest"           # optional alternative .config (still validated)
# every [build] key is accepted too, all additive on top of the base:
include_dirs   = ["tests/onboard"]          # also: sources, exclude, c_flags, libs, [[...file_flags]]
c_flags        = ["--coverage"]
libs           = ["gcov"]

[variant.hwtest.board.navixdev]             # the board/kconfig sub-tables work here as well
defines = ["HWTEST_DEV=1"]
[variant.hwtest.kconfig_when.USE_BARO]      # a variant's `kconfig` string and a
defines = ["HWTEST_BARO=1"]                 # [variant.x.kconfig.SYM] table cannot share a name

[variant.hwtest.dependencies.navlink]
options = ["NAVLINK_PROFILE=test"]          # merged over the dependency's own options
```
`nav build --variant hwtest` builds in `build/hwtest/`, so it never touches the production build in `build/`; `nav upload --variant hwtest` flashes `build/hwtest/hwtest`. An unknown variant name is an error that lists the known ones. Sources go through the same planner as `[build]` (a glob that matches nothing is an error); `kconfig` goes through the same app Kconfig validation as `.config`. Dependency options replace an option with the same `KEY` and add the others, for that variant only; naming a dependency the project does not have is an error. Variant names use letters, digits, `_` and `-`, and may not be `deps`, `extern`, `CMakeFiles` or `Testing`. Variants need a `CMakeLists.txt` from the current template (`nav regen`).

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

An `args` entry is a plain string, or a conditional group that is emitted only when
a variable has a value: `{ "if": "probe_serial", "args": ["--serial", "{probe_serial}"] }`
(`"a|b"` means either). The built-in `st-flash` and `openocd` runners use this for the
probe variables below.

### Probe selection

A bench with several ST-Links must flash the one you mean. nav never picks for you: with
more than one ST-Link attached and no probe chosen, `nav upload` stops, lists them and
says how to choose. With one probe (or none) and nothing configured it behaves as before.

```bash
nav upload --list-probes                    # attached ST-Links: USB path and serial (Linux, via sysfs)
nav upload --probe 1-3.4                    # by USB port path
nav upload --probe 0668FF3332504E3043232724 # by USB serial
nav upload --probe 1-3.4 --runner openocd --connect-under-reset
```

```toml
[upload]
probe = "1-3.4"                # same as --probe
connect_under_reset = true     # same as --connect-under-reset
```

Precedence: `--probe`, then `NAV_PROBE`, then `[upload] probe`. A value like `3-2` or
`1-3.3` is a USB path, anything else a serial; write `usb:<path>` or `serial:<sn>` to be
explicit. nav checks it against what is attached and fails on no match.

Runner variables: `{probe_serial}` and `{probe_usb_path}` (set from the choice, only
those the runner uses) and `connect_under_reset`.

| Runner | Serial | USB path | Connect under reset |
| :--- | :--- | :--- | :--- |
| `st-flash` | `--serial <sn>` | not supported by the tool | `--connect-under-reset` |
| `openocd` | `-c "adapter serial <sn>"` | `-c "adapter usb location <bus>-<port.path>"` | `-c "reset_config srst_only connect_assert_srst"` |

openocd also gets `transport select hla_swd` when a probe is chosen. Cloned ST-Links
can report the same serial, which no tool can tell apart: `--probe` refuses a serial
shared by several adapters, and a USB path given to `st-flash` is turned into its
serial only when that serial is unique. Otherwise use `--runner openocd`, which pins
the USB port. After flashing, the board's `st-flash reset` is aimed at the same probe
(or skipped with a warning when it cannot be). Probe listing and checks need Linux; on
other systems `--list-probes` says so and `--probe` is passed to the runner unchecked.

### Pinning NavHAL

`nav.toml` names the NavHAL a project builds against, and `nav.lock` pins the exact commit:

```toml
[navhal]
ref = "v0.4.0"                      # a tag, branch or commit; default "stable"
# git = "https://example.com/NavHAL.git"   # optional: a fork (default: upstream)
# exclude = ["src/utils/freestanding.c"]   # optional: NavHAL sources to drop, relative to its root
```

`exclude` removes those files from NavHAL's `hal` library and its `common` object library
(the target that owns most common sources), after NavHAL is added, for an app that supplies
its own version. A path that does not exist in the pinned NavHAL is an error. It needs a
`CMakeLists.txt` from the current template (`nav regen`).

The first `nav build` (and `nav create`) writes the commit to `nav.lock`; every later build
links `extern/NavHAL` to exactly that commit. Each commit has its own checkout under
`~/.nav/navhal/commits/`, so apps pinned to different versions build side by side. If a
branch ref now points somewhere else (seen when the locked commit is not cached, e.g. on a
clean machine), the build stops; `nav update` takes the new commit and re-pins. A project
with no `[navhal]` keeps using `stable`, pinned from its first build. `nav create` records
the release tag `stable` points at, when there is one. An `extern/NavHAL` that is a real
directory (vendored) is left alone.

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
