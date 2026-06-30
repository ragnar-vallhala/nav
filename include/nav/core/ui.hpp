#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <cstdio>
#include <io.h>
#else
#include <unistd.h>
#endif

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
#ifdef _WIN32
    return ::_isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

inline bool& enabled_ref() {
    static bool value = resolve();
    return value;
}

inline const std::string& maybe(const std::string& on, const std::string& off) {
    return enabled_ref() ? on : off;
}

// Whether the terminal can render the decorative glyphs (✔ ✖ ⚠ box-drawing).
// Legacy Windows consoles render these as '?', so we only enable them where a
// glyph-complete font is known to be in use. NAV_UNICODE=0/1 forces the choice.
inline bool resolve_unicode() {
    if (const char* u = std::getenv("NAV_UNICODE"); u && u[0] != '\0') {
        return u[0] != '0';
    }
#ifdef _WIN32
    if (std::getenv("WT_SESSION")) return true; // Windows Terminal (Cascadia)
    if (const char* tp = std::getenv("TERM_PROGRAM"); tp && std::string(tp) == "vscode") return true;
    return false;
#else
    return true;
#endif
}

inline bool& unicode_ref() {
    static bool value = resolve_unicode();
    return value;
}

} // namespace detail

inline void set_color_mode(ColorMode m) {
    detail::mode_ref() = m;
    detail::enabled_ref() = detail::resolve();
}

inline bool color_enabled() { return detail::enabled_ref(); }

// True when decorative glyphs are safe to print on this terminal.
inline bool unicode_enabled() { return detail::unicode_ref(); }

// Returns `g` when glyphs are supported, otherwise an empty string — so callers
// simply drop the symbol (rather than emitting a '?') on plain terminals.
inline std::string glyph(const char* g) {
    return detail::unicode_ref() ? std::string(g) : std::string();
}

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
    if (!unicode_enabled()) {
        // Box-drawing art renders as '?' on plain terminals — use a plain title.
        std::cout << BOLD() << CYAN() << "  N A V" << RESET() << "\n"
                  << "  embedded development orchestrator\n" << std::endl;
        return;
    }
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
    std::cout << CYAN() << glyph("ℹ ") << RESET() << msg << std::endl;
}

inline void success(const std::string& msg) {
    std::cout << GREEN() << BOLD() << glyph("✔ ") << RESET() << GREEN() << msg << RESET() << std::endl;
}

inline void error(const std::string& msg) {
    std::cout << RED() << BOLD() << glyph("✖ ") << RESET() << RED() << msg << RESET() << std::endl;
}

inline void warning(const std::string& msg) {
    std::cout << YELLOW() << BOLD() << glyph("⚠ ") << RESET() << YELLOW() << msg << RESET() << std::endl;
}

inline void step(const std::string& verb, const std::string& item) {
    std::cout << MAGENTA() << BOLD() << " => " << RESET() << BOLD() << verb << " " << RESET() << item << std::endl;
}

// Per-tool result lines. Consolidated here so the toolchain probe output
// (`nav check`) keeps a single rendering path and respects color gating.
inline void tool_ok(const std::string& name, const std::string& detail) {
    std::cout << "  " << GREEN() << glyph("✔ ") << RESET() << BOLD() << name << RESET()
              << " -> " << detail << "\n";
}

inline void tool_missing_critical(const std::string& name) {
    std::cout << "  " << RED() << glyph("✖ ") << RESET() << BOLD() << name << RESET()
              << " -> " << RED() << "NOT FOUND (CRITICAL)" << RESET() << "\n";
}

inline void tool_missing_optional(const std::string& name) {
    std::cout << "  " << YELLOW() << glyph("⚠ ") << RESET() << BOLD() << name << RESET()
              << " -> Optional (Not Installed)\n";
}

} // namespace nav::core::ui
