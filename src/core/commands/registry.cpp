// Stubs for the registry-facing verbs. These will become real HTTP clients
// once the registry-api contract stabilises (see docs/plan.md, docs/mvp.md).
#include "nav/core/command.hpp"
#include "nav/core/ui.hpp"

#include <string>

namespace nav::core {

namespace {

void preview_notice(const std::string& verb) {
    ui::warning("'nav " + verb + "' is coming soon — pending the registry rollout (see docs/plan.md).");
}

} // namespace


int AddCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& /*args*/) {
    preview_notice("add");
    return 0;
}

int SearchCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& /*args*/) {
    preview_notice("search");
    return 0;
}

int LoginCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& /*args*/) {
    preview_notice("login");
    return 0;
}

int PublishCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& /*args*/) {
    preview_notice("publish");
    return 0;
}

} // namespace nav::core
