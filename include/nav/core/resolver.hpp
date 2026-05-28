#pragma once

#include <map>
#include <string>

#include "nav/core/index.hpp"
#include "nav/core/lockfile.hpp"
#include "nav/core/semver.hpp"

namespace nav::core {

// One package pinned to a concrete version by the resolver, carrying the full
// index metadata for that version (downloads, deps, kind).
struct ResolvedPackage {
    std::string  name;
    IndexVersion version;
};

struct ResolveError {
    enum class Kind {
        PackageNotFound,    // a required package has no index entry
        NoMatchingVersion,  // no version satisfies the accumulated constraints
        DidNotConverge,     // resolution oscillated past the iteration bound
    };
    Kind        kind = Kind::PackageNotFound;
    std::string package;
    std::string message;
};

struct ResolveResult {
    bool ok = false;
    std::map<std::string, ResolvedPackage> resolved;  // keyed by package name
    ResolveError error;
};

// Resolves the full dependency closure of a set of root requirements against
// an index. Strategy: fixed-point constraint accumulation — each iteration
// rebuilds the per-package constraint set from the roots plus the dependencies
// of the currently-resolved versions, then picks the highest version
// satisfying ALL constraints on each package. This avoids the false conflicts
// a naive greedy walk would hit on diamond dependencies, without a full SAT
// solver. Genuine conflicts (no version satisfies all constraints) are
// reported rather than backtracked.
class Resolver {
public:
    explicit Resolver(IIndexClient& index) : index_(index) {}

    ResolveResult resolve(const std::map<std::string, VersionReq>& roots);

private:
    IIndexClient& index_;
};

// Build a Lockfile from a successful resolution. `source` is recorded verbatim
// on every entry (e.g. "registry+https://github.com/.../nav-index").
Lockfile to_lockfile(const ResolveResult& result, const std::string& source);

} // namespace nav::core
