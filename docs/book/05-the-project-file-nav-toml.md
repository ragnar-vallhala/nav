# Chapter 5: The project file, nav.toml

Every nav project is a directory with a `nav.toml` at its root. nav finds the project by walking up from the current directory until it sees one (`find_project_root`). `load_project_config` parses the file with toml++. A TOML syntax error is reported with the file path, line and column, and the command stops. Missing fields mostly default to an empty string.

## The identity tables

`[project]` holds `name`, `version` and `type`. `type` is `"executable"` or `"library"` and defaults to `"executable"` when absent. The code does not reject other values; only the exact string `"library"` makes nav treat the project as a library. `version` is a string, empty by default. `config.hpp` notes that a library fetched by a `v*` tag must match it.

`[target]` holds `arch`, `vendor` and `board`. Of the three, `nav build` checks only `board`. If it is empty, the build stops with "'nav.toml' has no [target].board entry. Set one (see 'nav board list') and re-run." If the id is not in the board catalog, the build stops with "Board id '<id>' not in catalog. Run 'nav board list' to see available ids."

`[build] backend = "cmake"` appears in both templates. The loader reads it into `build_backend`, but a search of `src/` and `include/` found no other use of that field. Treat it as a placeholder that the generated file carries.

`[navhal]` chooses the NavHAL the project builds against:

- `ref`: a tag, branch or commit. The default is `"stable"` (`kDefaultNavhalRef`).
- `git`: an optional fork URL. The default is the upstream NavHAL repository (`kNavhalRepoUrl`).
- `exclude`: NavHAL source files to drop, relative to the NavHAL root. An empty entry is rejected when the file loads. At build time an absolute path, a path containing `..`, or a file that does not exist in the pinned NavHAL is an error.

Setting `[navhal].exclude` also marks the project as using `[build]`-aware settings, so it needs a `CMakeLists.txt` from the current template (`nav regen`).

## The [build] keys

An app is `nav.toml`, `.config` and sources. If no `[build]` keys are present, the app is `src/main.c` plus `include/`, and a `CMakeLists.txt` from an older nav still works. Using any key marks the table as declared, and then nav needs a current-template `CMakeLists.txt` (`nav regen`).

| Key | Default | Meaning and validation (from `config.cpp` and `appbuild.cpp`) |
|---|---|---|
| `sources` | `["src/main.c"]` | Globs from the project root. A pattern that matches nothing is an error. |
| `exclude` | empty | Globs removed from the sources. If everything is removed, the error is "[build].exclude removes every source file". |
| `include_dirs` | `["include"]` | Include directories, relative to the root. |
| `defines` | empty | Preprocessor defines. |
| `c_flags` | empty | Flags for the app's own compile lines. |
| `c_standard` | unset | Integer, one of 90, 99, 11, 17, 23. Anything else fails the load. |
| `file_flags` | none | Array of tables, each with a non-empty `match` and `flags`. A rule that matches none of the build's sources is an error. |
| `linker_fragment` | unset | Path to an extra linker script, a second `-T`. Must be a non-empty string, and the file must exist at build time. |
| `linker_fragment_position` | `"before"` | `"before"` or `"after"` the board's `-T`. |
| `libc` | `"none"` | `"none"` (freestanding, `-nostdlib`) or `"newlib"`. |
| `syscalls` | `"nosys"` | `"nosys"` or `"none"`. The README ties it to `libc = "newlib"`. |
| `libs` | empty | Extra `-l` libraries. |
| `deps_opt` | unset | One of `O0 O1 O2 O3 Os Og Oz`, applied to NavHAL and every dependency library. |

Every list key must be an array of strings. A wrong type fails the load with a message such as `[build].sources must be an array of strings`, and a non-string element gives `must contain only strings`. A bad enumerated value fails with a message naming the allowed values, for example `[build].libc must be "none" or "newlib"`.

Two behaviours are worth knowing. First, unknown keys directly under `[build]` are not rejected by the loader, whereas unknown keys in the conditional and variant tables are. Second, `nav build` expands the globs each time, sorted, into `build/nav-app.cmake`. A glob that begins with a wildcard walks the project root but skips `build/` and `extern/` at the top level and any dot-directory. A literal prefix such as `extern/foo/*.c` can enter them (`expand_one`).

README and code agree on every key above.

## Board- and Kconfig-conditional settings

`[build.board.<board>]` applies when `[target].board` equals `<board>`. `[build.kconfig.<SYMBOL>]` applies when `CONFIG_<SYMBOL>` is present in the resolved config with any value other than `n` (`kconfig_symbol_set`; the `CONFIG_` prefix is added if you omit it). These tables accept only `sources`, `exclude`, `include_dirs`, `defines`, `c_flags`, `libs` and `file_flags`. Any other key stops the load with "unknown key ... (known: sources, exclude, include_dirs, defines, c_flags, file_flags, libs)". So `libc` or `linker_fragment` cannot be conditional.

Layers apply in this order: `[build]`, the matching board table, the matching Kconfig tables, then the selected variant. A layer's `exclude` removes sources from its own layer and every earlier one, never a later one. A base `exclude` can therefore drop a directory that a board table adds back.

## Build variants

`[variant.<name>]` describes a second image of the same project. A variant accepts every additive `[build]` key, plus these:

- `sources_add` and `sources_remove`: the older names for `sources` and `exclude`.
- `output`: the firmware file name. The default is the project name.
- `kconfig`: a path to an alternative `.config`. `nav build` fails with "[variant.<name>].kconfig '<file>' does not exist." if the file is missing.
- `[variant.<name>.board.<board>]` and `[variant.<name>.kconfig_when.<SYMBOL>]`: conditional sub-tables. `[variant.<name>.kconfig.<SYMBOL>]` is accepted as well, but a `kconfig` string and a `kconfig` table cannot share a name.
- `[variant.<name>.dependencies.<lib>]` with `options = [...]`: `KEY=VALUE` entries merged over the library's own options for that variant. An entry with the same `KEY` replaces the original, and others are added. Naming a dependency the project lacks is an error ("no such dependency").

A name must match letters, digits, `_` and `-`, starting with a letter or digit, and may not be `deps`, `extern`, `CMakeFiles` or `Testing`, because it becomes `build/<name>/`. `--variant` on a library project is refused. An unknown name gives `unknown variant '<name>' (known: ...)`.

## Dependencies, upload and the rest

`[dependencies]` entries are inline tables: `{ path = "..." }` or `{ git = "...", ref = "..." }`, optionally with `options = [...]`. A bare string is read as a path. An entry with neither a path nor a git URL is silently skipped (`read_deps`). `[overrides]` uses the same form and names the source to use for a library anywhere in the graph.

`[upload]` accepts `probe` (a string) and `connect_under_reset` (a boolean). Other keys are rejected with the known list. Command-line and environment values win over these.

The loader also reads `[library] navhal_submodule` (default false) and `[abi] hal_api_version` (default 0, meaning undeclared). The README sections cited here do not describe them.

## nav.toml and nav.lock

`nav.toml` states intent; `nav.lock` records what was resolved. It is JSON, not TOML (`load_lockfile`). It has a `navhal` object with `url`, `ref` and `commit`, and a `git` array for git dependencies with `name`, `url`, `ref` and `commit`. The first build writes the NavHAL commit. If `[navhal]` changes so that url or ref no longer matches the lock, nav re-pins and prints a "re-pinned" notice. If a git dependency's ref now resolves to a different commit than the lock says, the build stops with "A pinned git dependency no longer matches nav.lock:". A `nav.lock` that cannot be parsed stops the build and asks you to fix or remove it.

## A complete example

`templates/nav.toml.in` is the file `nav create` starts from (placeholders in double braces are substituted):

```toml
# Nav Configuration File
[project]
name = "{{PROJECT_NAME}}"
version = "0.1.0"
# "executable" builds firmware; "library" builds a reusable static archive.
type = "executable"

[target]
arch = "{{ARCH}}"
vendor = "{{VENDOR}}"
board = "{{BOARD}}"

[build]
backend = "cmake"
```

`nav create` then appends a `[navhal]` table with a `ref` (`create.cpp`).

## Sources

- `README.md`: "App sources and flags", "Board- and Kconfig-conditional settings", "Build variants", "Pinning NavHAL".
- `templates/nav.toml.in`, `templates/nav_lib.toml.in`.
- `src/core/config.cpp`: `load_project_config`, `parse_build_settings`, `parse_additions`, `parse_conditionals`, `parse_variants`, `parse_upload_settings`, `parse_navhal_exclude`, `read_deps`.
- `include/nav/core/config.hpp`: `BuildSettings`, `ProjectConfig`, `Variant`.
- `src/core/appbuild.cpp`: `plan_app_build`, `plan_navhal_exclude`, `expand_one`, `kconfig_symbol_set`.
- `include/nav/core/appbuild.hpp`: `AppBuildPlan` defaults.
- `src/core/variant.cpp`: `apply_variant`, `merge_options`, `apply_variant_options`.
- `src/core/commands/build.cpp`: board and variant checks, `nav.lock` handling.
- `src/core/navhal.cpp` and `include/nav/core/navhal.hpp`: `navhal_spec`, `kDefaultNavhalRef`, `kNavhalRepoUrl`, lock pinning.
- `src/core/lockfile.cpp`: `load_lockfile`.
- `src/core/commands/create.cpp`: the `[navhal]` text appended by `nav create`.
- `tests/test_appbuild.cpp`: `RejectsBadTypes`, `DefaultsWhenAbsent`.
