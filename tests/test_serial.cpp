#include "nav/core/serial.hpp"

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
        path_ = fs::temp_directory_path() / ("nav_serial_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void touch(const fs::path& p) { std::ofstream(p) << ""; }

} // namespace

TEST(Serial, EmptyDirReturnsEmpty) {
    TempDir td;
    EXPECT_TRUE(nav::core::find_serial_ports(td.path()).empty());
}

TEST(Serial, FiltersOnlyTtyAcmAndTtyUsb) {
    TempDir td;
    touch(td.path() / "ttyACM0");
    touch(td.path() / "ttyACM1");
    touch(td.path() / "ttyUSB0");
    touch(td.path() / "ttyS0");      // serial but should not match
    touch(td.path() / "null");
    touch(td.path() / "random");

    auto ports = nav::core::find_serial_ports(td.path());
    ASSERT_EQ(ports.size(), 3u);
    EXPECT_EQ(ports[0], (td.path() / "ttyACM0").string());
    EXPECT_EQ(ports[1], (td.path() / "ttyACM1").string());
    EXPECT_EQ(ports[2], (td.path() / "ttyUSB0").string());
}

TEST(Serial, ResultIsSortedRegardlessOfFsOrder) {
    TempDir td;
    // Create out of alphabetical order; the function must sort.
    touch(td.path() / "ttyUSB9");
    touch(td.path() / "ttyACM0");
    touch(td.path() / "ttyACM2");
    touch(td.path() / "ttyACM1");

    auto ports = nav::core::find_serial_ports(td.path());
    ASSERT_EQ(ports.size(), 4u);
    EXPECT_LT(ports[0], ports[1]);
    EXPECT_LT(ports[1], ports[2]);
    EXPECT_LT(ports[2], ports[3]);
}

TEST(Serial, NonexistentDirReturnsEmpty) {
    EXPECT_TRUE(nav::core::find_serial_ports("/this/does/not/exist/anywhere").empty());
}
