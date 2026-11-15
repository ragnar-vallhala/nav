#pragma once

#include <cstdlib>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

// Portable environment helpers: MinGW has no setenv/unsetenv.
inline void set_env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}

inline void unset_env(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

// True when the process can read files regardless of their mode bits. Windows
// has no such notion for the mode-000 tests, so they are skipped there anyway.
inline bool is_root() {
#ifdef _WIN32
    return false;
#else
    return ::geteuid() == 0;
#endif
}
