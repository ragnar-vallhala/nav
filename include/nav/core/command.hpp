#pragma once

#include <string>
#include <vector>

#include "nav/core/execution_context.hpp"

namespace CLI { class App; }

namespace nav::core {

class ICommand {
public:
    virtual ~ICommand() = default;

    // Main entry logic for a specific command verb.
    virtual int run(IExecutionContext& ctx, const std::vector<std::string>& args) = 0;

    // Basic text describing command for help printer.
    virtual std::string help_text() const = 0;

    // Optional CLI11 hook for registering per-command flags. Default no-op so
    // existing commands keep working while we migrate them piecemeal.
    virtual void register_flags(CLI::App& /*sub*/) {}
};

class CreateCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Create a new project (--lib for a library) directory with config."; }
};

class BuildCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Compile system software sources."; }
};

class UploadCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Push binary payloads to hardware targets."; }
};

class MonitorCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Open a real-time logging channel to device."; }
};

class CleanCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Purge cached artifacts and local build trees."; }
};

class CheckCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Run analysis tools and sanity health checks."; }
};

class UpdateCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Refresh installed package indices and toolchains."; }
};

class BoardCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Boards: list [--all], info <id>, check <id>, install <id>."; }
};

class LibCommand : public ICommand {
public:
    int run(IExecutionContext& ctx, const std::vector<std::string>& args) override;
    std::string help_text() const override { return "Library deps: add <path|git-url> [name] [--ref r], remove <name>, list."; }
};

} // namespace nav::core
