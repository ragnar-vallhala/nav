#include "nav/core/cache.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_cache_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

} // namespace

TEST(Cache, PackageDirIsContentAddressed) {
    fs::path root = "/tmp/navcache";
    auto dir = nav::core::package_dir(root, "nav-hal", "0.5.0",
                                      "abcdef0123456789ffffffff");
    // <root>/<name>-<version>-<first 12 hex of sha>
    EXPECT_EQ(dir, root / "nav-hal-0.5.0-abcdef012345");
}

TEST(Cache, PackageDirToleratesShortSha) {
    fs::path root = "/tmp/navcache";
    auto dir = nav::core::package_dir(root, "x", "1.0.0", "beef");
    EXPECT_EQ(dir, root / "x-1.0.0-beef");
}

TEST(Cache, RootHonoursNavHome) {
    ::setenv("NAV_HOME", "/opt/navhome", 1);
    EXPECT_EQ(nav::core::cache_root(), fs::path("/opt/navhome") / "packages");
    ::unsetenv("NAV_HOME");
}

TEST(Cache, IsInstalledRequiresMarker) {
    TempDir td;
    auto pkg = td.path() / "nav-hal-0.5.0-abc";

    EXPECT_FALSE(nav::core::is_installed(pkg));  // dir absent

    fs::create_directories(pkg);
    EXPECT_FALSE(nav::core::is_installed(pkg));  // present but no marker

    std::ofstream(nav::core::install_marker(pkg)) << "abc\n";
    EXPECT_TRUE(nav::core::is_installed(pkg));   // marker present
}
