#pragma once

#include <string>
#include <string_view>

namespace nav::core {

// Text of a project template. Normally the copy baked into the binary. If the
// environment variable NAV_TEMPLATE_DIR names a directory that contains `file`
// (the name under templates/, e.g. "cmakelists.txt.in"), that file is read from
// disk instead, so a dev build of nav can test template edits without a
// reinstall. A file missing from the directory falls back to the baked copy.
// Each override prints a one-line notice so it is never used by accident.
std::string template_text(const char* file, std::string_view embedded);

} // namespace nav::core
