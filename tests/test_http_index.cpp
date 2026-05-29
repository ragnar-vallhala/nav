#include "nav/core/http_index.hpp"

#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <cstdlib>

using nav::core::HttpIndexClient;
using nav::test::MockExecutionContext;

namespace {

constexpr const char* kLibraryJson = R"({
  "name": "nav-hal",
  "versions": [
    { "version": "0.5.0", "kind": "library",
      "downloads": { "source": { "url": "http://x/nav-hal-0.5.0.tar.gz", "sha256": "deadbeef" } },
      "dependencies": { "cmsis": "^6.0.0" } }
  ]
})";

} // namespace

TEST(HttpIndexClient, FetchHitsExpectedUrlAndParses) {
    MockExecutionContext mock;
    mock.scripted_result = {0, kLibraryJson};

    HttpIndexClient client("http://localhost:8081", mock);
    auto pkg = client.fetch("nav-hal");

    ASSERT_TRUE(pkg.has_value());
    EXPECT_EQ(pkg->name, "nav-hal");
    ASSERT_EQ(pkg->versions.size(), 1u);
    EXPECT_EQ(nav::core::to_string(pkg->versions[0].version), "0.5.0");

    // Verify the curl invocation targeted the index endpoint.
    ASSERT_EQ(mock.calls.size(), 1u);
    const auto& argv = mock.calls[0].argv;
    ASSERT_FALSE(argv.empty());
    EXPECT_EQ(argv.front(), "curl");
    EXPECT_EQ(argv.back(), "http://localhost:8081/api/v1/index/nav-hal");
}

TEST(HttpIndexClient, NonZeroCurlExitReturnsNullopt) {
    MockExecutionContext mock;
    mock.scripted_result = {22, ""};  // curl -f exit code for HTTP 404
    HttpIndexClient client("http://localhost:8081", mock);
    EXPECT_FALSE(client.fetch("ghost").has_value());
}

TEST(HttpIndexClient, EmptyNameReturnsNulloptWithoutCall) {
    MockExecutionContext mock;
    HttpIndexClient client("http://localhost:8081", mock);
    EXPECT_FALSE(client.fetch("").has_value());
    EXPECT_TRUE(mock.calls.empty());
}

TEST(HttpIndex, DefaultRegistryUrlHonoursEnv) {
    ::setenv("NAV_REGISTRY_URL", "http://example.test:9999", 1);
    EXPECT_EQ(nav::core::default_registry_url(), "http://example.test:9999");
    ::unsetenv("NAV_REGISTRY_URL");
    EXPECT_EQ(nav::core::default_registry_url(), "http://localhost:8081");
}
