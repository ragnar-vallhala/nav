#include "nav/core/navconfig.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_navcfg_" + std::to_string(::getpid())
                                           + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void write_file(const fs::path& p, const std::string& c) { std::ofstream(p) << c; }

} // namespace

TEST(NavConfig, ParseSkipsCommentsAndUnset) {
    TempDir td;
    write_file(td.path() / ".config", R"(
# Target
CONFIG_DRV_UART=y
CONFIG_BOARD="nucleo_f401re"
# CONFIG_TEST is not set

CONFIG_NUM=42
)");
    auto m = nav::core::parse_kconfig(td.path() / ".config");
    EXPECT_EQ(m.at("CONFIG_DRV_UART"), "y");
    EXPECT_EQ(m.at("CONFIG_BOARD"), "\"nucleo_f401re\"");
    EXPECT_EQ(m.at("CONFIG_NUM"), "42");
    EXPECT_EQ(m.count("CONFIG_TEST"), 0u); // "is not set" => absent
}

TEST(NavConfig, FindPrefersDotConfigThenNavhalConfig) {
    TempDir a, b, c;
    write_file(a.path() / ".config", "X=y\n");
    write_file(a.path() / "navhal.config", "X=y\n");
    EXPECT_EQ(nav::core::find_navhal_config(a.path())->filename(), ".config");

    write_file(b.path() / "navhal.config", "X=y\n");
    EXPECT_EQ(nav::core::find_navhal_config(b.path())->filename(), "navhal.config");

    EXPECT_FALSE(nav::core::find_navhal_config(c.path()).has_value());
}

TEST(NavConfig, UnmetReportsMissingAndDifferingValues) {
    std::map<std::string, std::string> have = {
        {"CONFIG_DRV_UART", "y"},
        {"CONFIG_BOARD", "\"nucleo_f401re\""},
    };
    std::map<std::string, std::string> required = {
        {"CONFIG_DRV_UART", "y"},        // satisfied
        {"CONFIG_DRV_UART_DMA", "y"},    // missing
        {"CONFIG_BOARD", "\"pico\""},    // differing value
    };
    auto unmet = nav::core::unmet_requirements(have, required);
    // Sorted: CONFIG_BOARD="pico" then CONFIG_DRV_UART_DMA=y
    ASSERT_EQ(unmet.size(), 2u);
    EXPECT_EQ(unmet[0], "CONFIG_BOARD=\"pico\"");
    EXPECT_EQ(unmet[1], "CONFIG_DRV_UART_DMA=y");
}

TEST(NavConfig, UnmetEmptyWhenSatisfied) {
    std::map<std::string, std::string> have = {{"A", "y"}, {"B", "1"}, {"C", "z"}};
    std::map<std::string, std::string> required = {{"A", "y"}, {"B", "1"}};
    EXPECT_TRUE(nav::core::unmet_requirements(have, required).empty());
}
