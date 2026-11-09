#include "nav/core/appbuild.hpp"
#include "nav/core/config.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nav::core;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_app_" + std::to_string(::getpid()) + "_" +
                                             std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void touch(const fs::path& root, const std::string& rel) {
    fs::create_directories((root / rel).parent_path());
    std::ofstream(root / rel) << "int x;\n";
}

void write_toml(const fs::path& root, const std::string& body) {
    std::ofstream(root / "nav.toml") << "[project]\nname = \"app\"\n[target]\nboard = \"b\"\n" << body;
}

std::string abs(const TempDir& td, const std::string& rel) {
    return (td.path() / rel).generic_string();
}

} // namespace

// --- glob matcher ---------------------------------------------------------

TEST(Glob, StarStaysWithinSegment) {
    EXPECT_TRUE(glob_match("src/*.c", "src/a.c"));
    EXPECT_FALSE(glob_match("src/*.c", "src/sub/a.c"));
    EXPECT_FALSE(glob_match("src/*.c", "src/a.h"));
    EXPECT_TRUE(glob_match("src/a*b?.c", "src/aXXbY.c"));
    EXPECT_FALSE(glob_match("src/?.c", "src/ab.c"));
}

TEST(Glob, DoubleStarSpansDirectories) {
    EXPECT_TRUE(glob_match("src/**/*.c", "src/a.c"));      // zero directories
    EXPECT_TRUE(glob_match("src/**/*.c", "src/x/y/a.c"));
    EXPECT_FALSE(glob_match("src/**/*.c", "lib/a.c"));
    EXPECT_TRUE(glob_match("**/*.c", "a.c"));
    EXPECT_TRUE(glob_match("src/driver/sensor/**", "src/driver/sensor/a.c"));
    EXPECT_TRUE(glob_match("src/driver/sensor/**", "src/driver/sensor/x/a.c"));
    EXPECT_FALSE(glob_match("src/driver/sensor/**", "src/driver/sensor"));
    EXPECT_FALSE(glob_match("src/driver/sensor/**", "src/driver/other/a.c"));
}

TEST(Glob, LiteralAndWholePath) {
    EXPECT_TRUE(glob_match("src/main.c", "src/main.c"));
    EXPECT_FALSE(glob_match("src/main.c", "src/main.cpp"));
    EXPECT_FALSE(glob_match("main.c", "src/main.c"));
}

TEST(Glob, WindowsSeparatorsAndDotSlash) {
    EXPECT_TRUE(glob_match("src\\**\\*.c", "src/a/b.c"));
    EXPECT_TRUE(glob_match("src/**/*.c", "src\\a\\b.c"));
    EXPECT_TRUE(glob_match("./src/*.c", "src/a.c"));
}

// --- config parsing -------------------------------------------------------

TEST(AppBuildConfig, DefaultsWhenAbsent) {
    TempDir td;
    write_toml(td.path(), "[build]\nbackend = \"cmake\"\n");
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_FALSE(cfg->build.declared);
    EXPECT_EQ(cfg->build.sources, std::vector<std::string>{"src/main.c"});
    EXPECT_EQ(cfg->build.include_dirs, std::vector<std::string>{"include"});
    EXPECT_TRUE(cfg->build.file_flags.empty());
    EXPECT_FALSE(cfg->build.c_standard);
}

TEST(AppBuildConfig, ParsesAllKeys) {
    TempDir td;
    write_toml(td.path(), R"(
[build]
sources = ["src/**/*.c"]
exclude = ["src/x/**"]
include_dirs = ["include", "board/b"]
defines = ["A", "B=1"]
c_flags = ["-O2", "-Wall"]
c_standard = 11

[[build.file_flags]]
match = ["src/est/*.c"]
flags = ["-Wextra", "-Werror"]

[[build.file_flags]]
match = ["src/c/**/*.c"]
flags = ["-Wconversion"]
)");
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    const auto& b = cfg->build;
    EXPECT_TRUE(b.declared);
    EXPECT_EQ(b.sources, std::vector<std::string>{"src/**/*.c"});
    EXPECT_EQ(b.exclude, std::vector<std::string>{"src/x/**"});
    EXPECT_EQ(b.include_dirs, (std::vector<std::string>{"include", "board/b"}));
    EXPECT_EQ(b.defines, (std::vector<std::string>{"A", "B=1"}));
    EXPECT_EQ(b.c_flags, (std::vector<std::string>{"-O2", "-Wall"}));
    EXPECT_EQ(b.c_standard, 11);
    ASSERT_EQ(b.file_flags.size(), 2u);
    EXPECT_EQ(b.file_flags[0].flags, (std::vector<std::string>{"-Wextra", "-Werror"}));
    EXPECT_EQ(b.file_flags[1].match, std::vector<std::string>{"src/c/**/*.c"});
}

TEST(AppBuildConfig, RejectsBadTypes) {
    {
        TempDir td;
        write_toml(td.path(), "[build]\nsources = \"src/*.c\"\n");
        EXPECT_FALSE(load_project_config(td.path()));
    }
    {
        TempDir td;
        write_toml(td.path(), "[build]\nc_standard = 12\n");
        EXPECT_FALSE(load_project_config(td.path()));
    }
    {
        TempDir td;
        write_toml(td.path(), "[build]\ndefines = [1]\n");
        EXPECT_FALSE(load_project_config(td.path()));
    }
    {
        TempDir td;
        write_toml(td.path(), "[[build.file_flags]]\nflags = [\"-Werror\"]\n");
        EXPECT_FALSE(load_project_config(td.path())); // no match
    }
}

// --- planning -------------------------------------------------------------

TEST(AppBuildPlan, ExpandsSortedWithExclude) {
    TempDir td;
    for (auto f : {"src/main.c", "src/z.c", "src/a/b.c", "src/a/c/d.c", "src/sensor/s.c",
                   "src/readme.txt", "extern/x.c"})
        touch(td.path(), f);
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.sources = {"src/**/*.c"};
    cfg.build.exclude = {"src/sensor/**"};
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->sources,
              (std::vector<std::string>{abs(td, "src/a/b.c"), abs(td, "src/a/c/d.c"),
                                        abs(td, "src/main.c"), abs(td, "src/z.c")}));
    EXPECT_EQ(plan->include_dirs, std::vector<std::string>{abs(td, "include")});
}

TEST(AppBuildPlan, DefaultIsMainC) {
    TempDir td;
    touch(td.path(), "src/main.c");
    touch(td.path(), "src/other.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->sources, std::vector<std::string>{abs(td, "src/main.c")});
}

TEST(AppBuildPlan, DedupesOverlappingPatterns) {
    TempDir td;
    touch(td.path(), "src/main.c");
    touch(td.path(), "src/a/b.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.sources = {"src/**/*.c", "src/main.c", "src/*.c"};
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->sources.size(), 2u);
}

TEST(AppBuildPlan, NoMatchIsAnError) {
    TempDir td;
    touch(td.path(), "src/main.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.sources = {"src/main.c", "drivers/**/*.c"};
    std::string err;
    EXPECT_FALSE(plan_app_build(cfg, err));
    EXPECT_NE(err.find("drivers/**/*.c"), std::string::npos);
    EXPECT_NE(err.find("matches no files"), std::string::npos);
}

TEST(AppBuildPlan, MissingLiteralSourceIsAnError) {
    TempDir td;
    ProjectConfig cfg;
    cfg.root = td.path();
    std::string err;
    EXPECT_FALSE(plan_app_build(cfg, err));
    EXPECT_NE(err.find("src/main.c"), std::string::npos);
}

TEST(AppBuildPlan, ExcludeEverythingIsAnError) {
    TempDir td;
    touch(td.path(), "src/main.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.exclude = {"src/**"};
    std::string err;
    EXPECT_FALSE(plan_app_build(cfg, err));
    EXPECT_NE(err.find("exclude"), std::string::npos);
}

TEST(AppBuildPlan, FileFlagsMatchOnlyMatchingSources) {
    TempDir td;
    for (auto f : {"src/main.c", "src/est/ekf.c", "src/est/sub/x.c", "src/control/pid.c",
                   "src/control/deep/m.c"})
        touch(td.path(), f);
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.sources = {"src/**/*.c"};
    cfg.build.file_flags = {{{"src/est/*.c", "src/control/**/*.c"}, {"-Werror"}},
                            {{"src/main.c"}, {"-Wextra"}}};
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    ASSERT_EQ(plan->file_flags.size(), 2u);
    EXPECT_EQ(plan->file_flags[0].files,
              (std::vector<std::string>{abs(td, "src/control/deep/m.c"), abs(td, "src/control/pid.c"),
                                        abs(td, "src/est/ekf.c")}));
    EXPECT_EQ(plan->file_flags[1].files, std::vector<std::string>{abs(td, "src/main.c")});
}

TEST(AppBuildPlan, FileFlagsRuleMatchingNothingIsAnError) {
    TempDir td;
    touch(td.path(), "src/main.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.file_flags = {{{"src/est/*.c"}, {"-Werror"}}};
    std::string err;
    EXPECT_FALSE(plan_app_build(cfg, err));
    EXPECT_NE(err.find("src/est/*.c"), std::string::npos);
}

TEST(AppBuildPlan, WindowsStylePatternsInToml) {
    TempDir td;
    touch(td.path(), "src/a/b.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.sources = {"src\\**\\*.c"};
    cfg.build.include_dirs = {"board\\navixsm"};
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->sources, std::vector<std::string>{abs(td, "src/a/b.c")});
    EXPECT_EQ(plan->include_dirs, std::vector<std::string>{abs(td, "board/navixsm")});
}

// --- rendering ------------------------------------------------------------

TEST(AppBuildRender, EmitsVariablesAndFileFlags) {
    AppBuildPlan p;
    p.sources = {"/p/src/a.c", "/p/src/b.c"};
    p.include_dirs = {"/p/include"};
    p.defines = {"CORTEX_M4=1"};
    p.c_flags = {"-O2", "-DNAME=\"x y\""};
    p.c_standard = 11;
    p.file_flags = {{{"/p/src/a.c"}, {"-Wextra", "-Werror"}}};
    const std::string out = render_app_cmake(p);
    EXPECT_NE(out.find("set(NAV_APP_SOURCES\n    \"/p/src/a.c\"\n    \"/p/src/b.c\")"), std::string::npos) << out;
    EXPECT_NE(out.find("set(NAV_APP_INCLUDE_DIRS\n    \"/p/include\")"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_APP_DEFINES\n    \"CORTEX_M4=1\")"), std::string::npos);
    EXPECT_NE(out.find("\"-DNAME=\\\"x y\\\"\""), std::string::npos) << out;
    EXPECT_NE(out.find("set(NAV_APP_C_STANDARD 11)"), std::string::npos);
    EXPECT_NE(out.find("set_property(SOURCE\n    \"/p/src/a.c\"\n    APPEND PROPERTY COMPILE_OPTIONS\n"
                       "    \"-Wextra\"\n    \"-Werror\")"), std::string::npos) << out;
    // No file flags for b.c.
    EXPECT_EQ(out.find("set_property(SOURCE\n    \"/p/src/b.c\""), std::string::npos);
}

TEST(AppBuildRender, EmptyStandardAndNoBackslashes) {
    AppBuildPlan p;
    p.sources = {"C:/proj/src/a.c"};
    const std::string out = render_app_cmake(p);
    EXPECT_NE(out.find("set(NAV_APP_C_STANDARD)"), std::string::npos);
    EXPECT_EQ(out.find('\\'), std::string::npos);
}

// --- template detection ---------------------------------------------------

TEST(AppBuildTemplate, NewTemplateIncludesNavApp) {
    const std::string fresh = render_app_cmakelists("demo");
    EXPECT_TRUE(cmakelists_supports_app_build(fresh));
    EXPECT_TRUE(cmakelists_is_nav_generated_app(fresh));
    EXPECT_NE(fresh.find("project(demo C CXX ASM)"), std::string::npos);
    EXPECT_EQ(fresh.find("{{PROJECT_NAME}}"), std::string::npos);
}

TEST(AppBuildTemplate, OldTemplateIsDetected) {
    const std::string old =
        "cmake_minimum_required(VERSION 3.20)\n"
        "include(\"${CMAKE_BINARY_DIR}/nav-board.cmake\")\n"
        "project(demo C CXX ASM)\n"
        "add_executable(${PROJECT_NAME} src/main.c)\n";
    EXPECT_FALSE(cmakelists_supports_app_build(old));
    EXPECT_TRUE(cmakelists_is_nav_generated_app(old));
}

TEST(AppBuildTemplate, HandWrittenCMakeListsIsNotNavGenerated) {
    EXPECT_FALSE(cmakelists_is_nav_generated_app(
        "project(mine C)\nadd_executable(mine src/main.c)\n"));
}
