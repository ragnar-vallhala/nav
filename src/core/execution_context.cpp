#include "nav/core/execution_context.hpp"
#include <iostream>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <array>

namespace nav::core {

// A robust helper to execute using fork/execvp and capture all output from a single merged stream (stdout+stderr)
static CommandResult run_raw_command(const std::vector<std::string>& cmd, const std::string& working_dir) {
    CommandResult result{-1, "", ""};

    if (cmd.empty()) {
        return result;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return result;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return result;
    }

    if (pid == 0) { // Child
        // Redirect stdout and stderr to write end of pipe
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);

        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) != 0) {
                perror("chdir failed");
                exit(1);
            }
        }

        // Prepare arguments for execvp
        std::vector<char*> args;
        for (const auto& s : cmd) {
            args.push_back(const_cast<char*>(s.c_str()));
        }
        args.push_back(nullptr);

        execvp(args[0], args.data());
        
        // If exec fails
        std::cerr << "Failed to execute: " << args[0] << "\n";
        exit(127);
    } else { // Parent
        close(pipefd[1]); // Close write end

        std::stringstream output;
        std::array<char, 256> buffer;
        ssize_t count;
        while ((count = read(pipefd[0], buffer.data(), buffer.size())) > 0) {
            output.write(buffer.data(), count);
            
            // Mirror stream live to standard output
            std::cout.write(buffer.data(), count);
            std::cout.flush();
        }
        close(pipefd[0]);

        int status;
        waitpid(pid, &status, 0);
        
        result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        result.stdout_output = output.str();
    }

    return result;
}

CommandResult HostExecutionContext::execute(const std::vector<std::string>& cmd, const std::string& working_dir) {
    // Delegate to generic runner
    return run_raw_command(cmd, working_dir);
}

} // namespace nav::core
