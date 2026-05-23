#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

#include <map>
#include "nav/core/command.hpp"
#include "nav/core/ui.hpp"

#ifndef NAV_VERSION
#define NAV_VERSION "0.0.0-dev"
#endif

void print_usage() {
    std::cout << "Usage: nav <command> [args...]\n\n"
              << "Commands:\n"
              << "  create  - Create new project space\n"
              << "  build   - Compile source tree\n"
              << "  upload  - Push images to chips\n"
              << "  monitor - Connect to serial telemetry\n"
              << "  clean   - Flush cache and build files\n"
              << "  check   - Perform verification checks\n"
              << "  update  - Upgrade environment definitions\n"
              << "  add     - Cache external library resource     (coming soon)\n"
              << "  search  - Search master package catalog       (coming soon)\n"
              << "  login   - Secure session authorization link   (coming soon)\n"
              << "  publish - Push artifact bundle to backend     (coming soon)\n"
              << "\nFlags:\n"
              << "  --version, -v   Print version and exit\n"
              << "  --help, help    Print this message\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || args[0] == "--help" || args[0] == "help") {
        print_usage();
        return 0;
    }

    if (args[0] == "--version" || args[0] == "-v") {
        std::cout << "nav " << NAV_VERSION << "\n";
        return 0;
    }

    // Factory setup for known verbs
    std::map<std::string, std::unique_ptr<nav::core::ICommand>> command_registry;
    command_registry["create"] = std::make_unique<nav::core::CreateCommand>();
    command_registry["build"] = std::make_unique<nav::core::BuildCommand>();
    command_registry["upload"] = std::make_unique<nav::core::UploadCommand>();
    command_registry["monitor"] = std::make_unique<nav::core::MonitorCommand>();
    command_registry["clean"] = std::make_unique<nav::core::CleanCommand>();
    command_registry["check"] = std::make_unique<nav::core::CheckCommand>();
    command_registry["update"] = std::make_unique<nav::core::UpdateCommand>();
    command_registry["add"] = std::make_unique<nav::core::AddCommand>();
    command_registry["search"] = std::make_unique<nav::core::SearchCommand>();
    command_registry["login"] = std::make_unique<nav::core::LoginCommand>();
    command_registry["publish"] = std::make_unique<nav::core::PublishCommand>();

    // Context remains strictly native host
    auto context = std::make_unique<nav::core::HostExecutionContext>();

    std::string verb = args[0];
    std::vector<std::string> remaining_args(args.begin() + 1, args.end());

    auto it = command_registry.find(verb);
    if (it == command_registry.end()) {
        std::cerr << "Error: Unknown command '" << verb << "'.\n";
        print_usage();
        return 1;
    }

    // Execute chosen command through designated context
    return it->second->run(*context, remaining_args);
}
