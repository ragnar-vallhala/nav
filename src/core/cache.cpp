#include "nav/core/cache.hpp"

#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

const char* getenv_nonempty(const char* key) {
    const char* v = std::getenv(key);
    return (v && v[0] != '\0') ? v : nullptr;
}

} // namespace

fs::path cache_root() {
    if (const char* nav_home = getenv_nonempty("NAV_HOME")) {
        return fs::path(nav_home) / "packages";
    }
    if (const char* home = getenv_nonempty("HOME")) {
        return fs::path(home) / ".nav" / "packages";
    }
    return fs::path(".nav") / "packages";
}

fs::path package_dir(const fs::path& root, const std::string& name,
                     const std::string& version, const std::string& sha256) {
    const std::string sha12 = sha256.substr(0, std::min<std::size_t>(12, sha256.size()));
    return root / (name + "-" + version + "-" + sha12);
}

fs::path install_marker(const fs::path& package_dir) {
    return package_dir / ".nav-ok";
}

bool is_installed(const fs::path& package_dir) {
    std::error_code ec;
    return fs::exists(package_dir, ec) && fs::exists(install_marker(package_dir), ec);
}

} // namespace nav::core
