#pragma once

#include "nav/core/board.hpp"

#include <map>
#include <string>
#include <vector>

namespace nav::core {

// One platform's portable download for a tool (used when no system package
// manager is available, e.g. a bare Windows box).
struct ToolDownload {
    std::string url;
    std::string archive;     // "zip" | "targz" | "sfxexe"
    std::string bin_subdir;  // dir under the install root holding the binaries
    std::vector<std::string> post; // "alias:src=dst", "pip:<package>"
};

// A tool nav can provision: a system command (`binary`) installable either via a
// system package manager (`packages`: manager -> package name) or, failing that,
// via a portable `download` per host platform key.
struct ToolDef {
    std::string id;
    std::string binary;   // command nav probes / invokes (may differ from id)
    std::string version;  // pinned, so an upstream bump can't silently change us
    std::map<std::string, std::string> packages;     // pm name -> package name
    std::map<std::string, ToolDownload> download;     // platform key -> download
};

// Normalized host platform key for download selection, e.g. "windows-x86_64",
// "linux-x86_64", "darwin-arm64".
std::string host_platform_key();

// The tool + board registry, sourced from the baked-in JSON (data/*.json) and
// merged with the user's ~/.nav/{toolchain,boards}.json when present.
class Registry {
public:
    const std::vector<std::string>& core_tool_ids() const { return core_; }
    const std::map<std::string, ToolDef>& tools() const { return tools_; }
    const std::map<std::string, Board>& boards() const { return boards_; }

    const ToolDef* tool(const std::string& id) const;
    const ToolDef* tool_for_binary(const std::string& binary) const;
    const Board* board(const std::string& id) const;

    // Mutating builders used by the loader (registry.cpp).
    void add_tool(ToolDef t) { tools_[t.id] = std::move(t); }
    void add_board(Board b) { boards_[b.id] = std::move(b); }
    void set_core(std::vector<std::string> c) { core_ = std::move(c); }

private:
    std::vector<std::string> core_;
    std::map<std::string, ToolDef> tools_;
    std::map<std::string, Board> boards_;
};

// Build a Registry purely from two JSON strings (no filesystem access). Used for
// the baked defaults and by tests. Malformed JSON is ignored.
Registry registry_from_json(const std::string& toolchain_json, const std::string& boards_json);

// Overlay user tool/board JSON onto an existing registry — the merge
// build_registry() applies to ~/.nav/{toolchain,boards}.json. Boards merge
// FIELD-LEVEL: only keys present in `boards_json` override; omitted keys keep
// the registry's current value, so new baked fields (e.g. reset_after_flash)
// reach users whose materialized ~/.nav files predate them. Malformed JSON is
// ignored.
void merge_json(Registry& r, const std::string& toolchain_json,
                const std::string& boards_json);

// Lazily built singleton: baked defaults, written to ~/.nav/ on first run and
// merged with any user overrides found there.
const Registry& registry();

} // namespace nav::core
