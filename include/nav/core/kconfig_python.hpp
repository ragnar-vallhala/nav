#pragma once

#include "nav/core/execution_context.hpp"

#include <filesystem>
#include <string>

namespace nav::core {

// NavHAL generates its configuration with tools/kconfig.py, which imports
// kconfiglib. CMake runs it with ${Python3_EXECUTABLE}, so nav hands CMake an
// interpreter that can import kconfiglib, and provides one itself when the
// system Python cannot: a venv nav owns, because distro Pythons (Debian,
// Ubuntu, Fedora) refuse `pip install` into the system environment.

// Version installed into nav's venv. Matches what NavHAL is tested against.
inline constexpr const char* kKconfiglibRequirement = "kconfiglib==14.1.0";

// nav's managed venv: <nav home>/python.
std::filesystem::path kconfig_venv_dir();

// The interpreter inside a venv (bin/python, or Scripts/python.exe on Windows).
std::filesystem::path venv_python(const std::filesystem::path& venv);

// Absolute path of a Python that can import kconfiglib, preferring nav's venv,
// then the system python3 / python. Empty if none can. Installs nothing.
std::string find_kconfig_python(IExecutionContext& ctx);

// find_kconfig_python(), and failing that create nav's venv with the system
// Python and install kconfiglib into it. Returns the interpreter path, or empty
// after printing what to do (no Python, no venv module, offline).
std::string ensure_kconfig_python(IExecutionContext& ctx);

} // namespace nav::core
