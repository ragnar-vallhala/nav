#include "nav/core/http_index.hpp"

#include <chrono>
#include <cstdlib>

namespace nav::core {

std::string default_registry_url() {
    if (const char* env = std::getenv("NAV_REGISTRY_URL"); env && env[0] != '\0') {
        return env;
    }
    return "http://localhost:8081";
}

std::optional<IndexPackage> HttpIndexClient::fetch(const std::string& package_name) {
    if (package_name.empty()) return std::nullopt;

    const std::string url = base_url_ + "/api/v1/index/" + package_name;

    // -f: fail (non-zero exit) on HTTP >= 400, so a 404 becomes "not found".
    // -s: no progress meter. -S: still show errors. Capture body silently.
    auto res = ctx_.execute({"curl", "-fsS", url}, "", /*silent=*/true,
                            std::chrono::seconds(30));
    if (res.exit_code != 0) {
        return std::nullopt;
    }
    return parse_index_string(res.output);
}

} // namespace nav::core
