#include "nav/core/lockfile.hpp"

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
        path_ = fs::temp_directory_path() / ("nav_lock_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

nav::core::Lockfile sample() {
    using namespace nav::core;
    Lockfile lock;
    {
        LockedPackage p;
        p.name = "nav-hal";
        p.version = Version{0,5,0,"",""};
        p.kind = PackageKind::Library;
        p.source = "registry+https://github.com/x/nav-index";
        p.checksums = {{"source", "deadbeef"}};
        p.dependencies = {"cmsis@6.0.0"};
        lock.packages.push_back(p);
    }
    {
        LockedPackage p;
        p.name = "arm-none-eabi-gcc";
        p.version = Version{13,2,0,"",""};
        p.kind = PackageKind::Toolchain;
        p.source = "registry+https://github.com/x/nav-index";
        p.checksums = {{"linux_x86_64", "aaaa"}, {"darwin_arm64", "bbbb"}};
        lock.packages.push_back(p);
    }
    return lock;
}

} // namespace

TEST(Lockfile, SaveLoadRoundTrip) {
    TempDir td;
    auto path = td.path() / "nav.lock";
    auto original = sample();
    ASSERT_TRUE(nav::core::save_lockfile(original, path));

    auto loaded = nav::core::load_lockfile(path);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->packages.size(), 2u);

    auto hal = loaded->find("nav-hal");
    ASSERT_TRUE(hal.has_value());
    EXPECT_EQ(nav::core::to_string(hal->version), "0.5.0");
    EXPECT_EQ(hal->kind, nav::core::PackageKind::Library);
    EXPECT_EQ(hal->checksums.at("source"), "deadbeef");
    ASSERT_EQ(hal->dependencies.size(), 1u);
    EXPECT_EQ(hal->dependencies[0], "cmsis@6.0.0");

    auto gcc = loaded->find("arm-none-eabi-gcc");
    ASSERT_TRUE(gcc.has_value());
    EXPECT_EQ(gcc->kind, nav::core::PackageKind::Toolchain);
    EXPECT_EQ(gcc->checksums.at("linux_x86_64"), "aaaa");
    EXPECT_EQ(gcc->checksums.at("darwin_arm64"), "bbbb");
}

TEST(Lockfile, RenderIsStableAcrossEquivalentInput) {
    using namespace nav::core;
    Lockfile a = sample();
    Lockfile b;
    // Same packages, reversed order — render must be identical (sorted by name).
    b.packages.push_back(a.packages[1]);
    b.packages.push_back(a.packages[0]);
    EXPECT_EQ(render_lockfile(a), render_lockfile(b));
}

TEST(Lockfile, LoadMissingFileReturnsNullopt) {
    EXPECT_FALSE(nav::core::load_lockfile("/no/such/nav.lock").has_value());
}

TEST(Lockfile, LoadMalformedReturnsNullopt) {
    TempDir td;
    auto path = td.path() / "nav.lock";
    std::ofstream(path) << "not json at all";
    EXPECT_FALSE(nav::core::load_lockfile(path).has_value());
}

TEST(Lockfile, LoadMissingPackagesKeyReturnsNullopt) {
    TempDir td;
    auto path = td.path() / "nav.lock";
    std::ofstream(path) << R"({"foo": 1})";
    EXPECT_FALSE(nav::core::load_lockfile(path).has_value());
}

TEST(Lockfile, FindAbsentReturnsNullopt) {
    auto lock = sample();
    EXPECT_FALSE(lock.find("nope").has_value());
}
