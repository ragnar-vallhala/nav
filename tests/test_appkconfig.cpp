#include "nav/core/appkconfig.hpp"
#include "nav/core/libdeps.hpp"

#include "test_env.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nav::core;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_appkc_" + std::to_string(::getpid()) + "_" +
                                             std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void write_file(const fs::path& p, const std::string& c) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << c;
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool have_kconfiglib() {
    return std::system("python3 -c 'import kconfiglib' >/dev/null 2>&1") == 0;
}

bool contains(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }

} // namespace

TEST(AppKconfig, DetectsRootKconfig) {
    TempDir td;
    EXPECT_FALSE(app_has_kconfig(td.path()));
    write_file(td.path() / "Kconfig", "");
    EXPECT_TRUE(app_has_kconfig(td.path()));
}

TEST(AppKconfig, MergedKconfigSourcesNavhalDepsThenAppInMenu) {
    auto s = render_merged_kconfig("demo", "/n/NavHAL/Kconfig",
                                   {{"vaios", "/c/vaios/Kconfig"}}, "/p/Kconfig");
    EXPECT_TRUE(contains(s, "mainmenu \"demo configuration\""));
    const auto n = s.find("source \"/n/NavHAL/Kconfig\"");
    const auto v = s.find("source \"/c/vaios/Kconfig\"");
    const auto m = s.find("menu \"Application options\"");
    const auto a = s.find("source \"/p/Kconfig\"");
    ASSERT_NE(n, std::string::npos);
    ASSERT_NE(v, std::string::npos);
    ASSERT_NE(m, std::string::npos);
    ASSERT_NE(a, std::string::npos);
    EXPECT_LT(n, v);
    EXPECT_LT(v, m);
    EXPECT_LT(m, a);
    EXPECT_TRUE(contains(s, "endmenu"));
}

TEST(AppKconfig, AutoconfRendersEachType) {
    auto h = render_app_autoconf({
        {"APP_ON", "bool", "y"},
        {"APP_OFF", "bool", "n"},
        {"APP_N", "int", "42"},
        {"APP_H", "hex", "0x1000"},
        {"APP_S", "string", "he said \"hi\"\\"},
        {"APP_EMPTY", "int", ""},
    });
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_ON 1\n"));
    EXPECT_FALSE(contains(h, "#define CONFIG_APP_OFF"));
    EXPECT_TRUE(contains(h, "CONFIG_APP_OFF is not set"));
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_N 42\n"));
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_H 0x1000\n"));
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_S \"he said \\\"hi\\\"\\\\\"\n"));
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_EMPTY 0\n"));
}

TEST(AppKconfig, CmakeVariables) {
    auto c = render_app_cmake({{"A", "bool", "y"}, {"B", "bool", "n"}, {"C", "int", "7"},
                               {"D", "string", "x$y"}},
                              "/b/app_autoconf.h");
    EXPECT_TRUE(contains(c, "set(NAV_APP_AUTOCONF \"/b/app_autoconf.h\")"));
    EXPECT_TRUE(contains(c, "set(CONFIG_A ON)"));
    EXPECT_TRUE(contains(c, "set(CONFIG_B OFF)"));
    EXPECT_TRUE(contains(c, "set(CONFIG_C 7)"));
    EXPECT_TRUE(contains(c, "set(CONFIG_D \"x\\$y\")"));
}

TEST(AppKconfig, LibdepsHandsDepTheMergedConfigOnlyWhenOptedIn) {
    ResolvedLib vaios{"vaios", "/c/vaios", {}, {"NAVHAL=ON"}, true};
    vaios.has_kconfig = true;
    ResolvedLib plain{"plain", "/c/plain", {}, {}, false};
    plain.has_kconfig = false;
    const auto off = render_libdeps_cmake({vaios, plain}, {"vaios"});
    EXPECT_FALSE(contains(off, "_CONFIG"));
    const auto on = render_libdeps_cmake({vaios, plain}, {"vaios"}, true);
    EXPECT_TRUE(contains(on, "set(VAIOS_CONFIG \"${CMAKE_BINARY_DIR}/nav.config\""));
    EXPECT_FALSE(contains(on, "PLAIN_CONFIG"));
    // An explicit option wins.
    ResolvedLib own{"vaios", "/c/vaios", {}, {"VAIOS_CONFIG=/x/y"}, true};
    own.has_kconfig = true;
    EXPECT_FALSE(contains(render_libdeps_cmake({own}, {"vaios"}, true), "nav.config"));
}

// ---- kconfiglib-backed (skipped where python3 lacks kconfiglib) ----

namespace {

struct Fixture {
    TempDir td;
    fs::path root, navhal;
    ProjectConfig cfg;
    Fixture() {
        root = td.path() / "app";
        navhal = td.path() / "NavHAL";
        write_file(navhal / "Kconfig", "config DRV_UART\n    bool \"uart\"\n    default y\n");
        write_file(root / "Kconfig",
                   "config APP_FOO\n    bool \"foo\"\n"
                   "config APP_COUNT\n    int \"count\"\n    default 3\n"
                   "config APP_NAME\n    string \"name\"\n    default \"dflt\"\n"
                   "config APP_ADDR\n    hex \"addr\"\n    default 0x20\n");
        cfg.project_name = "app";
        fs::create_directories(root / "build");
    }
};

} // namespace

TEST(AppKconfig, ValidatesAndGeneratesAutoconf) {
    if (!have_kconfiglib()) GTEST_SKIP() << "kconfiglib not available";
    Fixture f;
    write_file(f.root / ".config", "CONFIG_DRV_UART=y\nCONFIG_APP_FOO=y\nCONFIG_APP_COUNT=9\nCONFIG_APP_NAME=\"hi\"\n");
    HostExecutionContext ctx;
    std::string err;
    ASSERT_TRUE(prepare_app_kconfig(ctx, f.root, f.cfg, {}, f.navhal, &err)) << err;
    const auto h = read_file(f.root / "build" / "app_autoconf.h");
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_FOO 1"));
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_COUNT 9"));
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_NAME \"hi\""));
    EXPECT_TRUE(contains(h, "#define CONFIG_APP_ADDR 0x20"));
    EXPECT_FALSE(contains(h, "DRV_UART")); // NavHAL's symbols are not the app's
    EXPECT_TRUE(contains(read_file(f.root / "build" / "nav-app-kconfig.cmake"), "set(CONFIG_APP_FOO ON)"));
    EXPECT_TRUE(contains(read_file(f.root / "build" / "nav.config"), "CONFIG_APP_COUNT=9"));
    EXPECT_TRUE(contains(read_file(f.root / "build" / "nav.config"), "CONFIG_DRV_UART=y"));
}

TEST(AppKconfig, UnsetBoolIsAbsent) {
    if (!have_kconfiglib()) GTEST_SKIP() << "kconfiglib not available";
    Fixture f;
    write_file(f.root / ".config", "CONFIG_DRV_UART=y\n");
    HostExecutionContext ctx;
    std::string err;
    ASSERT_TRUE(prepare_app_kconfig(ctx, f.root, f.cfg, {}, f.navhal, &err)) << err;
    EXPECT_FALSE(contains(read_file(f.root / "build" / "app_autoconf.h"), "#define CONFIG_APP_FOO"));
}

TEST(AppKconfig, UnknownSymbolIsAnErrorNamingIt) {
    if (!have_kconfiglib()) GTEST_SKIP() << "kconfiglib not available";
    Fixture f;
    write_file(f.root / ".config", "CONFIG_DRV_UART=y\nCONFIG_APP_FOOO=y\n");
    HostExecutionContext ctx;
    std::string err;
    EXPECT_FALSE(prepare_app_kconfig(ctx, f.root, f.cfg, {}, f.navhal, &err));
    EXPECT_TRUE(contains(err, "APP_FOOO")) << err;
}

TEST(AppKconfig, InvalidValueIsAnError) {
    if (!have_kconfiglib()) GTEST_SKIP() << "kconfiglib not available";
    Fixture f;
    write_file(f.root / ".config", "CONFIG_DRV_UART=y\nCONFIG_APP_COUNT=lots\n");
    HostExecutionContext ctx;
    std::string err;
    EXPECT_FALSE(prepare_app_kconfig(ctx, f.root, f.cfg, {}, f.navhal, &err));
    EXPECT_TRUE(contains(err, "APP_COUNT")) << err;
}

TEST(AppKconfig, DependencyKconfigIsMergedAndNavhalNotDuplicated) {
    if (!have_kconfiglib()) GTEST_SKIP() << "kconfiglib not available";
    Fixture f;
    // A dependency that osources NavHAL itself, as vaios does.
    const fs::path dep = f.td.path() / "dep";
    write_file(dep / "Kconfig",
               "config DEP_HEAP\n    int \"heap\"\n    default 100\n"
               "osource \"$(NAVHAL_KCONFIG)\"\n");
    ResolvedLib lib{"dep", dep, {}, {}, false};
    lib.has_kconfig = true;
    set_env("NAVHAL_KCONFIG", (f.navhal / "Kconfig").string());
    write_file(f.root / ".config", "CONFIG_DRV_UART=y\nCONFIG_DEP_HEAP=4096\n");
    HostExecutionContext ctx;
    std::string err;
    EXPECT_TRUE(prepare_app_kconfig(ctx, f.root, f.cfg, {lib}, f.navhal, &err)) << err;
    EXPECT_TRUE(contains(read_file(f.root / "build" / "nav.config"), "CONFIG_DEP_HEAP=4096"));
    // The caller's environment is restored.
    EXPECT_STREQ(std::getenv("NAVHAL_KCONFIG"), (f.navhal / "Kconfig").c_str());
    unset_env("NAVHAL_KCONFIG");
    EXPECT_FALSE(contains(read_file(f.root / "build" / "app_autoconf.h"), "DEP_HEAP"));
}
