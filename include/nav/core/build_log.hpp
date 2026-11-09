#pragma once

#include <string>
#include <vector>

namespace nav::core {

// Compiler warnings found in captured build output.
struct BuildWarnings {
    // One entry per distinct warning: the "file:line:col: warning: ..." line and
    // the context lines after it (source excerpt, caret, notes) up to the next
    // diagnostic, "In function" banner or build-system progress line.
    std::vector<std::string> blocks;
    // Number of warning occurrences before deduplication (a header warning is
    // repeated once per translation unit that includes it).
    int total = 0;
};

// Extract and deduplicate the warnings in `output` (merged compiler stdout and
// stderr from `cmake --build`), in order of first appearance.
BuildWarnings extract_build_warnings(const std::string& output);

} // namespace nav::core
