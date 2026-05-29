#include "nav/core/fetch.hpp"

#include "nav/core/cache.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>

namespace fs = std::filesystem;

namespace nav::core {

std::string to_string(FetchStatus s) {
    switch (s) {
        case FetchStatus::AlreadyCached:         return "already cached";
        case FetchStatus::Fetched:               return "fetched";
        case FetchStatus::NoDownloadForPlatform: return "no download for platform";
        case FetchStatus::DownloadFailed:        return "download failed";
        case FetchStatus::ChecksumMismatch:      return "checksum mismatch";
        case FetchStatus::ExtractFailed:         return "extract failed";
        case FetchStatus::InstallFailed:         return "install failed";
    }
    return "unknown";
}

bool fetch_ok(FetchStatus s) {
    return s == FetchStatus::AlreadyCached || s == FetchStatus::Fetched;
}

std::optional<std::pair<std::string, Download>>
select_download(const std::map<std::string, Download>& downloads,
                PackageKind kind, const std::string& platform_key) {
    const std::string key = (kind == PackageKind::Toolchain) ? platform_key : "source";
    auto it = downloads.find(key);
    if (it == downloads.end()) return std::nullopt;
    return std::make_pair(it->first, it->second);
}

namespace {

// Case-insensitive equality for hex digests.
bool hex_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// First whitespace-delimited token of `sha256sum` output (the digest).
std::string first_token(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    std::size_t end = s.find_first_of(" \t\r\n", start);
    return s.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

} // namespace

FetchOutcome PackageFetcher::fetch(const FetchRequest& req) {
    FetchOutcome out;
    out.name = req.name;
    out.version = req.version;

    auto selected = select_download(req.downloads, req.kind, req.platform_key);
    if (!selected) {
        out.status = FetchStatus::NoDownloadForPlatform;
        out.detail = (req.kind == PackageKind::Toolchain)
                         ? ("no '" + req.platform_key + "' download")
                         : "no 'source' download";
        return out;
    }
    out.download_key = selected->first;
    const Download& dl = selected->second;

    const fs::path dir = package_dir(root_, req.name, req.version, dl.sha256);
    out.path = dir;

    // Idempotent: a complete prior install short-circuits everything.
    if (is_installed(dir)) {
        out.status = FetchStatus::AlreadyCached;
        return out;
    }

    std::error_code ec;
    const fs::path tmp = root_ / ".tmp";
    fs::create_directories(tmp, ec);
    if (ec) {
        out.status = FetchStatus::InstallFailed;
        out.detail = "cannot create cache tmp dir: " + ec.message();
        return out;
    }

    // Deterministic temp names keyed by the content hash — no timestamps, so a
    // retry reuses (and overwrites) the same scratch paths cleanly.
    const std::string tag = req.name + "-" + req.version + "-"
                          + dl.sha256.substr(0, std::min<std::size_t>(12, dl.sha256.size()));
    const fs::path archive = tmp / (tag + ".archive");
    const fs::path staging = tmp / (tag + ".stage");
    fs::remove(archive, ec);
    fs::remove_all(staging, ec);

    // 1. Download. -f: fail on HTTP >= 400; -L: follow redirects; -S: surface
    //    errors even though we capture silently.
    auto dlres = ctx_.execute({"curl", "-fsSL", "-o", archive.string(), dl.url},
                              "", /*silent=*/true, std::chrono::minutes(10));
    if (dlres.exit_code != 0) {
        fs::remove(archive, ec);
        out.status = FetchStatus::DownloadFailed;
        out.detail = "curl exited " + std::to_string(dlres.exit_code) + " for " + dl.url;
        return out;
    }

    // 2. Verify SHA256 before unpacking anything.
    auto sumres = ctx_.execute({"sha256sum", archive.string()},
                               "", /*silent=*/true, std::chrono::minutes(2));
    if (sumres.exit_code != 0) {
        fs::remove(archive, ec);
        out.status = FetchStatus::ChecksumMismatch;
        out.detail = "sha256sum failed to read the archive";
        return out;
    }
    const std::string got = first_token(sumres.output);
    if (!hex_equal(got, dl.sha256)) {
        fs::remove(archive, ec);
        out.status = FetchStatus::ChecksumMismatch;
        out.detail = "expected " + dl.sha256 + ", got " + got;
        return out;
    }

    // 3. Extract into staging. tar auto-detects gzip/xz/zstd compression.
    fs::create_directories(staging, ec);
    if (ec) {
        fs::remove(archive, ec);
        out.status = FetchStatus::InstallFailed;
        out.detail = "cannot create staging dir: " + ec.message();
        return out;
    }
    auto exres = ctx_.execute({"tar", "-xf", archive.string(), "-C", staging.string()},
                              "", /*silent=*/true, std::chrono::minutes(5));
    if (exres.exit_code != 0) {
        fs::remove(archive, ec);
        fs::remove_all(staging, ec);
        out.status = FetchStatus::ExtractFailed;
        out.detail = "tar exited " + std::to_string(exres.exit_code);
        return out;
    }

    // 4. Atomically place the staged tree, then stamp the completion marker.
    fs::remove_all(dir, ec);
    if (dir.has_parent_path()) fs::create_directories(dir.parent_path(), ec);
    fs::rename(staging, dir, ec);
    if (ec) {
        // Same-filesystem rename should not fail, but fall back to a copy in
        // case the cache root straddles a mount boundary.
        std::error_code copy_ec;
        fs::copy(staging, dir, fs::copy_options::recursive, copy_ec);
        fs::remove_all(staging, ec);
        if (copy_ec) {
            fs::remove(archive, ec);
            fs::remove_all(dir, ec);
            out.status = FetchStatus::InstallFailed;
            out.detail = "could not place package: " + copy_ec.message();
            return out;
        }
    }

    std::ofstream marker(install_marker(dir));
    marker << dl.sha256 << "\n";
    if (!marker.good()) {
        fs::remove(archive, ec);
        fs::remove_all(dir, ec);
        out.status = FetchStatus::InstallFailed;
        out.detail = "could not write completion marker";
        return out;
    }

    fs::remove(archive, ec);
    out.status = FetchStatus::Fetched;
    return out;
}

} // namespace nav::core
