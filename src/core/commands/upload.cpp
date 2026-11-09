#include "nav/core/board.hpp"
#include "nav/core/command.hpp"
#include "nav/core/config.hpp"
#include "nav/core/probe.hpp"
#include "nav/core/provision.hpp"
#include "nav/core/registry.hpp"
#include "nav/core/runner.hpp"
#include "nav/core/serial.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"
#include "nav/core/variant.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

std::string join(const std::vector<std::string>& argv) {
    std::string out;
    for (const auto& a : argv) {
        const bool quote = a.find_first_of(" \t\"") != std::string::npos;
        out += (out.empty() ? "" : " ") + (quote ? "'" + a + "'" : a);
    }
    return out;
}

const char* kUsage =
    "Usage: nav upload [--runner <name>] [--port <dev>] [--set <name>=<value>]... [--probe <serial|usb-path>]\n"
    "                  [--connect-under-reset] [--variant <name>] [--dry-run] [--list] [--list-probes]";

std::string env_value(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// Where ST-Links are enumerated; NAV_USB_SYSFS points it at a fake tree for tests.
fs::path usb_sysfs() {
    const std::string over = env_value("NAV_USB_SYSFS");
    return over.empty() ? fs::path("/sys/bus/usb/devices") : fs::path(over);
}

int list_probes() {
    if (!probe_discovery_supported()) {
        ui::error("--list-probes is not supported on this platform (it reads Linux sysfs). "
                  "Name the probe directly with --probe <serial|usb-path>.");
        return 1;
    }
    const auto probes = find_stlink_probes(usb_sysfs());
    if (probes.empty()) {
        ui::info("No ST-Link attached.");
        return 0;
    }
    ui::info(std::to_string(probes.size()) + " ST-Link(s) attached:");
    std::string text = describe_probes(probes, "  ");
    while (!text.empty() && text.back() == '\n') text.pop_back();
    ui::info(text);
    ui::info("Pick one with: nav upload --probe <usb-path|serial>   (or NAV_PROBE, or [upload] probe in nav.toml)");
    return 0;
}

} // namespace

int UploadCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    std::string runner_id;
    std::map<std::string, std::string> cli_vars;
    bool dry_run = false, list = false, want_list_probes = false, connect_under_reset = false;
    std::string cli_probe, variant_name;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        const bool has_value = i + 1 < args.size();
        if ((a == "--runner" || a == "-r") && has_value) runner_id = args[++i];
        else if ((a == "--port" || a == "-p") && has_value) cli_vars["port"] = args[++i];
        else if (a == "--set" && has_value) {
            const std::string kv = args[++i];
            const auto eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) { ui::error("--set expects <name>=<value>, got '" + kv + "'."); return 1; }
            cli_vars[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        else if (a == "--dry-run" || a == "-n") dry_run = true;
        else if (a == "--list") list = true;
        else if (a == "--list-probes") want_list_probes = true;
        else if (a == "--probe" && has_value) cli_probe = args[++i];
        else if (a == "--connect-under-reset") connect_under_reset = true;
        else if (a == "--variant" && has_value) variant_name = args[++i];
        else { ui::error(kUsage); return 1; }
    }

    if (want_list_probes) return list_probes();  // needs no project

    auto root = find_project_root();
    if (!root) {
        ui::error("No nav.toml found in this directory or any parent.");
        return 1;
    }
    fs::current_path(*root);

    auto cfg = load_project_config(*root);
    if (!cfg) { ui::error("Failed to parse nav.toml."); return 1; }
    // A library produces an archive, not a flashable firmware image.
    if (cfg->is_library()) {
        ui::error("This is a library project — nothing to flash. Build it ('nav build') or "
                  "depend on it from a firmware project, then upload that.");
        return 1;
    }
    if (!variant_name.empty() && !find_variant(*cfg, variant_name)) {
        ui::error("Unknown variant '" + variant_name + "' (known: " + variant_names(*cfg) + ").");
        return 1;
    }
    auto board = default_catalog(root).find(cfg->target_board);
    if (!board) {
        ui::error("Board '" + cfg->target_board + "' is not in the catalog ('nav board list --all').");
        return 1;
    }
    const Registry& reg = registry();

    if (list) {
        ui::info("Runners for " + board->id + " (first is the default):");
        for (const auto& name : board_runner_names(*board)) {
            const RunnerDef* def = reg.runner(name);
            ui::info("  " + name + (def ? "  (" + def->tool + ", flashes ." + def->image + ")" : "  (not defined)"));
        }
        return 0;
    }

    // The firmware ELF the generated CMakeLists builds: build/<project name>, or
    // build/<variant>/<its output name> for a variant.
    fs::path elf = variant_build_dir(variant_name) / firmware_name(*cfg, variant_name);
    if (!fs::exists(elf) && fs::exists(fs::path(elf) += ".elf")) elf += ".elf";
    if (!dry_run && !fs::exists(elf)) {
        ui::error("No firmware at " + elf.string() + ". Run 'nav build" +
                  (variant_name.empty() ? "" : " --variant " + variant_name) + "' first.");
        return 1;
    }

    // Serial-port runners (avrdude): find the board when no --port is given.
    if (const RunnerDef* def = reg.runner(runner_id.empty() ? default_runner(*board) : runner_id);
        def && runner_uses(*def, "port") && !cli_vars.count("port") && !dry_run) {
        const auto ports = find_serial_ports();
        if (ports.size() == 1) {
            cli_vars["port"] = ports.front();
            ui::info("Using port: " + ports.front());
        } else if (ports.empty()) {
            ui::error("No serial port found for the board. Connect it, or pass --port <dev>.");
            return 1;
        } else {
            ui::error("Several serial ports found. Pick one with --port <dev>:");
            for (const auto& p : ports) ui::error("  " + p);
            return 1;
        }
    }

    // Probe selection. Several ST-Links with nothing chosen is refused: flashing
    // "whichever enumerated first" has put firmware on the wrong board before.
    bool probe_chosen = false;
    std::string pinned_serial;
    if (const RunnerDef* def = reg.runner(runner_id.empty() ? default_runner(*board) : runner_id)) {
        const bool uses_serial = runner_uses(*def, "probe_serial");
        const bool uses_path = runner_uses(*def, "probe_usb_path");
        const std::string runner_name = def->id;
        const ProbeRequest request = choose_probe_request(cli_probe, env_value("NAV_PROBE"), cfg->upload.probe);
        const bool set_by_hand = cli_vars.count("probe_serial") || cli_vars.count("probe_usb_path");
        if (set_by_hand && request.value.empty()) {
            probe_chosen = true;  // --set probe_serial=... is a choice too
            if (cli_vars.count("probe_serial")) pinned_serial = cli_vars["probe_serial"];
        } else {
            const bool discovery = probe_discovery_supported();
            const auto attached = discovery ? find_stlink_probes(usb_sysfs()) : std::vector<Probe>{};
            const ProbeSelection sel = resolve_probe(request, attached, discovery, uses_serial, uses_path, runner_name);
            if (!sel.error.empty()) {
                std::string msg = "Cannot upload: " + sel.error;
                while (!msg.empty() && msg.back() == '\n') msg.pop_back();
                ui::error(msg);
                return 1;
            }
            for (const auto& [k, v] : sel.vars) cli_vars.emplace(k, v);
            probe_chosen = !request.value.empty();
            pinned_serial = sel.unique_serial;
            if (probe_chosen) {
                std::string how;
                for (const auto& [k, v] : sel.vars)
                    how += (how.empty() ? "" : ", ") + std::string(k == "probe_serial" ? "serial " : "USB port ") + v;
                ui::info("Probe: " + how + " (" + request.source + ")");
            }
        }
        if (connect_under_reset || cfg->upload.connect_under_reset) {
            if (!runner_uses(*def, "connect_under_reset")) {
                ui::error("Cannot upload: runner '" + runner_name + "' has no connect-under-reset form "
                          "(connect_under_reset is set by " + std::string(connect_under_reset ? "--connect-under-reset" : "nav.toml [upload]") +
                          "). Use st-flash or openocd, or drop the setting.");
                return 1;
            }
            cli_vars["connect_under_reset"] = "1";
        }
    }

    const FlashPlan plan = plan_flash(*board, reg, runner_id, elf, cli_vars);
    if (!plan.error.empty()) {
        ui::error("Cannot upload: " + plan.error + ".");
        return 1;
    }

    if (dry_run) {
        ui::info("Runner: " + plan.runner);
        for (const auto& step : plan.steps) ui::info(step.what + ": " + join(step.argv));
        return 0;
    }

    // Install the runner's tool on first use, as `nav build` does for compilers.
    const ToolDef* tool = reg.tool(plan.tool);
    const std::string binary = tool && !tool->binary.empty() ? tool->binary : plan.tool;
    ToolchainManager tm;
    if (!tm.probe_tool(ctx, ToolRequirement{binary, binary, true}).is_found) {
        ui::info("'" + binary + "' not found — installing it.");
        if (install_tools(ctx, {binary}) != 0) {
            ui::error("Could not install '" + binary + "'. Install it yourself, or pick another runner (--list).");
            return 1;
        }
    }

    for (const auto& step : plan.steps) {
        ui::step(step.what, join(step.argv));
        const bool is_flash = &step == &plan.steps.back();
        auto res = ctx.execute(step.argv, "", /*silent=*/!is_flash);
        if (res.exit_code != 0) {
            if (!is_flash) {
                ui::error("Image conversion failed:");
                if (!res.output.empty()) ui::error(res.output);
            } else {
                ui::error("'" + plan.runner + "' failed (exit " + std::to_string(res.exit_code) +
                          "). Check the probe/cable and board power; see its output above.");
            }
            return res.exit_code;
        }
    }

    // Some flashers leave the core halted after writing (e.g. st-flash on a
    // Nucleo whose NRST isn't wired to the debugger). Boards that need it declare
    // reset_after_flash + reset_command; it belongs to the default runner.
    if (board->reset_after_flash && !board->reset_command.empty() && plan.runner == default_runner(*board)) {
        ui::step("Resetting", "Restarting the target on the new image...");
        // st-flash reset aims at the first probe unless told otherwise; with a
        // probe chosen, give it the serial, or skip it when no serial can
        // single out that probe (cloned adapters).
        std::vector<std::string> reset = board->reset_command;
        bool skip = false;
        if (probe_chosen && reset.front() == "st-flash") {
            if (!pinned_serial.empty()) reset.insert(reset.begin() + 1, {"--serial", pinned_serial});
            else skip = true;
        }
        if (skip) {
            ui::warning("Flashed OK. The post-flash reset was skipped: it cannot be aimed at the chosen probe "
                        "(its serial is shared). Press the board's RESET button to start it.");
        } else {
            auto rst = ctx.execute(reset, "", /*silent=*/true);
            if (rst.exit_code != 0)
                ui::warning("Flashed OK, but the reset failed — press the board's RESET button to start it.");
        }
    }

    ui::success("Uploaded " + elf.filename().string() + " to " + board->id + " with " + plan.runner + ".");
    return 0;
}

} // namespace nav::core
