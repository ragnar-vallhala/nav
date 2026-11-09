// NavHAL pinning: [navhal] in nav.toml, the commit recorded in nav.lock, the
// per-commit checkouts in the cache, and `nav update` re-resolving them.
#include "nav/core/cache.hpp"
#include "nav/core/config.hpp"
#include "nav/core/lockfile.hpp"
#include "nav/core/navhal.hpp"

#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using nav::core::Lockfile;
using nav::core::NavhalSpec;
using nav::test::MockExecutionContext;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_navhalpin_" + std::to_string(::getpid()) + "_" +
                                           std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

const std::string kUrl = "https://example.com/NavHAL.git";
const std::string kA(40, 'a');
const std::string kB(40, 'b');

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Fake git. `clone --branch <ref> <url> <dest>` makes <dest> a "checkout" of
// whatever commit tips[url@ref] holds right now; `-C <dir> rev-parse HEAD`
// answers it. Moving a branch is just reassigning its tip.
class Pin : public ::testing::Test {
protected:
    void SetUp() override {
        ::setenv("NAV_HOME", (tmp.path() / "navhome").c_str(), 1);
        root = tmp.path() / "app";
        mock.handler = [this](const nav::test::ExecutedCall& c) -> nav::core::CommandResult {
            const auto& a = c.argv;
            if (a.size() >= 2 && a[0] == "git" && a[1] == "clone") {
                std::string ref;
                for (size_t i = 0; i + 1 < a.size(); ++i)
                    if (a[i] == "--branch") ref = a[i + 1];
                const auto it = tips.find(a[a.size() - 2] + "@" + ref);
                if (it == tips.end()) return {1, ""};
                write(fs::path(a.back()) / ".fake-commit", it->second);
                return {0, ""};
            }
            if (a.size() >= 4 && a[0] == "git" && a[1] == "-C" && a[3] == "rev-parse") {
                std::string sha = slurp(fs::path(a[2]) / ".fake-commit");
                return {sha.empty() ? 1 : 0, sha + "\n"};
            }
            return {1, ""};
        };
    }
    void TearDown() override { ::unsetenv("NAV_HOME"); }

    void project(const std::string& extra = "") {
        write(root / "nav.toml", "[project]\nname = \"app\"\n[target]\nboard = \"x\"\n" + extra);
    }
    nav::core::ProjectConfig cfg() { return *nav::core::load_project_config(root); }
    size_t clones() const {
        size_t n = 0;
        for (const auto& c : mock.calls) n += c.argv.size() > 1 && c.argv[1] == "clone";
        return n;
    }
    std::optional<Lockfile> lock() { return nav::core::load_lockfile(root / "nav.lock"); }

    TempDir tmp;
    fs::path root;
    MockExecutionContext mock;
    std::map<std::string, std::string> tips;
};

} // namespace

// ---- nav.toml ---------------------------------------------------------------

TEST_F(Pin, ParsesNavhalTable) {
    project("[navhal]\nref = \"v0.4.1\"\ngit = \"https://example.com/fork.git\"\n");
    const auto spec = nav::core::navhal_spec(cfg());
    EXPECT_EQ(spec.ref, "v0.4.1");
    EXPECT_EQ(spec.git, "https://example.com/fork.git");
}

TEST_F(Pin, RefAloneKeepsTheUpstreamUrl) {
    project("[navhal]\nref = \"release/0.4.1-stable\"\n");
    const auto spec = nav::core::navhal_spec(cfg());
    EXPECT_EQ(spec.ref, "release/0.4.1-stable");
    EXPECT_EQ(spec.git, nav::core::kNavhalRepoUrl);
}

TEST_F(Pin, DefaultsWhenTheTableIsAbsent) {
    project();
    const auto spec = nav::core::navhal_spec(cfg());
    EXPECT_EQ(spec.ref, nav::core::kDefaultNavhalRef);
    EXPECT_EQ(spec.git, nav::core::kNavhalRepoUrl);
}

// ---- nav.lock format --------------------------------------------------------

TEST_F(Pin, OldLockfileWithoutNavhalStillLoads) {
    write(root / "nav.lock",
          R"({"git":[{"name":"vaios","url":"u","ref":"v1","commit":"c1"}],"packages":[]})");
    auto l = lock();
    ASSERT_TRUE(l.has_value());
    EXPECT_FALSE(l->navhal.has_value());
    ASSERT_EQ(l->git.size(), 1u);
    // And a lock with no navhal entry renders without one.
    EXPECT_EQ(nav::core::render_lockfile(*l).find("navhal"), std::string::npos);
}

TEST_F(Pin, NavhalEntryRoundTripsAndOlderPackagesSurvive) {
    write(root / "nav.lock", R"({"packages":[]})");
    Lockfile l = *lock();
    EXPECT_TRUE(nav::core::pin_navhal(l, NavhalSpec{kUrl, "v0.4.1"}, kA));
    EXPECT_FALSE(nav::core::pin_navhal(l, NavhalSpec{kUrl, "v0.4.1"}, kA));  // unchanged
    ASSERT_TRUE(nav::core::save_lockfile(l, root / "nav.lock"));
    auto back = lock();
    ASSERT_TRUE(back && back->navhal);
    EXPECT_EQ(back->navhal->url, kUrl);
    EXPECT_EQ(back->navhal->ref, "v0.4.1");
    EXPECT_EQ(back->navhal->commit, kA);
}

TEST_F(Pin, LockWithOnlyANavhalEntryLoads) {
    write(root / "nav.lock", R"({"navhal":{"url":"u","ref":"r","commit":"c"}})");
    auto l = lock();
    ASSERT_TRUE(l && l->navhal);
    EXPECT_EQ(l->navhal->commit, "c");
}

// ---- first build writes the pin --------------------------------------------

TEST_F(Pin, FirstSyncPinsTheResolvedCommitAndLinksIt) {
    project("[navhal]\nref = \"v0.4.1\"\n");
    tips[std::string(nav::core::kNavhalRepoUrl) + "@v0.4.1"] = kA;

    const fs::path dir = nav::core::sync_project_navhal(mock, root, cfg());
    ASSERT_FALSE(dir.empty());

    auto l = lock();
    ASSERT_TRUE(l && l->navhal);
    EXPECT_EQ(l->navhal->commit, kA);
    EXPECT_EQ(l->navhal->ref, "v0.4.1");
    EXPECT_EQ(l->navhal->url, nav::core::kNavhalRepoUrl);

    // extern/NavHAL is the per-commit checkout.
    const fs::path expect = nav::core::navhal_commit_dir(nav::core::kNavhalRepoUrl, kA);
    EXPECT_TRUE(fs::equivalent(root / "extern" / "NavHAL", expect));
    EXPECT_EQ(slurp(root / "extern" / "NavHAL" / ".fake-commit"), kA);
}

TEST_F(Pin, SyncKeepsTheOtherLockEntries) {
    project();
    write(root / "nav.lock",
          R"({"git":[{"name":"vaios","url":"u","ref":"v1","commit":"c1"}],"packages":[]})");
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kA;
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    auto l = lock();
    ASSERT_TRUE(l && l->navhal);
    ASSERT_EQ(l->git.size(), 1u);
    EXPECT_EQ(l->git[0].commit, "c1");
}

TEST_F(Pin, ASecondSyncUsesTheCachedPinWithoutTheNetwork) {
    project();
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kA;
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    const size_t before = mock.calls.size();
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kB;  // branch moved; not consulted
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    EXPECT_EQ(mock.calls.size(), before);
    EXPECT_EQ(lock()->navhal->commit, kA);
}

TEST_F(Pin, TwoAppsOnDifferentCommitsGetTheirOwnCheckouts) {
    tips[std::string(nav::core::kNavhalRepoUrl) + "@v1"] = kA;
    tips[std::string(nav::core::kNavhalRepoUrl) + "@v2"] = kB;
    const fs::path app2 = tmp.path() / "app2";
    project("[navhal]\nref = \"v1\"\n");
    write(app2 / "nav.toml", "[project]\nname = \"app2\"\n[navhal]\nref = \"v2\"\n");
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, app2, *nav::core::load_project_config(app2)).empty());
    EXPECT_EQ(slurp(root / "extern" / "NavHAL" / ".fake-commit"), kA);
    EXPECT_EQ(slurp(app2 / "extern" / "NavHAL" / ".fake-commit"), kB);
}

// ---- a moved ref -----------------------------------------------------------

TEST_F(Pin, MovedBranchIsRefusedWhenTheLockedCheckoutIsNotCached) {
    project();
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kA;
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());

    // A clean machine: nav.lock came with the source, the cache did not.
    fs::remove_all(tmp.path() / "navhome");
    fs::remove(root / "extern" / "NavHAL");
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kB;

    EXPECT_TRUE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    EXPECT_EQ(lock()->navhal->commit, kA);  // the pin is untouched
    EXPECT_FALSE(fs::exists(root / "extern" / "NavHAL"));
}

TEST_F(Pin, UnmovedRefOnACleanMachineRestoresTheLockedCommit) {
    project();
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kA;
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    fs::remove_all(tmp.path() / "navhome");
    fs::remove(root / "extern" / "NavHAL");

    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    EXPECT_EQ(slurp(root / "extern" / "NavHAL" / ".fake-commit"), kA);
}

TEST_F(Pin, ChangingTheRefInNavTomlRepins) {
    project("[navhal]\nref = \"v1\"\n");
    tips[std::string(nav::core::kNavhalRepoUrl) + "@v1"] = kA;
    tips[std::string(nav::core::kNavhalRepoUrl) + "@v2"] = kB;
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    project("[navhal]\nref = \"v2\"\n");
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    EXPECT_EQ(lock()->navhal->commit, kB);
    EXPECT_EQ(lock()->navhal->ref, "v2");
    EXPECT_EQ(slurp(root / "extern" / "NavHAL" / ".fake-commit"), kB);
}

// ---- nav update ------------------------------------------------------------

TEST_F(Pin, UpdateReResolvesTheRefAndRewritesThePin) {
    project();
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kA;
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    ASSERT_EQ(lock()->navhal->commit, kA);

    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kB;
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg(), /*refresh=*/true).empty());

    EXPECT_EQ(lock()->navhal->commit, kB);
    EXPECT_EQ(slurp(root / "extern" / "NavHAL" / ".fake-commit"), kB);
    // The old commit's checkout stays for any app still pinned to it.
    EXPECT_TRUE(fs::exists(nav::core::navhal_commit_dir(nav::core::kNavhalRepoUrl, kA)));
}

TEST_F(Pin, AVendoredNavhalDirectoryIsLeftAlone) {
    project();
    write(root / "extern" / "NavHAL" / "mine.txt", "mine");
    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    EXPECT_TRUE(fs::exists(root / "extern" / "NavHAL" / "mine.txt"));
    EXPECT_FALSE(fs::exists(root / "nav.lock"));
    EXPECT_EQ(clones(), 0u);
}

// ---- legacy per-ref cache --------------------------------------------------

TEST_F(Pin, AnUnpinnedProjectAdoptsTheOldPerRefCloneItBuiltWith) {
    project();
    // What an older nav left behind: navhal/<ref> with the completion marker.
    const fs::path legacy = nav::core::navhal_cache_dir("stable");
    write(legacy / ".fake-commit", kA);
    write(nav::core::install_marker(legacy), "ok\n");
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kB;  // upstream has moved on

    ASSERT_FALSE(nav::core::sync_project_navhal(mock, root, cfg()).empty());
    EXPECT_EQ(lock()->navhal->commit, kA);   // keeps what it was building with
    EXPECT_EQ(clones(), 0u);
}

TEST_F(Pin, CreatePathIgnoresTheOldPerRefClone) {
    const fs::path legacy = nav::core::navhal_cache_dir("stable");
    write(legacy / ".fake-commit", kA);
    write(nav::core::install_marker(legacy), "ok\n");
    tips[std::string(nav::core::kNavhalRepoUrl) + "@stable"] = kB;

    auto r = nav::core::resolve_navhal(mock, NavhalSpec{}, "");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->commit, kB);
}

// ---- helpers ---------------------------------------------------------------

TEST(NavhalTags, PicksTheNewestVersionTagOnACommit) {
    const std::string out =
        "1111111111111111111111111111111111111111\trefs/tags/v0.3.9\n"
        "2222222222222222222222222222222222222222\trefs/tags/v0.4.0\n"
        "3333333333333333333333333333333333333333\trefs/tags/v0.4.0^{}\n"
        "3333333333333333333333333333333333333333\trefs/tags/v0.4.1-rc1\n"
        "3333333333333333333333333333333333333333\trefs/tags/nightly\n";
    // Annotated v0.4.0 peels to 333...; v0.4.1-rc1 is newer than v0.4.0.
    EXPECT_EQ(nav::core::tag_at_commit(out, std::string(40, '3')), "v0.4.1-rc1");
    EXPECT_EQ(nav::core::tag_at_commit(out, std::string(40, '1')), "v0.3.9");
    EXPECT_EQ(nav::core::tag_at_commit(out, std::string(40, '9')), "");
}

TEST_F(Pin, CommitDirsAreKeyedByRepoAndCommit) {
    const fs::path d = nav::core::navhal_commit_dir("https://github.com/x/NavHAL.git", kA);
    EXPECT_EQ(d.filename().string(), kA);
    EXPECT_EQ(d.parent_path().filename().string(), "NavHAL");
    EXPECT_NE(d, nav::core::navhal_commit_dir("https://github.com/x/NavHAL.git", kB));
}

// ---- library branch refresh ------------------------------------------------

TEST_F(Pin, RefreshGitCacheReplacesAStaleBranchClone) {
    const std::string url = "https://example.com/lib.git";
    tips[url + "@main"] = kA;
    ASSERT_FALSE(nav::core::ensure_git_cached(mock, url, "main").empty());
    EXPECT_EQ(slurp(nav::core::git_cache_dir(url, "main") / ".fake-commit"), kA);

    tips[url + "@main"] = kB;
    // Cached: ensure_git_cached never re-fetches, which is the gap.
    nav::core::ensure_git_cached(mock, url, "main");
    EXPECT_EQ(slurp(nav::core::git_cache_dir(url, "main") / ".fake-commit"), kA);

    EXPECT_EQ(nav::core::refresh_git_cache(mock, url, "main"), kB);
    EXPECT_EQ(slurp(nav::core::git_cache_dir(url, "main") / ".fake-commit"), kB);
    EXPECT_TRUE(nav::core::is_installed(nav::core::git_cache_dir(url, "main")));
}

// ---- option-injection hardening --------------------------------------------

TEST(GitArgs, DashPrefixIsUnsafe) {
    EXPECT_TRUE(nav::core::is_unsafe_git_arg("--upload-pack=touch /tmp/x"));
    EXPECT_TRUE(nav::core::is_unsafe_git_arg("-c"));
    EXPECT_FALSE(nav::core::is_unsafe_git_arg(""));
    EXPECT_FALSE(nav::core::is_unsafe_git_arg("https://example.com/lib.git"));
    EXPECT_FALSE(nav::core::is_unsafe_git_arg("v1.0-rc1"));
}

TEST_F(Pin, DashUrlOrRefIsRefusedBeforeAnyGitCall) {
    EXPECT_TRUE(nav::core::ensure_git_cached(mock, "--upload-pack=evil", "main").empty());
    EXPECT_TRUE(nav::core::ensure_git_cached(mock, "https://example.com/lib.git", "--evil").empty());
    EXPECT_TRUE(nav::core::refresh_git_cache(mock, "-oProxyCommand=evil", "main").empty());
    EXPECT_TRUE(nav::core::refresh_git_cache(mock, "https://example.com/lib.git", "-x").empty());
    NavhalSpec spec;
    spec.git = "--upload-pack=evil";
    EXPECT_FALSE(nav::core::resolve_navhal(mock, spec, ""));
    spec.git = "https://example.com/NavHAL.git";
    spec.ref = "-x";
    EXPECT_FALSE(nav::core::resolve_navhal(mock, spec, ""));
    EXPECT_TRUE(mock.calls.empty());
}

TEST_F(Pin, CloneSeparatesTheUrlFromOptions) {
    const std::string url = "https://example.com/lib.git";
    tips[url + "@main"] = kA;
    ASSERT_FALSE(nav::core::ensure_git_cached(mock, url, "main").empty());
    ASSERT_FALSE(mock.calls.empty());
    const auto& a = mock.calls.front().argv;
    ASSERT_GE(a.size(), 4u);
    EXPECT_EQ(a[a.size() - 3], "--");
    EXPECT_EQ(a[a.size() - 2], url);
}

TEST_F(Pin, RefreshGitCacheKeepsTheCacheWhenTheFetchFails) {
    const std::string url = "https://example.com/lib.git";
    tips[url + "@main"] = kA;
    ASSERT_FALSE(nav::core::ensure_git_cached(mock, url, "main").empty());
    tips.clear();  // upstream unreachable
    EXPECT_EQ(nav::core::refresh_git_cache(mock, url, "main"), "");
    EXPECT_EQ(slurp(nav::core::git_cache_dir(url, "main") / ".fake-commit"), kA);
}
