#include "nav/core/appbuild.hpp"
#include "nav/core/config.hpp"
#include "nav/core/libdeps.hpp"
#include "nav/core/variant.hpp"

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
        path_ = fs::temp_directory_path() / ("nav_var_" + std::to_string(::getpid()) + "_" +
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

const char* kToml = R"(
[project]
name = "vayu"
[target]
board = "b"
[build]
sources = ["src/**/*.c"]
defines = ["BASE=1"]

[variant.hwtest]
sources_add    = ["tests/onboard/**/*.c"]
sources_remove = ["src/main.c"]
defines        = ["VAYU_HW_TEST=1"]
output         = "hwtest"
kconfig        = ".config.hwtest"

[variant.hwtest.dependencies.navlink]
options = ["NAVLINK_PROFILE=test"]

[variant.bare]
defines = ["BARE=1"]
)";

ProjectConfig load(const TempDir& td, const std::string& toml = kToml) {
    std::ofstream(td.path() / "nav.toml") << toml;
    auto cfg = load_project_config(td.path());
    EXPECT_TRUE(cfg);
    return cfg ? *cfg : ProjectConfig{};
}

bool has_source(const AppBuildPlan& p, const TempDir& td, const std::string& rel) {
    const std::string abs = (td.path() / rel).generic_string();
    return std::find(p.sources.begin(), p.sources.end(), abs) != p.sources.end();
}

} // namespace

TEST(Variant, ParsesEveryKey) {
    TempDir td;
    const auto cfg = load(td);
    ASSERT_EQ(cfg.variants.size(), 2u);
    const Variant* v = find_variant(cfg, "hwtest");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->sources_add, std::vector<std::string>{"tests/onboard/**/*.c"});
    EXPECT_EQ(v->sources_remove, std::vector<std::string>{"src/main.c"});
    EXPECT_EQ(v->defines, std::vector<std::string>{"VAYU_HW_TEST=1"});
    EXPECT_EQ(v->output, "hwtest");
    EXPECT_EQ(v->kconfig, ".config.hwtest");
    ASSERT_EQ(v->dep_options.count("navlink"), 1u);
    EXPECT_EQ(v->dep_options.at("navlink"), std::vector<std::string>{"NAVLINK_PROFILE=test"});
    EXPECT_EQ(find_variant(cfg, "bare")->output, "");
    EXPECT_EQ(find_variant(cfg, "nope"), nullptr);
}

TEST(Variant, RejectsUnknownKeysAndBadNames) {
    TempDir td;
    std::ofstream(td.path() / "nav.toml") << "[project]\nname=\"a\"\n[variant.x]\nsource_add = [\"a.c\"]\n";
    EXPECT_FALSE(load_project_config(td.path()));
    std::ofstream(td.path() / "nav.toml") << "[project]\nname=\"a\"\n[variant.deps]\ndefines = [\"A\"]\n";
    EXPECT_FALSE(load_project_config(td.path()));
    std::ofstream(td.path() / "nav.toml") << "[project]\nname=\"a\"\n[variant.\"a/b\"]\ndefines = [\"A\"]\n";
    EXPECT_FALSE(load_project_config(td.path()));
}

TEST(Variant, NoVariantsByDefault) {
    TempDir td;
    const auto cfg = load(td, "[project]\nname=\"a\"\n");
    EXPECT_TRUE(cfg.variants.empty());
    EXPECT_EQ(variant_names(cfg), "none");
}

TEST(Variant, UnknownNameErrorsAndListsKnownOnes) {
    TempDir td;
    const auto cfg = load(td);
    std::string err;
    EXPECT_FALSE(apply_variant(cfg, "nope", err));
    EXPECT_NE(err.find("unknown variant 'nope'"), std::string::npos) << err;
    EXPECT_NE(err.find("bare, hwtest"), std::string::npos) << err;
}

TEST(Variant, EmptyNameLeavesTheConfigAlone) {
    TempDir td;
    const auto cfg = load(td);
    std::string err;
    auto eff = apply_variant(cfg, "", err);
    ASSERT_TRUE(eff);
    EXPECT_EQ(eff->build.defines, cfg.build.defines);
    EXPECT_EQ(eff->build.output, "");
}

TEST(Variant, SourcesAddAndRemoveGoThroughTheBuildPlanner) {
    TempDir td;
    touch(td.path(), "src/main.c");
    touch(td.path(), "src/util.c");
    touch(td.path(), "tests/onboard/a.c");
    touch(td.path(), "tests/onboard/sub/b.c");
    const auto cfg = load(td);
    std::string err;

    auto base = plan_app_build(cfg, err);
    ASSERT_TRUE(base) << err;
    EXPECT_TRUE(has_source(*base, td, "src/main.c"));
    EXPECT_FALSE(has_source(*base, td, "tests/onboard/a.c"));
    EXPECT_EQ(base->defines, std::vector<std::string>{"BASE=1"});
    EXPECT_EQ(base->output, "");

    auto eff = apply_variant(cfg, "hwtest", err);
    ASSERT_TRUE(eff) << err;
    auto hw = plan_app_build(*eff, err);
    ASSERT_TRUE(hw) << err;
    EXPECT_FALSE(has_source(*hw, td, "src/main.c"));
    EXPECT_TRUE(has_source(*hw, td, "src/util.c"));
    EXPECT_TRUE(has_source(*hw, td, "tests/onboard/a.c"));
    EXPECT_TRUE(has_source(*hw, td, "tests/onboard/sub/b.c"));
    EXPECT_EQ(hw->defines, (std::vector<std::string>{"BASE=1", "VAYU_HW_TEST=1"}));
    EXPECT_EQ(hw->output, "hwtest");
    EXPECT_NE(render_app_cmake(*hw).find("set(NAV_APP_OUTPUT \"hwtest\")"), std::string::npos);
    EXPECT_NE(render_app_cmake(*base).find("set(NAV_APP_OUTPUT)"), std::string::npos);
}

TEST(Variant, AddedGlobThatMatchesNothingIsAnError) {
    TempDir td;
    touch(td.path(), "src/main.c");
    const auto cfg = load(td);
    std::string err;
    auto eff = apply_variant(cfg, "hwtest", err);
    ASSERT_TRUE(eff);
    EXPECT_FALSE(plan_app_build(*eff, err));
    EXPECT_NE(err.find("tests/onboard"), std::string::npos) << err;
}

TEST(Variant, VariantOnAProjectWithoutBuildKeysStartsFromTheDefaultSource) {
    TempDir td;
    touch(td.path(), "src/main.c");
    touch(td.path(), "t/x.c");
    const auto cfg = load(td, "[project]\nname=\"a\"\n[variant.t]\nsources_add=[\"t/*.c\"]\n");
    std::string err;
    auto eff = apply_variant(cfg, "t", err);
    ASSERT_TRUE(eff);
    EXPECT_TRUE(eff->build.declared);
    auto plan = plan_app_build(*eff, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_EQ(plan->sources.size(), 2u);
}

TEST(Variant, BuildDirsAreSeparate) {
    EXPECT_EQ(variant_build_dir(""), fs::path("build"));
    EXPECT_EQ(variant_build_dir("hwtest"), fs::path("build") / "hwtest");
    EXPECT_NE(variant_build_dir("hwtest"), variant_build_dir(""));
}

TEST(Variant, FirmwareNameAndConfigPath) {
    TempDir td;
    const auto cfg = load(td);
    EXPECT_EQ(firmware_name(cfg, ""), "vayu");
    EXPECT_EQ(firmware_name(cfg, "hwtest"), "hwtest");
    EXPECT_EQ(firmware_name(cfg, "bare"), "vayu");
    EXPECT_EQ(variant_config_path(cfg, ""), td.path() / ".config");
    EXPECT_EQ(variant_config_path(cfg, "hwtest"), td.path() / ".config.hwtest");
    EXPECT_EQ(variant_config_path(cfg, "bare"), td.path() / ".config");
}

TEST(Variant, OptionsMergeOverTheBaseByKey) {
    EXPECT_EQ(merge_options({"A=1", "NAVLINK_PROFILE=flight", "B"}, {"NAVLINK_PROFILE=test", "C=3"}),
              (std::vector<std::string>{"A=1", "NAVLINK_PROFILE=test", "B", "C=3"}));
}

namespace {
ResolvedLib lib(const std::string& name, std::vector<std::string> opts) {
    ResolvedLib l;
    l.name = name;
    l.options = std::move(opts);
    return l;
}
} // namespace

TEST(Variant, DependencyOptionsAreAppliedPerVariantAndShowUpInTheCmake) {
    TempDir td;
    const auto cfg = load(td);
    const std::vector<ResolvedLib> base = {lib("navlink", {"NAVLINK_PROFILE=flight", "X=1"}), lib("other", {})};

    auto libs = base;
    std::string err;
    ASSERT_TRUE(apply_variant_options(libs, *find_variant(cfg, "hwtest"), err)) << err;
    EXPECT_EQ(libs[0].options, (std::vector<std::string>{"NAVLINK_PROFILE=test", "X=1"}));
    EXPECT_TRUE(libs[1].options.empty());

    const std::string variant_cmake = render_libdeps_cmake(libs, {"navlink"}, false);
    const std::string base_cmake = render_libdeps_cmake(base, {"navlink"}, false);
    EXPECT_NE(variant_cmake.find("set(NAVLINK_PROFILE \"test\" CACHE STRING"), std::string::npos) << variant_cmake;
    EXPECT_EQ(variant_cmake.find("\"flight\""), std::string::npos);
    EXPECT_NE(base_cmake.find("set(NAVLINK_PROFILE \"flight\""), std::string::npos);

    // A variant that touches no dependency leaves them alone.
    auto untouched = base;
    ASSERT_TRUE(apply_variant_options(untouched, *find_variant(cfg, "bare"), err));
    EXPECT_EQ(untouched[0].options, base[0].options);
}

TEST(Variant, OptionsForAnUnknownDependencyAreAnError) {
    TempDir td;
    const auto cfg = load(td);
    std::vector<ResolvedLib> libs = {lib("other", {})};
    std::string err;
    EXPECT_FALSE(apply_variant_options(libs, *find_variant(cfg, "hwtest"), err));
    EXPECT_NE(err.find("navlink"), std::string::npos) << err;
    EXPECT_NE(err.find("other"), std::string::npos);
}

TEST(Variant, OldCmakeListsIsRecognised) {
    EXPECT_FALSE(cmakelists_supports_variants("include(nav-app.cmake)\nadd_executable(x)\n"));
    EXPECT_TRUE(cmakelists_supports_variants(render_app_cmakelists("p")));
}

// ---- board- and Kconfig-conditional settings, full variants, [navhal] exclude

namespace {
const char* kCondToml = R"(
[project]
name = "vayu"
[target]
board = "navixdev"
[build]
sources = ["src/**/*.c"]
exclude = ["src/sensors/**"]
defines = ["BASE=1"]

[build.board.navixdev]
sources = ["src/sensors/dev/*.c"]
include_dirs = ["board/navixdev"]
defines = ["DEV=1"]
c_flags = ["-DDEVFLAG"]
libs = ["m"]

[build.board.navixsmf401re]
sources = ["src/sensors/f4/*.c"]
include_dirs = ["board/navixsmf401re"]

[build.kconfig.USE_BARO]
sources = ["opt/baro.c"]
defines = ["HAS_BARO=1"]
[[build.kconfig.USE_BARO.file_flags]]
match = ["opt/baro.c"]
flags = ["-O3"]

[variant.hwtest]
include_dirs = ["tests/onboard"]
exclude = ["src/sensors/dev/imu.c"]
c_flags = ["--coverage"]
libs = ["gcov"]
[variant.hwtest.board.navixdev]
defines = ["HWTEST_DEV=1"]
[variant.hwtest.kconfig_when.USE_BARO]
defines = ["HWTEST_BARO=1"]
)";

void cond_tree(const TempDir& td) {
    for (auto f : {"src/main.c", "src/sensors/dev/imu.c", "src/sensors/dev/mag.c", "src/sensors/f4/imu.c",
                   "opt/baro.c"})
        touch(td.path(), f);
}
}  // namespace

TEST(BuildConditional, BoardTableAppliesOnlyToItsBoard) {
    TempDir td;
    cond_tree(td);
    auto cfg = load(td, kCondToml);
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_TRUE(has_source(*plan, td, "src/sensors/dev/imu.c"));
    EXPECT_FALSE(has_source(*plan, td, "src/sensors/f4/imu.c"));
    EXPECT_NE(std::find(plan->include_dirs.begin(), plan->include_dirs.end(),
                        (td.path() / "board/navixdev").generic_string()),
              plan->include_dirs.end());
    EXPECT_EQ(plan->defines, (std::vector<std::string>{"BASE=1", "DEV=1"}));
    EXPECT_EQ(plan->c_flags, std::vector<std::string>{"-DDEVFLAG"});
    EXPECT_EQ(plan->libs, std::vector<std::string>{"m"});

    cfg.target_board = "navixsmf401re";
    plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_TRUE(has_source(*plan, td, "src/sensors/f4/imu.c"));
    EXPECT_FALSE(has_source(*plan, td, "src/sensors/dev/imu.c"));
    EXPECT_EQ(plan->defines, std::vector<std::string>{"BASE=1"});
}

// An exclude drops sources of its own layer and earlier ones, not of later layers.
TEST(BuildConditional, BaseExcludeDoesNotRemoveWhatABoardTableAdds) {
    TempDir td;
    cond_tree(td);
    auto cfg = load(td, kCondToml);
    cfg.build.exclude = {"src/sensors/dev/**"};
    std::string err;
    auto plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_TRUE(has_source(*plan, td, "src/sensors/dev/imu.c"));
    cfg.build.board["navixdev"].exclude = {"src/sensors/dev/mag.c"};
    plan = plan_app_build(cfg, err);
    ASSERT_TRUE(plan) << err;
    EXPECT_FALSE(has_source(*plan, td, "src/sensors/dev/mag.c"));
}

TEST(BuildConditional, KconfigTableFollowsTheResolvedConfig) {
    TempDir td;
    cond_tree(td);
    const auto cfg = load(td, kCondToml);
    std::string err;
    std::map<std::string, std::string> off{{"CONFIG_OTHER", "y"}, {"CONFIG_USE_BARO", "n"}};
    auto plan = plan_app_build(cfg, err, &off);
    ASSERT_TRUE(plan) << err;
    EXPECT_FALSE(has_source(*plan, td, "opt/baro.c"));

    std::map<std::string, std::string> on{{"CONFIG_USE_BARO", "y"}};
    plan = plan_app_build(cfg, err, &on);
    ASSERT_TRUE(plan) << err;
    EXPECT_TRUE(has_source(*plan, td, "opt/baro.c"));
    EXPECT_NE(std::find(plan->defines.begin(), plan->defines.end(), "HAS_BARO=1"), plan->defines.end());
    ASSERT_EQ(plan->file_flags.size(), 1u);
    EXPECT_EQ(plan->file_flags[0].flags, std::vector<std::string>{"-O3"});

    // A non-bool symbol that is set counts too.
    std::map<std::string, std::string> valued{{"CONFIG_USE_BARO", "\"x\""}};
    plan = plan_app_build(cfg, err, &valued);
    ASSERT_TRUE(plan) << err;
    EXPECT_TRUE(has_source(*plan, td, "opt/baro.c"));

    // No config at all: nothing is set.
    plan = plan_app_build(cfg, err, nullptr);
    ASSERT_TRUE(plan) << err;
    EXPECT_FALSE(has_source(*plan, td, "opt/baro.c"));
}

TEST(BuildConditional, EmptyMatchInAConditionalTableIsAnError) {
    TempDir td;
    cond_tree(td);
    const auto cfg = load(td, kCondToml);
    std::map<std::string, std::string> on{{"CONFIG_USE_BARO", "y"}};
    fs::remove(td.path() / "opt/baro.c");
    std::string err;
    EXPECT_FALSE(plan_app_build(cfg, err, &on));
    EXPECT_NE(err.find("[build.kconfig.USE_BARO].sources"), std::string::npos) << err;
}

TEST(BuildConditional, UnknownKeysAreErrors) {
    TempDir td;
    for (const char* t : {"[build.board.x]\nsource = [\"a.c\"]\n", "[build.kconfig.X]\nlibc = \"newlib\"\n",
                          "[build.board.x]\nc_standard = 11\n"}) {
        std::ofstream(td.path() / "nav.toml") << "[project]\nname=\"a\"\n" << t;
        EXPECT_FALSE(load_project_config(td.path())) << t;
    }
}

TEST(BuildConditional, TablesCountAsDeclaredBuildKeys) {
    TempDir td;
    const auto cfg = load(td, "[project]\nname=\"a\"\n[build.board.x]\ndefines=[\"A\"]\n");
    EXPECT_TRUE(cfg.build.declared);
}

TEST(VariantFull, ParsesEveryBuildKeyAndSubTables) {
    TempDir td;
    const auto cfg = load(td, kCondToml);
    const Variant* v = find_variant(cfg, "hwtest");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->add.include_dirs, std::vector<std::string>{"tests/onboard"});
    EXPECT_EQ(v->add.c_flags, std::vector<std::string>{"--coverage"});
    EXPECT_EQ(v->add.libs, std::vector<std::string>{"gcov"});
    EXPECT_EQ(v->board.at("navixdev").defines, std::vector<std::string>{"HWTEST_DEV=1"});
    EXPECT_EQ(v->kconfig_when.at("USE_BARO").defines, std::vector<std::string>{"HWTEST_BARO=1"});
}

TEST(VariantFull, EverythingIsAdditiveOnTheBase) {
    TempDir td;
    cond_tree(td);
    const auto cfg = load(td, kCondToml);
    std::string err;
    auto eff = apply_variant(cfg, "hwtest", err);
    ASSERT_TRUE(eff) << err;
    std::map<std::string, std::string> on{{"CONFIG_USE_BARO", "y"}};
    auto plan = plan_app_build(*eff, err, &on);
    ASSERT_TRUE(plan) << err;
    EXPECT_FALSE(has_source(*plan, td, "src/sensors/dev/imu.c"));   // variant exclude
    EXPECT_TRUE(has_source(*plan, td, "src/sensors/dev/mag.c"));    // base board source stays
    EXPECT_EQ(plan->c_flags, (std::vector<std::string>{"-DDEVFLAG", "--coverage"}));
    EXPECT_EQ(plan->libs, (std::vector<std::string>{"m", "gcov"}));
    EXPECT_NE(std::find(plan->include_dirs.begin(), plan->include_dirs.end(),
                        (td.path() / "tests/onboard").generic_string()),
              plan->include_dirs.end());
    EXPECT_EQ(plan->defines,
              (std::vector<std::string>{"BASE=1", "DEV=1", "HAS_BARO=1", "HWTEST_DEV=1", "HWTEST_BARO=1"}));
    // The base config is untouched.
    EXPECT_TRUE(cfg.build.libs.empty());
}

TEST(VariantFull, KconfigStringStillNamesTheAlternativeConfig) {
    TempDir td;
    const auto cfg = load(td, "[project]\nname=\"a\"\n[variant.v]\nkconfig = \".config.v\"\nlibs=[\"m\"]\n");
    EXPECT_EQ(find_variant(cfg, "v")->kconfig, ".config.v");
    EXPECT_EQ(find_variant(cfg, "v")->add.libs, std::vector<std::string>{"m"});
}

TEST(NavhalExclude, ParsesAndMarksBuildDeclared) {
    TempDir td;
    const auto cfg = load(td, "[project]\nname=\"a\"\n[navhal]\nref=\"v1\"\nexclude=[\"src/utils/freestanding.c\"]\n");
    EXPECT_EQ(cfg.navhal_exclude, std::vector<std::string>{"src/utils/freestanding.c"});
    EXPECT_EQ(cfg.navhal_ref, "v1");
    EXPECT_TRUE(cfg.build.declared);
}

TEST(NavhalExclude, MissingFileIsAClearError) {
    TempDir td;
    touch(td.path(), "hal/src/utils/freestanding.c");
    ProjectConfig cfg;
    cfg.navhal_exclude = {"src/utils/freestanding.c"};
    std::string err;
    auto ok = plan_navhal_exclude(cfg, td.path() / "hal", err);
    ASSERT_TRUE(ok) << err;
    EXPECT_EQ(ok->at(0), (td.path() / "hal/src/utils/freestanding.c").generic_string());

    cfg.navhal_exclude = {"src/utils/nope.c"};
    EXPECT_FALSE(plan_navhal_exclude(cfg, td.path() / "hal", err));
    EXPECT_NE(err.find("src/utils/nope.c"), std::string::npos);
    EXPECT_NE(err.find("does not exist in NavHAL"), std::string::npos);
    cfg.navhal_exclude = {"../x.c"};
    EXPECT_FALSE(plan_navhal_exclude(cfg, td.path() / "hal", err));
}

TEST(NavhalExclude, RendersAFilterOfTheHalTargets) {
    AppBuildPlan p;
    p.navhal_exclude = {"/n/src/utils/freestanding.c"};
    const std::string out = render_app_cmake(p);
    EXPECT_NE(out.find("\"/n/src/utils/freestanding.c\""), std::string::npos) << out;
    EXPECT_NE(out.find("get_target_property(_srcs ${_t} SOURCES)"), std::string::npos);
    EXPECT_NE(out.find("set_property(TARGET ${_t} PROPERTY SOURCES"), std::string::npos);
    EXPECT_EQ(render_app_cmake(AppBuildPlan{}).find("NAV_NAVHAL_EXCLUDE"), std::string::npos);
}
