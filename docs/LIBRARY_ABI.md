# Nav Library ABI — v1 (draft)

> Mirrored in the `NavHAL` repo at `docs/LIBRARY_ABI.md`. Keep both copies
> byte-identical; contributors usually only check one and assume it's
> authoritative.

The contract that any library or application built by `nav` must satisfy. The
goal is for `nav build` and `nav add` to operate on third-party code the
same way they operate on first-party code — versioned, capability-declared,
and reproducibly buildable across targets. There is no package server:
**a library is a Git repository, and GitHub is the host** (§12).

Status: **draft**. Stamp `abi_version = 1` once frozen.

---

## 1. Vocabulary

| Term | Meaning |
|---|---|
| **library** | A reusable unit of firmware code, hosted as a Git repository identified by its clone URL. Produces a static archive that an app (or another library) consumes. No `main()`. |
| **app**    | A firmware project. Owns `main()`. Produces a flashable binary. Depends on zero or more libraries. |
| **HAL contract** | The set of stable `NAVHAL_*` macros and `hal_*` symbols a library is allowed to consume. Versioned by `HAL_API_VERSION` in NavHAL. |
| **Target triple** | `<arch>/<vendor>/<family>/<board>` resolved by `nav` from `nav.toml`'s `[target]` block. |

Apps and libraries share most of this spec. Differences are called out in §11.

---

## 2. Manifest — `nav.toml`

Every library and app ships a single TOML manifest, `nav.toml`, at its repo root. It
is the same file `nav create` generates and `nav build` reads today; this spec
extends it rather than introducing a second manifest. The `[project].type`
field says which it is: `"library"` or `"executable"` (an **app**). These
are the only two kinds; "module" and "package" are not separate terms; they mean "library".

Status: fields marked † below are specified here but **not yet read by nav**.
Today nav reads `[project]`, `[target]`, `[build].backend`, `[dependencies]`,
`[library]` and `[abi].hal_api_version`.

### Library example

```toml
[project]
type    = "library"
name    = "bme280"
version = "1.2.0"
license = "Apache-2.0"   # †
authors = ["Jane Doe <jane@example.com>"]
description = "Bosch BME280 temperature / humidity / pressure driver."

[abi]
abi_version       = 1   # this spec's version
hal_api_version   = 1   # which NavHAL HAL_API_VERSION the library targets

[requires]
# All of these NAVHAL_HAS_* macros must be 1 at build time. Library
# configure fails with a clear error if any is 0.
caps = ["I2C", "TIMER"]

[requires.optional]
# Library compiles either way; the source uses `#if NAVHAL_HAS_I2C_DMA`
# to pick a fast path when present.
caps = ["I2C_DMA"]

[dependencies]
# Other nav libraries this library depends on, by Git URL and pinned ref (§10).
some-helper = { git = "https://github.com/acme/nav-some-helper", ref = "v1.0.3" }

[build]
sources       = ["src/**/*.c"]
include_dirs  = ["include"]
# Optional: a Kconfig fragment merged into the project's tree (see §6).
kconfig       = "Kconfig"
# Optional: extra CMake hook called from the generated build (see §5).
cmake_extras  = "cmake/extras.cmake"
```

### App example

```toml
[project]
type    = "executable"      # an app
name    = "my-firmware"
version = "0.1.0"
license = "MIT"          # †

[abi]
abi_version     = 1
hal_api_version = 1

[target]
# Apps pin a target triple. Libraries don't — they're built per the target the
# consuming app chose.
arch   = "cortex-m4"
vendor = "stm32"
board  = "nucleo_f401re"

[build]
sources      = ["src/**/*.c"]
include_dirs = ["include"]
entry        = "src/main.c"   # informational; nav still finds main() by symbol

[dependencies]
bme280 = { git = "https://github.com/jane/nav-bme280", ref = "v1.2.0" }
```

### Required fields

| Field | Library | App | Notes |
|---|---|---|---|
| `[project].type` | "library" | "executable" | required; implemented |
| `[project].name` | yes | yes | `^[a-z][a-z0-9_-]{1,63}$`; must equal the CMake target name; implemented |
| `[project].version` | yes | yes | semver `MAJOR.MINOR.PATCH`; implemented |
| `[project].license` | yes | yes | SPDX identifier † |
| `[abi].abi_version` | yes | yes | integer, this spec's version † |
| `[abi].hal_api_version` | yes | yes | integer; must equal NavHAL's `HAL_API_VERSION` at build time. Implemented: `nav build` checks it when declared (0/absent is not checked yet) |
| `[target].*` | optional | yes | target triple. A library's `[target]` is used only when it is built standalone; as a dependency it is rebuilt for the app's target. implemented |
| `[build].backend` | yes | yes | `"cmake"`; implemented |
| `[build].sources` | yes | yes | glob list † (today: `src/**` by CMake glob) |
| `[requires].caps` | optional | optional | list of `NAVHAL_HAS_*` (sans prefix) † |
| `[navhal].ref`, `[navhal].git` | no | optional | the NavHAL the app builds against: a tag, branch or commit (default `"stable"`) and an optional clone URL (default upstream). Pinned to a commit in `nav.lock` (`"navhal"`, §10); a library's own `[navhal]` is ignored when it is a dependency; implemented |
| `[library].navhal_submodule` | optional | no | library owns the CPU vectors (SysTick/PendSV/SVCall/HardFault); nav builds NavHAL with `-DSUBMODULE`; implemented |
| `[dependencies]` | optional | optional | name → `{ git, ref }` (or `{ path }` for local development) |

---

## 3. File layout

Conventional layout — nav doesn't enforce paths beyond what the manifest
points at, but libraries SHOULD follow this.

```
my-library/
├── nav.toml
├── README.md
├── LICENSE
├── include/
│   └── my_library/              ← public headers; included as <my_library/foo.h>
│       └── foo.h
├── src/
│   ├── foo.c
│   └── internal.h              ← private headers; no consumer access
├── Kconfig                     ← optional, see §6
└── cmake/
    └── extras.cmake            ← optional, see §5
```

Consumers `#include "my_library/foo.h"` — namespaced by the library slug.
Direct includes of files from `src/` are forbidden and may be rejected by
nav's validator.

---

## 4. Capability requirements

A library declares which `NAVHAL_HAS_*` macros it consumes:

* **`[requires].caps`** — hard requirements. Build fails at configure time if
  any cap is `0` for the resolved target. Error message names the missing cap
  and which manifest line declared it.
* **`[requires.optional].caps`** — soft. Library source uses
  `#if NAVHAL_HAS_<X>` to branch.

Library source **MUST NOT** redefine any `NAVHAL_*` macro. nav validates this
by inspecting the preprocessor output.

Apps can additionally pin a Kconfig fragment via `[build].kconfig` to flip
optional caps on for the build.

---

## 5. Build interface

Libraries don't ship `CMakeLists.txt`. nav generates one from the manifest at
build time. The generated rule is roughly:

```cmake
add_library(<name> STATIC ${SOURCES})
target_include_directories(<name> PUBLIC ${INCLUDE_DIRS})
target_link_libraries(<name> PUBLIC navhal)
target_compile_definitions(<name> PRIVATE
    NAV_LIBRARY_NAME="<name>"
    NAV_LIBRARY_VERSION="<version>"
)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/cmake/extras.cmake")
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/extras.cmake")
endif()
```

`cmake/extras.cmake` is the escape hatch for libraries that need something
unusual (linker scripts, asm flags, custom commands). It runs in the library's
scope; it MUST NOT modify global state (set parent-scope variables, add
top-level commands, etc.). Validated by nav.

Apps follow the same pattern but produce an `add_executable(<name>)` target
and inherit the project's linker/startup setup from the active toolchain
file (`cmake/toolchains/<arch>-toolchain.cmake`).

---

## 6. Optional Kconfig fragment

If a library ships a `Kconfig` file, nav appends it under a generated
`menu "<library-name>"` block inside the project's Kconfig tree before
running `tools/kconfig.py`. Library Kconfig symbols MUST be prefixed with
the library slug uppercased — e.g. `CONFIG_BME280_OVERSAMPLING_X16`.

Validation rules:

* No `mainmenu` in library Kconfig.
* No `source` statements pointing outside the library's own tree.
* All defined symbols start with `<LIBRARY_SLUG>_`.
* No `select` of a NavHAL `DRV_*` cap (use `[requires].caps` in the manifest
  for that; this keeps the cap requirement visible in the manifest, not
  buried in a Kconfig cascade).

The generated `navhal_target.h` exposes `NAVHAL_CONFIG_<X>` macros for every
Kconfig symbol — library sources read those, not the raw `CONFIG_*` names.

---

## 7. Lifecycle

NavHAL targets are bare-metal. There are no global constructors with side
effects (the AVR / Cortex-M startup paths don't run `__attribute__((constructor))`
or C++ static init). Lifecycle is **explicit**.

A library MAY expose:

```c
hal_status_t <library>_init(void);
hal_status_t <library>_deinit(void);
```

* `_init` MUST be idempotent (safe to call twice).
* `_init` MUST NOT touch hardware before `hal_clock_init` has returned `HAL_OK`
  — libraries that depend on a clock must say so in their docs, but
  there's no automatic ordering. Apps drive init order.
* `_init` MAY register IRQ callbacks via `hal_interrupt_attach_callback`. It
  MUST NOT install handlers by raw vector-table patching.
* `_deinit` is optional; if present, MUST release IRQs and put peripherals
  in a safe state.

A library that doesn't own state can omit both.

---

## 8. Naming conventions

* All public symbols (functions, types, macros) prefixed with `<library_slug>_`
  or `<LIBRARY_SLUG>_`. The slug is the manifest `name` with `-` → `_`.
* Headers under `include/<library_slug>/`.
* Status-returning functions return `hal_status_t`.
* No symbol shadowing of NavHAL's `hal_*` prefix.
* C++ public APIs are wrapped in `extern "C"` unless the library manifest
  declares `[abi].language = "cpp"`.

nav's validator runs `nm -gC` on the produced archive and rejects any
external symbol that doesn't start with the library's prefix.

---

## 9. Versioning

* `[project].version` is **semver**: `MAJOR.MINOR.PATCH`.
* `MAJOR` bumps on breaking changes to the library's public API.
* `[abi].abi_version` is this spec; a different value MAY make the library
  unbuildable.
* `[abi].hal_api_version` MUST equal `HAL_API_VERSION` at build time; nav
  rejects the library if NavHAL ships a different version.
* Pre-release: `MAJOR.MINOR.PATCH-rc.N` and `-alpha.N`; treated as
  lower-precedence than the released version (semver §11).

---

## 10. Dependencies

A dependency names its source directly. There is no central index and no name
lookup: the URL **is** the identity.

```toml
[dependencies]
# Remote library: any Git clone URL (GitHub is the supported host).
bme280  = { git = "https://github.com/jane/nav-bme280", ref = "v1.2.0" }
# Local checkout, for developing a library alongside an app. Not publishable.
mylib   = { path = "../mylib" }
# Per-dependency CMake options (e.g. a library that needs a feature switched on).
vaios   = { git = "https://github.com/ragnar-vallhala/vaios", ref = "v0.2.2", options = ["NAVHAL=ON"] }
```

* Exactly one of `git` or `path` per entry. `ref` is a tag, branch or commit;
  **tags are the supported release mechanism** — a release is a Git tag
  `vMAJOR.MINOR.PATCH` matching the manifest `[project].version`.
* `ref` is mandatory for published libraries. An empty `ref` (default branch) is
  allowed for local experiments and `nav` warns, because it is not reproducible.
* Branches and `path` deps are for development only; a library that declares
  one must not be depended on by a tagged release of another library.
* `nav.lock` records the **resolved commit SHA** for every `git` dependency.
  `nav build` uses the locked SHA; only `nav update` moves it. This is the
  integrity guarantee (§12) — a tag that is force-moved upstream is detected as
  a SHA mismatch and fails the build.
* **NavHAL is pinned the same way.** The app's NavHAL comes from `[navhal]`
  (not from the libraries), and `nav.lock` records `{url, ref, commit}` under
  `"navhal"`. `extern/NavHAL` is linked to that commit's own checkout in the
  shared cache (`navhal/commits/<repo>/<sha>`), so apps on different NavHAL
  versions do not share a tree. A ref that no longer resolves to the locked
  commit fails the build; `nav update` re-resolves NavHAL and the locked
  library refs and rewrites the pins. Lockfiles without a `"navhal"` entry stay
  valid: the first build adds it.
* The graph is walked transitively (git deps clone into the shared cache) and
  resolved with a topological sort. Cycles fail with a path-trace.
* **The graph is one DAG with one copy of each library.** nav builds the whole
  graph first, recording every request and the chain of projects that made it
  (`app -> radio -> crc`), then checks it. Two requests agree when they name the
  same `path`, or the same Git URL (cosmetic differences such as `.git`, a
  trailing `/` or `git@host:` vs `https://host/` ignored) with the same `ref` or
  the same resolved commit. Anything else is a **conflict**. All conflicts,
  cycles and fetch errors are reported together, each with its chains; no
  version unification is attempted.
* The app settles a conflict with `[overrides]`, which picks the source for a
  library everywhere in the graph (same entry form as `[dependencies]`). Only
  the project being built applies its overrides; a library's own
  `[overrides]` are ignored. nav prints which requests each override replaced.

  ```toml
  [overrides]
  crc = { git = "https://github.com/jane/nav-crc", ref = "v2.1.0" }
  ```
* **Submodules are bundled copies, not graph nodes.** A library may keep
  shared code (NavHAL, another library) as a Git submodule for its standalone
  build. In a nav build the project provides one copy: the library must use the
  provided CMake target (`if(NOT TARGET hal) add_subdirectory(${NAVHAL_DIR})
  endif()`) and get NavHAL headers **only by linking `hal`**
  (`target_link_libraries(mylib PRIVATE hal)`, or PUBLIC when its public headers
  include NavHAL's), never from an include path into a NavHAL tree. Non-header
  files (Kconfig, linker scripts, arch CMake fragments) are read from
  `${NAVHAL_DIR}`, a cache variable defaulting to the submodule, which nav sets
  to the project's NavHAL.
  nav reads each library's `.gitmodules` and warns when a bundled copy's commit
  differs from the copy the build uses.
* `nav lib tree` prints the graph with conflicts, overrides and bundled copies
  marked, and exits non-zero when the graph has a conflict or error.
* The manifest `version` is semver (§9) and is informational for resolution in
  v1: nav does not search for "a compatible version", it builds the ref asked
  for. Range resolution (`^1.2`) over a repository's tags via `git ls-remote
  --tags` is deferred to v2 (§14).
* Optional dependencies (compile only if available) are declared under
  `[dependencies.optional]` with the same entry form.

---
## 11. Differences between libraries and apps

| | Library | App |
|---|---|---|
| `[project].type` | "library" | "executable" |
| Pins target triple | standalone builds only | yes (in `[target]`) |
| Produces | static archive | flashable binary |
| Owns `main()` | no | yes |
| Drives init order | no | yes |
| Can ship its own Kconfig fragment | yes | yes |
| Can depend on apps | no | no (apps depend on libraries only) |
| Linker script | from toolchain file | from toolchain file (overridable in `cmake/extras.cmake`) |

A library that defines `main()` is rejected by nav's validator.

---

## 12. Distribution

* **GitHub is the host.** A library is published by pushing its source and
  tagging a release. There is nothing to upload and no account on a nav
  service; access control is the repository's own (public repos need nothing,
  private repos use the user's normal git credentials).
* **Source only.** Libraries are source trees. No precompiled binaries: an
  embedded target is the cross-product of arch × vendor × family × board ×
  toolchain × cap-set, which does not scale.
* **Release = tag.** `vMAJOR.MINOR.PATCH` on the repo, equal to
  `[project].version` in the manifest at that commit. `nav` rejects a fetched
  ref whose manifest version disagrees with a `v*` tag it was fetched by.
* **Sublibraries.** `nav` clones with `--recurse-sublibraries`, so a library that
  vendors NavHAL (as vaios does) arrives complete. Libraries SHOULD NOT depend
  on a sublibrary for anything the consuming app already provides — a library
  must reuse the app's NavHAL rather than building a second one.
* **Integrity** is the commit SHA in `nav.lock` (§10), not a tarball hash. Git's
  content addressing makes the SHA cover the whole tree.
* **Discovery** is out of band for v1: a README, a GitHub topic (`nav-library`),
  or a curated list in a repository. An index is just a file of URLs and can be
  a plain Git repo later; it is not part of the ABI.
* **CI replaces the quarantine worker.** Library authors SHOULD run `nav build`
  against the canonical targets in their own GitHub Actions. This is a
  convention, not a gate; a shared reusable workflow is a possible follow-up.

---
## 13. Validation rules nav enforces

At `nav add` time (and for every transitive dependency fetched at `nav build`):

1. Manifest parses; required fields present.
2. semver well-formed.
3. `hal_api_version` matches NavHAL at this nav install.
   (The dependency is fetched to the cache first; the manifest is read from
   the cloned ref.)
4. No `caps` reference an unknown `NAVHAL_HAS_*` name.

At `nav build` time:

1. All hard `[requires].caps` are 1 for the resolved target.
2. Generated static archive's external symbols all match the library prefix.
3. Library didn't redefine any `NAVHAL_*` macro.
4. `cmake/extras.cmake` (if present) only touches its own target.
5. No library defines `main()`; exactly one app does.

At release time (author-side, by convention — no server enforces it):

1. All of the above, via the library's own CI.
2. The `v*` tag matches `[project].version`.

---

## 14. Open questions

Things deliberately left out of v1 — flag for v2:

* **C++ libraries**: ABI-level rules around exceptions, RTTI, template
  instantiation. Today `extern "C"` is required.
* **Trait / interface protocol**: a library that says "I implement
  hal_sensor v1" so apps can swap drivers (BME280 vs SHT30). Useful but
  needs a separate interface index.
* **Runtime plugin loading**: not in scope. Embedded targets don't have
  dynamic linkers.
* **Cross-library IRQ priority arbitration**: today apps own IRQ priorities.
  A future "library declares max latency, nav assigns priorities" pass is
  out of scope.
* **License compatibility checks**: nav records SPDX ids but doesn't refuse
  GPL-into-Apache today. Could be a CI-workflow rule.
* **Semver range resolution over Git tags** (`^1.2`), and a `github:owner/repo`
  shorthand for `git` URLs.
* **Library index**: a plain Git repo of URLs, once enough libraries exist.
* **Reproducible binary builds across machines**: today reproducibility is
  best-effort; we don't pin compiler version per library.
