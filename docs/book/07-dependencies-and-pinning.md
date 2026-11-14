# Chapter 7: Dependencies and pinning

Chapter 5 showed the `[navhal]`, `[dependencies]` and `[overrides]` tables and the shape of `nav.lock`. This chapter follows what nav does with them. nav has no package registry; everything is fetched with `git` (the README's "Dependencies" section says the registry was removed).

## From a ref to a checkout

`[navhal]` is turned into a `NavhalSpec` by `navhal_spec`: the `git` URL (default `kNavhalRepoUrl`) and the `ref` (default `stable`). `sync_project_navhal` then works as follows.

1. If `extern/NavHAL` exists and is a real directory, nav returns at once. A vendored or submodule copy is never pinned, and no `nav.lock` is written for it.
2. It loads `nav.lock`. If the file exists but cannot be parsed, the build stops with "nav.lock exists but could not be parsed; fix or remove it and retry."
3. The lock's pin is used only if its `url` and `ref` equal the spec. If the pin is usable and its checkout is in the cache, nothing touches the network.
4. Otherwise `fetch_navhal` shallow-clones the ref into a temporary directory, reads `HEAD` with `git rev-parse`, and renames the directory to `<nav home>/navhal/commits/<repo-slug>/<commit>` (`navhal_commit_dir`). A `.nav-ok` marker file says the entry is complete. If another run already created that directory, the new copy is discarded.
5. `extern/NavHAL` is made to point at that directory (`link_navhal`: symlink, then a Windows junction, then a full copy).

The checkout is immutable because its path contains the commit, so two projects pinned to different commits sit side by side. The test `TwoAppsOnDifferentCommitsGetTheirOwnCheckouts` covers this.

There is one more path. An unpinned project on the upstream URL, with a non-commit ref, first adopts the older per-ref clone at `<nav home>/navhal/<ref>` if it exists, instead of fetching (`adopt_legacy_clone`). Its commit becomes the first pin, with the message "Adopting the cached NavHAL ...". `nav create` does not take this path.

## The ref rule

A `ref` is not limited to a branch or tag name: no such rule is enforced anywhere in `src/`, and the repo says so in several places. The README comment reads "a tag, branch or commit", as do `config.hpp` and the comment `nav create` writes into new projects.

What the code does with a commit-like ref depends on the table:

- For `[navhal]`, `fetch_ref_into` tests `looks_like_commit` (7 to 64 hex characters). A 40- or 64-character value is fetched directly with `git fetch --depth 1`; if that fails, and for any other length, nav falls back to a full clone and `checkout --detach`. Anything that does not look like a commit is cloned with `--branch`.
- For library dependencies, `ensure_git_cached` always runs `git clone --branch <ref>`. There is no commit path. The source does not say what git does with a SHA there; the code simply has no handling for one.

The only check on a ref's content is `is_unsafe_git_arg`: a URL or ref that starts with `-` is refused, with "Refusing git ref '...': it starts with '-' and would be read as a git option." The practical advice, pin a tag, comes from the code's own warning for a library with no ref: "tracking the default branch is not reproducible. Pin a release tag: ref = \"vX.Y.Z\"."

## nav.lock: creating, re-pinning, mismatch

The first resolution writes `navhal` into `nav.lock` and prints "nav.lock: pinned NavHAL ...". Editing `[navhal]` so that `git` or `ref` differs from the lock re-pins to whatever the new spec resolves to, with "nav.lock: re-pinned NavHAL (...)". `pin_navhal` reports no change, and nothing is rewritten, when the pin already matches.

A mismatch is possible only when the locked commit's checkout is missing from the cache, for example on a clean machine. nav then fetches the ref, and if it resolves to a different commit it fails with:

```
A pinned git dependency no longer matches nav.lock:
  navhal: <url> @ <ref> resolved to <commit>, but nav.lock pins <commit>
```

followed by advice to run `nav update` or pin a ref that still points at the locked commit. The pin in the file is left alone (`MovedBranchIsRefusedWhenTheLockedCheckoutIsNotCached`). If the cache still holds the locked commit, a moved branch goes unnoticed until `nav update`.

Git libraries have their own entries in the `git` array. `reconcile_git_lock`, called from `nav build`, adds a pin for a new library, re-pins one whose `url` or `ref` changed, drops pins for libraries that are gone, and reports a same-ref, different-commit pin as an error under the same headline. Unlike NavHAL, library checkouts are cached per URL and ref (`git_cache_dir`: `deps/<slug>/<ref>`), not per commit. The README does not cover this difference.

## Library dependencies and options

A dependency is `{ path = "..." }` or `{ git = "...", ref = "..." }`. A relative path is resolved against the project that declares it, and the directory must hold a `nav.toml`. Git dependencies are cloned with submodules into the cache. The dependency's `[project].type` must be `"library"`, or the graph records "is not a library project". If `ref` looks like `v<digit>...`, the rest of the tag must equal the library's `[project].version`, or the graph records "A release tag must match it." A branch ref skips that check (`BranchRefSkipsVersionCheck`).

`options = ["KEY=VALUE"]` become `set(KEY "VALUE" CACHE ... FORCE)` lines in `nav-deps-libs.cmake` in the build directory, written before that library's `add_subdirectory`. An entry with no `=` means `ON`. A value that is `ON`, `OFF`, `TRUE`, `FALSE`, `YES` or `NO` is typed as a CMake BOOL, anything else as a STRING (`render_libdeps_cmake`).

`nav lib add` edits `nav.toml` for you. Its flags in `LibCommand::run` are `--ref`, `--name`, `--git`, `--path` and `--option` (or `-D`). It also fetches a git source into the cache, so that it can merge the library's NavHAL requirements into `.config`.

## The graph, conflicts and `[overrides]`

`resolve_dep_graph` walks the graph depth first. Each library is explored once, from the first source seen, and its identity is its own `[project].name`, which can differ from the key in `nav.toml`. Later requests for the same library are only compared with the first. Two requests agree if they are the same path, or the same normalised git URL with the same ref or the same resolved commit (`DifferentRefsAtTheSameCommitAgree`). The walk does not stop at the first problem. It collects all errors (missing path, failed clone, unparsable manifest, cycle, tag mismatch) and all conflicts, so you can fix them in one pass.

A conflict is reported as:

```
Library '<name>' is required from <n> different sources:
  <source>  (required by <chain>)
```

then the hint "Make the requirers agree, or choose one source for the whole project in nav.toml, e.g.:" with a suggested `[overrides]` block. A `[overrides]` entry replaces the source of that library wherever it is requested, and an info line begins "[overrides] <name> replaces the source requested by ...". If the override sets no `options`, the requester's options are kept. Only the root project's overrides apply.

`nav lib tree` prints the graph and exits 1 if it has errors or conflicts. In it, a library seen before is shown as `name (*)`, a conflicting one carries "!! conflict: N sources", and an overridden one "[override]". A submodule inside a library appears as "bundles ...". `nav lib tree` fetches git dependencies if they are not cached, so it can use the network.

## What `nav update` does

`nav update` ignores its arguments. In a project it does three things in order.

1. It re-resolves NavHAL from upstream (`sync_project_navhal` with `refresh = true`, which calls `refresh_navhal` and skips the lock) and rewrites the pin. The old checkout stays in the cache.
2. For every git library already listed in `nav.lock`, it re-clones `<url>@<ref>` (`refresh_git_cache`) and rewrites the pin if the commit moved. If a refresh fails it warns and keeps the pin. A git dependency with no entry in the lock yet is not touched.
3. Whether or not there is a project, it probes the system tools, and the board's tools if the board is in the catalog. It installs what is missing and makes sure a Python with kconfiglib exists.

So `nav update` is both a re-pin and a toolchain command. One quirk: with a vendored `extern/NavHAL`, `sync_project_navhal` returns the directory without locking anything, and `nav update` still prints "NavHAL <ref> resolved and locked."

## README against code

The "Pinning NavHAL" section matches the code on the first-build pin, per-commit checkouts under `~/.nav/navhal/commits/` (the actual base is `$NAV_HOME`, else `~/.nav`), the moved-branch stop and the vendored directory. Its wording "a tag, branch or commit" is accurate for NavHAL and misleading for libraries. It does not mention that library caches are per ref. Its claim that `nav create` records the release tag `stable` points at is backed by `tag_at_commit` in `create.cpp`.

## Sources

- `README.md`: "Pinning NavHAL", "Dependencies", the `nav lib` and `nav update` rows.
- `src/core/navhal.cpp`: `navhal_spec`, `resolve_navhal`, `fetch_navhal`, `fetch_ref_into`, `looks_like_commit`, `adopt_legacy_clone`, `pin_navhal`, `sync_project_navhal`, `refresh_navhal`, `refresh_git_cache`, `is_unsafe_git_arg`, `link_navhal`, `tag_at_commit`.
- `src/core/cache.cpp`: `navhal_commit_dir`, `git_cache_dir`, `nav_home_base`, `is_installed`.
- `src/core/lockfile.cpp`: `load_lockfile`, `render_lockfile`.
- `src/core/libdeps.cpp`: `resolve_dep_graph`, `Walker::visit`, `report_dep_graph`, `render_dep_tree`, `reconcile_git_lock`, `render_libdeps_cmake`.
- `src/core/commands/lib.cpp`: `LibCommand::run`, `lib_tree`, `lib_add`.
- `src/core/commands/update.cpp`: `UpdateCommand::run`, `refresh_locked_libraries`.
- `src/core/commands/build.cpp`: library resolution and `reconcile_git_lock` use.
- `src/core/commands/create.cpp`: initial pin and `[navhal]` comment.
- `src/core/config.cpp`: `read_deps`.
- `include/nav/core/navhal.hpp`, `libdeps.hpp`, `config.hpp`.
- `tests/test_navhal_pin.cpp`, `tests/test_libdeps.cpp`, `tests/test_depgraph.cpp`.
