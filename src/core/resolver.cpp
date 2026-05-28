#include "nav/core/resolver.hpp"

#include <algorithm>

namespace nav::core {

namespace {

constexpr int kMaxIterations = 100;

// Highest version of `pkg` satisfying every requirement in `reqs`. Versions are
// stored ascending, so the last match is the highest.
std::optional<IndexVersion> pick_version(const IndexPackage& pkg,
                                          const std::vector<VersionReq>& reqs) {
    std::optional<IndexVersion> best;
    for (const auto& iv : pkg.versions) {
        bool all = true;
        for (const auto& r : reqs) {
            if (!matches(r, iv.version)) { all = false; break; }
        }
        if (all) best = iv;
    }
    return best;
}

std::map<std::string, Version> version_snapshot(const std::map<std::string, IndexVersion>& m) {
    std::map<std::string, Version> out;
    for (const auto& [n, iv] : m) out.emplace(n, iv.version);
    return out;
}

} // namespace

ResolveResult Resolver::resolve(const std::map<std::string, VersionReq>& roots) {
    ResolveResult result;

    // Memoize index lookups; fetch() may hit the network in real clients.
    std::map<std::string, std::optional<IndexPackage>> cache;
    auto fetch = [&](const std::string& name) -> const std::optional<IndexPackage>& {
        auto it = cache.find(name);
        if (it == cache.end()) {
            it = cache.emplace(name, index_.fetch(name)).first;
        }
        return it->second;
    };

    std::map<std::string, IndexVersion> resolved;

    for (int iter = 0; iter < kMaxIterations; ++iter) {
        // Rebuild constraints from scratch each pass: roots, plus the deps of
        // every currently-resolved package. Rebuilding (rather than
        // incrementally adding) discards stale constraints contributed by a
        // version we've since moved away from.
        std::map<std::string, std::vector<VersionReq>> constraints;
        for (const auto& [name, req] : roots) {
            constraints[name].push_back(req);
        }
        for (const auto& [name, iv] : resolved) {
            for (const auto& [dep_name, dep_req] : iv.dependencies) {
                constraints[dep_name].push_back(dep_req);
            }
        }

        std::map<std::string, IndexVersion> next;
        for (const auto& [name, reqs] : constraints) {
            const auto& pkg = fetch(name);
            if (!pkg) {
                result.ok = false;
                result.error = {ResolveError::Kind::PackageNotFound, name,
                                "package '" + name + "' not found in index"};
                return result;
            }
            auto chosen = pick_version(*pkg, reqs);
            if (!chosen) {
                result.ok = false;
                result.error = {ResolveError::Kind::NoMatchingVersion, name,
                                "no version of '" + name + "' satisfies all constraints"};
                return result;
            }
            next.emplace(name, std::move(*chosen));
        }

        if (version_snapshot(next) == version_snapshot(resolved)) {
            result.ok = true;
            for (auto& [name, iv] : next) {
                result.resolved.emplace(name, ResolvedPackage{name, std::move(iv)});
            }
            return result;
        }
        resolved = std::move(next);
    }

    result.ok = false;
    result.error = {ResolveError::Kind::DidNotConverge, "",
                    "dependency resolution did not converge within "
                    + std::to_string(kMaxIterations) + " iterations"};
    return result;
}

Lockfile to_lockfile(const ResolveResult& result, const std::string& source) {
    Lockfile lock;
    if (!result.ok) return lock;

    for (const auto& [name, rp] : result.resolved) {
        LockedPackage lp;
        lp.name    = name;
        lp.version = rp.version.version;
        lp.kind    = rp.version.kind;
        lp.source  = source;

        for (const auto& [platform, dl] : rp.version.downloads) {
            lp.checksums.emplace(platform, dl.sha256);
        }

        for (const auto& [dep_name, dep_req] : rp.version.dependencies) {
            // Reference the concrete resolved version of each dependency.
            auto it = result.resolved.find(dep_name);
            if (it != result.resolved.end()) {
                lp.dependencies.push_back(dep_name + "@" + to_string(it->second.version.version));
            } else {
                lp.dependencies.push_back(dep_name + "@" + to_string(dep_req.base));
            }
        }
        std::sort(lp.dependencies.begin(), lp.dependencies.end());

        lock.packages.push_back(std::move(lp));
    }

    std::sort(lock.packages.begin(), lock.packages.end(),
              [](const LockedPackage& a, const LockedPackage& b) { return a.name < b.name; });
    return lock;
}

} // namespace nav::core
