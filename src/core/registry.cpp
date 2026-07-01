#include "nav/core/registry.hpp"

#include "nav/core/cache.hpp"
#include "nav/config/embedded_config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nav::core {

namespace {

void load_tools(const json& j, Registry& r) {
    if (!j.contains("tools")) return;
    for (auto& [id, t] : j["tools"].items()) {
        ToolDef def;
        def.id = id;
        def.binary = t.value("binary", id);
        def.version = t.value("version", std::string{});
        if (t.contains("packages")) {
            for (auto& [pm, pkg] : t["packages"].items())
                def.packages[pm] = pkg.get<std::string>();
        }
        if (t.contains("download")) {
            for (auto& [plat, d] : t["download"].items()) {
                ToolDownload dl;
                dl.url = d.value("url", std::string{});
                dl.archive = d.value("archive", std::string{"zip"});
                dl.bin_subdir = d.value("bin_subdir", std::string{});
                if (d.contains("post"))
                    for (auto& p : d["post"]) dl.post.push_back(p.get<std::string>());
                def.download[plat] = std::move(dl);
            }
        }
        r.add_tool(std::move(def));
    }
}

void load_toolchain(const json& j, Registry& r) {
    load_tools(j, r);
    if (j.contains("core")) {
        std::vector<std::string> core;
        for (auto& x : j["core"]) core.push_back(x.get<std::string>());
        r.set_core(std::move(core));
    }
}

std::vector<std::string> str_array(const json& node, const char* key) {
    std::vector<std::string> out;
    if (node.contains(key))
        for (auto& x : node[key]) out.push_back(x.get<std::string>());
    return out;
}

void load_boards(const json& j, Registry& r) {
    load_tools(j, r); // board-specific tools (compilers, flashers)
    if (!j.contains("boards")) return;
    for (auto& [id, b] : j["boards"].items()) {
        // Field-level overlay: start from any existing (baked) board so a user
        // file that omits a key inherits the baked value rather than clearing
        // it. This lets new baked fields (e.g. reset_after_flash) reach users
        // whose materialized ~/.nav/boards.json predates them; only keys present
        // in this JSON override.
        Board board = r.board(id) ? *r.board(id) : Board{};
        board.id = id;
        if (b.contains("name"))                board.name = b.value("name", std::string{});
        if (b.contains("arch"))                board.arch = b.value("arch", std::string{});
        if (b.contains("vendor"))              board.vendor = b.value("vendor", std::string{});
        if (b.contains("mcu"))                 board.mcu = b.value("mcu", std::string{});
        if (b.contains("navhal_board"))        board.navhal_board = b.value("navhal_board", id);
        if (b.contains("compiler"))            board.compiler = b.value("compiler", std::string{});
        if (b.contains("flash_tool"))          board.flash_tool = b.value("flash_tool", std::string{});
        if (b.contains("flash_address"))       board.flash_address = b.value("flash_address", std::string{});
        if (b.contains("reset_after_flash"))   board.reset_after_flash = b.value("reset_after_flash", false);
        if (b.contains("reset_command"))       board.reset_command = str_array(b, "reset_command");
        if (b.contains("compile_flags"))       board.compile_flags = str_array(b, "compile_flags");
        if (b.contains("link_flags"))          board.link_flags = str_array(b, "link_flags");
        if (b.contains("default_framework"))   board.default_framework = b.value("default_framework", std::string{});
        if (b.contains("supported_frameworks")) board.supported_frameworks = str_array(b, "supported_frameworks");
        r.add_board(std::move(board));
    }
}

void parse_into(const std::string& text, Registry& r, bool is_boards) {
    try {
        json j = json::parse(text);
        if (is_boards) load_boards(j, r);
        else load_toolchain(j, r);
    } catch (const std::exception&) {
        // Ignore malformed config; baked defaults remain in effect.
    }
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

Registry build_registry() {
    // 1. Baked-in defaults (compiled into the binary; no external file needed).
    Registry r = registry_from_json(std::string(config::kBakedToolchainJson),
                                     std::string(config::kBakedBoardsJson));

    // 2. First run: drop the defaults into ~/.nav/ so the user can extend them.
    const fs::path base = nav_home_base();
    const fs::path tc = base / "toolchain.json";
    const fs::path bd = base / "boards.json";
    std::error_code ec;
    fs::create_directories(base, ec);
    if (!fs::exists(tc, ec)) std::ofstream(tc) << config::kBakedToolchainJson;
    if (!fs::exists(bd, ec)) std::ofstream(bd) << config::kBakedBoardsJson;

    // 3. Merge user overrides/additions (field-level for boards).
    merge_json(r, fs::exists(tc, ec) ? read_file(tc) : std::string{},
               fs::exists(bd, ec) ? read_file(bd) : std::string{});

    return r;
}

} // namespace

Registry registry_from_json(const std::string& toolchain_json, const std::string& boards_json) {
    Registry r;
    parse_into(toolchain_json, r, /*is_boards=*/false);
    parse_into(boards_json, r, /*is_boards=*/true);
    return r;
}

void merge_json(Registry& r, const std::string& toolchain_json,
                const std::string& boards_json) {
    if (!toolchain_json.empty()) parse_into(toolchain_json, r, /*is_boards=*/false);
    if (!boards_json.empty())    parse_into(boards_json, r, /*is_boards=*/true);
}

std::string host_platform_key() {
    std::string os;
#if defined(_WIN32)
    os = "windows";
#elif defined(__APPLE__)
    os = "darwin";
#else
    os = "linux";
#endif
    std::string arch;
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    arch = "arm64";
#else
    arch = "unknown";
#endif
    return os + "-" + arch;
}

const ToolDef* Registry::tool(const std::string& id) const {
    auto it = tools_.find(id);
    return it == tools_.end() ? nullptr : &it->second;
}

const ToolDef* Registry::tool_for_binary(const std::string& binary) const {
    for (const auto& [id, t] : tools_) {
        if (t.binary == binary || id == binary) return &t;
    }
    return nullptr;
}

const Board* Registry::board(const std::string& id) const {
    auto it = boards_.find(id);
    return it == boards_.end() ? nullptr : &it->second;
}

const Registry& registry() {
    static const Registry r = build_registry();
    return r;
}

} // namespace nav::core
