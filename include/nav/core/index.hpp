#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "nav/core/semver.hpp"

namespace nav::core {

enum class PackageKind {
    Library,
    Toolchain,
};

std::optional<PackageKind> parse_kind(const std::string& s);
std::string                to_string(PackageKind k);

// A single platform's download for a package version. Keyed externally by
// "<os>_<arch>" (e.g. "linux_x86_64", "darwin_arm64") so toolchain packages
// can ship multi-arch tarballs from one entry.
struct Download {
    std::string url;
    std::string sha256;   // hex digest (no "sha256:" prefix)
};

// One concrete version of a package as listed in the registry index.
struct IndexVersion {
    Version     version;
    PackageKind kind = PackageKind::Library;

    // Library kind: usually a single "source" download in `downloads`.
    // Toolchain kind: one entry per supported host platform.
    std::map<std::string, Download> downloads;

    // Transitive deps: package name → required version range.
    std::map<std::string, VersionReq> dependencies;

    // Toolchain-kind only: which binaries the package contributes.
    std::vector<std::string> toolchain_binaries;
};

// All known versions of a single package, sorted ascending by SemVer.
struct IndexPackage {
    std::string               name;
    std::vector<IndexVersion> versions;
};

// Parse a registry-index JSON file describing a single package. Returns
// nullopt on parse failure or when a required field is missing.
std::optional<IndexPackage> parse_index_file(const std::filesystem::path& path);

// Interface clients use to consult the registry. Tests use LocalIndexClient
// against a fixture directory; Phase 2.3 will add an HttpIndexClient backed
// by `nav-index`.
class IIndexClient {
public:
    virtual ~IIndexClient() = default;
    virtual std::optional<IndexPackage> fetch(const std::string& package_name) = 0;
};

// Reads a sharded directory tree of *.json files:
//   <root>/<two-char-prefix>/<package-name>.json
// Names shorter than two characters use a single-char prefix dir.
class LocalIndexClient : public IIndexClient {
public:
    explicit LocalIndexClient(std::filesystem::path root) : root_(std::move(root)) {}
    std::optional<IndexPackage> fetch(const std::string& package_name) override;

    // Compute the sharded path under root_ for `package_name`. Exposed for
    // tests + future writers (publishers).
    std::filesystem::path index_path(const std::string& package_name) const;

private:
    std::filesystem::path root_;
};

} // namespace nav::core
