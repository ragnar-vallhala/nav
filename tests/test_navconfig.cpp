#include "nav/core/navconfig.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_navcfg_" + std::to_string(::getpid())
                                           + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void write_file(const fs::path& p, const std::string& c) { std::ofstream(p) << c; }

} // namespace

TEST(NavConfig, ParseSkipsCommentsAndUnset) {
    TempDir td;
    write_file(td.path() / ".config", R"(
# Target
CONFIG_DRV_UART=y
CONFIG_BOARD="nucleo_f401re"
# CONFIG_TEST is not set

CONFIG_NUM=42
)");
    auto m = nav::core::parse_kconfig(td.path() / ".config");
    EXPECT_EQ(m.at("CONFIG_DRV_UART"), "y");
    EXPECT_EQ(m.at("CONFIG_BOARD"), "\"nucleo_f401re\"");
    EXPECT_EQ(m.at("CONFIG_NUM"), "42");
    EXPECT_EQ(m.count("CONFIG_TEST"), 0u); // "is not set" => absent
}

TEST(NavConfig, FindPrefersDotConfigThenNavhalConfig) {
    TempDir a, b, c;
    write_file(a.path() / ".config", "X=y\n");
    write_file(a.path() / "navhal.config", "X=y\n");
    EXPECT_EQ(nav::core::find_navhal_config(a.path())->filename(), ".config");

    write_file(b.path() / "navhal.config", "X=y\n");
    EXPECT_EQ(nav::core::find_navhal_config(b.path())->filename(), "navhal.config");

    EXPECT_FALSE(nav::core::find_navhal_config(c.path()).has_value());
}

TEST(NavConfig, UnmetReportsMissingAndDifferingValues) {
    std::map<std::string, std::string> have = {
        {"CONFIG_DRV_UART", "y"},
        {"CONFIG_BOARD", "\"nucleo_f401re\""},
    };
    std::map<std::string, std::string> required = {
        {"CONFIG_DRV_UART", "y"},        // satisfied
        {"CONFIG_DRV_UART_DMA", "y"},    // missing
        {"CONFIG_BOARD", "\"pico\""},    // differing value
    };
    auto unmet = nav::core::unmet_requirements(have, required);
    // Sorted: CONFIG_BOARD="pico" then CONFIG_DRV_UART_DMA=y
    ASSERT_EQ(unmet.size(), 2u);
    EXPECT_EQ(unmet[0], "CONFIG_BOARD=\"pico\"");
    EXPECT_EQ(unmet[1], "CONFIG_DRV_UART_DMA=y");
}

TEST(NavConfig, UnmetEmptyWhenSatisfied) {
    std::map<std::string, std::string> have = {{"A", "y"}, {"B", "1"}, {"C", "z"}};
    std::map<std::string, std::string> required = {{"A", "y"}, {"B", "1"}};
    EXPECT_TRUE(nav::core::unmet_requirements(have, required).empty());
}

TEST(NavConfig, DiffSeparatesMissingFromConflicts) {
    std::map<std::string, std::string> have = {
        {"CONFIG_DRV_UART", "y"},
        {"CONFIG_BOARD", "\"nucleo_f401re\""},
    };
    std::map<std::string, std::string> required = {
        {"CONFIG_DRV_UART", "y"},        // satisfied
        {"CONFIG_DRV_UART_DMA", "y"},    // missing -> append-safe
        {"CONFIG_FAMILY", "\"stm32f4\""},// missing -> append-safe
        {"CONFIG_BOARD", "\"pico\""},    // conflict: have differs
    };
    auto d = nav::core::diff_requirements(have, required);

    // Missing keys are exactly the two absent ones, sorted, in KEY=VALUE form.
    ASSERT_EQ(d.missing.size(), 2u);
    EXPECT_EQ(d.missing[0], "CONFIG_DRV_UART_DMA=y");
    EXPECT_EQ(d.missing[1], "CONFIG_FAMILY=\"stm32f4\"");

    // The satisfied key is neither missing nor a conflict; the differing key is.
    ASSERT_EQ(d.conflicts.size(), 1u);
    EXPECT_EQ(d.conflicts[0].key, "CONFIG_BOARD");
    EXPECT_EQ(d.conflicts[0].have, "\"nucleo_f401re\"");
    EXPECT_EQ(d.conflicts[0].need, "\"pico\"");
}

TEST(NavConfig, DiffEmptyWhenSatisfied) {
    std::map<std::string, std::string> have = {{"A", "y"}, {"B", "1"}, {"C", "z"}};
    std::map<std::string, std::string> required = {{"A", "y"}, {"B", "1"}};
    auto d = nav::core::diff_requirements(have, required);
    EXPECT_TRUE(d.missing.empty());
    EXPECT_TRUE(d.conflicts.empty());
}

TEST(NavConfig, ApplyReplacesInPlaceAndAppendsNew) {
    const std::string text = "CONFIG_A=y\n# CONFIG_B is not set\nCONFIG_C=1\n";
    const std::string out = nav::core::apply_config_assignments(
        text, {"CONFIG_B=y", "CONFIG_C=2", "CONFIG_D=y"}, "# added");
    EXPECT_EQ(out, "CONFIG_A=y\nCONFIG_B=y\nCONFIG_C=2\n\n# added\nCONFIG_D=y\n");
}

TEST(NavConfig, ApplyHandlesNotSetAndDropsDuplicates) {
    const std::string text = "CONFIG_A=y\nCONFIG_B=y\nCONFIG_A=n\n";
    const std::string out = nav::core::apply_config_assignments(text, {"# CONFIG_A is not set"});
    EXPECT_EQ(out, "# CONFIG_A is not set\nCONFIG_B=y\n");
}

TEST(NavConfig, ApplyNoChangeAndRepeatedCallsAreStable) {
    const std::string text = "CONFIG_A=y\n";
    EXPECT_EQ(nav::core::apply_config_assignments(text, {"CONFIG_A=y"}), text);
    const std::string once = nav::core::apply_config_assignments(text, {"CONFIG_X=1"}, "# h");
    EXPECT_EQ(nav::core::apply_config_assignments(once, {"CONFIG_X=1"}, "# h"), once);
}

TEST(NavConfig, UpdateConfigFileRewritesFile) {
    TempDir td;
    const fs::path p = td.path() / ".config";
    write_file(p, "CONFIG_A=y\n# CONFIG_B is not set\n");
    ASSERT_TRUE(nav::core::update_config_file(p, {"CONFIG_B=y", "CONFIG_B=n"}));
    std::ifstream in(p);
    std::string all((std::istreambuf_iterator<char>(in)), {});
    EXPECT_EQ(all, "CONFIG_A=y\nCONFIG_B=n\n");
}

TEST(NavConfig, ParseUnsetFindsOnlyExplicitNotSetLines) {
    TempDir td;
    const fs::path p = td.path() / ".config";
    write_file(p, "CONFIG_A=y\n# CONFIG_B is not set\n# a comment\n  # CONFIG_C is not set\n");
    auto u = nav::core::parse_kconfig_unset(p);
    EXPECT_EQ(u, (std::set<std::string>{"CONFIG_B", "CONFIG_C"}));
}

TEST(NavConfig, MergeAddsMissingAndAppWinsOnExplicitChoices) {
    std::map<std::string, std::string> have = {{"CONFIG_A", "y"}, {"CONFIG_V", "2"}};
    std::set<std::string> unset = {"CONFIG_OFF"};
    std::map<std::string, std::string> required = {
        {"CONFIG_A", "y"}, {"CONFIG_NEW", "y"}, {"CONFIG_OFF", "y"}, {"CONFIG_V", "1"}};
    std::vector<nav::core::RequirementOverride> ov;
    auto add = nav::core::merge_requirements(have, unset, required, "libx", ov);
    EXPECT_EQ(add, (std::vector<std::string>{"CONFIG_NEW=y"}));
    ASSERT_EQ(ov.size(), 2u);
    EXPECT_EQ(ov[0].dep, "libx");
    EXPECT_EQ(ov[0].key, "CONFIG_OFF");
    EXPECT_EQ(ov[0].have, "not set");
    EXPECT_EQ(ov[1].key, "CONFIG_V");
    EXPECT_EQ(ov[1].need, "1");
    EXPECT_EQ(ov[1].have, "2");
    // A later dependency sees what an earlier one added.
    std::vector<nav::core::RequirementOverride> ov2;
    EXPECT_TRUE(nav::core::merge_requirements(have, unset, {{"CONFIG_NEW", "y"}}, "liby", ov2).empty());
    EXPECT_TRUE(ov2.empty());
}

TEST(NavConfig, EffectiveConfigLeavesAppTextIntact) {
    // What `nav build` writes to the build dir: app text + additions; the app's
    // explicit "not set" line stays when its requirement is overridden.
    const std::string text = "CONFIG_A=y\n# CONFIG_OFF is not set\n";
    const std::string eff = nav::core::apply_config_assignments(text, {"CONFIG_NEW=y"}, "# nav");
    EXPECT_EQ(eff, "CONFIG_A=y\n# CONFIG_OFF is not set\n\n# nav\nCONFIG_NEW=y\n");
}
