#pragma once

#include <string>
#include <vector>

namespace nav::core {

struct CommandResult {
    int exit_code;
    // Merged stdout+stderr capture (the child has both fds duped onto a single pipe).
    std::string output;
};

class IExecutionContext {
public:
    virtual ~IExecutionContext() = default;
    
    virtual CommandResult execute(const std::vector<std::string>& cmd, 
                                  const std::string& working_dir = "",
                                  bool silent = false) = 0;
};

class HostExecutionContext : public IExecutionContext {
public:
    CommandResult execute(const std::vector<std::string>& cmd, 
                          const std::string& working_dir = "",
                          bool silent = false) override;
};

} // namespace nav::core
