#include "nav/core/registry.hpp"

#include <gtest/gtest.h>

using namespace nav::core;

namespace {

const char* kToolchain = R"({
  "schema": 1,
  "core": ["cmake", "python"],
  "tools": {
    "cmake": { "binary": "cmake", "version": "3.30.5",
      "packages": { "apt": "cmake", "winget": "Kitware.CMake" },
      "download": { "windows-x86_64": { "url": "http://x/cmake.zip", "archive": "zip", "bin_subdir": "cmake/bin" } } },
    "python": { "binary": "python3", "version": "3.12.7",
      "packages": { "apt": "python3" },
      "download": { "windows-x86_64": { "url": "http://x/py.tar.gz", "archive": "targz", "bin_subdir": "python", "post": ["alias:python.exe=python3.exe", "pip:kconfiglib"] } } }
  }
})";

const char* kBoards = R"({
  "schema": 1,
  "tools": {
    "arm-none-eabi-gcc": { "binary": "arm-none-eabi-gcc", "version": "13.3.1",
      "packages": { "apt": "gcc-arm-none-eabi" },
      "download": { "windows-x86_64": { "url": "http://x/arm.zip", "archive": "zip", "bin_subdir": "arm/bin" } } },
    "st-flash": { "binary": "st-flash", "version": "1.8.0", "packages": { "apt": "stlink-tools" } }
  },
  "boards": {
    "nucleo_f401re": { "name": "STM32 Nucleo-F401RE", "arch": "cortex-m4", "vendor": "stm32",
      "mcu": "stm32f401re", "navhal_board": "nucleo_f401re",
      "compiler": "arm-none-eabi-gcc", "flash_tool": "st-flash", "flash_address": "0x08000000",
      "compile_flags": ["-mcpu=cortex-m4", "-mthumb"], "link_flags": ["-mthumb"],
      "default_framework": "navhal", "supported_frameworks": ["navhal"] }
  }
})";

} // namespace

TEST(Registry, ParsesCoreAndTools) {
    Registry r = registry_from_json(kToolchain, kBoards);
    EXPECT_EQ(r.core_tool_ids().size(), 2u);

    const ToolDef* cmake = r.tool("cmake");
    ASSERT_NE(cmake, nullptr);
    EXPECT_EQ(cmake->binary, "cmake");
    EXPECT_EQ(cmake->version, "3.30.5");
    EXPECT_EQ(cmake->packages.at("apt"), "cmake");
    EXPECT_EQ(cmake->packages.at("winget"), "Kitware.CMake");
    ASSERT_TRUE(cmake->download.count("windows-x86_64"));
    EXPECT_EQ(cmake->download.at("windows-x86_64").bin_subdir, "cmake/bin");
}

TEST(Registry, BinaryDiffersFromId) {
    Registry r = registry_from_json(kToolchain, kBoards);
    // python tool id "python" but the command is "python3"
    const ToolDef* by_id = r.tool("python");
    const ToolDef* by_bin = r.tool_for_binary("python3");
    ASSERT_NE(by_id, nullptr);
    ASSERT_NE(by_bin, nullptr);
    EXPECT_EQ(by_id->id, by_bin->id);
    EXPECT_EQ(by_bin->binary, "python3");
    // post steps carried through
    ASSERT_TRUE(by_id->download.count("windows-x86_64"));
    EXPECT_EQ(by_id->download.at("windows-x86_64").post.size(), 2u);
}

TEST(Registry, BoardCarriesCompilerAndFlasher) {
    Registry r = registry_from_json(kToolchain, kBoards);
    const Board* b = r.board("nucleo_f401re");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->compiler, "arm-none-eabi-gcc");
    EXPECT_EQ(b->flash_tool, "st-flash");
    EXPECT_EQ(b->navhal_board, "nucleo_f401re");
    EXPECT_EQ(b->compile_flags.size(), 2u);
    // board-specific tools were merged into the registry's tool map
    EXPECT_NE(r.tool("arm-none-eabi-gcc"), nullptr);
    EXPECT_NE(r.tool_for_binary("st-flash"), nullptr);
}

TEST(Registry, UserOverrideWins) {
    // Simulate a user adding a board + overriding a tool version by parsing a
    // second config into the same registry (mirrors the baked+user merge).
    Registry r = registry_from_json(kToolchain, kBoards);
    Registry extra = registry_from_json(
        R"({"tools":{"cmake":{"binary":"cmake","version":"9.9.9"}}})",
        R"({"boards":{"my_board":{"name":"Custom","arch":"x","mcu":"y","compiler":"gcc"}}})");
    // emulate merge: copy extra entries over base
    for (auto& [id, t] : extra.tools()) const_cast<Registry&>(r).add_tool(t);
    for (auto& [id, b] : extra.boards()) const_cast<Registry&>(r).add_board(b);

    EXPECT_EQ(r.tool("cmake")->version, "9.9.9");
    ASSERT_NE(r.board("my_board"), nullptr);
    EXPECT_EQ(r.board("my_board")->name, "Custom");
}
