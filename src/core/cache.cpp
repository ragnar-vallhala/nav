#include "nav/core/cache.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

const char* getenv_nonempty(const char* key) {
    const char* v = std::getenv(key);
    return (v && v[0] != '\0') ? v : nullptr;
}

} // namespace

fs::path nav_home_base() {
    if (const char* nav_home = getenv_nonempty("NAV_HOME")) {
        return fs::path(nav_home);
    }
    if (const char* home = getenv_nonempty("HOME")) {
        return fs::path(home) / ".nav";
    }
#ifdef _WIN32
    if (const char* up = getenv_nonempty("USERPROFILE")) {
        return fs::path(up) / ".nav";
    }
#endif
    return fs::path(".nav");
}

fs::path cache_root() {
    return nav_home_base() / "packages";
}

fs::path navhal_cache_dir(const std::string& ref) {
    return nav_home_base() / "navhal" / ref;
}

bool navhal_is_cached(const fs::path& dir) {
    return is_installed(dir);
}

fs::path git_cache_dir(const std::string& url, const std::string& ref) {
    // Derive a filesystem-safe slug from the URL's final path component.
    std::string tail = url;
    if (auto slash = tail.find_last_of("/\\"); slash != std::string::npos)
        tail = tail.substr(slash + 1);
    if (tail.size() >= 4 && tail.compare(tail.size() - 4, 4, ".git") == 0)
        tail.resize(tail.size() - 4);
    auto sanitize = [](std::string s) {
        for (char& c : s)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
                c = '_';
        if (s.empty()) s = "repo";
        return s;
    };
    const std::string slug = sanitize(tail);
    const std::string ref_key = ref.empty() ? "default" : sanitize(ref);
    return nav_home_base() / "deps" / slug / ref_key;
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
