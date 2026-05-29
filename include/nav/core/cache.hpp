#pragma once

#include <filesystem>
#include <string>

namespace nav::core {

// Root of the content-addressed package cache, shared across all projects:
//   $NAV_HOME/packages   when NAV_HOME is set (used by tests + sandboxes)
//   $HOME/.nav/packages  otherwise
// Falls back to "./.nav/packages" if neither env var is set.
std::filesystem::path cache_root();

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
