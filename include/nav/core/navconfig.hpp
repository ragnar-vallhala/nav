#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <set>
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

// Symbols a .config explicitly turns off with "# CONFIG_X is not set" (as
// "CONFIG_X"). parse_kconfig() drops these lines, so this is how an explicit
// "off" is told apart from a symbol the app never mentioned.
std::set<std::string> parse_kconfig_unset(const std::filesystem::path& path);

// A dependency requirement the app's own .config contradicts. The app wins.
struct RequirementOverride {
    std::string dep, key, need, have; // `have` is "not set" or the app's value
};

// Merge one dependency's `required` config into the app's view of it without
// touching any file. `have`/`unset` describe the app's .config (`have` is
// updated as requirements are accepted so a later dependency sees them).
// Returns the "KEY=VALUE" assignments to add to the effective config;
// requirements the app explicitly sets otherwise (another value, or
// "# ... is not set") go to `overrides` and are left out: the app's choice wins.
std::vector<std::string> merge_requirements(std::map<std::string, std::string>& have,
                                            const std::set<std::string>& unset,
                                            const std::map<std::string, std::string>& required,
                                            const std::string& dep,
                                            std::vector<RequirementOverride>& overrides);

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

// Apply Kconfig assignments to .config text. Each entry is "CONFIG_X=value" or
// "# CONFIG_X is not set". A line for a symbol already in `text` (in either
// form) is replaced in place; further duplicate lines for it are dropped.
// Symbols not present are appended after a blank line and `header` comment
// (omitted when empty). Later entries in `assignments` win over earlier ones.
std::string apply_config_assignments(const std::string& text,
                                     const std::vector<std::string>& assignments,
                                     const std::string& header = "");

// File form of apply_config_assignments; rewrites `path` only on change.
// Returns false if the file cannot be written.
bool update_config_file(const std::filesystem::path& path,
                        const std::vector<std::string>& assignments,
                        const std::string& header = "");

} // namespace nav::core
