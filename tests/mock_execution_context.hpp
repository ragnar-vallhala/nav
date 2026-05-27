#pragma once

#include "nav/core/execution_context.hpp"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace nav::test {

struct ExecutedCall {
    std::vector<std::string> argv;
    std::string working_dir;
    bool silent;
    std::chrono::milliseconds timeout;
};

class MockExecutionContext : public nav::core::IExecutionContext {
public:
    using Handler = std::function<nav::core::CommandResult(const ExecutedCall&)>;

    nav::core::CommandResult execute(const std::vector<std::string>& cmd,
                                     const std::string& working_dir = "",
                                     bool silent = false,
                                     std::chrono::milliseconds timeout = nav::core::kNoTimeout) override {
        ExecutedCall call{cmd, working_dir, silent, timeout};
        calls.push_back(call);
        if (handler) {
            return handler(call);
        }
        return scripted_result;
    }

    std::vector<ExecutedCall> calls;
    Handler handler;
    nav::core::CommandResult scripted_result{0, ""};
};

} // namespace nav::test
