#include "nav/core/runner.hpp"

#include <regex>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

const std::regex& placeholder() {
    static const std::regex re(R"(\{([A-Za-z_][A-Za-z0-9_]*)\})");
    return re;
}

// build/app -> build/app.bin; build/app.elf -> build/app.bin.
fs::path image_path(const fs::path& elf, const std::string& ext) {
    fs::path p = elf;
    if (p.extension() == ".elf") p.replace_extension(ext);
    else p += ext;
    return p;
}

} // namespace

std::string default_runner(const Board& board) {
    if (!board.runner.empty()) return board.runner;
    if (!board.flash_tool.empty()) return board.flash_tool;
    if (!board.runners.empty()) return board.runners.begin()->first;
    return {};
}

std::string default_flash_binary(const Board& board, const Registry& reg) {
    if (const RunnerDef* def = reg.runner(default_runner(board))) {
        const ToolDef* tool = reg.tool(def->tool);
        return tool && !tool->binary.empty() ? tool->binary : def->tool;
    }
    return board.flash_tool;
}

std::vector<std::string> board_runner_names(const Board& board) {
    std::vector<std::string> out;
    const std::string def = default_runner(board);
    if (!def.empty()) out.push_back(def);
    for (const auto& [name, vars] : board.runners)
        if (name != def) out.push_back(name);
    return out;
}

bool runner_uses(const RunnerDef& runner, const std::string& var) {
    const std::string token = "{" + var + "}";
    for (const auto& a : runner.args)
        if (a.find(token) != std::string::npos) return true;
    return false;
}

std::string objcopy_for(const Board& board) {
    const std::string& cc = board.compiler;
    const std::string suffix = "gcc";
    if (cc.size() >= suffix.size() && cc.compare(cc.size() - suffix.size(), suffix.size(), suffix) == 0)
        return cc.substr(0, cc.size() - suffix.size()) + "objcopy";
    return "objcopy";
}

std::string expand_placeholders(const std::string& tpl, const std::map<std::string, std::string>& vars,
                                std::set<std::string>& missing) {
    std::string out;
    auto last = tpl.cbegin();
    for (std::sregex_iterator it(tpl.begin(), tpl.end(), placeholder()), end; it != end; ++it) {
        const auto& m = *it;
        out.append(last, m[0].first);
        const std::string name = m[1].str();
        if (auto v = vars.find(name); v != vars.end()) {
            out += v->second;
        } else {
            missing.insert(name);
            out += m[0].str();
        }
        last = m[0].second;
    }
    out.append(last, tpl.cend());
    return out;
}

FlashPlan plan_flash(const Board& board, const Registry& reg, const std::string& runner_id,
                     const fs::path& elf, const std::map<std::string, std::string>& cli_vars) {
    FlashPlan plan;
    plan.runner = runner_id.empty() ? default_runner(board) : runner_id;
    if (plan.runner.empty()) {
        plan.error = "board '" + board.id + "' declares no flash runner";
        return plan;
    }
    const RunnerDef* def = reg.runner(plan.runner);
    if (!def) {
        std::string known;
        for (const auto& [id, r] : reg.runners()) known += (known.empty() ? "" : ", ") + id;
        plan.error = "unknown runner '" + plan.runner + "' (known: " + (known.empty() ? "none" : known) + ")";
        return plan;
    }
    plan.tool = def->tool;

    std::map<std::string, std::string> vars = def->defaults;
    if (auto b = board.runners.find(plan.runner); b != board.runners.end())
        for (const auto& [k, v] : b->second) vars[k] = v;
    vars["elf"] = elf.string();
    vars["bin"] = image_path(elf, ".bin").string();
    vars["hex"] = image_path(elf, ".hex").string();
    if (!board.flash_address.empty()) vars["address"] = board.flash_address;
    for (const auto& [k, v] : cli_vars) vars[k] = v;

    if (def->image == "bin") {
        plan.steps.push_back({"Converting", {objcopy_for(board), "-O", "binary", vars["elf"], vars["bin"]}});
    } else if (def->image == "hex") {
        plan.steps.push_back(
            {"Converting", {objcopy_for(board), "-O", "ihex", "-R", ".eeprom", vars["elf"], vars["hex"]}});
    } else if (def->image != "elf") {
        plan.error = "runner '" + plan.runner + "' has unknown image type '" + def->image + "' (use elf, bin or hex)";
        return plan;
    }

    const ToolDef* tool = reg.tool(def->tool);
    FlashStep flash{"Flashing", {tool && !tool->binary.empty() ? tool->binary : def->tool}};
    std::set<std::string> missing;
    for (const auto& a : def->args) flash.argv.push_back(expand_placeholders(a, vars, missing));
    if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        plan.error = "runner '" + plan.runner + "' needs a value for " + list + " on board '" + board.id +
                     "'. Pass --set <name>=<value>, or add it under boards." + board.id + ".runners." +
                     plan.runner + " in ~/.nav/boards.json";
        return plan;
    }
    plan.steps.push_back(std::move(flash));
    return plan;
}

} // namespace nav::core
