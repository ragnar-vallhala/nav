#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <chrono>

using nav::core::CommandResult;
using nav::test::MockExecutionContext;

TEST(MockExecutionContext, RecordsArgvWorkingDirSilentTimeout) {
    MockExecutionContext mock;
    mock.scripted_result = {0, "hello"};

    auto r = mock.execute({"echo", "hi"}, "/tmp", true, std::chrono::seconds(5));

    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(r.output, "hello");
    ASSERT_EQ(mock.calls.size(), 1u);
    EXPECT_EQ(mock.calls[0].argv, (std::vector<std::string>{"echo", "hi"}));
    EXPECT_EQ(mock.calls[0].working_dir, "/tmp");
    EXPECT_TRUE(mock.calls[0].silent);
    EXPECT_EQ(mock.calls[0].timeout, std::chrono::seconds(5));
}

TEST(MockExecutionContext, HandlerOverridesScriptedResult) {
    MockExecutionContext mock;
    mock.handler = [](const nav::test::ExecutedCall& call) {
        CommandResult r{42, ""};
        if (!call.argv.empty() && call.argv[0] == "fail") r.exit_code = 1;
        return r;
    };

    EXPECT_EQ(mock.execute({"ok"}).exit_code, 42);
    EXPECT_EQ(mock.execute({"fail"}).exit_code, 1);
    EXPECT_EQ(mock.calls.size(), 2u);
}
