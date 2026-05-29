#pragma once

#include <optional>
#include <string>

#include "nav/core/execution_context.hpp"
#include "nav/core/index.hpp"

namespace nav::core {

// Registry base URL resolution: $NAV_REGISTRY_URL if set, else the local
// development default.
std::string default_registry_url();

// IIndexClient backed by an HTTP registry. Fetches
//   <base_url>/api/v1/index/<name>
// by shelling out to `curl` through the injected execution context, then
// parsing the JSON body. Using IExecutionContext keeps this testable (the
// mock records the curl argv) and avoids linking an HTTP library.
class HttpIndexClient : public IIndexClient {
public:
    HttpIndexClient(std::string base_url, IExecutionContext& ctx)
        : base_url_(std::move(base_url)), ctx_(ctx) {}

    std::optional<IndexPackage> fetch(const std::string& package_name) override;

    const std::string& base_url() const { return base_url_; }

private:
    std::string base_url_;
    IExecutionContext& ctx_;
};

} // namespace nav::core
