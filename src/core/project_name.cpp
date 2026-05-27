#include "nav/core/project_name.hpp"

namespace nav::core {

std::string validate_project_name(const std::string& name) {
    if (name.empty()) return "name is empty";
    if (name.size() > 128) return "name is longer than 128 characters";
    if (name.front() == '.') return "name cannot begin with '.'";
    if (name.front() == '-') return "name cannot begin with '-'";
    for (unsigned char c : name) {
        bool ok = (c >= 'a' && c <= 'z')
               || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9')
               || c == '.' || c == '_' || c == '-';
        if (!ok) {
            std::string ch;
            if (c == '/')       ch = "'/'";
            else if (c == '\\') ch = "'\\\\'";
            else if (c == 0)    ch = "NUL";
            else                ch = std::string("'") + static_cast<char>(c) + "'";
            return "invalid character " + ch + " (allow only A-Z, a-z, 0-9, '.', '_', '-')";
        }
    }
    return "";
}

} // namespace nav::core
