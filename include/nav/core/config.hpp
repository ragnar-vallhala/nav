#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace nav::core {

struct ProjectConfig {
    std::filesystem::path root;
    std::string project_name;
    std::string target_arch;
    std::string target_vendor;
    std::string target_board;
    std::string build_backend;
};

// Walk up from `start` (default: CWD) until a directory containing nav.toml is
// found. Returns nullopt if no project root is reachable.
std::optional<std::filesystem::path> find_project_root(
    std::filesystem::path start = std::filesystem::current_path());

// Parse nav.toml at the given project root. Returns nullopt on parse failure;
// missing fields default to empty strings.
std::optional<ProjectConfig> load_project_config(
    const std::filesystem::path& project_root);

} // namespace nav::core
