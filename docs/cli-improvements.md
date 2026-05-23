# Nav CLI — Fixes & Upgrades Plan

Scope: the C++ CLI in `src/` and `include/`. Registry-side work (`registry-api/`, `registry-ui/`) is tracked separately.

NavHAL branch policy: we will continue to consume `--branch stable`. Branch pinning is **not** part of this plan.

---

## P0 — Correctness bugs

These are observable defects that should be fixed before the next release.

### P0-1. `CommandResult::stderr_output` is always empty
- **Where:** `include/nav/core/execution_context.hpp:9-13`, `src/core/execution_context.cpp:35-37,81`.
- **Symptom:** child stdout and stderr are both `dup2`'d onto the same pipe, then the merged stream is written to `result.stdout_output`. `stderr_output` is dead.
- **Fix:** either
  - (a) drop the `stderr_output` field and rename `stdout_output` → `output` to honestly reflect merged capture, **or**
  - (b) use two pipes (`pipe(stdout_fd)`, `pipe(stderr_fd)`), `dup2` each on the child, and `poll(2)` both in the parent loop.
- **Recommendation:** (a). Every current caller treats output as a single blob and the live mirroring relies on merge order being preserved. Splitting buys nothing right now and complicates the live-mirror path.
- **Caller impact:** none — no caller reads `stderr_output` today.

### P0-2. Project-root detection only looks at CWD
- **Where:** `src/core/command.cpp:246, 283, 288, 428, 530`.
- **Symptom:** `nav build` / `upload` / `clean` / `update` fail from any subdirectory of a project because `fs::exists("nav.toml")` is CWD-relative.
- **Fix:** add a helper `std::optional<fs::path> find_project_root(fs::path start = fs::current_path())` in a new `src/core/project.cpp` that walks parents until it finds `nav.toml` or hits `/`. Replace each `fs::exists("nav.toml")` check with a call to it; `chdir` into the found root before invoking cmake/git so relative paths inside `CMakeLists.txt` keep working.
- **Edge case:** if `start` is not under any project, return `nullopt` and surface the existing "must be executed from within a valid project root" error.

### P0-3. Naive TOML parsing breaks on legal TOML
- **Where:** `src/core/command.cpp:484-494, 554-564` (both `CheckCommand` and `UpdateCommand` re-implement the same hand-rolled scan).
- **Symptom:** any of these legal forms breaks the parser silently:
  - `board = "x"  # inline comment` (last_quote matches the `#`-line's quote on the next iteration)
  - `board="x"` (no spaces around `=`)
  - `[target] \n board = "x"` works by accident; `[other.target.board]` table headers can match `find("board")` falsely (`UpdateCommand` only checks for both `"board"` and `"="`, so `[board.profile]` plus a later `= ...` line could trip it).
- **Fix:** vendor `toml++` (header-only, single file `tomlplusplus/toml.hpp`) into `extern/` or pull it via `FetchContent`. Then:
  ```cpp
  auto tbl = toml::parse_file("nav.toml");
  std::string board_id = tbl["target"]["board"].value_or("");
  ```
- **Bonus:** the same helper can read `[project].name`, `[target].arch`, `[target].vendor`, `[build].backend` — used by P1-2 below.
- **Acceptance:** add a `tests/test_config_parse.cpp` with the comment-and-spacing fixtures listed above.

### P0-4. `update` silently no-ops on non-Debian hosts even when probes detect their package manager
- **Where:** `src/core/command.cpp:606-612`; cross-reference `src/core/toolchain.cpp:37-49` (which already discovers `dnf`, `pacman`, `brew`).
- **Symptom:** on Fedora/Arch/macOS, `UpdateCommand` reports "Automatic Provisioning only available on Debian/Ubuntu/Mint" and returns success, masking the fact that `arm-none-eabi-gcc` is still missing.
- **Fix:** introduce a `PackageManager` enum + small dispatch table:

  | Manager | Install command | Mapping |
  |---|---|---|
  | apt | `sudo apt install -y <pkg>` | as today |
  | dnf | `sudo dnf install -y <pkg>` | gcc-arm-none-eabi → `arm-none-eabi-gcc-cs` (or distro pkg) |
  | pacman | `sudo pacman -S --noconfirm <pkg>` | gcc-arm-none-eabi → `arm-none-eabi-gcc` |
  | brew | `brew install <pkg>` | gcc-arm-none-eabi → `gcc-arm-embedded` |

  The binary→package map needs to be per-manager because package names differ. Centralise in `toolchain.cpp`.
- **Return early properly:** if no supported manager is detected, return non-zero and print the missing binaries so the user can install them manually.

### P0-5. Cosmetic noise in generated `CMakeLists.txt`
- **Where:** `src/core/command.cpp:138-212` — section numbers go `1, 2, 2.5, 3, 4, 5, 6, 6, 5, 6` (duplicates at `command.cpp:189, 197, 203`).
- **Fix:** renumber to `1..N` in order. The same template is what every Nav-created project ships with, so the typo is visible to every user.

---

## P1 — Engineering hygiene

### P1-1. `command.cpp` is becoming a god-file (663 lines, 11 verbs)
- **Plan:** split into `src/core/commands/{create,build,upload,monitor,clean,check,update,registry}.cpp`. Keep `ICommand` declarations in `include/nav/core/command.hpp` (or split that too). `CMakeLists.txt:9-13` needs the new sources added to the `nav_core` library.
- **Sequencing:** do this **after** P0-3 (TOML parser), so the per-command files start with the shared parser already in place rather than each duplicating the hand-rolled scan.

### P1-2. Extract the embedded CMake/TOML/C templates from `command.cpp`
- **Where:** `src/core/command.cpp:45-96` (`.config`), `45` (`nav.toml`), `107-130` (`main.c`), `138-212` (`CMakeLists.txt`).
- **Symptom:** 170 lines of project-template R-strings inside `CreateCommand::run`, making it the longest function in the codebase.
- **Fix:** move templates to `templates/` at repo root, embed them at build time via `configure_file(... NEWLINE_STYLE LF)` + `#include` of the generated header, or use C++23 `#embed` once toolchains catch up. For now, the cleanest path is:
  1. `templates/cmakelists.txt.in`, `templates/nav.toml.in`, `templates/main.c.in`, `templates/dot_config.in`
  2. CMake step that runs `xxd -i` (or our own generator) to produce `include/nav/templates/embedded.hpp` with `constexpr std::string_view kCmakeListsTpl = ...;` etc.
  3. `CreateCommand` does a small `{{proj_name}}` substitution pass.
- **Bonus:** unblocks future board-specific templates (rp2040 vs stm32 main.c, different linker dirs, etc.).

### P1-3. `<filesystem>` included twice; `<memory>` included where unused
- **Where:** `src/core/command.cpp:5,14` (duplicate `<filesystem>`); `include/nav/core/command.hpp:5` (`<memory>` unused inside that header).
- **Fix:** trivial cleanup. Bundle with P1-1.

### P1-4. `find_binary_in_path` doesn't honour executable bit
- **Where:** `src/core/toolchain.cpp:11-28`.
- **Symptom:** `fs::exists(full_path)` returns true for non-executable files of the same name (rare but possible if a user has a stray `cmake` text file in a PATH dir).
- **Fix:** additionally check `fs::status(full_path).permissions() & fs::perms::owner_exec` (or use `access(full_path.c_str(), X_OK) == 0` for the simplest POSIX check). Same for following symlinks — `fs::exists` already does.

### P1-5. Version probing assumes `--version`
- **Where:** `src/core/toolchain.cpp:81`.
- **Symptom:** most tools agree, but it's a single point of failure for any future tool that uses `-V` / `version` / `--ver`.
- **Fix:** extend `ToolRequirement` with an optional `std::string version_flag = "--version"`. Default keeps current behaviour; per-tool overrides only when needed.
- **Priority note:** low — only matters when we add a tool that needs it.

---

## P2 — UX & polish

### P2-1. `print_usage` advertises mocked commands as if real
- **Where:** `src/cli/main.cpp:11-25`.
- **Symptom:** `add`, `search`, `login`, `publish` are listed identically to `build`/`upload`. README does not mention them at all, which is the more honest stance.
- **Interim fix:** tag mocked verbs with `(preview)` in `print_usage` and have each mocked command print a one-line "this command is a preview stub pending the registry rollout — see docs/plan.md" message. Remove the tag as each gets wired to the real registry.

### P2-2. No `--version` / `-v` flag on the binary itself
- **Where:** `src/cli/main.cpp:30`.
- **Fix:** add a check before the registry lookup:
  ```cpp
  if (args[0] == "--version" || args[0] == "-v") {
      std::cout << "nav " << NAV_VERSION << "\n";
      return 0;
  }
  ```
  Plumb `NAV_VERSION` from `CMakeLists.txt:2` via `target_compile_definitions(nav PRIVATE NAV_VERSION="${PROJECT_VERSION}")`.

### P2-3. `monitor` autodetect picks the first `tty*` alphabetically
- **Where:** `src/core/command.cpp:325-332`.
- **Symptom:** with two boards plugged in, you always get whichever sorted first. There's no list-and-prompt path and no way to know which one was chosen until output starts (or doesn't).
- **Fix:**
  - When multiple candidates exist, print the list and require `--port` rather than silently picking one.
  - When exactly one exists, log the chosen device path at INFO level (currently only logged inside the next `step` line — easy to miss).
- **Skip if:** stays single-port in practice. This only bites users with multiple ACM devices.

### P2-4. Registry verbs need real implementations
- **Where:** `src/core/command.cpp:633-661` (`add`, `search`, `login`, `publish`).
- **Plan:** out of scope for this document — depends on the `registry-api` contract. Tracked in `docs/plan.md` / `docs/mvp.md`. Once the API stabilises, these become HTTP clients reading `~/.nav/credentials` (already promised by the `login` stub at `command.cpp:653`).

---

## Sequencing & milestones

A reasonable order that minimises rebase pain:

1. **P0-3** (TOML parser via toml++) — unblocks P0-2 and P1-1 because both want a clean config reader.
2. **P0-2** (project-root walk) — small, isolated, immediately useful.
3. **P0-1** (drop `stderr_output`) — touches the public header, do it before more callers appear.
4. **P0-5** (renumber CMake template comments) — trivial, ship alongside P0-1.
5. **P0-4** (multi-distro provisioning) — independent; can land anytime.
6. **P1-1 + P1-2 + P1-3** (split god-file, extract templates, header cleanup) — one focused refactor PR.
7. **P1-4, P1-5** — opportunistic, bundle with any toolchain.cpp change.
8. **P2-1, P2-2** — small UX improvements, good "first PR" candidates.
9. **P2-3** — only if multi-port users complain.
10. **P2-4** — gated on registry-api landing.

## Testing approach

Currently no tests exist. Each fix above should land with:

- **P0-3:** unit test against `nav.toml` fixtures with comments, no-space `=`, table headers.
- **P0-2:** integration test that runs `nav build` from a subdirectory of a temp project.
- **P0-1:** compile check + caller audit (`grep stderr_output src/ include/`).
- **P0-4:** mockable via `IExecutionContext` — record the executed argv and assert per-manager.
- **P1-2:** snapshot test of generated `CMakeLists.txt` against a checked-in golden file.

The mockable `IExecutionContext` (`include/nav/core/execution_context.hpp:15-22`) is already the right shape — a `MockExecutionContext` that records calls and returns scripted `CommandResult`s makes most of the above straightforward to test without spawning real processes.
