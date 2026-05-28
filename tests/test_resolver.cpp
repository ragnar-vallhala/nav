#include "nav/core/resolver.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>

using namespace nav::core;

namespace {

// In-memory index for tests. Build packages with add().
class FakeIndex : public IIndexClient {
public:
    // Add a version of a package. `deps` are "name" -> "version-req" strings.
    void add(const std::string& name, const std::string& version,
             std::map<std::string, std::string> deps = {},
             PackageKind kind = PackageKind::Library) {
        auto v = parse_version(version);
        if (!v) throw std::runtime_error("bad test version: " + version);

        IndexVersion iv;
        iv.version = *v;
        iv.kind = kind;
        iv.downloads.emplace("source", Download{"https://example/" + name + "-" + version + ".tar.gz",
                                                "sha-" + name + "-" + version});
        for (auto& [dn, dr] : deps) {
            auto req = parse_version_req(dr);
            if (!req) throw std::runtime_error("bad test req: " + dr);
            iv.dependencies.emplace(dn, *req);
        }
        packages_[name].name = name;
        packages_[name].versions.push_back(std::move(iv));
        std::sort(packages_[name].versions.begin(), packages_[name].versions.end(),
                  [](const IndexVersion& a, const IndexVersion& b) { return a.version < b.version; });
    }

    std::optional<IndexPackage> fetch(const std::string& name) override {
        auto it = packages_.find(name);
        if (it == packages_.end()) return std::nullopt;
        return it->second;
    }

private:
    std::map<std::string, IndexPackage> packages_;
};

std::map<std::string, VersionReq> roots(std::map<std::string, std::string> in) {
    std::map<std::string, VersionReq> out;
    for (auto& [n, r] : in) out.emplace(n, *parse_version_req(r));
    return out;
}

std::string resolved_version(const ResolveResult& r, const std::string& name) {
    auto it = r.resolved.find(name);
    if (it == r.resolved.end()) return "<absent>";
    return to_string(it->second.version.version);
}

} // namespace

TEST(Resolver, SingleRootHighestMatching) {
    FakeIndex idx;
    idx.add("a", "1.0.0");
    idx.add("a", "1.2.0");
    idx.add("a", "2.0.0");

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}}));
    ASSERT_TRUE(res.ok) << res.error.message;
    EXPECT_EQ(resolved_version(res, "a"), "1.2.0");
}

TEST(Resolver, LinearChain) {
    FakeIndex idx;
    idx.add("a", "1.0.0", {{"b", "^1.0.0"}});
    idx.add("b", "1.3.0", {{"c", "^1.0.0"}});
    idx.add("c", "1.1.0");

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}}));
    ASSERT_TRUE(res.ok) << res.error.message;
    EXPECT_EQ(resolved_version(res, "a"), "1.0.0");
    EXPECT_EQ(resolved_version(res, "b"), "1.3.0");
    EXPECT_EQ(resolved_version(res, "c"), "1.1.0");
}

TEST(Resolver, DiamondCompatible) {
    // a -> b, a -> c, both b and c -> d. d has two versions; should pick highest
    // satisfying both ^1.0.0 constraints.
    FakeIndex idx;
    idx.add("a", "1.0.0", {{"b", "^1.0.0"}, {"c", "^1.0.0"}});
    idx.add("b", "1.0.0", {{"d", "^1.0.0"}});
    idx.add("c", "1.0.0", {{"d", "^1.0.0"}});
    idx.add("d", "1.0.0");
    idx.add("d", "1.5.0");

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}}));
    ASSERT_TRUE(res.ok) << res.error.message;
    EXPECT_EQ(resolved_version(res, "d"), "1.5.0");
}

TEST(Resolver, ConstraintNarrowingAvoidsFalseConflict) {
    // a -> c ^1.0.0 (would prefer 1.5.0 alone), b -> c =1.2.0.
    // Constraint accumulation must pick 1.2.0, which satisfies BOTH.
    FakeIndex idx;
    idx.add("a", "1.0.0", {{"c", "^1.0.0"}});
    idx.add("b", "1.0.0", {{"c", "=1.2.0"}});
    idx.add("c", "1.0.0");
    idx.add("c", "1.2.0");
    idx.add("c", "1.5.0");

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}, {"b", "^1.0.0"}}));
    ASSERT_TRUE(res.ok) << res.error.message;
    EXPECT_EQ(resolved_version(res, "c"), "1.2.0");
}

TEST(Resolver, GenuineConflictReported) {
    FakeIndex idx;
    idx.add("a", "1.0.0", {{"c", "=1.0.0"}});
    idx.add("b", "1.0.0", {{"c", "=1.2.0"}});
    idx.add("c", "1.0.0");
    idx.add("c", "1.2.0");

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}, {"b", "^1.0.0"}}));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.error.kind, ResolveError::Kind::NoMatchingVersion);
    EXPECT_EQ(res.error.package, "c");
}

TEST(Resolver, MissingPackageReported) {
    FakeIndex idx;
    idx.add("a", "1.0.0", {{"ghost", "^1.0.0"}});

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}}));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.error.kind, ResolveError::Kind::PackageNotFound);
    EXPECT_EQ(res.error.package, "ghost");
}

TEST(Resolver, MissingRootReported) {
    FakeIndex idx;
    Resolver r(idx);
    auto res = r.resolve(roots({{"nope", "^1.0.0"}}));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.error.kind, ResolveError::Kind::PackageNotFound);
    EXPECT_EQ(res.error.package, "nope");
}

TEST(Resolver, CycleTerminates) {
    // a <-> b mutual dependency. Must terminate, not loop forever.
    FakeIndex idx;
    idx.add("a", "1.0.0", {{"b", "^1.0.0"}});
    idx.add("b", "1.0.0", {{"a", "^1.0.0"}});

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}}));
    ASSERT_TRUE(res.ok) << res.error.message;
    EXPECT_EQ(resolved_version(res, "a"), "1.0.0");
    EXPECT_EQ(resolved_version(res, "b"), "1.0.0");
}

TEST(Resolver, NoMatchingVersionForRoot) {
    FakeIndex idx;
    idx.add("a", "1.0.0");
    idx.add("a", "1.5.0");

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^2.0.0"}}));
    ASSERT_FALSE(res.ok);
    EXPECT_EQ(res.error.kind, ResolveError::Kind::NoMatchingVersion);
    EXPECT_EQ(res.error.package, "a");
}

TEST(Resolver, ToLockfileCapturesGraph) {
    FakeIndex idx;
    idx.add("a", "1.0.0", {{"b", "^1.0.0"}});
    idx.add("b", "1.3.0");

    Resolver r(idx);
    auto res = r.resolve(roots({{"a", "^1.0.0"}}));
    ASSERT_TRUE(res.ok) << res.error.message;

    auto lock = to_lockfile(res, "registry+https://example/nav-index");
    ASSERT_EQ(lock.packages.size(), 2u);

    auto a = lock.find("a");
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->source, "registry+https://example/nav-index");
    EXPECT_EQ(a->checksums.at("source"), "sha-a-1.0.0");
    ASSERT_EQ(a->dependencies.size(), 1u);
    EXPECT_EQ(a->dependencies[0], "b@1.3.0");

    auto b = lock.find("b");
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(b->dependencies.empty());
}
