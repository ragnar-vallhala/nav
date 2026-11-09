#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nav::core {

// Debug-probe selection for `nav upload`.
//
// A bench with several ST-Links needs the flash to go to one named probe, and
// never to "whichever enumerated first". Cloned adapters can even share a
// serial, so a probe can also be named by the USB port it is plugged into,
// which a clone cannot spoof.

struct Probe {
    std::string serial;    // USB serial descriptor
    std::string usb_path;  // "<bus>-<port>[.<port>...]", e.g. "1-3.3"
    std::string vid_pid;   // "0483:374b"
    std::string product;   // USB product string, may be empty
};

// True where probes can be enumerated (Linux, via sysfs). Elsewhere
// find_stlink_probes() returns nothing and selection is passed through unchecked.
bool probe_discovery_supported();

// ST-Links attached to the machine, sorted by USB path. `usb_sysfs` is the
// sysfs USB device directory; tests point it at a fake tree. Only ST-Link
// products of vendor 0483 count (a board in DFU mode is not a probe).
std::vector<Probe> find_stlink_probes(const std::filesystem::path& usb_sysfs = "/sys/bus/usb/devices");

// One line per probe, for listings and error messages.
std::string describe_probes(const std::vector<Probe>& probes, const std::string& indent = "  ");

// The probe the user asked for, and where the request came from.
struct ProbeRequest {
    std::string value;   // empty: nothing asked for
    std::string source;  // "--probe", "NAV_PROBE" or "nav.toml [upload].probe"
};

// Precedence: command line, then environment, then nav.toml.
ProbeRequest choose_probe_request(const std::string& cli, const std::string& env, const std::string& toml);

// What to hand the runner. `vars` holds probe_serial and/or probe_usb_path
// (only those the runner uses). A non-empty `error` means: do not flash.
struct ProbeSelection {
    std::map<std::string, std::string> vars;
    std::string error;
    // Serial the flash will be pinned by, when known and unique; used to aim a
    // follow-up command (st-flash reset) at the same probe.
    std::string unique_serial;
};

// Turn `request` into runner variables.
//   * "usb:<path>" / "serial:<sn>" are explicit; a bare value that looks like a
//     USB path ("3-2", "1-3.3") is a path, anything else a serial.
//   * `uses_serial` / `uses_usb_path` say which variables the runner's command
//     takes. A runner using neither cannot select a probe.
//   * With no request, one attached probe (or none) selects nothing and the
//     runner behaves as it always did; several attached probes is an error
//     that lists them. A probe is never picked silently.
//   * A serial shared by several attached probes is refused: only the USB path
//     tells them apart.
//   * Where discovery is unsupported the request is passed through unchecked.
ProbeSelection resolve_probe(const ProbeRequest& request, const std::vector<Probe>& attached,
                             bool discovery_supported, bool uses_serial, bool uses_usb_path,
                             const std::string& runner);

} // namespace nav::core
