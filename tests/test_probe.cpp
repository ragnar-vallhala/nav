#include "nav/config/embedded_config.hpp"
#include "nav/core/config.hpp"
#include "nav/core/probe.hpp"
#include "nav/core/runner.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nav::core;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_probe_" + std::to_string(::getpid()) + "_" +
                                             std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void usb_device(const fs::path& root, const std::string& name, const std::string& vid, const std::string& pid,
                const std::string& serial, const std::string& product = "") {
    fs::create_directories(root / name);
    std::ofstream(root / name / "idVendor") << vid << "\n";
    std::ofstream(root / name / "idProduct") << pid << "\n";
    if (!serial.empty()) std::ofstream(root / name / "serial") << serial << "\n";
    if (!product.empty()) std::ofstream(root / name / "product") << product << "\n";
}

Probe probe(const std::string& path, const std::string& serial) { return {serial, path, "0483:374b", "STM32 STLink"}; }

const std::vector<Probe> kTwo = {probe("1-3.3", "AAA"), probe("1-3.4", "BBB")};
const std::vector<Probe> kClones = {probe("1-3.3", "000000000001"), probe("1-3.4", "000000000001")};

bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// The last step (the flash itself) of the baked runner on a Nucleo.
std::vector<std::string> baked_flash(const std::string& runner, std::map<std::string, std::string> vars) {
    static const auto r = registry_from_json("", std::string(nav::config::kBakedBoardsJson));
    const auto plan = plan_flash(*r.board("nucleo_f401re"), r, runner, "build/app", vars);
    EXPECT_TRUE(plan.error.empty()) << plan.error;
    return plan.steps.empty() ? std::vector<std::string>{} : plan.steps.back().argv;
}

} // namespace

// --- enumeration over a fake sysfs tree ------------------------------------

TEST(ProbeScan, FindsOnlyStLinksAndSortsByPath) {
    TempDir td;
    usb_device(td.path(), "1-3.4", "0483", "374b", "SN4", "STM32 STLink");
    usb_device(td.path(), "1-3.3", "0483", "3748", "SN3");
    usb_device(td.path(), "1-3.2", "0403", "6001", "FTDI");          // other vendor
    usb_device(td.path(), "2-1", "0483", "df11", "DFU");              // ST, but a bootloader
    usb_device(td.path(), "usb1", "1d6b", "0002", "0000:02:00.0");    // root hub
    fs::create_directories(td.path() / "1-3.4:1.0");                  // an interface node
    std::ofstream(td.path() / "1-3.4:1.0" / "idVendor") << "0483\n";

    const auto probes = find_stlink_probes(td.path());
    ASSERT_EQ(probes.size(), 2u);
    EXPECT_EQ(probes[0].usb_path, "1-3.3");
    EXPECT_EQ(probes[0].serial, "SN3");
    EXPECT_EQ(probes[1].usb_path, "1-3.4");
    EXPECT_EQ(probes[1].product, "STM32 STLink");
    EXPECT_EQ(probes[1].vid_pid, "0483:374b");
}

TEST(ProbeScan, MissingDirectoryIsEmpty) {
    TempDir td;
    EXPECT_TRUE(find_stlink_probes(td.path() / "nope").empty());
}

TEST(ProbeScan, DescribeListsPathAndSerial) {
    const std::string s = describe_probes(kTwo);
    EXPECT_NE(s.find("usb path 1-3.3  serial AAA"), std::string::npos) << s;
    EXPECT_NE(s.find("usb path 1-3.4  serial BBB"), std::string::npos);
}

// --- precedence ------------------------------------------------------------

TEST(ProbeRequest, CliBeatsEnvBeatsToml) {
    auto r = choose_probe_request("cli", "env", "toml");
    EXPECT_EQ(r.value, "cli");
    EXPECT_EQ(r.source, "--probe");
    r = choose_probe_request("", "env", "toml");
    EXPECT_EQ(r.value, "env");
    EXPECT_EQ(r.source, "NAV_PROBE");
    r = choose_probe_request("", "", "toml");
    EXPECT_EQ(r.value, "toml");
    EXPECT_NE(r.source.find("nav.toml"), std::string::npos);
    EXPECT_TRUE(choose_probe_request("", "", "").value.empty());
}

TEST(ProbeConfig, UploadTableParses) {
    TempDir td;
    std::ofstream(td.path() / "nav.toml") << "[project]\nname=\"a\"\n[upload]\nprobe = \"1-3.4\"\n"
                                             "connect_under_reset = true\n";
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_EQ(cfg->upload.probe, "1-3.4");
    EXPECT_TRUE(cfg->upload.connect_under_reset);
}

TEST(ProbeConfig, UploadDefaultsAndBadKeys) {
    TempDir td;
    std::ofstream(td.path() / "nav.toml") << "[project]\nname=\"a\"\n";
    auto cfg = load_project_config(td.path());
    ASSERT_TRUE(cfg);
    EXPECT_TRUE(cfg->upload.probe.empty());
    EXPECT_FALSE(cfg->upload.connect_under_reset);

    std::ofstream(td.path() / "nav.toml") << "[project]\nname=\"a\"\n[upload]\nprobee = \"x\"\n";
    EXPECT_FALSE(load_project_config(td.path()));
}

// --- resolution ------------------------------------------------------------

TEST(ProbeResolve, NothingChosenWithOneProbeChangesNothing) {
    const auto sel = resolve_probe({}, {probe("1-3.3", "AAA")}, true, true, true, "st-flash");
    EXPECT_TRUE(sel.error.empty());
    EXPECT_TRUE(sel.vars.empty());
}

TEST(ProbeResolve, NothingChosenWithNoProbeChangesNothing) {
    const auto sel = resolve_probe({}, {}, true, true, true, "st-flash");
    EXPECT_TRUE(sel.error.empty());
    EXPECT_TRUE(sel.vars.empty());
}

TEST(ProbeResolve, SeveralProbesAndNoChoiceRefusesAndListsThem) {
    const auto sel = resolve_probe({}, kTwo, true, true, false, "st-flash");
    EXPECT_FALSE(sel.error.empty());
    EXPECT_TRUE(sel.vars.empty());
    EXPECT_NE(sel.error.find("2 ST-Links"), std::string::npos);
    EXPECT_NE(sel.error.find("1-3.3"), std::string::npos);
    EXPECT_NE(sel.error.find("BBB"), std::string::npos);
    EXPECT_NE(sel.error.find("--probe"), std::string::npos);
}

TEST(ProbeResolve, RunnerThatCannotSelectIsNotBlockedByAttachedStLinks) {
    // avrdude flashes over a serial port; two ST-Links on the bench are irrelevant.
    const auto sel = resolve_probe({}, kTwo, true, false, false, "avrdude");
    EXPECT_TRUE(sel.error.empty());
}

TEST(ProbeResolve, RequestToARunnerThatCannotSelectIsAnError) {
    const auto sel = resolve_probe({"AAA", "--probe"}, kTwo, true, false, false, "avrdude");
    EXPECT_NE(sel.error.find("cannot select a probe"), std::string::npos) << sel.error;
}

TEST(ProbeResolve, SerialGoesToTheSerialVariable) {
    const auto sel = resolve_probe({"BBB", "--probe"}, kTwo, true, true, true, "x");
    ASSERT_TRUE(sel.error.empty()) << sel.error;
    EXPECT_EQ(sel.vars.at("probe_serial"), "BBB");
    EXPECT_EQ(sel.vars.count("probe_usb_path"), 0u);
}

TEST(ProbeResolve, UsbPathGoesToThePathVariable) {
    const auto sel = resolve_probe({"1-3.4", "NAV_PROBE"}, kTwo, true, true, true, "x");
    ASSERT_TRUE(sel.error.empty()) << sel.error;
    EXPECT_EQ(sel.vars.at("probe_usb_path"), "1-3.4");
    EXPECT_EQ(sel.vars.count("probe_serial"), 0u);
}

TEST(ProbeResolve, PathForASerialOnlyRunnerUsesTheSerialWhenUnique) {
    const auto sel = resolve_probe({"1-3.4", "--probe"}, kTwo, true, true, false, "st-flash");
    ASSERT_TRUE(sel.error.empty()) << sel.error;
    EXPECT_EQ(sel.vars.at("probe_serial"), "BBB");
}

TEST(ProbeResolve, PathForASerialOnlyRunnerIsRefusedWhenSerialsAreShared) {
    const auto sel = resolve_probe({"1-3.4", "--probe"}, kClones, true, true, false, "st-flash");
    EXPECT_FALSE(sel.error.empty());
    EXPECT_TRUE(sel.vars.empty());
    EXPECT_NE(sel.error.find("--runner openocd"), std::string::npos);
}

TEST(ProbeResolve, PathForAPathRunnerWorksWithSharedSerials) {
    const auto sel = resolve_probe({"1-3.4", "--probe"}, kClones, true, true, true, "openocd");
    ASSERT_TRUE(sel.error.empty()) << sel.error;
    EXPECT_EQ(sel.vars.at("probe_usb_path"), "1-3.4");
}

TEST(ProbeResolve, SharedSerialIsRefused) {
    const auto sel = resolve_probe({"000000000001", "--probe"}, kClones, true, true, true, "openocd");
    EXPECT_NE(sel.error.find("share"), std::string::npos) << sel.error;
    EXPECT_NE(sel.error.find("usb-path"), std::string::npos);
}

TEST(ProbeResolve, SerialForAPathOnlyRunnerMapsToThePath) {
    const auto sel = resolve_probe({"AAA", "--probe"}, kTwo, true, false, true, "x");
    ASSERT_TRUE(sel.error.empty()) << sel.error;
    EXPECT_EQ(sel.vars.at("probe_usb_path"), "1-3.3");
}

TEST(ProbeResolve, UnknownProbeIsAnErrorListingWhatIsAttached) {
    auto sel = resolve_probe({"9-9", "--probe"}, kTwo, true, true, true, "x");
    EXPECT_NE(sel.error.find("no ST-Link is attached at USB port 9-9"), std::string::npos) << sel.error;
    EXPECT_NE(sel.error.find("1-3.3"), std::string::npos);
    sel = resolve_probe({"ZZZ", "--probe"}, kTwo, true, true, true, "x");
    EXPECT_NE(sel.error.find("no ST-Link has serial ZZZ"), std::string::npos) << sel.error;
    sel = resolve_probe({"ZZZ", "--probe"}, {}, true, true, true, "x");
    EXPECT_NE(sel.error.find("None are attached"), std::string::npos);
}

TEST(ProbeResolve, ExplicitPrefixesOverrideTheGuess) {
    // A serial that happens to look like a USB path.
    auto sel = resolve_probe({"serial:1-3", "--probe"}, {probe("1-3.3", "1-3")}, true, true, true, "x");
    ASSERT_TRUE(sel.error.empty()) << sel.error;
    EXPECT_EQ(sel.vars.at("probe_serial"), "1-3");
    sel = resolve_probe({"usb:1-3.4", "--probe"}, kTwo, true, true, true, "x");
    ASSERT_TRUE(sel.error.empty()) << sel.error;
    EXPECT_EQ(sel.vars.at("probe_usb_path"), "1-3.4");
}

TEST(ProbeResolve, WithoutDiscoveryTheRequestIsPassedThrough) {
    auto sel = resolve_probe({"AAA", "--probe"}, {}, false, true, true, "x");
    ASSERT_TRUE(sel.error.empty());
    EXPECT_EQ(sel.vars.at("probe_serial"), "AAA");
    sel = resolve_probe({"1-3.4", "--probe"}, {}, false, true, true, "x");
    EXPECT_EQ(sel.vars.at("probe_usb_path"), "1-3.4");
    sel = resolve_probe({"1-3.4", "--probe"}, {}, false, true, false, "st-flash");
    EXPECT_FALSE(sel.error.empty());  // cannot map a path to a serial blind
    sel = resolve_probe({}, kTwo, false, true, true, "x");
    EXPECT_TRUE(sel.error.empty());   // cannot see, so cannot refuse
}

// --- runner argument rendering (the baked st-flash and openocd runners) ----

TEST(ProbeRunner, StFlashWithoutProbeOptionsIsUnchanged) {
    EXPECT_EQ(baked_flash("st-flash", {}),
              (std::vector<std::string>{"st-flash", "write", "build/app.bin", "0x08000000"}));
}

TEST(ProbeRunner, StFlashTakesSerialAndConnectUnderReset) {
    EXPECT_EQ(baked_flash("st-flash", {{"probe_serial", "ABC123"}, {"connect_under_reset", "1"}}),
              (std::vector<std::string>{"st-flash", "--serial", "ABC123", "--connect-under-reset", "write",
                                        "build/app.bin", "0x08000000"}));
}

TEST(ProbeRunner, StFlashIgnoresAnEmptyVariable) {
    EXPECT_EQ(baked_flash("st-flash", {{"probe_serial", ""}}),
              (std::vector<std::string>{"st-flash", "write", "build/app.bin", "0x08000000"}));
}

TEST(ProbeRunner, OpenocdWithoutProbeOptionsIsUnchanged) {
    EXPECT_EQ(baked_flash("openocd", {}),
              (std::vector<std::string>{"openocd", "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg", "-c",
                                        "program \"build/app\" verify reset exit"}));
}

TEST(ProbeRunner, OpenocdPinsByUsbPathBeforeTheTarget) {
    EXPECT_EQ(baked_flash("openocd", {{"probe_usb_path", "1-3.4"}}),
              (std::vector<std::string>{"openocd", "-f", "interface/stlink.cfg", "-c", "adapter usb location 1-3.4",
                                        "-c", "transport select hla_swd", "-f", "target/stm32f4x.cfg", "-c",
                                        "program \"build/app\" verify reset exit"}));
}

TEST(ProbeRunner, OpenocdPinsBySerial) {
    const auto argv = baked_flash("openocd", {{"probe_serial", "0672FF"}});
    EXPECT_TRUE(has(argv, "adapter serial 0672FF"));
    EXPECT_TRUE(has(argv, "transport select hla_swd"));
    EXPECT_FALSE(has(argv, "adapter usb location 0672FF"));
}

TEST(ProbeRunner, OpenocdConnectUnderResetComesAfterTheTargetConfig) {
    const auto argv = baked_flash("openocd", {{"probe_usb_path", "3-2"}, {"connect_under_reset", "1"}});
    const auto target = std::find(argv.begin(), argv.end(), "target/stm32f4x.cfg");
    const auto srst = std::find(argv.begin(), argv.end(), "reset_config srst_only connect_assert_srst");
    ASSERT_NE(target, argv.end());
    ASSERT_NE(srst, argv.end());
    EXPECT_LT(target, srst);  // the target script sets its own reset_config; ours must win
    EXPECT_EQ(*(srst - 1), "-c");
}

TEST(ProbeRunner, RunnerUsesSeesConditionalVariables) {
    const auto r = registry_from_json("", std::string(nav::config::kBakedBoardsJson));
    EXPECT_TRUE(runner_uses(*r.runner("st-flash"), "probe_serial"));
    EXPECT_FALSE(runner_uses(*r.runner("st-flash"), "probe_usb_path"));
    EXPECT_TRUE(runner_uses(*r.runner("openocd"), "probe_usb_path"));
    EXPECT_TRUE(runner_uses(*r.runner("openocd"), "connect_under_reset"));
    EXPECT_FALSE(runner_uses(*r.runner("avrdude"), "probe_serial"));
}
