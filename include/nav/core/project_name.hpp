#pragma once

#include <string>

namespace nav::core {

// Reject anything that could escape CWD or produce a brittle directory tree.
// Policy: non-empty, no leading dot or dash, every byte in [A-Za-z0-9._-].
// Returns an empty string on success, otherwise a human-readable reason.
std::string validate_project_name(const std::string& name);

} // namespace nav::core
