#include "nav/core/platform.hpp"

#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

using nav::core::host_platform_key;
using nav::core::platform_key_from_uname;
using nav::test::MockExecutionContext;

TEST(Platform, MapsLinuxX86_64) {
    EXPECT_EQ(platform_key_from_uname("Linux", "x86_64"), "linux_x86_64");
}

TEST(Platform, MapsDarwinArm64) {
    EXPECT_EQ(platform_key_from_uname("Darwin", "arm64"), "darwin_arm64");
}

TEST(Platform, NormalizesAliases) {
    EXPECT_EQ(platform_key_from_uname("Linux", "amd64"), "linux_x86_64");
    EXPECT_EQ(platform_key_from_uname("Linux", "aarch64"), "linux_arm64");
}

TEST(Platform, MapsWindowsSysnames) {
    EXPECT_EQ(platform_key_from_uname("MINGW64_NT-10.0", "x86_64"), "windows_x86_64");
    EXPECT_EQ(platform_key_from_uname("MSYS_NT-10.0", "x86_64"), "windows_x86_64");
}

TEST(Platform, UnknownTokensFallBack) {
    EXPECT_EQ(platform_key_from_uname("", ""), "unknown_unknown");
}

TEST(Platform, HostKeyParsesUnameOutput) {
    MockExecutionContext mock;
    mock.scripted_result = {0, "Linux x86_64\n"};
    EXPECT_EQ(host_platform_key(mock), "linux_x86_64");

    ASSERT_EQ(mock.calls.size(), 1u);
    EXPECT_EQ(mock.calls[0].argv, (std::vector<std::string>{"uname", "-s", "-m"}));
}

TEST(Platform, HostKeyFallsBackWhenUnameFails) {
    MockExecutionContext mock;
    mock.scripted_result = {127, ""};  // uname not found
    EXPECT_EQ(host_platform_key(mock), "unknown_unknown");
}
