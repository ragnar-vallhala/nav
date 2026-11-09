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

TEST(AppBuildPlan, RootGlobSkipsBuildExternAndDotDirs) {
    TempDir td;
    for (auto f : {"main.c", "src/a.c", "src/deep/b.c", "build/gen.c", "extern/NavHAL/hal.c",
                   ".git/x.c", "src/.hidden/h.c", "src/build/keep.c"})
        touch(td.path(), f);
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.sources = {"**/*.c"};
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->sources,
              (std::vector<std::string>{abs(td, "main.c"), abs(td, "src/a.c"),
                                        abs(td, "src/build/keep.c"), abs(td, "src/deep/b.c")}));
}

TEST(AppBuildPlan, LiteralPrefixMayEnterExternAndBuild) {
    TempDir td;
    for (auto f : {"extern/foo/a.c", "extern/NavHAL/hal.c", "build/gen/g.c"})
        touch(td.path(), f);
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.sources = {"extern/foo/*.c", "build/gen/*.c"};
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->sources,
              (std::vector<std::string>{abs(td, "build/gen/g.c"), abs(td, "extern/foo/a.c")}));
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

TEST(AppBuildTemplate, OldTemplateLacksAppKconfig) {
    const std::string old =
        "include(\"${CMAKE_BINARY_DIR}/nav-board.cmake\")\n"
        "include(\"${CMAKE_BINARY_DIR}/nav-app.cmake\")\n"
        "add_executable(${PROJECT_NAME} src/main.c)\n";
    EXPECT_FALSE(cmakelists_supports_app_kconfig(old));
    EXPECT_TRUE(cmakelists_supports_app_kconfig(render_app_cmakelists("demo")));
}

TEST(AppBuildTemplate, HandWrittenCMakeListsIsNotNavGenerated) {
    EXPECT_FALSE(cmakelists_is_nav_generated_app(
        "project(mine C)\nadd_executable(mine src/main.c)\n"));
}

// --- linker fragment / libc / libs -----------------------------------------

TEST(AppBuildLink, ParsesFragmentLibcLibs) {
    TempDir td;
    write_toml(td.path(), "[build]\nlinker_fragment = \"app.ld\"\nlibc = \"newlib\"\nlibs = [\"m\", \"c\"]\n");
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_TRUE(cfg->build.declared);
    EXPECT_EQ(cfg->build.linker_fragment, "app.ld");
    EXPECT_EQ(cfg->build.libc, "newlib");
    EXPECT_EQ(cfg->build.libs, (std::vector<std::string>{"m", "c"}));
}

TEST(AppBuildLink, DefaultsAndBadLibc) {
    TempDir td;
    write_toml(td.path(), "");
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_EQ(cfg->build.libc, "none");
    EXPECT_TRUE(cfg->build.linker_fragment.empty());
    TempDir bad;
    write_toml(bad.path(), "[build]\nlibc = \"glibc\"\n");
    EXPECT_FALSE(load_project_config(bad.path()));
}

TEST(AppBuildLink, PlanResolvesFragmentAndErrorsWhenMissing) {
    TempDir td;
    touch(td.path(), "src/main.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    cfg.build.linker_fragment = "app.ld";
    std::string err;
    EXPECT_FALSE(plan_app_build(cfg, err));
    EXPECT_NE(err.find("app.ld"), std::string::npos);
    touch(td.path(), "app.ld");
    cfg.build.libc = "newlib";
    cfg.build.libs = {"m"};
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->linker_fragment, abs(td, "app.ld"));
    const std::string out = render_app_cmake(*plan);
    EXPECT_NE(out.find("set(NAV_APP_LINKER_FRAGMENT \"" + abs(td, "app.ld") + "\")"), std::string::npos) << out;
    EXPECT_NE(out.find("set(NAV_APP_LIBC \"newlib\")"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_APP_LIBS\n    \"m\")"), std::string::npos);
}

TEST(AppBuildLink, RenderDefaultsAreNone) {
    const std::string out = render_app_cmake(AppBuildPlan{});
    EXPECT_NE(out.find("set(NAV_APP_LINKER_FRAGMENT)"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_APP_LIBC \"none\")"), std::string::npos);
}

// --- link order / syscalls --------------------------------------------------

TEST(AppBuildSyscalls, ParsesAndValidates) {
    TempDir td;
    write_toml(td.path(), "[build]\nlibc = \"newlib\"\nsyscalls = \"none\"\n");
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_EQ(cfg->build.syscalls, "none");

    TempDir def;
    write_toml(def.path(), "");
    auto d = load_project_config(def.path());
    ASSERT_TRUE(d);
    EXPECT_TRUE(d->build.syscalls.empty());

    TempDir bad;
    write_toml(bad.path(), "[build]\nsyscalls = \"stubs\"\n");
    EXPECT_FALSE(load_project_config(bad.path()));
}

TEST(AppBuildSyscalls, PlanDefaultsToNosysAndRenders) {
    TempDir td;
    touch(td.path(), "src/main.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->syscalls, "nosys");
    EXPECT_NE(render_app_cmake(*plan).find("set(NAV_APP_SYSCALLS \"nosys\")"), std::string::npos);
    cfg.build.syscalls = "none";
    plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_NE(render_app_cmake(*plan).find("set(NAV_APP_SYSCALLS \"none\")"), std::string::npos);
}

TEST(AppBuildTemplate, SystemLibsComeAfterDependencyArchives) {
    const std::string t = render_app_cmakelists("p");
    // One group: hal, every dependency archive, then the system libraries
    // (-lc -lm, libs, -lnosys). They must share the group so ld rescans the
    // dependency archives when libc pulls in _write/_sbrk.
    const auto hal = t.find("-Wl,--whole-archive hal");
    ASSERT_NE(hal, std::string::npos);
    const auto deps = t.find("${NAV_DEP_TARGETS}", hal);
    const auto sys = t.find("${NAV_APP_SYS_LIBS}", hal);
    ASSERT_NE(deps, std::string::npos);
    ASSERT_NE(sys, std::string::npos);
    EXPECT_LT(deps, sys);
    const auto end = t.find("-Wl,--end-group", hal);
    ASSERT_NE(end, std::string::npos);
    EXPECT_LT(sys, end);
    EXPECT_EQ(t.find("-Wl,--start-group", hal), std::string::npos);
    // -lnosys sits after the group, so it never beats a dependency's syscalls.
    const auto stubs = t.find("${NAV_APP_STUB_LIBS}", hal);
    ASSERT_NE(stubs, std::string::npos);
    EXPECT_LT(end, stubs);
    // -lnosys is only added for syscalls != "none".
    EXPECT_NE(t.find("NOT NAV_APP_SYSCALLS STREQUAL \"none\""), std::string::npos);
}

// --- deps_opt ---------------------------------------------------------------

TEST(AppBuildDepsOpt, ParsesAndValidates) {
    TempDir td;
    write_toml(td.path(), "[build]\ndeps_opt = \"Os\"\n");
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_EQ(cfg->build.deps_opt, "Os");

    TempDir def;
    write_toml(def.path(), "");
    auto d = load_project_config(def.path());
    ASSERT_TRUE(d);
    EXPECT_TRUE(d->build.deps_opt.empty());

    for (const char* bad : {"O4", "-Os", "os", ""}) {
        TempDir b;
        write_toml(b.path(), std::string("[build]\ndeps_opt = \"") + bad + "\"\n");
        EXPECT_FALSE(load_project_config(b.path())) << bad;
    }
    for (const char* ok : {"O0", "O1", "O2", "O3", "Os", "Og", "Oz"}) {
        TempDir g;
        write_toml(g.path(), std::string("[build]\ndeps_opt = \"") + ok + "\"\n");
        EXPECT_TRUE(load_project_config(g.path())) << ok;
    }
}

TEST(AppBuildDepsOpt, PlanAndRender) {
    TempDir td;
    touch(td.path(), "src/main.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_NE(render_app_cmake(*plan).find("set(NAV_APP_DEPS_OPT)"), std::string::npos);
    cfg.build.deps_opt = "Oz";
    plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->deps_opt, "Oz");
    EXPECT_NE(render_app_cmake(*plan).find("set(NAV_APP_DEPS_OPT \"Oz\")"), std::string::npos);
}

TEST(AppBuildDepsOpt, TemplateAppliesPerTarget) {
    const std::string t = render_app_cmakelists("p");
    EXPECT_NE(t.find("target_compile_options(${_nav_t} PRIVATE \"-${NAV_APP_DEPS_OPT}\")"), std::string::npos);
    // It runs after nav-app.cmake is read, i.e. after every library target exists.
    EXPECT_LT(t.find("include(\"${CMAKE_BINARY_DIR}/nav-app.cmake\")"), t.find("if(NAV_APP_DEPS_OPT)"));
}

// --- SUBMODULE define scoping -----------------------------------------------

TEST(AppBuildTemplate, NavhalDefinesAreAppliedToNavhalTargetsOnly) {
    const std::string t = render_app_cmakelists("p");
    EXPECT_NE(t.find("target_compile_definitions(${_nav_t} PRIVATE ${NAV_NAVHAL_DEFINES})"), std::string::npos);
    // No directory-wide define in the template (it would reach the app and libraries).
    EXPECT_EQ(t.find("add_compile_definitions"), std::string::npos);
    EXPECT_NE(t.find("set(NAV_TEMPLATE_SCOPED_DEFINES ON)"), std::string::npos);
    // The define is applied right after NavHAL is added, before any library.
    EXPECT_LT(t.find("add_subdirectory(extern/NavHAL)"), t.find("PRIVATE ${NAV_NAVHAL_DEFINES}"));
    EXPECT_LT(t.find("PRIVATE ${NAV_NAVHAL_DEFINES}"), t.find("nav-deps-libs.cmake"));
}

// --- linker_fragment_position ------------------------------------------------

TEST(AppBuildFragmentPosition, ParsesAndValidates) {
    TempDir td;
    write_toml(td.path(), "[build]\nlinker_fragment_position = \"after\"\n");
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_EQ(cfg->build.linker_fragment_position, "after");

    TempDir def;
    write_toml(def.path(), "");
    auto d = load_project_config(def.path());
    ASSERT_TRUE(d);
    EXPECT_EQ(d->build.linker_fragment_position, "before");

    TempDir bad;
    write_toml(bad.path(), "[build]\nlinker_fragment_position = \"middle\"\n");
    EXPECT_FALSE(load_project_config(bad.path()));
}

TEST(AppBuildFragmentPosition, PlanAndRender) {
    TempDir td;
    touch(td.path(), "src/main.c");
    ProjectConfig cfg;
    cfg.root = td.path();
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_NE(render_app_cmake(*plan).find("set(NAV_APP_LINKER_FRAGMENT_POSITION \"before\")"), std::string::npos);
    cfg.build.linker_fragment_position = "after";
    plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_NE(render_app_cmake(*plan).find("set(NAV_APP_LINKER_FRAGMENT_POSITION \"after\")"), std::string::npos);
}

TEST(AppBuildFragmentPosition, TemplatePlacesFragmentAroundBoardScript) {
    const std::string t = render_app_cmakelists("p");
    const auto before = t.find("AND NOT NAV_APP_LINKER_FRAGMENT_POSITION STREQUAL \"after\"");
    const auto board = t.find("list(APPEND NAV_APP_LINK_OPTIONS \"SHELL:-T \\\"${NAV_LINKER_SCRIPT}\\\"\")");
    const auto after = t.find("AND NAV_APP_LINKER_FRAGMENT_POSITION STREQUAL \"after\"");
    ASSERT_NE(before, std::string::npos);
    ASSERT_NE(board, std::string::npos);
    ASSERT_NE(after, std::string::npos);
    EXPECT_LT(before, board);
    EXPECT_LT(board, after);
}

TEST(AppBuildTemplate, DetectsTemplateWithLinkSettings) {
    EXPECT_TRUE(cmakelists_supports_dep_opts(render_app_cmakelists("p")));
    EXPECT_FALSE(cmakelists_supports_dep_opts("include(\"nav-app.cmake\")"));
}

TEST(AppBuildTemplate, OlderTemplateWithoutAliasHandlingIsStale) {
    std::string t = render_app_cmakelists("p");
    ASSERT_TRUE(cmakelists_supports_dep_opts(t));
    const auto pos = t.find("NAV_ALIAS_SAFE");
    ASSERT_NE(pos, std::string::npos);
    t.erase(pos, std::string("NAV_ALIAS_SAFE").size());
    EXPECT_FALSE(cmakelists_supports_dep_opts(t));
}

TEST(AppBuildTemplate, ResolvesAliasTargetsBeforeMutatingThem) {
    const std::string t = render_app_cmakelists("p");
    // Both loops that mutate dependency targets resolve ALIASED_TARGET first.
    const auto walk = t.find("set(_nav_todo hal");
    ASSERT_NE(walk, std::string::npos);
    const auto walk_alias = t.find("ALIASED_TARGET", walk);
    const auto walk_set = t.find("set_property(TARGET ${_nav_t}", walk);
    ASSERT_NE(walk_alias, std::string::npos);
    ASSERT_NE(walk_set, std::string::npos);
    EXPECT_LT(walk_alias, walk_set);

    const auto opt = t.find("if(NAV_APP_DEPS_OPT)");
    ASSERT_NE(opt, std::string::npos);
    const auto opt_alias = t.find("ALIASED_TARGET", opt);
    const auto opt_set = t.find("target_compile_options(${_nav_t}", opt);
    ASSERT_NE(opt_alias, std::string::npos);
    ASSERT_NE(opt_set, std::string::npos);
    EXPECT_LT(opt_alias, opt_set);
}
