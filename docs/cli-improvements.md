# Nav CLI — Concrete Improvement Plan

Scope: the C++ CLI in `src/` and `include/`. Registry-side work (`registry-api/`, `registry-ui/`) is tracked separately in `docs/plan.md` and `docs/mvp.md`.

NavHAL branch policy: continue consuming `--branch stable`. Branch pinning is not part of this plan.

## Status

Several rounds of work have landed since this doc was first drafted. Items listed here are done — the per-defect entries in Part A still call out specifics for searchability.

### Round 0 — original P0/P1 list (pre-Phase-0)

- Merged stdout+stderr capture, single `output` field (`include/nav/core/execution_context.hpp:8`).
- Walk-up `find_project_root` (`src/core/config.cpp:9`).
- `toml++` replaces the hand-rolled scan (`src/core/config.cpp`).
- Multi-distro provisioning with `PackageManager` enum + per-manager dispatch (`src/core/toolchain.cpp:33-91`).
- God-file split into `src/core/commands/{create,build,upload,monitor,clean,check,update,registry}.cpp`.
- Embedded templates extracted under `templates/` and inlined via `configure_file` into `embedded.hpp`.
- `access(X_OK)` replaces `fs::exists` for PATH probing (`src/core/toolchain.cpp:25`).
- Per-tool `version_flag` field (`include/nav/core/toolchain.hpp:17`).
- `--version` flag on the binary (since rebound to `-V` in Phase 0; see CLI conventions).

### Phase 0 — defect cleanup, CLI restructure, test harness (commit 2499b14)

- P0-1, P0-2, P0-3 (`create` hardening: name validation, refuse-overwrite, temp-dir + atomic rename).
- P0-4 (`update` exit-code propagation for unmapped binaries).
- P0-5 (`IExecutionContext::execute` takes a `chrono` timeout; `poll()` + SIGTERM/SIGKILL cascade; 1 MiB silent-mode cap).
- P1-1 (`ui.hpp` colours gated on `isatty` / `NO_COLOR`).
- P1-2 + P1-3 (CLI11 vendored via `FetchContent`; per-command `--help` works; `ICommand::register_flags` hook).
- P1-6 (monitor typo fixes).
- P2-2 (`-v` rebound to `--verbose`; `-V` reserved for `--version`).
- P2-4 (`tests/` with GoogleTest, `MockExecutionContext`).

### Phase 1.1 — board catalog primitive (commit ec49ca1)

- `include/nav/core/board.hpp` + `src/core/board.cpp` — `Board` struct + `BoardCatalog`.
- Five board files under `share/nav/boards/`: `pico`, `nucleo_f401re`, `nucleo_h743zi`, `esp32_devkitc`, `arduino_uno`.
- `nav board list` / `nav board info <id>` verbs.
- `ToolchainManager::get_project_requirements(const Board&)` replaces substring matching.

### Phase 1.2 — catalog-driven CMake generation

- `nav::core::render_board_cmake(const Board&)` + `write_board_cmake(...)` — emit a `nav-board.cmake` file with `NAV_BOARD_{ID,NAME,ARCH,VENDOR,MCU,COMPILER,COMPILE_FLAGS,LINK_FLAGS,FLASH_TOOL,FLASH_ADDRESS}` set directives.
- `BuildCommand` now loads `nav.toml`, resolves `[target].board` via `default_catalog`, and writes `build/nav-board.cmake` before invoking cmake. Errors cleanly when the board id is missing or unknown.
- `templates/cmakelists.txt.in` rewritten:
  - Includes `${CMAKE_BINARY_DIR}/nav-board.cmake` *before* `project()` so toolchain settings (compiler, prefix-derived objcopy/size) flow from the board.
  - Replaces the hardcoded `-mcpu=cortex-m4` / `-mfpu=fpv4-sp-d16` block with `${NAV_BOARD_COMPILE_FLAGS}` and `${NAV_BOARD_LINK_FLAGS}`.
  - Adds a "Nav-resolved → NavHAL fallback" precedence chain for the flash target so the catalog wins when populated but NavHAL's `BOARD`/`FLASHER`/`FLASH_ADDRESS` continue to drive linker-script lookup.
- Existing projects keep building (the hardcoded flags they were generated with still work); they migrate by replacing their `CMakeLists.txt` with the new template.

### CLI design conventions (established in Phase 0 + 1.1)

These are decisions, not tradeoffs to revisit. Document them so future contributors don't try to "fix" them.

- **CLI11 with `prefix_command()` per subcommand.** Everything after the verb name is collected raw into `remaining()` and handed to the verb. Verbs that still parse their own argv (`monitor --port`, `create --force`, `board list`, `board info <id>`) keep working without per-verb CLI11 wiring.
- **Global flags come before the verb.** `nav --verbose build` works; `nav build --verbose` does not — the latter is swallowed by `prefix_command()`. This matches git, cargo, npm, kubectl. The earlier `app.fallthrough()` approach (which would allow flags after the verb) was deliberately removed because it hijacks positional extras like `board list`.
- **Per-verb migration to CLI11 is opt-in.** Each `ICommand` exposes `virtual void register_flags(CLI::App&)` with a default no-op body. Verbs that want CLI11-managed flags (instead of scanning `remaining()` themselves) override it; the others continue as-is.
- **Subcommand `--help` is automatic.** CLI11 generates `nav <verb> --help` for every registered subcommand. Override `ICommand::help_text()` for richer per-verb text.
- **Flag aliases:** `-V`/`--version` for version, `-v`/`--verbose` for verbosity, `-q`/`--quiet`, `--no-color`, `--color={auto,always,never}`, `--cwd`.

### Phase 2.1 — registry foundation: semver + index reader

The pure-logic layer of Phase 2. No HTTP, no cache, no verbs hooked up yet. What the resolver and lockfile work in 2.2 will build on:

- `include/nav/core/semver.hpp` + `src/core/semver.cpp` — `Version` (with full SemVer 2.0.0 precedence including prerelease identifier rules), `VersionReq` covering `^`, `~`, `=`, `>=`, `<=`, `>`, `<`, `*`, with `matches(req, v)`. Bare `1.2.3` parses as `=1.2.3` (exact) — conservative default; an operator-less version is taken literally rather than inferred as a range.
- `include/nav/core/index.hpp` + `src/core/index.cpp` — `Download`, `IndexVersion` (with `PackageKind::Library` / `Toolchain`, multi-platform downloads keyed `<os>_<arch>`, dependency map, toolchain-binary list), `IndexPackage` (sorted versions ascending), `parse_index_file` reading JSON via `nlohmann/json` (vendored alongside the existing `tomlplusplus` / `CLI11`), `IIndexClient` interface, `LocalIndexClient` with sharded `<root>/<2char>/<name>.json` layout.

Tests: +30 (semver parse/compare/match across SemVer §11 examples, index JSON round-trip for library + toolchain kinds, malformed-JSON rejection, sharded path computation, fetch-success / fetch-missing / wrong-shard).

### Data-format policy (effective Phase 2.1 onward)

Nav's data-format surface is **JSON and YAML only** going forward. Drivers for the choice:

- JSON for machine-authored / registry-served artifacts: the index, future `nav.lock`, cache manifests. Ubiquitous parsers, terse, no whitespace-sensitivity.
- YAML for user-authored config we want to read pleasantly (planned migration target for `nav.toml` and `share/nav/boards/*.toml`).
- TOML is no longer the target for new schemas. The existing TOML files (`nav.toml`, board catalog) remain on TOML for now to avoid a breaking change mid-roadmap; their migration is tracked under "Open" below.

We will not model Nav's CLI / config surface after Cargo. SemVer operators (`^`, `~`, `>=`, etc.) stay because they're industry-standard — but Nav's own conventions (bare-version semantics, manifest field names, verb behaviour) are independent decisions, justified on their own terms rather than by reference to Cargo.

### Phase 1.3 — small P1 sweep

- P1-4 (`find_serial_ports` extracted to `nav::core::serial`; sorted; `nav monitor` errors and lists candidates when ≥2 are present).
- P1-5 (`nav monitor` read loop now uses `poll(POLLIN, 100ms)` instead of busy-sleep).
- P1-7 (`load_project_config` prints `nav.toml:line:column: <description>` on parse error before returning nullopt).
- P1-8 (`ui::tool_ok` / `tool_missing_critical` / `tool_missing_optional` helpers; `check.cpp` routes through them and the per-result branching moved to a local `render_probe` helper).

### Open

- Phase 2 — registry (libraries **and** toolchains) + lockfile. Replaces the originally-planned standalone "toolchain manager" phase: we'll host toolchain binaries as packages on Nav's own registry rather than pulling tarballs from upstream Arm/AVR/etc. The earlier Phase 2/3 split is collapsed; sequencing summary updated.
- Migrate existing TOML files (`nav.toml`, `share/nav/boards/*.toml`) to YAML or JSON to honour the format policy above. Currently kept on TOML to avoid a breaking change while Phase 2 is in flight; treat as a coordinated migration after the registry lands.
- Phase 3 — framework / flashing / monitor filters / debug / test / registry auth (the old long-tail Phase 4).

---

# Part A — Remaining defects

## P0 — Correctness & safety

### P0-1. `create` accepts unvalidated project names
- **Where:** `src/core/commands/create.cpp:34`.
- **Symptom:** `nav create ../../etc/foo` or `nav create /tmp/x` writes outside CWD. `nav create "name with spaces"` produces a brittle directory tree.
- **Fix:** validate that `args[0]` is a non-empty single path component (no `/`, no `\`, no leading `.`, not absolute, no NUL). Starting policy: reject anything not matching `[A-Za-z0-9._-]+`. Surface a clear error and exit non-zero.
- **Acceptance:** unit tests for `nav create ../foo`, `nav create /tmp/foo`, `nav create ""`, `nav create "a b"`, `nav create .hidden`.

### P0-2. `create` silently overwrites existing projects
- **Where:** `src/core/commands/create.cpp:38-49`.
- **Symptom:** `fs::create_directories` is a no-op on existing dirs; `std::ofstream` truncates files. Re-running `nav create x` overwrites `nav.toml`, `CMakeLists.txt`, `src/main.c`.
- **Fix:** check `fs::exists(proj_name)` before any write. If present, fail unless `--force`. Document `--force` in `help_text()` and main usage.
- **Acceptance:** integration test that runs `nav create x` twice; assert second call exits non-zero and no file mtime changes.

### P0-3. `create` leaves a half-initialised project on clone failure
- **Where:** `src/core/commands/create.cpp:44-69`.
- **Symptom:** config + CMakeLists + main.c are written before `git clone`. A clone failure (offline, bad branch, full disk) leaves a partial project plus non-zero exit code, confusing the user.
- **Fix:** clone into a temp directory first; on success, scaffold the project and move the clone into `extern/NavHAL`. Equivalent alternative: scaffold + clone, then `fs::remove_all(proj_name)` on failure. Prefer the temp-dir approach — atomic and friendlier on partial-disk scenarios.
- **Acceptance:** test with a deliberately broken `repo_url`; assert that `proj_name/` does not exist after the failed call.

### P0-4. `update` swallows unmapped-binary warnings on the install path
- **Where:** `src/core/commands/update.cpp:88-105`.
- **Symptom:** when `packages_to_install` is non-empty, the function returns `ins_res.exit_code` and ignores `unmapped`. Partial success (some installed, some unmapped) looks like total success.
- **Fix:** the final return must be `(ins_res.exit_code != 0 || !unmapped.empty()) ? 1 : 0`. Re-print the unmapped list after the install spam so it remains visible.
- **Acceptance:** mock the toolchain to return a missing binary with no package mapping; run `update`; assert exit code 1 and that the unmapped binary name appears in output.

### P0-5. `probe_tool` and `execute` have no timeout
- **Where:** `src/core/execution_context.cpp:13` (no timeout in `run_raw_command`); `src/core/toolchain.cpp:144` (caller).
- **Symptom:** a misbehaving `--version` (interactive prompt, hung subprocess, license-check phone-home) hangs the entire CLI indefinitely.
- **Fix:** extend `IExecutionContext::execute` with `std::chrono::milliseconds timeout = std::chrono::seconds(10)`. In `HostExecutionContext::execute`, replace the blocking `read` + `waitpid` with a `poll(pipefd[0], POLLIN, deadline_ms)` loop and `waitpid(WNOHANG)`. On timeout: `kill(pid, SIGTERM)`, wait 500 ms, `kill(pid, SIGKILL)`, drain pipe, return `exit_code=-1` with appended `"<timed out>"` marker.
- **Caller impact:** `probe_tool` should keep the 10 s default. Build / upload / clone should pass a sentinel (e.g. `0ms` meaning "no timeout") or an explicit hour-scale value. Audit every call site as part of this change.
- **Acceptance:** unit test that runs `["sleep", "30"]` with a 1 s timeout; assert non-zero exit and elapsed wall time under 2 s.

## P1 — UX & semantics

### P1-1. No `isatty` / `NO_COLOR` handling
- **Where:** `include/nav/core/ui.hpp:9-15` — ANSI constants are unconditional `std::string`s.
- **Symptom:** `nav check > log.txt` writes literal `\033[…m` into the log; CI logs render with garbage colour codes.
- **Fix:** convert the colour constants from `const std::string X = "..."` to inline accessors that consult a process-wide singleton. The singleton checks `isatty(STDOUT_FILENO)` and the `NO_COLOR` env var on first call; if either disables colour, every accessor returns an empty string. After P1-3 lands, also honour `--color={auto,always,never}`.
- **Acceptance:** test that pipes `nav check` into a file and `grep -P '\\033'` returns zero matches.

### P1-2. Per-command `--help` is not routed
- **Where:** `src/cli/main.cpp:67` (dispatch); `include/nav/core/command.hpp:17` (`help_text()` defined but never invoked anywhere).
- **Symptom:** `nav build --help` runs the build. `help_text()` is dead code.
- **Fix:** after lookup but before `it->second->run(...)`, if `remaining_args[0]` is `--help` or `-h`, print `it->second->help_text()` and return 0. Expand each `help_text()` override to a multi-line usage block listing flags. Once P1-3 lands, route through the flag parser instead.
- **Acceptance:** for each verb, `nav <verb> --help` exits 0 with non-empty output that mentions the verb name.

### P1-3. No global flag parser — **LANDED (Phase 0)**
- **Where:** `src/cli/main.cpp` now wraps CLI11.
- **Resolved by:** CLI11 vendored via `FetchContent`. Per-subcommand `prefix_command()` collects verb argv raw into `remaining()`. `ICommand::register_flags(CLI::App&)` is an opt-in hook (default no-op).
- **Design note:** the original acceptance criterion called for `nav build --verbose` to also work (flags after the verb). That requires CLI11's `fallthrough()` mode, which hijacks positional extras like `board list` — incompatible with `prefix_command()`. We chose `prefix_command()`: **global flags must come before the verb** (`nav --verbose build`). Matches git/cargo/npm/kubectl conventions. See the "CLI design conventions" section above; this is not a follow-up to undo.

### P1-4. `monitor` autodetect is non-deterministic
- **Where:** `src/core/commands/monitor.cpp:45-53`.
- **Symptom:** with multiple `ttyACM*`/`ttyUSB*` devices, `directory_iterator` returns them in filesystem order. The chosen device is logged only as part of the next `step` line, easy to miss.
- **Fix:**
  - Collect candidates into a `std::vector<std::string>`, `std::sort` them.
  - If exactly one, log it at INFO level (`ui::info("Using port: " + port)`) *before* the existing `step` line.
  - If two or more, print the sorted list and exit non-zero requesting an explicit `--port`. Do not silently pick one.
- **Acceptance:** test that exposes two fake candidates (parameterise the scan path to allow a test fixture); assert non-zero exit + both paths printed.

### P1-5. `monitor` busy-polls instead of using `poll()`
- **Where:** `src/core/commands/monitor.cpp:117-125`.
- **Symptom:** 1 ms `sleep_for` in a `read` loop. Wastes CPU on every idle monitor session.
- **Fix:** replace with `poll(&pollfd{fd, POLLIN, 0}, 1, 100ms)` in the loop. Check `g_running_monitor` after each poll return. SIGINT path unchanged.
- **Acceptance:** measured idle CPU drops from a few percent to ~0.

### P1-6. Spelling: "detatch" / "detatched"
- **Where:** `src/core/commands/monitor.cpp:109,132`.
- **Fix:** "detach" / "detached". Two-character edits; bundle with P1-5.

### P1-7. `toml::parse_error` is swallowed without diagnostic
- **Where:** `src/core/config.cpp:30-32`.
- **Symptom:** users with a broken `nav.toml` see "Failed to parse 'nav.toml'" with no line/column.
- **Fix:** catch `const toml::parse_error& e`. Either return a `tl::expected<ProjectConfig, ParseError>` (preferred), or print the error with `e.description()` and `e.source().begin.line` / `.column` before returning `nullopt`.
- **Acceptance:** test against a malformed fixture; assert the error message contains the line number of the broken token.

### P1-8. `check.cpp` mixes `std::cout` writes with `ui::*` helpers
- **Where:** `src/core/commands/check.cpp:20-30,55-66`.
- **Symptom:** per-tool detection lines hard-code colour escapes inline, bypassing `ui::*`. Inconsistent with the rest of the codebase and complicates the P1-1 colour-disable work.
- **Fix:** add `ui::tool_ok(name, detail)`, `ui::tool_missing_critical(name)`, `ui::tool_missing_optional(name)` helpers in `ui.hpp` and route all three branches through them. Land alongside P1-1.

### P1-9. `ProjectConfig` has unused fields
- **Where:** `include/nav/core/config.hpp:9-16`. `target_arch`, `target_vendor`, and `build_backend` are parsed in `config.cpp:37-40` but read by no caller.
- **Decision:** keep, do not remove. Phase 1 (board catalog) will consume `target_arch` and `target_vendor`; Phase 2+ will dispatch on `build_backend`.
- **Action now:** add a `// consumed in Phase 1/2` comment at the parse site as a marker, nothing more.

## P2 — Polish

### P2-1. Silent-mode output accumulates unbounded
- **Where:** `src/core/execution_context.cpp:62-73`.
- **Symptom:** even with `silent=true`, the merged stream is appended to a `stringstream`. Currently fine because all silent callers are short `--version` probes. Becomes a latent OOM as soon as any new caller goes silent on a chatty toolchain.
- **Fix:** if `silent=true`, cap the captured buffer at 1 MiB; truncate with a `"<output truncated>"` marker. Document the cap in the header comment.

### P2-2. `-v` shortcut binds to `--version`, colliding with conventional verbose — **LANDED (Phase 0)**
- **Resolved by:** CLI11 `set_version_flag("--version,-V", NAV_VERSION)` + `add_flag("--verbose,-v", ...)`. `-V` is version, `-v` is verbose.

### P2-3. Stub registry verbs still listed in main help
- **Where:** `src/cli/main.cpp:25-28`. Keep the `(coming soon)` tag until each gets a real implementation. As each Phase 2 verb lands, drop the tag for that verb individually.

### P2-4. No tests anywhere in the repo
- **Where:** no `tests/` directory exists.
- **Fix:** create `tests/` with:
  - `tests/CMakeLists.txt` wiring GoogleTest via `FetchContent`.
  - `tests/mock_execution_context.hpp` — records argv, returns scripted `CommandResult`s, supports per-call timeouts.
  - First targets: `test_config_parse.cpp` (P1-7), `test_toolchain.cpp` (P0-4, P0-5), `test_create.cpp` (P0-1, P0-2, P0-3), `test_update.cpp` (P0-4).
- **Sequencing:** must land *before* the Phase 1 board-catalog work. Refactors without tests are dangerous.

---

# Part B — Roadmap to PlatformIO-parity

The vision in `docs/plan.md` is "PlatformIO + Cargo + Docker + ROS for robotics." The current CLI implements a small fraction of that surface. This roadmap phases the gap into three milestones, each independently shippable. (Originally four — Phase 2 absorbed the standalone toolchain manager; see the strategic note in Phase 2.)

## Phase 0 — Defect cleanup

**Duration:** 1–2 weeks.
**Goal:** close Part A so the foundation is honest before any new feature work.

**Deliverables:**
- All P0 items (5 fixes).
- P1-1, P1-2, P1-3 (colour gating, `--help` routing, global flag parser).
- P2-4 (test harness + first tests).

**Exit criterion:** existing verbs are correct, scriptable, colour-respectful, and have non-trivial test coverage. P1-3 unblocks every subsequent phase.

## Phase 1 — Board catalog (LANDED)

**Goal:** replace substring-based board detection (`src/core/toolchain.cpp:121`) with a declarative catalog. Precondition for everything else.

Both halves shipped: 1.1 (catalog primitive + verbs) and 1.2 (CMake generation).

### Phase 1.1 — Catalog primitive (LANDED)

The board catalog, the verbs that read it, and the toolchain integration. Does *not* yet touch the generated CMake template or the NavHAL-driven build flag flow.

- `include/nav/core/board.hpp` + `src/core/board.cpp` — `Board` struct + `BoardCatalog` with first-add-wins merge across search paths.
- 5 board files shipped under `share/nav/boards/`: `pico`, `nucleo_f401re`, `nucleo_h743zi`, `esp32_devkitc`, `arduino_uno`.
- `default_catalog()` walks `$NAV_BOARD_PATH` → `<project>/.nav/boards` → exe-relative `<bindir>/../share/nav/boards` → `/usr/share/nav/boards`.
- CMake install rule for the bundled catalog (`install(DIRECTORY share/nav/boards/ ...)`).
- `nav board list`, `nav board info <id>` verbs (`src/core/commands/board.cpp`).
- `ToolchainManager::get_project_requirements` now takes a resolved `Board` and derives `{compiler, flash_tool}` from it. `check` and `update` look up the catalog and warn cleanly when the board id isn't known.
- 6 catalog tests (parse, missing-required-fields, first-add-wins, `NAV_BOARD_PATH` override).

### Phase 1.2 — Catalog-driven CMake generation (LANDED)

The harder half: make `nav build` actually consume the catalog at build time instead of inheriting NavHAL's per-board CMake. Touches the generated `templates/cmakelists.txt.in`. Tracked separately because it interacts with NavHAL's existing `.config` / `config.cmake` flow.

**Deliverables:**

1. `nav build` generates `<project>/build/nav-board.cmake` from the catalog before invoking cmake. The file exposes `NAV_BOARD_*` cache variables (compile flags, link flags, flasher, flash address, board id, mcu).

2. `templates/cmakelists.txt.in` rewritten to `include()` the generated file and consume the variables via `target_compile_options` / `target_link_options`. The current hard-coded `-mcpu=cortex-m4` / `-mfpu=fpv4-sp-d16` block (`templates/cmakelists.txt.in:38-58`) goes away.

3. Decide the interaction with NavHAL: either (a) Nav fully replaces NavHAL's `config.cmake` for the board portion (preferred), or (b) Nav writes a higher-precedence file that NavHAL's CMake explicitly consults.

4. Consume `ProjectConfig::target_arch` and `target_vendor` (resolves P1-9) in the generated CMake or in board id validation.

5. Golden-file tests: generated `nav-board.cmake` per board.

**Exit criterion:** changing a board's compile flags is a TOML edit, not a CMake edit. `nav create my-app --board pico` produces a project that builds out of the box for the Pico, not for the F401.

**Dependencies:** Phase 1.1 (done).

## Phase 2 — Registry: libraries + toolchains + lockfile

**Duration:** 8–14 weeks.
**Goal:** real `nav add`, `nav search`, `nav toolchain install`, `nav.lock`. Stand up Nav's own registry, then use it to distribute *both* libraries and toolchains as package types under a single mechanism.

**Strategic note:** the previous version of this plan split package management (Phase 3) from a standalone toolchain manager (Phase 2) that downloaded tarballs from upstream Arm/AVR/etc. That split is collapsed. Toolchains are repackaged and hosted by Nav itself, so the registry, cache, lockfile, and download path are shared. Benefits: one HTTP/auth/signing pipeline instead of two; stable URLs we control; SHA256s we sign instead of depending on upstreams that move their CDNs.

### Package model

Every registry entry is a *package* with one of two kinds:

- `kind = "library"` — C/C++ sources/headers/static archives consumed by the build.
- `kind = "toolchain"` — pre-built binaries (cross-compilers, flashers, assemblers) exposed as resolved binary paths.

The cache layout, lockfile schema, and resolver are shared. The build wires each kind in differently: libraries become include + link inputs; toolchain binaries become the resolved `CMAKE_C_COMPILER` / objcopy / size paths flowing into `nav-board.cmake`.

### Deliverables

1. **Registry index** — phase-1 strategy from `docs/plan.md:303-315`. A GitHub repo `nav-index/` of JSON metadata files, two-character prefix sharded:
   ```
   nav-index/
     ar/arm-none-eabi-gcc.json
     im/imu-driver.json
     na/nav-hal.json
   ```
   Each entry lists versions, per-platform tarball URLs, SHA256s, kind, dependency specs. Phase 2.1 reads this format via `LocalIndexClient`; Phase 2.3 adds the HTTP equivalent. (JSON chosen per the data-format policy in the Status section.)

2. **Index file format** (publisher side, library kind):
   ```json
   {
     "name": "nav-hal",
     "versions": [
       {
         "version": "0.5.0",
         "kind": "library",
         "description": "...",
         "license": "MIT",
         "downloads": {
           "source": {
             "url": "https://...",
             "sha256": "..."
           }
         },
         "dependencies": {
           "cmsis": "^6.0.0"
         }
       }
     ]
   }
   ```

   And toolchain kind:
   ```json
   {
     "name": "arm-none-eabi-gcc",
     "versions": [
       {
         "version": "13.2.0",
         "kind": "toolchain",
         "downloads": {
           "linux_x86_64": {
             "url": "https://registry.navrobotec.com/.../arm-none-eabi-gcc-13.2-linux-x86_64.tar.xz",
             "sha256": "..."
           },
           "darwin_arm64": {
             "url": "https://...",
             "sha256": "..."
           }
         },
         "toolchain_binaries": [
           "arm-none-eabi-gcc",
           "arm-none-eabi-g++",
           "arm-none-eabi-objcopy",
           "arm-none-eabi-size"
         ]
       }
     ]
   }
   ```

3. **Resolver** — DFS-based with cycle + conflict detection. SAT solver deferred. Same code path resolves library and toolchain dependency graphs; toolchains typically have no transitive deps.

4. **Lockfile** at `nav.lock` (JSON; eventual successor to today's `nav.toml`-style manifests):
   ```json
   {
     "packages": [
       {
         "name": "nav-hal",
         "version": "0.5.0",
         "kind": "library",
         "source": "registry+https://github.com/ragnar-vallhala/nav-index",
         "checksum": "sha256:...",
         "dependencies": ["cmsis@6.0.1"]
       },
       {
         "name": "arm-none-eabi-gcc",
         "version": "13.2.0",
         "kind": "toolchain",
         "source": "registry+https://github.com/ragnar-vallhala/nav-index",
         "checksum": "sha256:..."
       }
     ]
   }
   ```

5. **Cache** at `~/.nav/packages/<name>-<version>-<sha>/`. Content-addressed; shared across projects. Toolchain packages put binaries under `bin/`; library packages put headers under `include/` and archives under `lib/`. The kind determines layout.

6. **Project manifest additions.** Project config currently lives in `nav.toml`; per the data-format policy it will migrate to YAML during Phase 2. For the registry consumer-side fields the structure is (TOML shown for current compatibility; YAML translation is mechanical):
   ```toml
   [dependencies]
   nav-hal = "^0.5.0"

   [toolchain]
   compiler = "arm-none-eabi-gcc@13.2.0"
   ```
   Both blocks are optional. With no `[toolchain]`, nav falls back to the binary name from the board catalog resolved against `$PATH` — today's behaviour. With a pinned `[toolchain]`, nav resolves the binary path from the cache and feeds it into `nav-board.cmake`.

7. **Build wiring**:
   - Libraries: extracted under `~/.nav/packages/`; the generated CMake (Phase 1.2 already writes `nav-board.cmake`) gets a sibling `nav-deps.cmake` adding include and link directories.
   - Toolchains: `nav build` resolves `[toolchain].compiler` from the lockfile to a concrete path and overwrites `NAV_BOARD_COMPILER` in `nav-board.cmake` with the resolved path.

8. **Verbs promoted from stubs**:
   - `nav add <name>[@<spec>]` — edits `nav.toml`, resolves, locks, fetches.
   - `nav remove <name>`.
   - `nav search <query>` — queries the index.
   - `nav update [<name>]` — re-resolves. **Naming clash:** today's `nav update` (toolchain/system-package refresh) needs renaming first. Rename existing verb to `nav doctor --fix`; reuse `nav update` for re-resolving dependencies.
   - `nav toolchain list` / `nav toolchain which <name>` — read-only views into resolved toolchain packages. (Install is just `nav add` with a toolchain-kind package; no separate install verb.)
   - `nav publish` — gated on Phase 3 auth.
   - `nav login` — gated on Phase 3 auth.

9. **`nav check` → `nav doctor` rename**. Today's `check` is an environment doctor; rename it. `nav check` is reserved for Phase 3 static analysis. Make the rename visible in `--help` for one minor release before retiring the old name.

### Exit criteria

- A fresh machine with only `nav` + `git` installed can `nav add` a library and `nav add arm-none-eabi-gcc@13.2`, then build a Phase-1 example board end-to-end. No host package manager involvement.
- `nav.lock` round-trips: deleting the cache and re-running `nav build` reproduces the exact same artifact set.
- Feature parity with `pio pkg install` for static C/C++ libs.

### Dependencies

Phase 0 + Phase 1.

### Open questions to settle before starting

- **Registry hosting timeline.** Phase 1 GitHub-repo index is enough to bootstrap; the dedicated registry service (`docs/plan.md:323-335`) is a separate workstream. Toolchain tarballs are larger than library tarballs — fine on GitHub LFS or a CDN bucket for v1, but worth budgeting.
- **Signing.** SHA256 is the floor; package signing (cosign / minisign / in-house key) before opening publish to third parties.
- **Toolchain repackaging policy.** We host repackaged upstream tarballs (Arm GNU Toolchain, avr-gcc, xtensa-esp32-elf-gcc, etc.). Need a documented re-pack process: which upstream releases get mirrored, when, with what license attribution.

## Phase 3 — Framework, flashing, monitor filters, debug, test

**Duration:** long tail; each item is its own multi-week effort. Sequence by user demand, not by dependency.

**Sub-items:**

1. **Framework dispatch** — Arduino, ESP-IDF, pico-sdk, STM32Cube, Mbed. Hard part is maintenance burden (each framework has its own build conventions), not the dispatch glue. Plan for plugin-loadable frameworks per `docs/plan.md` §8 from the start so external maintainers can carry the load.

2. **Flash protocol selection** — `[flashing]` per board (already in Phase 1 schema). Built-in adapters: `picotool`, `stlink`, `openocd`, `esptool`, `dfu-util`, `jlink`. Stop deferring to CMake's `flash` target — that indirection is opaque and inflexible.

3. **`nav monitor` filters** — `--filter timestamp`, `--filter hex`, `--filter log2file=path`, `--filter send_on_enter`. Configurable EOL, local echo, baud preset list via `[monitor]` in `nav.toml`.

4. **`nav debug`** — GDB + `gdbserver` / OpenOCD integration. `[debug]` config block. Probe selection (stlink / jlink / cmsis-dap). Start with one-board-one-probe support; generalise later.

5. **`nav test`** — Unity runner for embedded targets, GoogleTest for host targets. On-device asserts marshalled back over serial.

6. **`nav check` (static analysis)** — `cppcheck`, `clang-tidy`. Phase 2's rename of today's `check` to `nav doctor` clears the namespace.

7. **Registry auth** — `nav login` writes `~/.nav/credentials` (chmod 600). `nav publish` uses it. TLS pinning to the registry host.

Exit criteria are per sub-item; no single phase-level criterion.

## Out of scope (deferred indefinitely)

From `docs/plan.md`: ROS integration, fleet deployment, OTA, distributed build cache, GUI, AI model deployment, simulator integration. These are real differentiators *after* PIO-parity, not paths to it. Re-evaluate after Phase 3 completes its core sub-items.

---

# Sequencing summary

1. **Phase 0** (1–2 weeks): defect cleanup + test harness. Hard prerequisite. **LANDED.**
2. **Phase 1** (3–6 weeks): board catalog. **LANDED** across 1.1 / 1.2 / 1.3.
3. **Phase 2** (8–14 weeks): registry — libraries + toolchains + lockfile. **The watershed.** Stand up `nav-index`, build the resolver, ship `nav add` / `nav.lock`, host repackaged toolchains as packages so the same machinery covers both. (Was previously two phases — a standalone toolchain manager and a separate dependency manager — collapsed because they share the cache, lockfile, HTTP, and signing pipeline.)
4. **Phase 3** (open-ended): framework / flash / monitor filters / debug / test / registry auth.

Floor estimate to PlatformIO-parity-equivalent: roughly 8–14 weeks of focused C++ work on the core, plus Phase 3 on top. Phase 2 is the only phase that takes real calendar time before it ships meaningful UX, because standing up the registry index is most of the work; library / toolchain consumption follows quickly once the index exists.

## The Rust-rewrite question

`docs/plan.md:113-126` recommends Rust for the core. The C++ implementation is past the point where a rewrite would be cheap (~1.5k LOC + 40 tests + templates + CPack + Debian packaging + CI). Revisit this decision **explicitly before starting Phase 2**, not drifted into. The triggering signals would be:

- Async story for the registry HTTP client becomes load-bearing.
- Cross-platform packaging pain (Windows support) becomes a blocker.
- The team grows and onboarding cost favours Rust.

Until one of those is concretely true, stay in C++.

---

# Testing approach

Every defect fix above ships with a test. Every phase ships with golden-file or end-to-end tests for new schemas:

- **Phase 0:** unit tests for each P0 fix. `MockExecutionContext` available from P2-4. (Landed.)
- **Phase 1:** schema validation tests for `share/nav/boards/*.toml`; golden-file tests for generated `nav-board.cmake`. (Landed.)
- **Phase 2:** resolver unit tests (cycles, conflicts, semver edge cases); lockfile round-trip tests; integration test against a fake index repo served via `python3 -m http.server`; toolchain-kind package install + path resolution test.
- **Phase 3:** per sub-item. Debug and on-device test sub-items need real hardware in CI — defer until the rest of the phase is in users' hands.

The mockable `IExecutionContext` (`include/nav/core/execution_context.hpp:14-21`) is the keystone — every command takes one by reference, so almost any test can be written without spawning real processes.
