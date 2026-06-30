# Plan — Consuming vaios as a nav library

Scope: make the vaios RTOS (`../vaios`, `github.com/ragnar-vallhala/vaios`) consumable as a
nav `[dependencies]` library, so a firmware project can `nav lib add ../vaios && nav build`
and link `libvaios.a`. Builds on the library-project support already in nav
(`type = "library"`, `[dependencies]`, `nav lib`, `add_subdirectory`-based linking).

## Status

- **Tier A — DONE.** `nav lib add <vaios> --option NAVHAL=ON` then `nav build` configures,
  compiles `libvaios.a` against the consumer's NavHAL, and exports vaios's headers. Delivered:
  per-dependency `options` in nav (`config`/`libdeps`/`nav lib`), and on the vaios side a
  `nav.toml`, the `if(NOT TARGET hal)` NavHAL-reuse guard, and `include/` exported PUBLIC.
- **Tier B — vector ownership + config-ownership DONE; on-target boot remains.**
  - *Vectors:* a library may declare `[library].navhal_submodule = true` (vaios does); nav writes
    `nav-deps-pre.cmake` (`add_compile_definitions(SUBMODULE)`) included **before** NavHAL is added,
    so NavHAL cedes SysTick/PendSV/SVCall/HardFault. A firmware depending on vaios **links a
    complete ARM ELF** — handlers resolve once (from vaios), `v_init`/`v_start` linked in.
  - *Config (B2):* the `.config` is a capability set. NavHAL needs none; a library ships its own
    (`.config` or `navhal.config`) to declare what it needs; the app's `.config` is the single
    authority that configures NavHAL. `nav lib add` **unions** a path-dep's requirements into the
    app `.config` (missing keys only; the user's values win, conflicts warned). `nav build`
    **enforces** that the app `.config` satisfies every dependency's needs across the transitive
    graph, failing early with the exact `lib needs KEY=VALUE` lines. (`navconfig.{hpp,cpp}`.)
  - Still open: B4 (boot on hardware/QEMU).

## Background — what vaios is

vaios is **not a leaf library**; it is a NavHAL-based RTOS:

- Builds `add_library(vaios STATIC …)` from `kernel/*.c`, plus a `portable` static lib from
  `portable/<arch>/`, and (on a NavHAL build) embeds NavHAL itself via
  `add_subdirectory(extern/NavHAL)` and links `hal`/`common`.
- Already has standalone-vs-submodule detection
  (`if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR) project(vaios) else …`) and
  `target_include_directories(vaios PUBLIC include/ … extern/NavHAL/include)`.
- Gated on `-DNAVHAL=ON` (default **OFF** → it builds the QEMU/semihosting non-HAL path).
- **Owns** the vector table, `startup.s`, the SysTick/PendSV handlers (`portable/cortex-m4/port.c`,
  `kernel/utils.c`), and uses NavHAL's board `linker.ld`. It stages its own `navhal.config`
  into `extern/NavHAL/.config` and drives NavHAL's Kconfig.
- Targets `nucleo_f401re` / cortex-m4, FPU hard.
- Pulls NavHAL as a **git submodule** (`.gitmodules`).

This overlaps with nav's firmware-consumer model, which itself owns NavHAL (`add_subdirectory`
+ `--whole-archive hal`) and the linker. The conflicts below all stem from that overlap.

## How nav consumes a library today (recap)

`nav build` → `resolve_library_deps()` walks the transitive graph (path / git-clone into the
shared cache) → emits `build/nav-deps-libs.cmake` which `add_subdirectory()`s each library
(running the dependency's **own** CMakeLists) and `target_link_libraries()`s the edges. The
consumer links its direct deps; CMake propagates PUBLIC includes and the inherited
compiler/flags. nav passes **no** CMake options to a dependency, and clones git deps with
`--depth 1 --branch` (**no** `--recurse-submodules`).

---

## Tier A — configures, compiles, links `libvaios.a` (symbols resolve)

Goal: `nav lib add ../vaios && nav build` in a firmware project configures cleanly, compiles
vaios, and links `libvaios.a` with all symbols resolved. **Not** a bootable image (that is
Tier B). Estimate: ~half a day.

### A1. Add `nav.toml` to vaios *(trivial)*
```toml
[project]
name = "vaios"          # must equal the CMake target name vaios defines
version = "0.1.0"
type = "library"

[target]
arch = "cortex-m4"
vendor = "stm32"
board = "nucleo_f401re"

[build]
backend = "cmake"
```
`name = "vaios"` is the canonical name nav links; vaios's `add_library(vaios …)` already
provides that target.

### A2. Resolve the double-NavHAL conflict *(edit in vaios, ~10 lines)*
A nav firmware consumer does `add_subdirectory(extern/NavHAL)` → defines `hal`/`common`.
vaios, when `add_subdirectory`'d by that consumer, would add NavHAL **again** → duplicate
targets → configure error. Make vaios reuse the consumer's NavHAL:

- Guard vaios's NavHAL block: `if(NAVHAL AND NOT TARGET hal)` around the
  `add_subdirectory(extern/NavHAL)` + `configure_file(navhal.config …)` + the
  `target_compile_options(hal/common …)` lines.
- In `kernel/CMakeLists.txt`, link `hal`/`common` only `if(TARGET hal)`.

vaios's NavHAL headers then resolve through the consumer's `hal` PUBLIC include dirs (vaios
already links `hal`). When vaios *is* the top-level (standalone), the guard is a no-op and it
embeds NavHAL as before.

### A3. Enable `NAVHAL` for the dependency build *(nav feature, ~1–2 hrs)*
vaios needs `-DNAVHAL=ON`; nav passes nothing today. Add **per-dependency CMake options** to
`nav.toml`:
```toml
[dependencies]
vaios = { path = "../vaios", options = ["NAVHAL=ON"] }
```
- `config.{hpp,cpp}`: parse an `options` string array on `Dependency`.
- `libdeps.{hpp,cpp}`: carry options through `ResolvedLib`; before each `add_subdirectory`,
  emit `set(<KEY> <VAL> CACHE … FORCE)` (or `set(<KEY> <VAL>)`) for each option.
- `nav lib add … --option NAVHAL=ON` (repeatable) to write them.

Alternative (no nav change): flip vaios's default to `NAVHAL ON`. Cheaper now, but couples
vaios's standalone default to nav; the nav feature is the cleaner long-term answer and helps
any option-driven dependency.

### A4. Verify
A firmware project depending on vaios configures, compiles, and links; a vaios symbol (e.g.
`vaios_init` / a task API) is present in the consumer ELF. Symbols only — boot is Tier B.

---

## Tier B — a firmware that boots and runs the RTOS via `nav build` / `nav upload`

Estimate: ~2–4 days. The hard part is **ownership of the vector table, startup, and linker**.
nav's firmware template provides no startup and links `--whole-archive hal` against NavHAL's
`linker.ld`; vaios provides `startup.s`, owns SysTick/PendSV/SVCall, and expects the
`SUBMODULE` define so NavHAL cedes those vectors. To produce a runnable image:

### B1. Let a library supply startup + linker + vectors
Extend nav's firmware (executable) template / board model so a dependency can contribute:
- a startup object (`startup.s`) added to the firmware target's sources,
- a linker script override (use vaios's, or the board `linker.ld` vaios already points at),
- the `SUBMODULE` compile definition on the consumer's NavHAL so vaios owns the vectors
  (today the consumer builds NavHAL, so vaios cannot inject this — needs a nav-level hook,
  e.g. a board/dep flag that sets `SUBMODULE` on the NavHAL build).

### B2. Single NavHAL `.config` owner
Decide whose driver set wins: the firmware's `.config` (nav stages it today) vs vaios's
`navhal.config`. Likely the firmware should adopt vaios's required driver set, or vaios
exposes its `navhal.config` as the consumer default. Needs a documented rule + wiring.

### B3. Flag/ABI consistency
FPU (`-mfloat-abi=hard -mfpu=fpv4-sp-d16`), `-mcpu`, `-mthumb`, C11 must match between the
firmware, NavHAL, and vaios. Inherited flags mostly cover this; verify no `-O0`/soft-float
drift (vaios already patches NavHAL's `-O0` per-target).

### B4. Verify
`nav build` produces an ELF whose reset/vector path is vaios's; `nav upload` flashes; the
RTOS boots on a nucleo_f401re (or Renode/QEMU) and runs a task.

---

## Cross-cutting: git dependencies + submodules *(optional, 1-line)*

vaios pulls NavHAL as a submodule. nav's `ensure_git_cached` clones with `--depth 1 --branch`
and **no** `--recurse-submodules`, so a `git`-sourced vaios would have an empty
`extern/NavHAL`. Options:
- Use a local `path` dep (submodule already populated) — fine for now.
- Rely on the Tier-A NavHAL-reuse guard (consumer provides NavHAL; vaios's empty submodule
  is never entered).
- Add `--recurse-submodules` to `ensure_git_cached` to make git deps self-contained generally.

---

## Recommendation

Do **Tier A** first (A1–A4): it proves the dependency plumbing end-to-end with a real,
non-trivial library and is low-risk. Treat **Tier B** as a separate design effort — "a
bootable RTOS image through nav" is its own integration (vector/startup/linker ownership),
not just wiring.

## Touch list

- **vaios**: add `nav.toml` (A1); guard NavHAL block + conditional `hal`/`common` link (A2).
- **nav**: per-dependency `options` — `config.{hpp,cpp}`, `libdeps.{hpp,cpp}`, `commands/lib.cpp`
  (A3); firmware-template startup/linker/`SUBMODULE` hooks (B1); NavHAL `.config` ownership
  rule (B2); optional `--recurse-submodules` in `navhal.cpp` (cross-cutting).
