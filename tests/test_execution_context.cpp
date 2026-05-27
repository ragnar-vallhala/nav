#include "nav/core/execution_context.hpp"

#include <gtest/gtest.h>

#include <chrono>

using nav::core::HostExecutionContext;

TEST(HostExecutionContext, EchoExits0AndCapturesOutput) {
    HostExecutionContext ctx;
    auto r = ctx.execute({"/bin/echo", "hello"}, "", true);
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
    auto r = ctx.execute({"/bin/sleep", "30"}, "", true, std::chrono::seconds(1));
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
