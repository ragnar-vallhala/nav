#pragma once

#include "nav/core/config.hpp"
#include "nav/core/execution_context.hpp"
#include "nav/core/lockfile.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nav::core {

// A git submodule a library carries in its own repository (.gitmodules + the
// gitlink commit). In a nav build the project provides shared libraries, so a
// bundled copy of one is unused — unless the library's CMake reads its headers
// by path, which mixes two versions. check_bundled_copies() reports that.
struct BundledCopy {
    std::string path;    // submodule path inside the library, e.g. extern/NavHAL
    std::string url;     // as written in .gitmodules
    std::string commit;  // gitlink commit (empty if it could not be read)
};

// One library in a project's resolved dependency graph.
struct ResolvedLib {
    // Canonical name = the library's own [project].name, used as the CMake
    // target name (so add_subdirectory + target_link_libraries line up).
    std::string name;
    // Absolute source directory of the library project.
    std::filesystem::path src;
    // Canonical names of this library's own direct dependencies (graph edges).
    std::vector<std::string> deps;
    // CMake "KEY=VALUE" options to set before this library's add_subdirectory().
    std::vector<std::string> options;
    // True when this library owns the NavHAL-ceded CPU vectors (see
    // ProjectConfig::navhal_submodule).
    bool navhal_submodule = false;
    // Source of a git dependency (empty for a path dependency): the clone URL,
    // the ref nav.toml asked for, and the commit that ref resolved to.
    std::string git_url;
    std::string git_ref;
    std::string commit;
    // [abi].hal_api_version from the library's nav.toml (0 = undeclared).
    int hal_api_version = 0;
    // Human-readable source ("<url> @ <ref>" or "path <dir>").
    std::string source;
    // [project].version from the library's nav.toml.
    std::string version;
    // Submodules in the library's own repository.
    std::vector<BundledCopy> bundled;
    // Set when the root project's [overrides] chose this library's source.
    bool overridden = false;
    // The library's CMakeLists.txt reads NAVHAL_DIR, which nav sets to the
    // project's NavHAL — so a NavHAL submodule it bundles is not used.
    bool uses_navhal_dir = false;
};

// One place in the graph that asks for a library: the source it names and the
// chain of projects that led there (root first, the requiring project last).
struct DepRequest {
    std::string source;
    std::vector<std::string> chain;
};

// A library that different parts of the graph ask for from different sources.
// `requests` holds one entry per distinct source (first requester of each).
struct LibraryConflict {
    std::string name;
    std::vector<DepRequest> requests;
};

// The whole dependency graph of a project, with every problem found in it.
// Resolution does not stop at the first problem: all conflicts and errors are
// collected so they can be fixed in one pass.
struct DepGraph {
    // Chosen libraries, dependencies-first (a library precedes anything that
    // links it). For a conflicting library the first source seen is explored.
    std::vector<ResolvedLib> libs;
    // The root project's direct dependencies (canonical names).
    std::vector<std::string> direct;
    std::vector<LibraryConflict> conflicts;
    // Hard errors: missing path, failed clone, unparsable manifest, not a
    // library, cycle (with its chain), tag/version mismatch.
    std::vector<std::string> errors;
    // Informational: which requests an [overrides] entry replaced.
    std::vector<std::string> notes;
    bool ok() const { return conflicts.empty() && errors.empty(); }
    const ResolvedLib* find(const std::string& name) const;
};

// Build the dependency graph of the project at `root` (config `cfg`). Path
// deps resolve relative to their declaring project's root; git deps are cloned
// into the shared cache. Two requests name the same source when they use the
// same path, or the same git URL (cosmetic differences ignored) with the same
// ref or the same resolved commit. The root's [overrides] replace a library's
// source everywhere it is requested.
DepGraph resolve_dep_graph(IExecutionContext& ctx, const std::filesystem::path& root,
                           const ProjectConfig& cfg);

// Print a graph's errors and conflicts (with the chains that cause them and an
// [overrides] hint) and its notes.
void report_dep_graph(const DepGraph& g);

// resolve_dep_graph() + report_dep_graph(). Returns the libraries, or nullopt
// when the graph has any error or conflict. `out_direct`, if non-null,
// receives the root's direct dependency names (canonical).
std::optional<std::vector<ResolvedLib>>
resolve_library_deps(IExecutionContext& ctx, const std::filesystem::path& root,
                     const ProjectConfig& cfg, std::vector<std::string>* out_direct = nullptr);

// Text tree of the graph rooted at `root_name`, e.g.
//   app
//   ├── vaios v0.2.0  (https://github.com/x/vaios @ v0.2.0)
//   │   └── bundles NavHAL at 3864e50 (extern/NavHAL)
//   └── crc  !! conflict: 2 sources
// A library reached again is shown once and then as "name (*)".
std::string render_dep_tree(const DepGraph& g, const std::string& root_name);

// Compare each library's bundled submodules with what the build actually
// uses: another library in the graph with the same URL, or the project's
// NavHAL (`navhal_url` at `navhal_commit`). One warning per bundled copy whose
// commit differs. A bundled NavHAL is skipped for a library that reads
// NAVHAL_DIR, since nav points that at the project's NavHAL.
std::vector<std::string> check_bundled_copies(const std::vector<ResolvedLib>& libs,
                                              const std::string& navhal_url,
                                              const std::string& navhal_commit);

// A git URL with cosmetic differences removed: trailing "/" and ".git",
// scp-style "git@host:a/b" -> "https://host/a/b", host lowercased.
std::string normalize_git_url(std::string url);

// Parse a .gitmodules file's text into (path, url) entries.
std::vector<std::pair<std::string, std::string>> parse_gitmodules(const std::string& text);

// Outcome of reconciling nav.lock's git pins with a freshly resolved graph.
struct GitLockResult {
    bool changed = false;               // lock was modified; caller should save it
    std::vector<std::string> notes;     // pins added / re-locked / dropped
    std::vector<std::string> errors;    // a pinned ref now resolves elsewhere
};

// Reconcile `lock.git` with the git libraries in `libs`:
//   * no pin yet           -> pin the resolved commit
//   * nav.toml changed the URL or ref -> re-pin (the user asked for it)
//   * same URL and ref, different commit -> error: the ref moved upstream
//     (e.g. a force-moved tag) or the cache holds another commit
//   * pin for a library no longer depended on via git -> dropped
// A library whose commit could not be read is left unpinned (noted).
GitLockResult reconcile_git_lock(Lockfile& lock, const std::vector<ResolvedLib>& libs);

// Render build/nav-deps-libs.cmake: set NAVHAL_DIR to the project's NavHAL (so
// libraries build against it, not a bundled copy), add_subdirectory() each resolved library,
// wire inter-library link edges, and set NAV_DEP_TARGETS to the consumer's
// direct dependencies for the top-level CMake to link.
std::string render_libdeps_cmake(const std::vector<ResolvedLib>& libs,
                                 const std::vector<std::string>& direct);

// Write render_libdeps_cmake() to `out_path`, creating parent dirs. True on success.
bool write_libdeps_cmake(const std::vector<ResolvedLib>& libs,
                         const std::vector<std::string>& direct,
                         const std::filesystem::path& out_path);

} // namespace nav::core
