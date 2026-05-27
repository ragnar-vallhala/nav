#pragma once

#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

namespace nav::core::ui {

// Colour policy. Resolved once on first access, with overrides exposed so the
// global flag parser can force a value at startup.
enum class ColorMode { Auto, Always, Never };

namespace detail {

inline ColorMode& mode_ref() {
    static ColorMode m = ColorMode::Auto;
    return m;
}

inline bool resolve() {
    switch (mode_ref()) {
        case ColorMode::Always: return true;
        case ColorMode::Never:  return false;
        case ColorMode::Auto:   break;
    }
    if (const char* nc = std::getenv("NO_COLOR"); nc && nc[0] != '\0') return false;
    return ::isatty(STDOUT_FILENO) != 0;
}

inline bool& enabled_ref() {
    static bool value = resolve();
    return value;
}

inline const std::string& maybe(const std::string& on, const std::string& off) {
    return enabled_ref() ? on : off;
}

} // namespace detail

inline void set_color_mode(ColorMode m) {
    detail::mode_ref() = m;
    detail::enabled_ref() = detail::resolve();
}

inline bool color_enabled() { return detail::enabled_ref(); }

#define NAV_UI_COLOR(name, esc)                                   \
    inline const std::string& name() {                            \
        static const std::string on  = esc;                       \
        static const std::string off;                             \
        return detail::maybe(on, off);                            \
    }

NAV_UI_COLOR(RESET,   "\033[0m")
NAV_UI_COLOR(BOLD,    "\033[1m")
NAV_UI_COLOR(GREEN,   "\033[32m")
NAV_UI_COLOR(RED,     "\033[31m")
NAV_UI_COLOR(YELLOW,  "\033[33m")
NAV_UI_COLOR(CYAN,    "\033[36m")
NAV_UI_COLOR(MAGENTA, "\033[35m")

#undef NAV_UI_COLOR

inline void print_banner() {
    std::cout << BOLD() << CYAN();
    std::cout << R"(
███╗   ██╗ █████╗ ██╗   ██╗
████╗  ██║██╔══██╗██║   ██║
██╔██╗ ██║███████║██║   ██║
██║╚██╗██║██╔══██║╚██╗ ██╔╝
██║ ╚████║██║  ██║ ╚████╔╝
╚═╝  ╚═══╝╚═╝  ╚═╝  ╚═══╝

)" << RESET() << std::endl;
}

inline void info(const std::string& msg) {
    std::cout << CYAN() << "ℹ " << RESET() << msg << std::endl;
}

inline void success(const std::string& msg) {
    std::cout << GREEN() << BOLD() << "✔ " << RESET() << GREEN() << msg << RESET() << std::endl;
}

inline void error(const std::string& msg) {
    std::cout << RED() << BOLD() << "✖ " << RESET() << RED() << msg << RESET() << std::endl;
}

inline void warning(const std::string& msg) {
    std::cout << YELLOW() << BOLD() << "⚠ " << RESET() << YELLOW() << msg << RESET() << std::endl;
}

inline void step(const std::string& verb, const std::string& item) {
    std::cout << MAGENTA() << BOLD() << " => " << RESET() << BOLD() << verb << " " << RESET() << item << std::endl;
}

} // namespace nav::core::ui
