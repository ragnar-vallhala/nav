#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "nav/core/command.hpp"
#include "nav/core/execution_context.hpp"
#include "nav/core/ui.hpp"

#ifndef NAV_VERSION
#define NAV_VERSION "0.0.0-dev"
#endif

namespace {

void apply_color_mode(const std::string& mode) {
    using nav::core::ui::ColorMode;
    if (mode == "always") nav::core::ui::set_color_mode(ColorMode::Always);
    else if (mode == "never") nav::core::ui::set_color_mode(ColorMode::Never);
    else nav::core::ui::set_color_mode(ColorMode::Auto);
}

} // namespace

int main(int argc, char* argv[]) {
    CLI::App app{"Nav — embedded development orchestrator"};
    app.set_version_flag("--version,-V", NAV_VERSION);
    app.require_subcommand(1);

    std::string color_mode = "auto";
    bool verbose = false;
    bool quiet = false;
    std::string cwd;

    app.add_option("--color", color_mode, "Color output policy")
        ->check(CLI::IsMember({"auto", "always", "never"}))
        ->default_val("auto");
    app.add_flag("--no-color", [&](std::int64_t) { color_mode = "never"; }, "Disable color output");
    app.add_flag("--verbose,-v", verbose, "Verbose output");
    app.add_flag("--quiet,-q", quiet, "Suppress informational output");
    app.add_option("--cwd", cwd, "Run as if invoked from this directory");

    std::map<std::string, std::unique_ptr<nav::core::ICommand>> commands;
    commands["create"]  = std::make_unique<nav::core::CreateCommand>();
    commands["build"]   = std::make_unique<nav::core::BuildCommand>();
    commands["upload"]  = std::make_unique<nav::core::UploadCommand>();
    commands["monitor"] = std::make_unique<nav::core::MonitorCommand>();
    commands["clean"]   = std::make_unique<nav::core::CleanCommand>();
    commands["check"]   = std::make_unique<nav::core::CheckCommand>();
    commands["update"]  = std::make_unique<nav::core::UpdateCommand>();
    commands["add"]     = std::make_unique<nav::core::AddCommand>();
    commands["search"]  = std::make_unique<nav::core::SearchCommand>();
    commands["fetch"]   = std::make_unique<nav::core::FetchCommand>();
    commands["login"]   = std::make_unique<nav::core::LoginCommand>();
    commands["publish"] = std::make_unique<nav::core::PublishCommand>();
    commands["board"]   = std::make_unique<nav::core::BoardCommand>();

    // Each subcommand uses prefix_command() so anything after the verb name
    // is collected raw into remaining() — letting verbs that still parse their
    // own argv (monitor: --port/--baud; create: --force; board: list/info)
    // keep working unchanged. Global flags must come before the verb.
    for (auto& [name, cmd] : commands) {
        auto* sub = app.add_subcommand(name, cmd->help_text());
        sub->prefix_command();
        cmd->register_flags(*sub);
    }

    CLI11_PARSE(app, argc, argv);

    apply_color_mode(color_mode);

    if (!cwd.empty()) {
        std::error_code ec;
        std::filesystem::current_path(cwd, ec);
        if (ec) {
            nav::core::ui::error("Failed to chdir to '" + cwd + "': " + ec.message());
            return 1;
        }
    }

    auto subs = app.get_subcommands();
    if (subs.empty()) {
        // require_subcommand(1) should have already errored, but guard anyway.
        std::cerr << app.help() << std::endl;
        return 1;
    }
    CLI::App* selected = subs.front();
    const std::string verb = selected->get_name();

    auto it = commands.find(verb);
    if (it == commands.end()) {
        nav::core::ui::error("Unknown command '" + verb + "'.");
        return 1;
    }

    std::vector<std::string> remaining = selected->remaining();

    auto context = std::make_unique<nav::core::HostExecutionContext>();
    return it->second->run(*context, remaining);
}
