#include "nav/core/execution_context.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

using nav::core::HostExecutionContext;

namespace {
#ifdef _WIN32
const std::vector<std::string> kEcho{"cmd", "/c", "echo", "hello"};
const std::vector<std::string> kSleep30{"ping", "-n", "30", "127.0.0.1"};
#else
const std::vector<std::string> kEcho{"/bin/echo", "hello"};
const std::vector<std::string> kSleep30{"/bin/sleep", "30"};
#endif
} // namespace

TEST(HostExecutionContext, EchoExits0AndCapturesOutput) {
    HostExecutionContext ctx;
    auto r = ctx.execute(kEcho, "", true);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_NE(r.output.find("hello"), std::string::npos);
}

TEST(HostExecutionContext, ReturnsNonZeroOnNonexistentBinary) {
    HostExecutionContext ctx;
    auto r = ctx.execute({"this-binary-does-not-exist-1f4d3a"}, "", true);
    EXPECT_NE(r.exit_code, 0);
}

TEST(HostExecutionContext, TimesOutLongRunningCommand) {
    HostExecutionContext ctx;
    auto start = std::chrono::steady_clock::now();
    auto r = ctx.execute(kSleep30, "", true, std::chrono::seconds(1));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_NE(r.exit_code, 0);
    EXPECT_NE(r.output.find("timed out"), std::string::npos);
    EXPECT_LT(elapsed, std::chrono::seconds(3));
}

TEST(HostExecutionContext, EmptyCmdReturnsErrorWithoutForking) {
    HostExecutionContext ctx;
    auto r = ctx.execute({}, "", true);
    EXPECT_EQ(r.exit_code, -1);
}
