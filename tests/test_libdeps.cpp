#include "nav/core/libdeps.hpp"

#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <atomic>
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