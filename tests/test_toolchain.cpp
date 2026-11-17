#include "nav/core/toolchain.hpp"

#include <gtest/gtest.h>

using nav::core::PackageManager;
using nav::core::build_install_command;
using nav::core::map_binary_to_package;
using nav::core::merge_path_entries;

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

TEST(Toolchain, MergePath_AppendsNewEntriesAfterCurrent) {
    EXPECT_EQ(merge_path_entries("C:\\a;C:\\b", "C:\\m", "C:\\u"), "C:\\a;C:\\b;C:\\m;C:\\u");
}

TEST(Toolchain, MergePath_DedupesAcrossAllLists) {
    EXPECT_EQ(merge_path_entries("C:\\a", "C:\\a;C:\\m", "C:\\m;C:\\u"), "C:\\a;C:\\m;C:\\u");
}

TEST(Toolchain, MergePath_CaseInsensitiveOnWindows) {
    EXPECT_EQ(merge_path_entries("C:\\Python312", "c:\\PYTHON312;C:\\New", ""), "C:\\Python312;C:\\New");
}

TEST(Toolchain, MergePath_CaseSensitiveWhenAsked) {
    EXPECT_EQ(merge_path_entries("/a", "/A", "", ':', false), "/a:/A");
}

TEST(Toolchain, MergePath_IgnoresTrailingSlash) {
    EXPECT_EQ(merge_path_entries("C:\\a\\;C:\\b/", "C:\\a;C:\\b\\;C:\\c\\", ""), "C:\\a\\;C:\\b/;C:\\c\\");
}

TEST(Toolchain, MergePath_DropsEmptyEntries) {
    EXPECT_EQ(merge_path_entries(";C:\\a;;", ";", ";C:\\u;"), "C:\\a;C:\\u");
    EXPECT_EQ(merge_path_entries("", "", ""), "");
}

TEST(Toolchain, MergePath_PreservesCurrentOrderAndUsesSeparator) {
    EXPECT_EQ(merge_path_entries("/z:/y:/x", "/y:/w", "/v", ':', false), "/z:/y:/x:/w:/v");
}
