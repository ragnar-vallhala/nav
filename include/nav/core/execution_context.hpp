#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace nav::core {

struct CommandResult {
    int exit_code;
    // Merged stdout+stderr capture (the child has both fds duped onto a single pipe).
    std::string output;
};

// Sentinel for "no timeout" — pass this (or simply omit the argument) to wait
// for the child to exit on its own. Long-running calls (build, clone, install)
// should use this; short probes should pass a real deadline.
inline constexpr std::chrono::milliseconds kNoTimeout{0};

class IExecutionContext {
public:
    virtual ~IExecutionContext() = default;

    virtual CommandResult execute(const std::vector<std::string>& cmd,
                                  const std::string& working_dir = "",
                                  bool silent = false,
                                  std::chrono::milliseconds timeout = kNoTimeout) = 0;
};

class HostExecutionContext : public IExecutionContext {
public:
    CommandResult execute(const std::vector<std::string>& cmd,
                          const std::string& working_dir = "",
                          bool silent = false,
                          std::chrono::milliseconds timeout = kNoTimeout) override;
};

} // namespace nav::core
