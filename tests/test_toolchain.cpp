#include "nav/core/toolchain.hpp"

#include <gtest/gtest.h>

using nav::core::PackageManager;
using nav::core::build_install_command;
using nav::core::map_binary_to_package;

TEST(Toolchain, BuildInstallCommand_AptUsesSudo) {
    auto cmd = build_install_command(PackageManager::Apt, {"cmake", "git"});
    ASSERT_GE(cmd.size(), 4u);
    EXPECT_EQ(cmd[0], "sudo");
    EXPECT_EQ(cmd[1], "apt");
    EXPECT_EQ(cmd[2], "install");
    EXPECT_EQ(cmd[3], "-y");
    EXPECT_EQ(cmd[4], "cmake");
    EXPECT_EQ(cmd[5], "git");
}

TEST(Toolchain, BuildInstallCommand_BrewSkipsSudo) {
    auto cmd = build_install_command(PackageManager::Brew, {"cmake"});
    ASSERT_GE(cmd.size(), 3u);
    EXPECT_EQ(cmd[0], "brew");
    EXPECT_EQ(cmd[1], "install");
    EXPECT_EQ(cmd[2], "cmake");
}

TEST(Toolchain, BuildInstallCommand_PacmanUsesNoConfirm) {
    auto cmd = build_install_command(PackageManager::Pacman, {"git"});
    ASSERT_GE(cmd.size(), 4u);
    EXPECT_EQ(cmd[0], "sudo");
    EXPECT_EQ(cmd[1], "pacman");
    EXPECT_EQ(cmd[2], "-S");
    EXPECT_EQ(cmd[3], "--noconfirm");
}

TEST(Toolchain, MapBinaryToPackage_KnownTools) {
    EXPECT_EQ(map_binary_to_package(PackageManager::Apt,    "arm-none-eabi-gcc").value_or(""),
              "gcc-arm-none-eabi");
    EXPECT_EQ(map_binary_to_package(PackageManager::Dnf,    "arm-none-eabi-gcc").value_or(""),
              "arm-none-eabi-gcc-cs");
    EXPECT_EQ(map_binary_to_package(PackageManager::Pacman, "python3").value_or(""),
              "python");
    EXPECT_EQ(map_binary_to_package(PackageManager::Brew,   "st-flash").value_or(""),
              "stlink");
}

TEST(Toolchain, MapBinaryToPackage_UnknownReturnsNullopt) {
    EXPECT_FALSE(map_binary_to_package(PackageManager::Apt, "totally-not-a-tool").has_value());
}
