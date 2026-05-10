#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

#include <map>
#include "nav/core/command.hpp"
#include "nav/core/ui.hpp"

void print_usage() {
    std::cout << "Usage: nav <command> [args...]\n\n"
              << "Commands:\n"
              << "  create  - Create new project space\n"
              << "  build   - Compile source tree\n"
              << "  upload  - Push images to chips\n"
              << "  monitor - Connect to serial telemetry\n"
              << "  clean   - Flush cache and build files\n"
              << "  check   - Perform verification checks\n"
              << "  update  - Upgrade environment definitions\n";
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);

    if (args.empty() || args[0] == "--help" || args[0] == "help") {
        print_usage();
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
