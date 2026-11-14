# Chapter 3: Install and first project

This chapter covers getting `nav` onto a machine, checking that the machine can build firmware, creating a project, and running the first build. It describes nav v1.3.1 (`CMakeLists.txt` declares `project(Nav VERSION 1.3.1 ...)`).

## Installing nav

### Linux: the .deb

The README's Install section gives one binary route: download the `.deb` from the GitHub Releases page and install it.

```bash
sudo dpkg -i nav-X.X.X-Linux.deb
```

The package's declared dependencies are `git, cmake, python3, build-essential`, and CPack is told to add the libc and libstdc++ dependencies the binary actually needs (`CMakeLists.txt`, packaging section). The release workflow builds the package on `ubuntu-22.04`, and its release notes say the result declares a glibc dependency and runs on Ubuntu 22.04, 24.04 and later. The repo states no other distribution requirements.

### Building from source

The README gives these steps:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

That produces a binary in `build/` but does not install it. `scripts/install.sh` builds and installs in one step. It requires `cmake`, forces `-DNAV_BUILD_TESTS=OFF`, and installs `bin/nav` and `share/nav/boards/` under a prefix. The default prefix is `$PREFIX` or `/usr/local`, and the script uses `sudo` only when the prefix is not writable. Its own header gives these examples:

```bash
./scripts/install.sh                      # -> /usr/local (uses sudo if needed)
./scripts/install.sh --prefix ~/.local    # per-user, no sudo
```

If `<prefix>/bin` is not on `PATH`, the script prints the `export PATH=...` line to add. `scripts/build_deb.sh` builds the `.deb` locally with `cpack -G DEB`; it is the maintainer-side counterpart of the release job. The README does not mention either script.

### Windows

The repo shows that Windows release files exist but does not document how to use them. What it does show:

- `.github/workflows/release.yml` has a `windows` job that uploads `build/nav-*-win64.exe` and `build/nav-*-win64.zip`. The release body describes them as an installer and a portable zip.
- `CMakeLists.txt` sets the Windows CPack generators to `NSIS;ZIP`. The NSIS installer runs `scripts/windows/AddToPath.ps1`, which adds the installed `bin` directory to the machine-wide (all-users) `PATH`. The uninstaller removes it. New terminals pick up the change without a reboot, according to the script's comments.
- `scripts/windows/ci-build.ps1` is the build script CI uses. It downloads its own MinGW-w64, NSIS and Ninja, and the workflow comment says the output is a static `nav.exe`.
- The README says only that nav "builds on Linux and Windows" and that it ships as a `.deb`.

The repo does not say which Windows versions are supported, what the installer's pages look like, or whether the zip needs manual `PATH` setup. It also has no Windows-specific source-build instructions beyond the CI script. This chapter does not fill those gaps.

## Checking the environment: `nav check`

`nav check` verifies the toolchain, package manager and flasher (README, Commands). From `src/core/commands/check.cpp`, it runs in two stages.

Stage 1 checks the "core" tools from `data/toolchain.json`: `cmake`, `git`, `ninja` and `python`. A missing core tool counts as a critical failure. It also reports which host package manager it finds (`apt`, `dnf` or `pacman` on Linux, `brew` on macOS) as a non-critical item, and checks for `kconfiglib`. A missing `kconfiglib` is not critical, because `nav build` and `nav update` install it into nav's own virtual environment.

Stage 2 runs only inside a project. It reads `board` from `nav.toml`, looks the board up in the catalog, and probes the tools that board needs. Outside a project it prints a warning and skips this stage.

If any critical tool is missing, `nav check` prints the number of critical defects and exits with status 1. Otherwise it exits 0.

`nav update` is the command for filling gaps: the README describes it as installing missing local tools. Tool definitions live in two files. `data/toolchain.json` defines the four core tools (`cmake`, `git`, `ninja`, `python`); `data/boards.json` defines the board-specific tools (`arm-none-eabi-gcc`, `st-flash`, `avr-gcc`, `openocd`, `avrdude`). Each tool entry has a `binary`, a `version`, `packages` for several package managers and a `download` entry. For example, `arm-none-eabi-gcc` in `data/boards.json` is version 13.3.1 with apt package `gcc-arm-none-eabi`. The README calls this installation "best-effort".

## Creating a project

The README's quick start:

```bash
nav create my_project --board nucleo_f401re   # scaffold, pull NavHAL, write .config for the board
cd my_project
```

`--board` defaults to `nucleo_f401re`; `-b` and `--board=<id>` also work. The ids in `data/boards.json` are `nucleo_f401re`, `navixsmf401re`, `navixdev` and `atmega328p`. An unknown id is rejected with a pointer to `nav board list --all`. `--force` replaces an existing directory of the same name; without it nav refuses to overwrite. `--lib` creates a library project instead of firmware.

`nav create` needs network access, because it resolves NavHAL from git before writing any project files. It builds the project in a temporary sibling directory and renames it into place, so a failure leaves no half-made tree.

### What a new project contains

For a firmware project, `create.cpp` and `templates/` produce:

- `extern/NavHAL`, linked from a shared per-user cache rather than freshly cloned.
- `include/`, `lib/` and `tests/`, created empty.
- `src/main.c`, from `templates/main.c.in`. It defines `CORTEX_M4`, includes `navhal.h`, starts the 1 ms timebase, opens UART2 at 115200 baud, writes `System Up.`, then loops forever.
- `nav.toml`, from `templates/nav.toml.in`: a `[project]` table (name, version `0.1.0`, `type = "executable"`), a `[target]` table (`arch`, `vendor`, `board`), `[build]` with `backend = "cmake"`, and a commented example of `[dependencies]`. `create` appends a `[navhal]` table with a `ref`.
- `nav.lock`, which pins the exact NavHAL commit.
- `.config`, seeded from NavHAL's defconfig for the board, with the driver options in `templates/dot_config.in` added on top (clock, GPIO, UART, I2C, SPI and others, plus `CONFIG_USE_FPU=y`). A board that lacks one of them ignores it.
- `CMakeLists.txt`, generated from a template.

## First build

```bash
cd my_project
nav build
```

The README describes `nav build` as a cross-compile with the `arm-none-eabi` toolchain that rebuilds when `.config` changes. Output goes under `build/`. `nav build` stops if `.config` selects a different board from `nav.toml`. If `kconfiglib` is missing, `nav build` installs it, as noted above.

The next commands in the quick start, `nav upload` and `nav monitor --baud 115200`, are covered in later chapters.

## Sources

- `README.md`: Features list (lines 15-24); Quick start (25-33); Commands table (37-51); Install (228-242).
- `CMakeLists.txt`: line 2 (version); "Packaging and Installation Setup" section (about lines 136-194).
- `scripts/install.sh`: header comment, argument parsing, preflight, install steps.
- `scripts/build_deb.sh`: whole file.
- `scripts/windows/AddToPath.ps1`: whole file.
- `scripts/windows/ci-build.ps1`: header comment and tool download steps.
- `.github/workflows/release.yml`: `linux` and `windows` jobs, release body.
- `src/core/commands/check.cpp`: `CheckCommand::run`.
- `src/core/toolchain.cpp`: `get_system_requirements` (lines 164-189).
- `src/core/commands/create.cpp`: argument scan, scaffolding, file writes.
- `templates/main.c.in`, `templates/nav.toml.in`, `templates/dot_config.in`.
- `data/boards.json`: `boards` object (ids) and `tools` object (board-specific tools, including `arm-none-eabi-gcc`).
- `data/toolchain.json`: `core` list and the core tools' `packages` and `download` entries.
