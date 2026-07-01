#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nav::core {

// A NavHAL Kconfig .config models *capabilities*. An application owns the
// authoritative .config that configures NavHAL; a library ships its own to
// declare the capabilities it needs. nav unions a library's needs into the app
// config when the dependency is added, and enforces at build time that the app
// config satisfies every dependency's needs.

// Parse a Kconfig .config into its assigned entries (KEY -> VALUE), e.g.
// CONFIG_DRV_UART -> "y", CONFIG_BOARD -> "\"nucleo_f401re\"". Blank lines,
// comments, and "# CONFIG_X is not set" lines are skipped (the last means X is
// unset, i.e. absent for satisfaction purposes).
std::map<std::string, std::string> parse_kconfig(const std::filesystem::path& path);

// The NavHAL capability config a library ships, if any: <root>/.config (the
// nav-native name) then <root>/navhal.config. Empty optional when the library
// declares no NavHAL needs (NavHAL itself needs none).
std::optional<std::filesystem::path> find_navhal_config(const std::filesystem::path& root);

// Entries in `required` (a library's config) NOT satisfied by `have` (the app's
// config): a key missing, unset, or set to a different value. Each is returned
// in "KEY=VALUE" form (the value the library requires), sorted for stable output.
std::vector<std::string> unmet_requirements(
    const std::map<std::string, std::string>& have,
    const std::map<std::string, std::string>& required);

// A capability required by a dependency but not satisfied by the app config,
// split by kind so callers can treat them differently: a `missing` key can be
// safely unioned in (append), whereas a `conflict` (key present with a value
// the dependency can't use) must be reconciled by a human.
struct RequirementDiff {
    struct Conflict { std::string key, have, need; };
    std::vector<std::string> missing;    // "KEY=VALUE" — absent from `have`, append-safe
    std::vector<Conflict>    conflicts;  // present in `have` with a differing value
};

// Classify how `required` (a dependency's config) fails against `have` (the
// app's), separating append-safe missing keys from human-reconcile conflicts.
// Both lists are sorted by key for stable, deterministic output.
RequirementDiff diff_requirements(
    const std::map<std::string, std::string>& have,
    const std::map<std::string, std::string>& required);

} // namespace nav::core
