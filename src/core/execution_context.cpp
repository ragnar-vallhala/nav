#include "nav/core/execution_context.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <thread>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace nav::core {

namespace {

using steady_clock = std::chrono::steady_clock;

// Returns ms remaining until `deadline`, or -1 if `deadline` is unset (no timeout).
int poll_remaining_ms(std::optional<steady_clock::time_point> deadline) {
    if (!deadline) return -1;
    auto now = steady_clock::now();
    if (now >= *deadline) return 0;
    auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now).count();
    if (remaining > std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    return static_cast<int>(remaining);
}

// SIGTERM, give the child 500ms to exit, then SIGKILL. Always reaps.
void terminate_child(pid_t pid) {
    ::kill(pid, SIGTERM);
    auto deadline = steady_clock::now() + std::chrono::milliseconds(500);
    while (steady_clock::now() < deadline) {
        int status = 0;
        pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid || r == -1) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
}

CommandResult run_raw_command(const std::vector<std::string>& cmd,
                              const std::string& working_dir,
                              bool silent,
                              std::chrono::milliseconds timeout) {
    CommandResult result{-1, ""};

    if (cmd.empty()) {
        return result;
    }

    int pipefd[2];
    if (::pipe(pipefd) == -1) {
        ::perror("pipe");
        return result;
    }

    pid_t pid = ::fork();
    if (pid == -1) {
        ::perror("fork");
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return result;
    }

    if (pid == 0) { // Child
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[0]);
        ::close(pipefd[1]);

        if (!working_dir.empty()) {
            if (::chdir(working_dir.c_str()) != 0) {
                ::perror("chdir failed");
                ::_exit(1);
            }
        }

        std::vector<char*> args;
        args.reserve(cmd.size() + 1);
        for (const auto& s : cmd) {
            args.push_back(const_cast<char*>(s.c_str()));
        }
        args.push_back(nullptr);

        ::execvp(args[0], args.data());
        ::_exit(127);
    }

    // Parent
    ::close(pipefd[1]);

    std::optional<steady_clock::time_point> deadline;
    if (timeout > std::chrono::milliseconds(0)) {
        deadline = steady_clock::now() + timeout;
    }

    int flags = ::fcntl(pipefd[0], F_GETFL, 0);
    if (flags != -1) {
        ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    std::stringstream output;
    constexpr std::size_t kSilentCap = 1u << 20; // 1 MiB; see P2-1.
    std::size_t captured_bytes = 0;
    bool truncated = false;
    std::array<char, 4096> buffer{};

    bool timed_out = false;
    bool eof = false;

    while (!eof) {
        int wait_ms = poll_remaining_ms(deadline);
        if (deadline && wait_ms == 0) {
            timed_out = true;
            break;
        }

        struct pollfd pfd{pipefd[0], POLLIN, 0};
        int prc = ::poll(&pfd, 1, wait_ms);

        if (prc == -1) {
            if (errno == EINTR) continue;
            break;
        }
        if (prc == 0) {
            timed_out = true;
            break;
        }

        if (pfd.revents & (POLLIN | POLLHUP)) {
            for (;;) {
                ssize_t count = ::read(pipefd[0], buffer.data(), buffer.size());
                if (count > 0) {
                    if (!silent) {
                        std::cout.write(buffer.data(), count);
                        std::cout.flush();
                        output.write(buffer.data(), count);
                    } else if (!truncated) {
                        std::size_t available = (captured_bytes < kSilentCap)
                                              ? (kSilentCap - captured_bytes) : 0;
                        std::size_t to_keep = std::min<std::size_t>(static_cast<std::size_t>(count), available);
                        if (to_keep > 0) {
                            output.write(buffer.data(), to_keep);
                            captured_bytes += to_keep;
                        }
                        if (captured_bytes >= kSilentCap && !truncated) {
                            output << "\n<output truncated>\n";
                            truncated = true;
                        }
                    }
                    continue;
                }
                if (count == 0) {
                    eof = true;
                    break;
                }
                // count < 0
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                eof = true;
                break;
            }
        } else if (pfd.revents & POLLERR) {
            break;
        }
    }

    ::close(pipefd[0]);

    if (timed_out) {
        terminate_child(pid);
        result.output = output.str()
                      + "\n<command timed out after "
                      + std::to_string(timeout.count()) + "ms>\n";
        result.exit_code = -1;
        return result;
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.output = output.str();
    return result;
}

} // namespace

CommandResult HostExecutionContext::execute(const std::vector<std::string>& cmd,
                                            const std::string& working_dir,
                                            bool silent,
                                            std::chrono::milliseconds timeout) {
    return run_raw_command(cmd, working_dir, silent, timeout);
}

} // namespace nav::core
