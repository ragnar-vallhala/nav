#include "nav/core/fetch.hpp"

#include "nav/core/cache.hpp"
#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

using nav::core::Download;
using nav::core::FetchRequest;
using nav::core::FetchStatus;
using nav::core::PackageFetcher;
using nav::core::PackageKind;
using nav::test::ExecutedCall;
using nav::test::MockExecutionContext;

namespace {

constexpr const char* kSha = "1111111111111111111111111111111111111111111111111111111111111111";

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_fetch_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

FetchRequest library_request(const std::string& sha = kSha) {
    FetchRequest r;
    r.name = "nav-hal";
    r.version = "0.5.0";
    r.kind = PackageKind::Library;
    r.downloads.emplace("source", Download{"http://reg/nav-hal-0.5.0.tar.gz", sha});
    return r;
}

// A scripted runner emulating curl/sha256sum/tar. `reported_sha` is what
// sha256sum prints; per-tool exit codes default to success.
MockExecutionContext::Handler scripted(const std::string& reported_sha,
                                       int curl_exit = 0, int tar_exit = 0,
                                       int sum_exit = 0) {
    return [=](const ExecutedCall& call) -> nav::core::CommandResult {
        const std::string& tool = call.argv.front();
        if (tool == "curl")      return {curl_exit, ""};
        if (tool == "sha256sum") return {sum_exit, reported_sha + "  /tmp/archive\n"};
        if (tool == "tar")       return {tar_exit, ""};
        return {0, ""};
    };
}

} // namespace

TEST(SelectDownload, LibraryPicksSource) {
    std::map<std::string, Download> dls{{"source", {"u", "s"}}};
    auto sel = nav::core::select_download(dls, PackageKind::Library, "linux_x86_64");
    ASSERT_TRUE(sel.has_value());
    EXPECT_EQ(sel->first, "source");
}

TEST(SelectDownload, ToolchainPicksPlatform) {
    std::map<std::string, Download> dls{{"linux_x86_64", {"u1", "s1"}},
                                        {"darwin_arm64", {"u2", "s2"}}};
    auto sel = nav::core::select_download(dls, PackageKind::Toolchain, "darwin_arm64");
    ASSERT_TRUE(sel.has_value());
    EXPECT_EQ(sel->first, "darwin_arm64");
    EXPECT_EQ(sel->second.url, "u2");
}

TEST(SelectDownload, ToolchainMissingPlatformReturnsNullopt) {
    std::map<std::string, Download> dls{{"linux_x86_64", {"u1", "s1"}}};
    auto sel = nav::core::select_download(dls, PackageKind::Toolchain, "windows_x86_64");
    EXPECT_FALSE(sel.has_value());
}

TEST(Fetch, HappyPathDownloadsVerifiesExtractsAndInstalls) {
    TempDir td;
    MockExecutionContext mock;
    mock.handler = scripted(kSha);

    PackageFetcher fetcher(mock, td.path());
    auto out = fetcher.fetch(library_request());

    EXPECT_EQ(out.status, FetchStatus::Fetched) << out.detail;
    EXPECT_EQ(out.download_key, "source");
    EXPECT_TRUE(nav::core::is_installed(out.path));

    // curl, sha256sum, tar were each invoked (order preserved).
    ASSERT_EQ(mock.calls.size(), 3u);
    EXPECT_EQ(mock.calls[0].argv.front(), "curl");
    EXPECT_EQ(mock.calls[1].argv.front(), "sha256sum");
    EXPECT_EQ(mock.calls[2].argv.front(), "tar");
    // curl's -o target lives inside the cache root; its last arg is the URL.
    ASSERT_GE(mock.calls[0].argv.size(), 5u);
    EXPECT_NE(mock.calls[0].argv[3].find(td.path().string()), std::string::npos);
    EXPECT_EQ(mock.calls[0].argv.back(), "http://reg/nav-hal-0.5.0.tar.gz");
}

TEST(Fetch, AlreadyCachedShortCircuits) {
    TempDir td;
    auto req = library_request();
    // Pre-create the installed package dir + marker.
    auto dir = nav::core::package_dir(td.path(), req.name, req.version, kSha);
    fs::create_directories(dir);
    std::ofstream(nav::core::install_marker(dir)) << kSha << "\n";

    MockExecutionContext mock;
    mock.handler = scripted(kSha);
    PackageFetcher fetcher(mock, td.path());
    auto out = fetcher.fetch(req);

    EXPECT_EQ(out.status, FetchStatus::AlreadyCached);
    EXPECT_TRUE(mock.calls.empty());  // no curl/sha/tar
}

TEST(Fetch, ChecksumMismatchIsRejectedAndNothingInstalled) {
    TempDir td;
    MockExecutionContext mock;
    // sha256sum reports a different digest than the request expects.
    mock.handler = scripted("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");

    PackageFetcher fetcher(mock, td.path());
    auto out = fetcher.fetch(library_request());

    EXPECT_EQ(out.status, FetchStatus::ChecksumMismatch);
    EXPECT_FALSE(nav::core::is_installed(out.path));
    // tar must NOT run after a checksum failure.
    for (const auto& c : mock.calls) EXPECT_NE(c.argv.front(), "tar");
}

TEST(Fetch, DownloadFailureReported) {
    TempDir td;
    MockExecutionContext mock;
    mock.handler = scripted(kSha, /*curl_exit=*/22);  // HTTP 404
    PackageFetcher fetcher(mock, td.path());
    auto out = fetcher.fetch(library_request());

    EXPECT_EQ(out.status, FetchStatus::DownloadFailed);
    EXPECT_FALSE(nav::core::is_installed(out.path));
    ASSERT_EQ(mock.calls.size(), 1u);  // stopped after curl
    EXPECT_EQ(mock.calls[0].argv.front(), "curl");
}

TEST(Fetch, ExtractFailureReported) {
    TempDir td;
    MockExecutionContext mock;
    mock.handler = scripted(kSha, /*curl_exit=*/0, /*tar_exit=*/2);
    PackageFetcher fetcher(mock, td.path());
    auto out = fetcher.fetch(library_request());

    EXPECT_EQ(out.status, FetchStatus::ExtractFailed);
    EXPECT_FALSE(nav::core::is_installed(out.path));
}

TEST(Fetch, NoDownloadForToolchainPlatform) {
    TempDir td;
    FetchRequest r;
    r.name = "arm-none-eabi-gcc";
    r.version = "13.2.0";
    r.kind = PackageKind::Toolchain;
    r.platform_key = "windows_x86_64";  // not offered
    r.downloads.emplace("linux_x86_64", Download{"http://reg/gcc.tar.xz", kSha});

    MockExecutionContext mock;
    PackageFetcher fetcher(mock, td.path());
    auto out = fetcher.fetch(r);

    EXPECT_EQ(out.status, FetchStatus::NoDownloadForPlatform);
    EXPECT_TRUE(mock.calls.empty());
}

TEST(Fetch, ChecksumComparisonIsCaseInsensitive) {
    TempDir td;
    MockExecutionContext mock;
    // request sha is lowercase kSha (all 1s — case-irrelevant); use a mixed-case
    // digest to exercise the path with hex letters.
    const std::string mixed = "AbCdEf0123456789aBcDeF0123456789ABCDEF0123456789abcdef0123456789";
    mock.handler = scripted(/*reported*/ mixed);
    PackageFetcher fetcher(mock, td.path());

    auto req = library_request(/*sha=*/"abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    auto out = fetcher.fetch(req);
    EXPECT_EQ(out.status, FetchStatus::Fetched) << out.detail;
}
