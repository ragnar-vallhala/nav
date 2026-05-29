#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "nav/core/execution_context.hpp"
#include "nav/core/index.hpp"

namespace nav::core {

enum class FetchStatus {
    AlreadyCached,          // package present in the cache; nothing to do
    Fetched,                // downloaded, verified, and extracted this run
    NoDownloadForPlatform,  // no download entry matched the package kind/host
    DownloadFailed,         // curl could not retrieve the archive
    ChecksumMismatch,       // SHA256 of the archive did not match the index
    ExtractFailed,          // archive retrieved + verified but tar failed
    InstallFailed,          // extracted, but could not be placed in the cache
};

std::string to_string(FetchStatus s);

// Whether a status means the package now sits, intact, in the cache.
bool fetch_ok(FetchStatus s);

// What to fetch: the candidate downloads for one resolved package version. The
// fetcher selects the right entry from `downloads` based on `kind` and the
// host `platform_key`.
struct FetchRequest {
    std::string                     name;
    std::string                     version;       // string form, for paths/messages
    PackageKind                     kind = PackageKind::Library;
    std::string                     platform_key;  // host key for toolchain selection
    std::map<std::string, Download> downloads;     // candidates keyed as in the index
};

struct FetchOutcome {
    FetchStatus           status = FetchStatus::DownloadFailed;
    std::string           name;
    std::string           version;
    std::filesystem::path path;          // cache dir (set whenever a download was selected)
    std::string           download_key;  // which downloads-map key was used
    std::string           detail;        // human-readable note / error
};

// Pick the download appropriate for a package kind and host platform:
//   library   -> the "source" entry
//   toolchain -> the entry keyed by `platform_key`
// Returns {key, Download} or nullopt when nothing applies.
std::optional<std::pair<std::string, Download>>
select_download(const std::map<std::string, Download>& downloads,
                PackageKind kind, const std::string& platform_key);

// Downloads, verifies, and extracts packages into a content-addressed cache.
// All external work (curl, sha256sum, tar) goes through the injected execution
// context, so the whole flow is exercisable against the mock.
//
// The install is atomic: the archive is downloaded to a temp file, its SHA256
// checked before anything is unpacked, extracted into a staging directory, and
// only then moved into place and stamped with a completion marker. A partial or
// interrupted run leaves no half-populated package directory behind.
class PackageFetcher {
public:
    PackageFetcher(IExecutionContext& ctx, std::filesystem::path root)
        : ctx_(ctx), root_(std::move(root)) {}

    FetchOutcome fetch(const FetchRequest& req);

    const std::filesystem::path& root() const { return root_; }

private:
    IExecutionContext&    ctx_;
    std::filesystem::path root_;
};

} // namespace nav::core
