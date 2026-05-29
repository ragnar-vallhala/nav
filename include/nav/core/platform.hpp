#pragma once

#include <string>

#include "nav/core/execution_context.hpp"

namespace nav::core {

// The host platform key used to pick a toolchain download from a package's
// multi-platform `downloads` map. Format is "<os>_<arch>", e.g. "linux_x86_64"
// or "darwin_arm64" — matching the keys publishers use in the registry index.
//
// Determined by shelling `uname -s -m` through the execution context (testable
// via the mock, no platform #ifdef sprawl). Returns "unknown_unknown" when
// uname is unavailable or its output is unparseable.
std::string host_platform_key(IExecutionContext& ctx);

// Pure mapping from `uname -s` / `uname -m` tokens to a normalized platform
// key. Exposed for tests and for callers that already have the raw values.
// Normalizes common aliases: amd64 -> x86_64, aarch64 -> arm64, and the
// MinGW/MSYS/Cygwin sysnames -> windows.
std::string platform_key_from_uname(const std::string& sysname,
                                     const std::string& machine);

} // namespace nav::core
