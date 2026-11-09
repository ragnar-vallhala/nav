// Dependency graph: every conflict reported with its chain, [overrides],
// cycles, bundled submodule copies, and the `nav lib tree` rendering.
#include "nav/core/config.hpp"
#include "nav/core/libdeps.hpp"

#include "mock_execution_context.hpp"
#include "test_env.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using nav::core::DepGraph;
using nav::core::Dependency;
using nav::core::ProjectConfig;
using nav::core::ResolvedLib;
using nav::test::MockExecutionContext;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_depgraph_" + std::to_string(::getpid()) + "_" +
                                           std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

std::string lib_toml(const std::string& name, const std::string& deps = "") {
    std::string t = "[project]\nname = \"" + name + "\"\ntype = \"library\"\n";
    if (!deps.empty()) t += "\n[dependencies]\n" + deps;
    return t;
}

Dependency git_dep(const std::string& name, const std::string& url, const std::string& ref) {
    Dependency d;
    d.name = name;
    d.git = url;
    d.ref = ref;
    return d;
}

// Fake git: `clone --branch <ref> <url> <dest>` creates <dest>/nav.toml from
// repos["<url>@<ref>"] and records that repo's commit; `rev-parse HEAD`
// answers it. Each url@ref is its own repo, so refs can share or differ in
// commit as a test needs.
class Graph : public ::testing::Test {
protected:
    struct Repo { std::string toml; std::string commit; };

    void SetUp() override {
        set_env("NAV_HOME", (home.path() / "navhome").string());
        mock.handler = [this](const nav::test::ExecutedCall& c) -> nav::core::CommandResult {
            const auto& a = c.argv;
            if (a.size() >= 2 && a[0] == "git" && a[1] == "clone") {
                std::string ref;
                for (size_t i = 0; i + 1 < a.size(); ++i)
                    if (a[i] == "--branch") ref = a[i + 1];
                const fs::path dest = a.back();
                const auto it = repos.find(a[a.size() - 2] + "@" + ref);
                if (it == repos.end()) return {1, ""};
                fs::create_directories(dest);
                std::ofstream(dest / "nav.toml") << it->second.toml;
                std::ofstream(dest / ".fake-commit") << it->second.commit;
                return {0, ""};
            }
            if (a.size() >= 4 && a[0] == "git" && a[1] == "-C" && a[3] == "rev-parse") {
                std::ifstream f(fs::path(a[2]) / ".fake-commit");
                std::string sha;
                f >> sha;
                return {sha.empty() ? 1 : 0, sha + "\n"};
            }
            return {1, ""};
        };
        cfg.root = home.path();
        cfg.project_name = "app";
    }
    void TearDown() override { unset_env("NAV_HOME"); }

    void repo(const std::string& url, const std::string& ref, const std::string& toml, char commit) {
        repos[url + "@" + ref] = {toml, std::string(40, commit)};
    }

    TempDir home;
    MockExecutionContext mock;
    std::map<std::string, Repo> repos;
    ProjectConfig cfg;
};

const std::string kA = "https://github.com/x/a";
const std::string kB = "https://github.com/x/b";
const std::string kC = "https://github.com/x/c";
const std::string kD = "https://github.com/x/d";

} // namespace

TEST_F(Graph, ReportsEveryConflictWithItsChain) {
    // app -> a -> c@v1, app -> b -> c@v2   and   a -> d@v1, b -> d@main
    repo(kA, "v1.0.0", lib_toml("a", "c = { git = \"" + kC + "\", ref = \"v1\" }\nd = { git = \"" + kD + "\", ref = \"v1\" }\n"), '1');
    repo(kB, "v1.0.0", lib_toml("b", "c = { git = \"" + kC + ".git\", ref = \"v2\" }\nd = { git = \"" + kD + "\", ref = \"main\" }\n"), '2');
    repo(kC, "v1", lib_toml("c"), '3');
    repo(kC + ".git", "v2", lib_toml("c"), '4');  // same repo, cosmetic URL difference
    repo(kD, "v1", lib_toml("d"), '5');
    repo(kD, "main", lib_toml("d"), '6');
    cfg.dependencies = {git_dep("a", kA, "v1.0.0"), git_dep("b", kB, "v1.0.0")};

    DepGraph g = nav::core::resolve_dep_graph(mock, home.path(), cfg);
    EXPECT_TRUE(g.errors.empty());
    ASSERT_EQ(g.conflicts.size(), 2u);  // both found in one pass
    EXPECT_EQ(g.conflicts[0].name, "c");
    ASSERT_EQ(g.conflicts[0].requests.size(), 2u);
    EXPECT_EQ(g.conflicts[0].requests[0].source, kC + " @ v1");
    EXPECT_EQ(g.conflicts[0].requests[0].chain, (std::vector<std::string>{"app", "a"}));
    EXPECT_EQ(g.conflicts[0].requests[1].source, kC + " @ v2");
    EXPECT_EQ(g.conflicts[0].requests[1].chain, (std::vector<std::string>{"app", "b"}));
    EXPECT_EQ(g.conflicts[1].name, "d");
    EXPECT_FALSE(g.ok());
    EXPECT_FALSE(nav::core::resolve_library_deps(mock, home.path(), cfg).has_value());
}

TEST_F(Graph, DifferentRefsAtTheSameCommitAgree) {
    // a asks for c@v1, b for c@release-1 -- both resolve to the same commit.
    repo(kA, "v1.0.0", lib_toml("a", "c = { git = \"" + kC + "\", ref = \"v1\" }\n"), '1');
    repo(kB, "v1.0.0", lib_toml("b", "c = { git = \"git@github.com:x/c.git\", ref = \"release-1\" }\n"), '2');
    repo(kC, "v1", lib_toml("c"), '3');
    repo("git@github.com:x/c.git", "release-1", lib_toml("c"), '3');
    cfg.dependencies = {git_dep("a", kA, "v1.0.0"), git_dep("b", kB, "v1.0.0")};

    DepGraph g = nav::core::resolve_dep_graph(mock, home.path(), cfg);
    EXPECT_TRUE(g.ok());
    EXPECT_EQ(g.libs.size(), 3u);
}

TEST_F(Graph, OverrideSettlesAConflict) {
    repo(kA, "v1.0.0", lib_toml("a", "c = { git = \"" + kC + "\", ref = \"v1\" }\n"), '1');
    repo(kB, "v1.0.0", lib_toml("b", "c = { git = \"" + kC + "\", ref = \"v2\" }\n"), '2');
    repo(kC, "v1", lib_toml("c"), '3');
    repo(kC, "v2", lib_toml("c"), '4');
    cfg.dependencies = {git_dep("a", kA, "v1.0.0"), git_dep("b", kB, "v1.0.0")};
    cfg.overrides = {git_dep("c", kC, "v2")};

    DepGraph g = nav::core::resolve_dep_graph(mock, home.path(), cfg);
    EXPECT_TRUE(g.ok());
    const ResolvedLib* c = g.find("c");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->git_ref, "v2");
    EXPECT_TRUE(c->overridden);
    EXPECT_EQ(g.notes.size(), 2u);  // both requests were replaced
}

TEST_F(Graph, OverridesParseFromNavToml) {
    std::ofstream(home.path() / "nav.toml")
        << "[project]\nname = \"app\"\n[dependencies]\na = { git = \"" << kA << "\", ref = \"v1.0.0\" }\n"
        << "[overrides]\nc = { git = \"" << kC << "\", ref = \"v2\" }\n";
    auto loaded = nav::core::load_project_config(home.path());
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->overrides.size(), 1u);
    EXPECT_EQ(loaded->overrides[0].name, "c");
    EXPECT_EQ(loaded->overrides[0].ref, "v2");
}

TEST_F(Graph, CycleIsReportedWithItsPath) {
    repo(kA, "main", lib_toml("a", "b = { git = \"" + kB + "\", ref = \"main\" }\n"), '1');
    repo(kB, "main", lib_toml("b", "a = { git = \"" + kA + "\", ref = \"main\" }\n"), '2');
    cfg.dependencies = {git_dep("a", kA, "main")};

    DepGraph g = nav::core::resolve_dep_graph(mock, home.path(), cfg);
    ASSERT_EQ(g.errors.size(), 1u);
    EXPECT_EQ(g.errors[0], "Dependency cycle: a -> b -> a");
}

TEST_F(Graph, KeepsGoingPastAMissingDependency) {
    // One dependency cannot be fetched; the rest of the graph is still checked.
    repo(kB, "v1.0.0", lib_toml("b", "c = { git = \"" + kC + "\", ref = \"v1\" }\n"), '2');
    repo(kD, "v1.0.0", lib_toml("d", "c = { git = \"" + kC + "\", ref = \"v2\" }\n"), '5');
    repo(kC, "v1", lib_toml("c"), '3');
    repo(kC, "v2", lib_toml("c"), '4');
    cfg.dependencies = {git_dep("a", kA, "v9"), git_dep("b", kB, "v1.0.0"), git_dep("d", kD, "v1.0.0")};

    DepGraph g = nav::core::resolve_dep_graph(mock, home.path(), cfg);
    EXPECT_EQ(g.errors.size(), 1u);
    EXPECT_EQ(g.conflicts.size(), 1u);
}

TEST_F(Graph, TreeMarksConflictsAndRepeats) {
    repo(kA, "v1.0.0", lib_toml("a", "c = { git = \"" + kC + "\", ref = \"v1\" }\n"), '1');
    repo(kB, "v1.0.0", lib_toml("b", "c = { git = \"" + kC + "\", ref = \"v2\" }\n"), '2');
    repo(kC, "v1", lib_toml("c"), '3');
    repo(kC, "v2", lib_toml("c"), '4');
    cfg.dependencies = {git_dep("a", kA, "v1.0.0"), git_dep("b", kB, "v1.0.0")};

    DepGraph g = nav::core::resolve_dep_graph(mock, home.path(), cfg);
    const std::string tree = nav::core::render_dep_tree(g, "app");
    EXPECT_NE(tree.find("├── a"), std::string::npos) << tree;
    EXPECT_NE(tree.find("c  (" + kC + " @ v1, 3333333)  !! conflict: 2 sources"), std::string::npos) << tree;
    EXPECT_NE(tree.find("└── c (*)"), std::string::npos) << tree;
}

TEST(DepGraphHelpers, NormalizesGitUrls) {
    using nav::core::normalize_git_url;
    EXPECT_EQ(normalize_git_url("https://github.com/x/c.git/"), "https://github.com/x/c");
    EXPECT_EQ(normalize_git_url("git@github.com:x/c.git"), "https://github.com/x/c");
    EXPECT_EQ(normalize_git_url("ssh://git@GitHub.com/x/c"), "https://github.com/x/c");
    EXPECT_EQ(normalize_git_url("https://GITHUB.com/X/c"), "https://github.com/X/c");
}

TEST(DepGraphHelpers, ParsesGitmodules) {
    auto mods = nav::core::parse_gitmodules(
        "[submodule \"extern/NavHAL\"]\n\tpath = extern/NavHAL\n\turl = https://github.com/r/NavHAL.git\n"
        "[submodule \"vtest\"]\n  url = https://github.com/r/vtest\n  path = vtest\n"
        "[core]\n  path = ignored\n  url = ignored\n");
    ASSERT_EQ(mods.size(), 2u);
    EXPECT_EQ(mods[0].first, "extern/NavHAL");
    EXPECT_EQ(mods[0].second, "https://github.com/r/NavHAL.git");
    EXPECT_EQ(mods[1].first, "vtest");
}

TEST(DepGraphHelpers, BundledCopyMismatchWarns) {
    ResolvedLib vaios;
    vaios.name = "vaios";
    vaios.bundled = {{"extern/NavHAL", "https://github.com/r/NavHAL", std::string(40, '3')},
                     {"vtest", "https://github.com/r/vtest", std::string(40, '9')}};
    ResolvedLib crc;
    crc.name = "crc";
    crc.git_url = "https://github.com/r/crc.git";
    crc.commit = std::string(40, 'c');
    ResolvedLib uses_crc;
    uses_crc.name = "proto";
    uses_crc.bundled = {{"extern/crc", "git@github.com:r/crc.git", std::string(40, 'c')}};

    const std::vector<ResolvedLib> libs{vaios, crc, uses_crc};
    auto w = nav::core::check_bundled_copies(libs, "https://github.com/r/NavHAL.git", std::string(40, 'e'));
    ASSERT_EQ(w.size(), 1u);  // vtest is not in the build; proto's crc matches
    EXPECT_NE(w[0].find("vaios bundles https://github.com/r/NavHAL at 3333333 (extern/NavHAL)"),
              std::string::npos) << w[0];
    EXPECT_NE(w[0].find("at eeeeeee"), std::string::npos) << w[0];

    EXPECT_TRUE(nav::core::check_bundled_copies(libs, "https://github.com/r/NavHAL", std::string(40, '3')).empty());
}

TEST(DepGraphHelpers, NavhalDirSilencesBundledNavhal) {
    ResolvedLib vaios;
    vaios.name = "vaios";
    vaios.uses_navhal_dir = true;
    vaios.bundled = {{"extern/NavHAL", "https://github.com/r/NavHAL", std::string(40, '3')}};
    EXPECT_TRUE(nav::core::check_bundled_copies({vaios}, "https://github.com/r/NavHAL", std::string(40, 'e')).empty());
}

TEST(DepGraphHelpers, RenderSetsNavhalDirBeforeLibraries) {
    ResolvedLib l;
    l.name = "vaios";
    l.src = "/x/vaios";
    const std::string out = nav::core::render_libdeps_cmake({l}, {"vaios"});
    const auto set = out.find("set(NAVHAL_DIR \"${CMAKE_SOURCE_DIR}/extern/NavHAL\"");
    ASSERT_NE(set, std::string::npos) << out;
    EXPECT_LT(set, out.find("add_subdirectory("));
}

TEST(DepGraphHelpers, RenderAppliesBoardFlagsToLibraries) {
    ResolvedLib l;
    l.name = "navlink";
    l.src = "/x/navlink";
    const std::string out = nav::core::render_libdeps_cmake({l}, {"navlink"});
    const auto flags = out.find("add_compile_options(${NAV_BOARD_COMPILE_FLAGS})");
    ASSERT_NE(flags, std::string::npos) << out;
    EXPECT_LT(flags, out.find("add_subdirectory("));
}
