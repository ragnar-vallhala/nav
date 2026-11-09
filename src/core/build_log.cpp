#include "nav/core/build_log.hpp"

#include <cctype>
#include <set>
#include <sstream>

namespace nav::core {

namespace {

bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

bool is_indented(const std::string& l) { return !l.empty() && (l[0] == ' ' || l[0] == '\t'); }

bool is_warning(const std::string& l) { return !is_indented(l) && contains(l, ": warning: "); }

bool is_error(const std::string& l) {
    return !is_indented(l) && (contains(l, ": error: ") || contains(l, ": fatal error: "));
}

// "[ 12%] Building ..." (make) or "[12/50] Building ..." (ninja).
bool is_progress(const std::string& l) {
    if (l.size() < 3 || l[0] != '[') return false;
    size_t i = 1;
    while (i < l.size() && l[i] == ' ') ++i;
    const size_t d = i;
    while (i < l.size() && std::isdigit(static_cast<unsigned char>(l[i]))) ++i;
    return i > d && i < l.size() && (l[i] == '%' || l[i] == '/');
}

// Lines gcc prints before a diagnostic to say where it is.
bool is_banner(const std::string& l) {
    return l.rfind("In file included from", 0) == 0 || contains(l, ": In function ") ||
           contains(l, ": In member function ") || contains(l, ": In constructor ") ||
           contains(l, ": At top level:");
}

} // namespace

BuildWarnings extract_build_warnings(const std::string& output) {
    BuildWarnings out;
    std::set<std::string> seen;
    std::string block;
    bool open = false;

    auto flush = [&] {
        if (!open) return;
        open = false;
        while (!block.empty() && block.back() == '\n') block.pop_back();
        ++out.total;
        if (seen.insert(block).second) out.blocks.push_back(block);
        block.clear();
    };

    std::istringstream in(output);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (is_warning(line)) {
            flush();
            open = true;
            block = line + "\n";
        } else if (is_error(line) || is_progress(line) || is_banner(line)) {
            flush();
        } else if (open) {
            block += line + "\n";
        }
    }
    flush();
    return out;
}

} // namespace nav::core
