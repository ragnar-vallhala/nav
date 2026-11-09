#include "nav/core/libdeps.hpp"
#include "nav/core/navhal.hpp"

#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <map>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using nav::core::ProjectConfig;
using nav::core::ResolvedLib;
using nav::test::MockExecutionContext;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_libdeps_" + std::to_string(::getpid())
                                           + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

// Lay down a minimal library project named `name` at <root>/<name> with the
// given inline [dependencies] body (may be empty).
void make_library(const fs::path& root, const std::string& name,
                  const std::string& deps_body = "") {
    const fs::path dir = root / name;
    fs::create_directories(dir / "src");
    fs::create_directories(dir / "include");
    std::string toml = "[project]\nname = \"" + name + "\"\ntype = \"library\"\n";
    if (!deps_body.empty()) toml += "\n[dependencies]\n" + deps_body;
    std::ofstream(dir / "nav.toml") << toml;
}

void make_executable(const fs::path& root, const std::string& name) {
    const fs::path dir = root / name;
    fs::create_directories(dir / "src");
    std::ofstream(dir / "nav.toml") << "[project]\nname = \"" + name + "\"\n";
}

ProjectConfig config_with_path_dep(const fs::path& root, const std::string& dep_name,
                                   const std::string& rel_path) {
    ProjectConfig cfg;
    cfg.root = root;
    cfg.project_name = "app";
    nav::core::Dependency d;
    d.name = dep_name;
    d.path = rel_path;
    cfg.dependencies.push_back(d);
    return cfg;
}

} // namespace

TEST(LibDeps, ResolvesSinglePathDependency) {
    TempDir td;
    make_library(td.path(), "sensors");
    auto cfg = config_with_path_dep(td.path(), "sensors", "sensors");

    MockExecutionContext mock; // never invoked for path deps
    std::vector<std::string> direct;
    auto libs = nav::core::resolve_library_deps(mock, td.path(), cfg, &direct);

    ASSERT_TRUE(libs.has_value());
    ASSERT_EQ(libs->size(), 1u);
    EXPECT_EQ((*libs)[0].name, "sensors");
    EXPECT_EQ(direct, (std::vector<std::string>{"sensors"}));
    EXPECT_TRUE(mock.calls.empty());
}

TEST(LibDeps, ResolvesTransitiveDependenciesFirst) {
    TempDir td;
    make_library(td.path(), "core");
    make_library(td.path(), "sensors", "core = { path = \"../core\" }\n");
    auto cfg = config_with_path_dep(td.path(), "sensors", "sensors");

    MockExecutionContext mock;
    std::vector<std::string> direct;
    auto libs = nav::core::resolve_library_deps(mock, td.path(), cfg, &direct);

    ASSERT_TRUE(libs.has_value());
    ASSERT_EQ(libs->size(), 2u);
    // Dependencies-first: core precedes sensors.
    EXPECT_EQ((*libs)[0].name, "core");
    EXPECT_EQ((*libs)[1].name, "sensors");
    EXPECT_EQ((*libs)[1].deps, (std::vector<std::string>{"core"}));
    // Only the consumer's direct dep is reported.
    EXPECT_EQ(direct, (std::vector<std::string>{"sensors"}));
}

TEST(LibDeps, DedupesSharedDependency) {
    TempDir td;
    make_library(td.path(), "core");
    make_library(td.path(), "a", "core = { path = \"../core\" }\n");
    make_library(td.path(), "b", "core = { path = \"../core\" }\n");
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.project_name = "app";
    for (const char* n : {"a", "b"}) {
        nav::core::Dependency d; d.name = n; d.path = n;
        cfg.dependencies.push_back(d);
    }

    MockExecutionContext mock;
    auto libs = nav::core::resolve_library_deps(mock, td.path(), cfg);
    ASSERT_TRUE(libs.has_value());
    // core, a, b — core appears exactly once.
    ASSERT_EQ(libs->size(), 3u);
    int core_count = 0;
    for (const auto& l : *libs) if (l.name == "core") ++core_count;
    EXPECT_EQ(core_count, 1);
}

TEST(LibDeps, DetectsCycle) {
    TempDir td;
    make_library(td.path(), "a", "b = { path = \"../b\" }\n");
    make_library(td.path(), "b", "a = { path = \"../a\" }\n");
    auto cfg = config_with_path_dep(td.path(), "a", "a");

    MockExecutionContext mock;
    auto libs = nav::core::resolve_library_deps(mock, td.path(), cfg);
    EXPECT_FALSE(libs.has_value());
}

TEST(LibDeps, RejectsNonLibraryDependency) {
    TempDir td;
    make_executable(td.path(), "tool");
    auto cfg = config_with_path_dep(td.path(), "tool", "tool");

    MockExecutionContext mock;
    auto libs = nav::core::resolve_library_deps(mock, td.path(), cfg);
    EXPECT_FALSE(libs.has_value());
}

TEST(LibDeps, FailsOnMissingPath) {
    TempDir td;
    auto cfg = config_with_path_dep(td.path(), "ghost", "does-not-exist");

    MockExecutionContext mock;
    auto libs = nav::core::resolve_library_deps(mock, td.path(), cfg);
    EXPECT_FALSE(libs.has_value());
}

TEST(LibDeps, RenderEmitsSubdirsEdgesAndTargets) {
    std::vector<ResolvedLib> libs = {
        {"core", "/abs/core", {}},
        {"sensors", "/abs/sensors", {"core"}},
    };
    std::string out = nav::core::render_libdeps_cmake(libs, {"sensors"});

    EXPECT_NE(out.find("add_subdirectory(\"/abs/core\""), std::string::npos);
    EXPECT_NE(out.find("add_subdirectory(\"/abs/sensors\""), std::string::npos);
    EXPECT_NE(out.find("target_link_libraries(sensors PUBLIC core)"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_DEP_TARGETS sensors)"), std::string::npos);
    // core's add_subdirectory must precede sensors's (dependencies first).
    EXPECT_LT(out.find("deps/core"), out.find("deps/sensors"));
}

TEST(LibDeps, RenderEmitsDependencyOptionsBeforeSubdir) {
    std::vector<ResolvedLib> libs = {
        {"vaios", "/abs/vaios", {}, {"NAVHAL=ON", "VAIOS_FPU=ON", "NAVLINK_PROFILE=test"}},
    };
    std::string out = nav::core::render_libdeps_cmake(libs, {"vaios"});

    const auto navhal = out.find("set(NAVHAL \"ON\" CACHE BOOL \"nav dep option\" FORCE)");
    const auto fpu    = out.find("set(VAIOS_FPU \"ON\" CACHE BOOL \"nav dep option\" FORCE)");
    const auto subdir = out.find("add_subdirectory(\"/abs/vaios\"");
    ASSERT_NE(navhal, std::string::npos);
    ASSERT_NE(fpu, std::string::npos);
    ASSERT_NE(subdir, std::string::npos);
    // Options must be forced into the cache before the library is added.
    EXPECT_NE(out.find("set(NAVLINK_PROFILE \"test\" CACHE STRING"), std::string::npos);
    EXPECT_LT(navhal, subdir);
    EXPECT_LT(fpu, subdir);
}
// ---- git dependencies -------------------------------------------------------

namespace {

// Points NAV_HOME at a temp dir so git clones land in an isolated cache, and
// fakes git: `clone` materializes the destination with the nav.toml registered
// for that URL; `rev-parse HEAD` answers `sha`.
class GitDeps : public ::testing::Test {
protected:
    void SetUp() override {
        ::setenv("NAV_HOME", (home.path() / "navhome").c_str(), 1);
        mock.handler = [this](const nav::test::ExecutedCall& c) -> nav::core::CommandResult {
            const auto& a = c.argv;
            if (a.size() >= 2 && a[0] == "git" && a[1] == "clone") {
                const fs::path dest = a.back();
                const std::string url = a[a.size() - 2];
                fs::create_directories(dest);
                std::ofstream(dest / "nav.toml") << tomls[url];
                return {0, ""};
            }
            if (a.size() >= 4 && a[0] == "git" && a[1] == "-C" && a[3] == "rev-parse")
                return {0, sha + "\n"};
            return {1, ""};
        };
    }
    void TearDown() override { ::unsetenv("NAV_HOME"); }

    static std::string lib_toml(const std::string& name, const std::string& version) {
        return "[project]\nname = \"" + name + "\"\nversion = \"" + version +
               "\"\ntype = \"library\"\n";
    }
    ProjectConfig app_with_git(const std::string& url, const std::string& ref) {
        ProjectConfig cfg;
        cfg.root = home.path();
        cfg.project_name = "app";
        nav::core::Dependency d;
        d.name = "dep";
        d.git = url;
        d.ref = ref;
        cfg.dependencies.push_back(d);
        return cfg;
    }

    TempDir home;
    MockExecutionContext mock;
    std::map<std::string, std::string> tomls;
    std::string sha = std::string(40, 'a');
};

} // namespace

TEST_F(GitDeps, ClonesWithSubmodulesAndRecordsCommit) {
    tomls["https://github.com/x/vaios"] = lib_toml("vaios", "0.2.0");
    auto libs = nav::core::resolve_library_deps(mock, home.path(),
                                                app_with_git("https://github.com/x/vaios", "v0.2.0"));
    ASSERT_TRUE(libs.has_value());
    ASSERT_EQ(libs->size(), 1u);
    const auto& lib = (*libs)[0];
    EXPECT_EQ(lib.name, "vaios");
    EXPECT_EQ(lib.git_url, "https://github.com/x/vaios");
    EXPECT_EQ(lib.git_ref, "v0.2.0");
    EXPECT_EQ(lib.commit, sha);

    ASSERT_FALSE(mock.calls.empty());
    const auto& clone = mock.calls.front().argv;
    EXPECT_NE(std::find(clone.begin(), clone.end(), "--recurse-submodules"), clone.end());
    EXPECT_NE(std::find(clone.begin(), clone.end(), "--shallow-submodules"), clone.end());
}

TEST_F(GitDeps, TagMustMatchManifestVersion) {
    tomls["https://github.com/x/lib"] = lib_toml("lib", "1.0.0");
    auto libs = nav::core::resolve_library_deps(mock, home.path(),
                                                app_with_git("https://github.com/x/lib", "v1.2.0"));
    EXPECT_FALSE(libs.has_value());
}

TEST_F(GitDeps, BranchRefSkipsVersionCheck) {
    tomls["https://github.com/x/lib"] = lib_toml("lib", "1.0.0");
    auto libs = nav::core::resolve_library_deps(mock, home.path(),
                                                app_with_git("https://github.com/x/lib", "main"));
    EXPECT_TRUE(libs.has_value());
}

TEST_F(GitDeps, UnreadableCommitLeavesItEmpty) {
    tomls["https://github.com/x/lib"] = lib_toml("lib", "1.0.0");
    sha = "not-a-sha";
    auto libs = nav::core::resolve_library_deps(mock, home.path(),
                                                app_with_git("https://github.com/x/lib", "v1.0.0"));
    ASSERT_TRUE(libs.has_value());
    EXPECT_TRUE((*libs)[0].commit.empty());
}

TEST(LibDeps, SameLibraryFromTwoPathsFails) {
    // app -> a -> ../c1 (named "c"), app -> b -> ../c2 (also named "c").
    TempDir td;
    make_library(td.path(), "a", "c = { path = \"../c1\" }\n");
    make_library(td.path(), "b", "c = { path = \"../c2\" }\n");
    for (const char* dir : {"c1", "c2"}) {
        fs::create_directories(td.path() / dir);
        std::ofstream(td.path() / dir / "nav.toml") << "[project]\nname = \"c\"\ntype = \"library\"\n";
    }
    ProjectConfig cfg = config_with_path_dep(td.path(), "a", "a");
    nav::core::Dependency b;
    b.name = "b";
    b.path = "b";
    cfg.dependencies.push_back(b);

    MockExecutionContext mock;
    EXPECT_FALSE(nav::core::resolve_library_deps(mock, td.path(), cfg).has_value());
}

// ---- nav.lock git pins ------------------------------------------------------

namespace {
ResolvedLib git_lib(const std::string& name, const std::string& ref, const std::string& commit) {
    ResolvedLib l;
    l.name = name;
    l.git_url = "https://github.com/x/" + name;
    l.git_ref = ref;
    l.commit = commit;
    return l;
}
const std::string kA(40, 'a'), kB(40, 'b');
} // namespace

TEST(GitLock, PinsNewDependency) {
    nav::core::Lockfile lock;
    auto r = nav::core::reconcile_git_lock(lock, {git_lib("vaios", "v0.2.0", kA)});
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.errors.empty());
    ASSERT_NE(lock.find_git("vaios"), nullptr);
    EXPECT_EQ(lock.find_git("vaios")->commit, kA);
}

TEST(GitLock, MatchingPinIsUnchanged) {
    nav::core::Lockfile lock;
    lock.git.push_back({"vaios", "https://github.com/x/vaios", "v0.2.0", kA});
    auto r = nav::core::reconcile_git_lock(lock, {git_lib("vaios", "v0.2.0", kA)});
    EXPECT_FALSE(r.changed);
    EXPECT_TRUE(r.errors.empty());
}

TEST(GitLock, MovedRefIsAnErrorAndKeepsPin) {
    nav::core::Lockfile lock;
    lock.git.push_back({"vaios", "https://github.com/x/vaios", "v0.2.0", kA});
    auto r = nav::core::reconcile_git_lock(lock, {git_lib("vaios", "v0.2.0", kB)});
    ASSERT_EQ(r.errors.size(), 1u);
    EXPECT_FALSE(r.changed);
    EXPECT_EQ(lock.find_git("vaios")->commit, kA);
}

TEST(GitLock, ChangedRefRepins) {
    nav::core::Lockfile lock;
    lock.git.push_back({"vaios", "https://github.com/x/vaios", "v0.2.0", kA});
    auto r = nav::core::reconcile_git_lock(lock, {git_lib("vaios", "v0.3.0", kB)});
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(r.errors.empty());
    EXPECT_EQ(lock.find_git("vaios")->ref, "v0.3.0");
    EXPECT_EQ(lock.find_git("vaios")->commit, kB);
}

TEST(GitLock, DropsPinForRemovedDependency) {
    nav::core::Lockfile lock;
    lock.git.push_back({"old", "https://github.com/x/old", "v1.0.0", kA});
    auto r = nav::core::reconcile_git_lock(lock, {});
    EXPECT_TRUE(r.changed);
    EXPECT_TRUE(lock.git.empty());
}

TEST(GitLock, PathDependenciesAreNotPinned) {
    nav::core::Lockfile lock;
    ResolvedLib local;
    local.name = "local";
    auto r = nav::core::reconcile_git_lock(lock, {local});
    EXPECT_FALSE(r.changed);
    EXPECT_TRUE(lock.git.empty());
}

TEST(GitLock, UnknownCommitIsNotPinned) {
    nav::core::Lockfile lock;
    auto r = nav::core::reconcile_git_lock(lock, {git_lib("vaios", "v0.2.0", "")});
    EXPECT_FALSE(r.changed);
    EXPECT_TRUE(lock.git.empty());
    EXPECT_EQ(r.notes.size(), 1u);
}

// ---- NavHAL helpers ---------------------------------------------------------

TEST(NavHal, ReadsHalApiVersion) {
    TempDir td;
    fs::create_directories(td.path() / "include");
    std::ofstream(td.path() / "include" / "navhal.h")
        << "/* HAL_API_VERSION 9 in a comment */\n#ifndef X\n#  define HAL_API_VERSION 3\n#endif\n";
    auto v = nav::core::read_hal_api_version(td.path());
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 3);
}

TEST(NavHal, MissingHalApiVersionIsNullopt) {
    TempDir td;
    EXPECT_FALSE(nav::core::read_hal_api_version(td.path()).has_value());
}

TEST(NavHal, GitHeadCommitTrimsAndValidates) {
    MockExecutionContext mock;
    mock.scripted_result = {0, std::string(40, 'f') + "\n"};
    EXPECT_EQ(nav::core::git_head_commit(mock, "/repo"), std::string(40, 'f'));
    mock.scripted_result = {0, "fatal: not a git repository\n"};
    EXPECT_TRUE(nav::core::git_head_commit(mock, "/repo").empty());
    mock.scripted_result = {128, ""};
    EXPECT_TRUE(nav::core::git_head_commit(mock, "/repo").empty());
}
