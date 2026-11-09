#include "nav/core/runner.hpp"
#include "nav/config/embedded_config.hpp"

#include <gtest/gtest.h>

#include <algorithm>

using namespace nav::core;

namespace {

const char* kBoards = R"({
  "tools": {
    "stlink": { "binary": "st-flash" },
    "openocd": { "binary": "openocd" }
  },
  "runners": {
    "st-flash": { "tool": "stlink", "image": "bin", "args": ["write", "{bin}", "{address}"] },
    "openocd": { "tool": "openocd", "image": "elf",
                 "args": ["-f", "{interface}", "-f", "{target}", "-c", "program \"{elf}\" verify reset exit"],
                 "defaults": { "interface": "interface/stlink.cfg" } },
    "avrdude": { "image": "hex", "args": ["-p", "{part}", "-P", "{port}", "-U", "flash:w:{hex}:i"] },
    "weird": { "image": "srec", "args": [] }
  },
  "boards": {
    "f4": { "compiler": "arm-none-eabi-gcc", "flash_tool": "st-flash", "flash_address": "0x08000000",
            "runners": { "st-flash": {}, "openocd": { "target": "target/stm32f4x.cfg" } } },
    "uno": { "compiler": "avr-gcc", "runner": "avrdude", "runners": { "avrdude": { "part": "m328p" } } },
    "bare": { }
  }
})";

bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

TEST(Runner, ExpandsPlaceholdersAndReportsMissing) {
    std::set<std::string> missing;
    EXPECT_EQ(expand_placeholders("flash:w:{hex}:i", {{"hex", "a.hex"}}, missing), "flash:w:a.hex:i");
    EXPECT_TRUE(missing.empty());
    EXPECT_EQ(expand_placeholders("{a}-{b}", {{"a", "1"}}, missing), "1-{b}");
    EXPECT_EQ(missing, std::set<std::string>{"b"});
}

TEST(Runner, ObjcopyFollowsCompilerPrefix) {
    Board b;
    b.compiler = "arm-none-eabi-gcc";
    EXPECT_EQ(objcopy_for(b), "arm-none-eabi-objcopy");
    b.compiler = "avr-gcc";
    EXPECT_EQ(objcopy_for(b), "avr-objcopy");
    b.compiler = "";
    EXPECT_EQ(objcopy_for(b), "objcopy");
}

TEST(Runner, DefaultIsRunnerThenFlashToolThenFirstDeclared) {
    const auto r = registry_from_json("", kBoards);
    EXPECT_EQ(default_runner(*r.board("uno")), "avrdude");
    EXPECT_EQ(default_runner(*r.board("f4")), "st-flash");
    EXPECT_EQ(board_runner_names(*r.board("f4")), (std::vector<std::string>{"st-flash", "openocd"}));
    EXPECT_EQ(default_runner(*r.board("bare")), "");
    EXPECT_EQ(default_flash_binary(*r.board("f4"), r), "st-flash");
}

TEST(Runner, StFlashConvertsToBinThenWrites) {
    const auto r = registry_from_json("", kBoards);
    const auto plan = plan_flash(*r.board("f4"), r, "", "build/app", {});
    ASSERT_TRUE(plan.error.empty()) << plan.error;
    ASSERT_EQ(plan.steps.size(), 2u);
    EXPECT_EQ(plan.steps[0].argv,
              (std::vector<std::string>{"arm-none-eabi-objcopy", "-O", "binary", "build/app", "build/app.bin"}));
    EXPECT_EQ(plan.steps[1].argv, (std::vector<std::string>{"st-flash", "write", "build/app.bin", "0x08000000"}));
    EXPECT_EQ(plan.tool, "stlink");
}

TEST(Runner, OpenocdFlashesElfWithBoardTargetAndDefaultInterface) {
    const auto r = registry_from_json("", kBoards);
    const auto plan = plan_flash(*r.board("f4"), r, "openocd", "build/app.elf", {});
    ASSERT_TRUE(plan.error.empty()) << plan.error;
    ASSERT_EQ(plan.steps.size(), 1u);
    const auto& argv = plan.steps[0].argv;
    EXPECT_EQ(argv[0], "openocd");
    EXPECT_TRUE(has(argv, "interface/stlink.cfg"));
    EXPECT_TRUE(has(argv, "target/stm32f4x.cfg"));
    EXPECT_TRUE(has(argv, "program \"build/app.elf\" verify reset exit"));
}

TEST(Runner, CommandLineValuesWin) {
    const auto r = registry_from_json("", kBoards);
    const auto plan =
        plan_flash(*r.board("f4"), r, "openocd", "build/app", {{"interface", "interface/cmsis-dap.cfg"}});
    ASSERT_TRUE(plan.error.empty()) << plan.error;
    EXPECT_TRUE(has(plan.steps[0].argv, "interface/cmsis-dap.cfg"));
}

TEST(Runner, AvrdudeConvertsToHexAndNeedsPort) {
    const auto r = registry_from_json("", kBoards);
    auto plan = plan_flash(*r.board("uno"), r, "", "build/app", {});
    EXPECT_NE(plan.error.find("port"), std::string::npos) << plan.error;
    EXPECT_NE(plan.error.find("--set"), std::string::npos);

    plan = plan_flash(*r.board("uno"), r, "", "build/app", {{"port", "/dev/ttyACM0"}});
    ASSERT_TRUE(plan.error.empty()) << plan.error;
    ASSERT_EQ(plan.steps.size(), 2u);
    EXPECT_EQ(plan.steps[0].argv[0], "avr-objcopy");
    EXPECT_TRUE(has(plan.steps[0].argv, "ihex"));
    EXPECT_EQ(plan.steps[1].argv,
              (std::vector<std::string>{"avrdude", "-p", "m328p", "-P", "/dev/ttyACM0", "-U", "flash:w:build/app.hex:i"}));
}

TEST(Runner, MissingBoardVariableNamesIt) {
    const auto r = registry_from_json("", kBoards);
    // "uno" doesn't configure openocd, so {target} has no value.
    const auto plan = plan_flash(*r.board("uno"), r, "openocd", "build/app", {});
    EXPECT_NE(plan.error.find("target"), std::string::npos) << plan.error;
    EXPECT_NE(plan.error.find("boards.uno.runners.openocd"), std::string::npos) << plan.error;
}

TEST(Runner, UnknownRunnerImageOrNoneAreErrors) {
    const auto r = registry_from_json("", kBoards);
    EXPECT_NE(plan_flash(*r.board("f4"), r, "jlink", "a", {}).error.find("unknown runner 'jlink'"), std::string::npos);
    EXPECT_NE(plan_flash(*r.board("f4"), r, "weird", "a", {}).error.find("image type"), std::string::npos);
    EXPECT_NE(plan_flash(*r.board("bare"), r, "", "a", {}).error.find("no flash runner"), std::string::npos);
}

// Every built-in board's runners resolve with nothing but a port supplied.
TEST(Runner, BakedBoardsPlanEveryDeclaredRunner) {
    const auto r = registry_from_json("", std::string(nav::config::kBakedBoardsJson));
    ASSERT_FALSE(r.boards().empty());
    for (const auto& [id, board] : r.boards()) {
        const auto names = board_runner_names(board);
        EXPECT_FALSE(names.empty()) << id;
        for (const auto& name : names) {
            const auto plan = plan_flash(board, r, name, "build/app", {{"port", "/dev/ttyUSB0"}});
            EXPECT_TRUE(plan.error.empty()) << id << " / " << name << ": " << plan.error;
            EXPECT_NE(r.tool(plan.tool), nullptr) << id << " / " << name << ": tool " << plan.tool;
        }
    }
}
