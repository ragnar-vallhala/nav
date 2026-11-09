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
