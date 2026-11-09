#include "nav/core/probe.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <set>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

// ST Microelectronics' ST-Link products (lsusb: V1, V2, V2.1, V3 and variants).
const std::set<std::string>& stlink_products() {
    static const std::set<std::string> ids = {"3744", "3748", "374b", "374d", "374e",
                                              "374f", "3752", "3753", "3754", "3755"};
    return ids;
}

std::string read_trimmed(const fs::path& p) {
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool looks_like_usb_path(const std::string& s) {
    static const std::regex re(R"(\d+-\d+(\.\d+)*)");
    return std::regex_match(s, re);
}

std::string how_to_choose() {
    return "Choose one with --probe <usb-path|serial>, NAV_PROBE, or [upload] probe in nav.toml "
           "(a USB path tells apart adapters that share a serial).";
}

} // namespace

bool probe_discovery_supported() {
#ifdef __linux__
    return true;
#else
    return false;
#endif
}

std::vector<Probe> find_stlink_probes(const fs::path& usb_sysfs) {
    std::vector<Probe> out;
    std::error_code ec;
    for (fs::directory_iterator it(usb_sysfs, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.find(':') != std::string::npos) continue;  // an interface, not a device
        if (lower(read_trimmed(it->path() / "idVendor")) != "0483") continue;
        const std::string pid = lower(read_trimmed(it->path() / "idProduct"));
        if (!stlink_products().count(pid)) continue;
        Probe p;
        p.usb_path = name;
        p.serial = read_trimmed(it->path() / "serial");
        p.vid_pid = "0483:" + pid;
        p.product = read_trimmed(it->path() / "product");
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const Probe& a, const Probe& b) { return a.usb_path < b.usb_path; });
    return out;
}

std::string describe_probes(const std::vector<Probe>& probes, const std::string& indent) {
    std::string out;
    for (const auto& p : probes) {
        out += indent + "usb path " + p.usb_path + "  serial " + (p.serial.empty() ? "(none)" : p.serial) + "  " +
               (p.product.empty() ? "ST-Link" : p.product) + " (" + p.vid_pid + ")\n";
    }
    return out;
}

ProbeRequest choose_probe_request(const std::string& cli, const std::string& env, const std::string& toml) {
    if (!cli.empty()) return {cli, "--probe"};
    if (!env.empty()) return {env, "NAV_PROBE"};
    if (!toml.empty()) return {toml, "nav.toml [upload].probe"};
    return {};
}

ProbeSelection resolve_probe(const ProbeRequest& request, const std::vector<Probe>& attached,
                             bool discovery_supported, bool uses_serial, bool uses_usb_path,
                             const std::string& runner) {
    ProbeSelection sel;
    const bool probe_capable = uses_serial || uses_usb_path;

    if (request.value.empty()) {
        if (probe_capable && discovery_supported && attached.size() > 1) {
            sel.error = std::to_string(attached.size()) + " ST-Links are attached and no probe was chosen; "
                        "refusing to pick one. Attached:\n" + describe_probes(attached, "  ") + how_to_choose();
        } else if (attached.size() == 1 && discovery_supported) {
            sel.unique_serial = attached.front().serial;  // implicit, only for the reset follow-up
        }
        return sel;
    }

    if (!probe_capable) {
        sel.error = "runner '" + runner + "' cannot select a probe (" + request.source + " = '" + request.value +
                    "'); use a runner whose command takes {probe_serial} or {probe_usb_path}";
        return sel;
    }

    bool by_path;
    std::string value = request.value;
    if (value.rfind("usb:", 0) == 0) { by_path = true; value = value.substr(4); }
    else if (value.rfind("serial:", 0) == 0) { by_path = false; value = value.substr(7); }
    else by_path = looks_like_usb_path(value);
    if (value.empty()) {
        sel.error = request.source + " names no probe";
        return sel;
    }

    const std::string what = std::string(by_path ? "USB port " : "serial ") + value + " (" + request.source + ")";

    if (!discovery_supported) {
        // Cannot look: hand over what was asked for, to the variable it fits.
        if (by_path && uses_usb_path) sel.vars["probe_usb_path"] = value;
        else if (!by_path && uses_serial) sel.vars["probe_serial"] = value;
        else sel.error = "runner '" + runner + "' selects probes by " + std::string(uses_serial ? "serial" : "USB path") +
                         " only, and this system cannot map " + what + " to that";
        if (sel.error.empty() && !by_path) sel.unique_serial = value;
        return sel;
    }

    const Probe* match = nullptr;
    if (by_path) {
        for (const auto& p : attached)
            if (p.usb_path == value) match = &p;
        if (!match) {
            sel.error = "no ST-Link is attached at " + what + ".";
            sel.error += attached.empty() ? " None are attached." : " Attached:\n" + describe_probes(attached, "  ");
            return sel;
        }
    } else {
        std::vector<const Probe*> hits;
        for (const auto& p : attached)
            if (p.serial == value) hits.push_back(&p);
        if (hits.empty()) {
            sel.error = "no ST-Link has " + what + ".";
            sel.error += attached.empty() ? " None are attached." : " Attached:\n" + describe_probes(attached, "  ");
            return sel;
        }
        if (hits.size() > 1) {
            sel.error = std::to_string(hits.size()) + " attached ST-Links share " + what +
                        "; only their USB path tells them apart. Use --probe <usb-path>:\n" +
                        describe_probes(attached, "  ");
            return sel;
        }
        match = hits.front();
    }

    const bool serial_unique =
        !match->serial.empty() &&
        std::count_if(attached.begin(), attached.end(), [&](const Probe& p) { return p.serial == match->serial; }) == 1;

    if (by_path) {
        if (uses_usb_path) {
            sel.vars["probe_usb_path"] = match->usb_path;
            if (serial_unique) sel.unique_serial = match->serial;
        } else if (serial_unique) {
            sel.vars["probe_serial"] = match->serial;  // unambiguous, so equivalent
            sel.unique_serial = match->serial;
        } else {
            sel.error = "runner '" + runner + "' selects probes by serial, but the ST-Link at " + what +
                        " shares its serial with another adapter. Use a runner that takes a USB path "
                        "(--runner openocd).";
        }
    } else {
        if (uses_serial) {
            sel.vars["probe_serial"] = match->serial;
            sel.unique_serial = match->serial;
        } else {
            sel.vars["probe_usb_path"] = match->usb_path;
            sel.unique_serial = match->serial;
        }
    }
    return sel;
}

} // namespace nav::core
