#pragma once

#include <filesystem>
#include <string>

namespace nav::core {

// Per-user nav home that hosts all shared caches:
//   $NAV_HOME            when set (tests + sandboxes)
//   $HOME/.nav           otherwise (and %USERPROFILE%\.nav on Windows)
// Falls back to "./.nav" if none of those are set.
std::filesystem::path nav_home_base();

// Root of the content-addressed package cache, shared across all projects:
//   <nav_home_base>/packages
std::filesystem::path cache_root();

// Shared, version-keyed NavHAL clone location:
//   <nav_home_base>/navhal/<ref>
// where <ref> is the branch/tag the framework was cloned at. One clone per ref
// is reused by every project instead of re-cloning into each project tree.
std::filesystem::path navhal_cache_dir(const std::string& ref);

// True when a NavHAL clone at `dir` exists and carries the completion marker
// (i.e. a prior clone finished and wasn't interrupted).
bool navhal_is_cached(const std::filesystem::path& dir);

// Shared clone location for a git dependency, keyed by repo + ref so the same
// dependency at the same ref is cloned once and reused by every project:
//   <nav_home_base>/deps/<repo-slug>/<ref>
// <repo-slug> is the URL's final path component minus a trailing ".git",
// sanitised to a filesystem-safe token; <ref> defaults to "default" when empty.
std::filesystem::path git_cache_dir(const std::string& url, const std::string& ref);

// Per-package install directory under `root`:
//   <root>/<name>-<version>-<sha12>
// where <sha12> is the first 12 hex chars of the download's SHA256. Including
// the checksum makes the path content-addressed: two builds that pin the same
// version+artifact share a directory, and a re-published (mutated) artifact
// with the same version lands in a distinct directory rather than colliding.
std::filesystem::path package_dir(const std::filesystem::path& root,
                                  const std::string& name,
                                  const std::string& version,
                                  const std::string& sha256);

// The completion marker written inside a package dir once extraction finishes.
// Its presence is what distinguishes a fully-installed package from a partial
// or interrupted extract.
std::filesystem::path install_marker(const std::filesystem::path& package_dir);

// True when `package_dir` exists and carries the completion marker.
bool is_installed(const std::filesystem::path& package_dir);

} // namespace nav::core
