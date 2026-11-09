#include "nav/core/board_config.hpp"

#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nav::core;
using nav::test::ExecutedCall;
using nav::test::MockExecutionContext;

namespace {

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string slurp(const fs::path& p) {
    std::ifstream f(p);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A NavHAL tree laid out like the real one: arch / vendor / family / board
// choice members, board identity Kconfigs, and one defconfig per board.
class BoardConfig : public ::testing::Test {
protected:
    void SetUp() override {
        root = fs::temp_directory_path() /
               ("nav_bcfg_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        hal = root / "NavHAL";
        write(hal / "src/arch/cortex-m4/Kconfig.choice", "config ARCH_CORTEX_M4\n    bool \"m4\"\n");
        write(hal / "src/vendor/stm32/Kconfig.choice", "config VENDOR_STM32\n    bool \"stm32\"\n");
        write(hal / "src/vendor/stm32/family/stm32f4/Kconfig.choice", "config FAMILY_STM32F4\n    bool \"f4\"\n");
        board("nucleo_f401re", "BOARD_NUCLEO_F401RE");
        board("navixsmf401re", "BOARD_NAVIXSM_F401RE");
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
    void board(const std::string& name, const std::string& sym) {
        write(hal / "src/board" / name / "Kconfig.choice", "config " + sym + "\n    bool \"" + name + "\"\n");
        write(hal / "src/board" / name / "Kconfig",
              "config BOARD\n    string\n    default \"" + name + "\" if " + sym + "\n\nconfig PIN_LED\n    int\n");
        write(hal / "cmake/defconfigs" / ("cortex-m4_stm32f4_" + name + ".defconfig"),
              "# " + name + "\nCONFIG_ARCH_CORTEX_M4=y\nCONFIG_VENDOR_STM32=y\nCONFIG_FAMILY_STM32F4=y\nCONFIG_" + sym +
                  "=y\n");
    }

    fs::path root, hal;
    static inline std::atomic<int> counter{0};
};

} // namespace

TEST_F(BoardConfig, ScanReadsBoardSymbolsAndChoices) {
    const auto t = scan_navhal_targets(hal);
    EXPECT_EQ(t.board_symbol.at("navixsmf401re"), "CONFIG_BOARD_NAVIXSM_F401RE");
    EXPECT_EQ(t.board_name.at("CONFIG_BOARD_NUCLEO_F401RE"), "nucleo_f401re");
    for (const char* s : {"CONFIG_ARCH_CORTEX_M4", "CONFIG_VENDOR_STM32", "CONFIG_FAMILY_STM32F4",
                          "CONFIG_BOARD_NUCLEO_F401RE", "CONFIG_BOARD_NAVIXSM_F401RE"})
        EXPECT_TRUE(t.choice_symbols.count(s)) << s;
    EXPECT_FALSE(t.choice_symbols.count("CONFIG_PIN_LED"));
}

TEST_F(BoardConfig, FindsDefconfigBySymbol) {
    auto p = find_board_defconfig(hal, "CONFIG_BOARD_NAVIXSM_F401RE");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->filename(), "cortex-m4_stm32f4_navixsmf401re.defconfig");
    EXPECT_FALSE(find_board_defconfig(hal, "CONFIG_BOARD_NOPE"));
}

TEST_F(BoardConfig, CheckAcceptsTheNamedBoard) {
    const auto t = scan_navhal_targets(hal);
    EXPECT_EQ(check_config_board(t, "navixsmf401re", {{"CONFIG_BOARD_NAVIXSM_F401RE", "y"}}), "");
}

TEST_F(BoardConfig, CheckRejectsOtherMissingAndExtraBoards) {
    const auto t = scan_navhal_targets(hal);
    EXPECT_NE(check_config_board(t, "navixsmf401re", {{"CONFIG_BOARD_NUCLEO_F401RE", "y"}}).find("nucleo_f401re"),
              std::string::npos);
    EXPECT_NE(check_config_board(t, "navixsmf401re", {}).find("no board"), std::string::npos);
    // The stale-.config case: the old board plus one a dependency appended.
    const auto two = check_config_board(
        t, "navixsmf401re", {{"CONFIG_BOARD_NUCLEO_F401RE", "y"}, {"CONFIG_BOARD_NAVIXSM_F401RE", "y"}});
    EXPECT_NE(two.find("navixsmf401re, nucleo_f401re"), std::string::npos) << two;
    EXPECT_NE(check_config_board(t, "pyboard", {}).find("no board named"), std::string::npos);
}

TEST_F(BoardConfig, DependencyCannotAppendATargetChoice) {
    const auto t = scan_navhal_targets(hal);
    auto diff = diff_requirements({{"CONFIG_BOARD_NUCLEO_F401RE", "y"}},
                                  {{"CONFIG_BOARD_NAVIXSM_F401RE", "y"}, {"CONFIG_DRV_MPU", "y"}});
    hold_target_choices(diff, t.choice_symbols);
    ASSERT_EQ(diff.missing.size(), 1u);
    EXPECT_EQ(diff.missing[0], "CONFIG_DRV_MPU=y");
    ASSERT_EQ(diff.conflicts.size(), 1u);
    EXPECT_EQ(diff.conflicts[0].key, "CONFIG_BOARD_NAVIXSM_F401RE");
}

TEST_F(BoardConfig, SeedWithoutPythonWritesDefconfigAndStarter) {
    MockExecutionContext mock;
    const fs::path out = root / ".config";
    EXPECT_EQ(seed_board_config(mock, hal, "navixsmf401re", "CONFIG_DRV_UART=y\n", out, ""), "");
    EXPECT_TRUE(mock.calls.empty());
    const auto cfg = parse_kconfig(out);
    EXPECT_EQ(cfg.at("CONFIG_BOARD_NAVIXSM_F401RE"), "y");
    EXPECT_EQ(cfg.at("CONFIG_FAMILY_STM32F4"), "y");
    EXPECT_EQ(cfg.at("CONFIG_DRV_UART"), "y");
    EXPECT_FALSE(cfg.count("CONFIG_BOARD_NUCLEO_F401RE"));
}

TEST_F(BoardConfig, SeedExpandsWithKconfigWhenPythonGiven) {
    MockExecutionContext mock;
    const fs::path out = root / ".config";
    mock.handler = [&](const ExecutedCall& c) -> nav::core::CommandResult {
        // Stand-in for kconfiglib: append a default, as write_config would.
        std::ofstream(c.argv.at(4), std::ios::app) << "CONFIG_PIN_LED=5\n";
        return {0, ""};
    };
    EXPECT_EQ(seed_board_config(mock, hal, "nucleo_f401re", "", out, "/venv/bin/python"), "");
    ASSERT_EQ(mock.calls.size(), 1u);
    const auto& argv = mock.calls[0].argv;
    EXPECT_EQ(argv[0], "/venv/bin/python");
    EXPECT_EQ(argv[3], fs::absolute(hal / "Kconfig").string());
    EXPECT_EQ(argv[5], fs::absolute(hal).string()); // srctree
    EXPECT_EQ(parse_kconfig(out).at("CONFIG_PIN_LED"), "5");
}

TEST_F(BoardConfig, SeedReportsKconfigFailure) {
    MockExecutionContext mock;
    mock.handler = [](const ExecutedCall&) -> nav::core::CommandResult { return {1, "boom"}; };
    EXPECT_NE(seed_board_config(mock, hal, "nucleo_f401re", "", root / ".config", "py").find("boom"),
              std::string::npos);
}

TEST_F(BoardConfig, SeedRejectsUnknownBoardOrMissingDefconfig) {
    MockExecutionContext mock;
    EXPECT_FALSE(seed_board_config(mock, hal, "pyboard", "", root / ".config", "").empty());
    fs::remove(hal / "cmake/defconfigs/cortex-m4_stm32f4_nucleo_f401re.defconfig");
    EXPECT_NE(seed_board_config(mock, hal, "nucleo_f401re", "", root / ".config", "").find("defconfig"),
              std::string::npos);
}

TEST(RetargetNavToml, ReplacesTargetKeysAndKeepsTheRest) {
    Board b;
    b.id = "navixsmf401re";
    b.arch = "cortex-m4";
    b.vendor = "stm32";
    const std::string in =
        "[project]\nname = \"x\"\n\n[target]\n# keep me\narch = \"avr\"\nvendor = \"microchip\"\n"
        "board = \"atmega328p\"\n\n[build]\nboard = \"not-target\"\n";
    const std::string out = retarget_nav_toml(in, b);
    EXPECT_NE(out.find("# keep me\narch = \"cortex-m4\"\nvendor = \"stm32\"\nboard = \"navixsmf401re\"\n"),
              std::string::npos)
        << out;
    EXPECT_NE(out.find("[build]\nboard = \"not-target\""), std::string::npos) << out;
}

TEST(RetargetNavToml, AddsMissingKeysAndSection) {
    Board b;
    b.id = "nucleo_f401re";
    b.arch = "cortex-m4";
    b.vendor = "stm32";
    const std::string partial = retarget_nav_toml("[target]\nboard = \"old\"\n", b);
    EXPECT_NE(partial.find("[target]\narch = \"cortex-m4\"\nvendor = \"stm32\"\nboard = \"nucleo_f401re\""),
              std::string::npos)
        << partial;
    const std::string none = retarget_nav_toml("[project]\nname = \"x\"\n", b);
    EXPECT_NE(none.find("\n\n[target]\narch = \"cortex-m4\""), std::string::npos) << none;
}
