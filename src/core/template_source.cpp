#include "nav/core/template_source.hpp"

#include "nav/core/ui.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace nav::core {

std::string template_text(const char* file, std::string_view embedded) {
    const char* dir = std::getenv("NAV_TEMPLATE_DIR");
    if (dir && *dir) {
        const std::filesystem::path p = std::filesystem::path(dir) / file;
        std::ifstream in(p, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            ui::info("Using template " + p.string() + " (NAV_TEMPLATE_DIR)");
            return ss.str();
        }
    }
    return std::string(embedded);
}

} // namespace nav::core
