#pragma once

#include <string>
#include <vector>
#include <memory>

namespace nav::core {

struct CommandResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

class IExecutionContext {
public:
    virtual ~IExecutionContext() = default;
    
    virtual CommandResult execute(const std::vector<std::string>& cmd, 
                                  const std::string& working_dir = "") = 0;
};

class HostExecutionContext : public IExecutionContext {
public:
    CommandResult execute(const std::vector<std::string>& cmd, 
                          const std::string& working_dir = "") override;
};

} // namespace nav::core
