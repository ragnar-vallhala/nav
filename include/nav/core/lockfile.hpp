#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nav/core/index.hpp"
#include "nav/core/semver.hpp"

namespace nav::core {

// A single pinned package in nav.lock. The lockfile fully determines what gets
// fetched, so a clean checkout + `nav build` reproduces the same artifact set.
struct LockedPackage {
    std::string name;
    Version     version;
    PackageKind kind = PackageKind::Library;
    std::string source;   // e.g. "registry+https://github.com/.../nav-index"

    // Per-download SHA256, keyed exactly as the index entry's downloads map
    // ("source" for libraries; "<os>_<arch>" for toolchains).
    std::map<std::string, std::string> checksums;

    // Resolved dependency references as "name@version" strings, pointing at
    // other entries in the same lockfile.
    std::vector<std::string> dependencies;
};

// A git library dependency pinned to the commit its ref resolved to. This is
// nav's integrity guarantee for GitHub-hosted libraries (docs/LIBRARY_ABI.md
// §10): `nav build` refuses a ref that now resolves to a different commit.
struct LockedGit {
    std::string name;    // canonical library name ([project].name)
    std::string url;     // clone URL as written in nav.toml
    std::string ref;     // tag / branch / commit as written in nav.toml
    std::string commit;  // full object name the ref resolved to

    bool operator==(const LockedGit&) const = default;
};

struct Lockfile {
    std::vector<LockedPackage> packages;
    std::vector<LockedGit> git;

    // Lookup by name; nullopt if absent.
    std::optional<LockedPackage> find(const std::string& name) const;
    // Git pin lookup by canonical name; nullptr if absent.
    const LockedGit* find_git(const std::string& name) const;
};

// Parse nav.lock (JSON). Returns nullopt on read/parse failure.
std::optional<Lockfile> load_lockfile(const std::filesystem::path& path);

// Serialize to nav.lock (JSON, packages sorted by name for stable diffs).
// Returns true on success.
bool save_lockfile(const Lockfile& lock, const std::filesystem::path& path);

// Render to a JSON string (used by save_lockfile and tests).
std::string render_lockfile(const Lockfile& lock);

} // namespace nav::core
