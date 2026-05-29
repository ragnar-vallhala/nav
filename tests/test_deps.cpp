#include "nav/core/deps.hpp"

#include "nav/core/cache.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nav::core;

namespace {

class TempDir {
public:
    TempDir() {
        path_ = fs::temp_directory_path() / ("nav_deps_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void touch(const fs::path& p, const std::string& content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << content;
}

// Create an installed package dir in `root` and return it.
fs::path install_pkg(const fs::path& root, const std::string& name,
                     const std::string& version, const std::string& sha) {
    auto dir = package_dir(root, name, version, sha);
    fs::create_directories(dir);
    std::ofstream(install_marker(dir)) << sha << "\n";
    return dir;
}

LockedPackage lib(const std::string& name, const std::string& sha) {
    LockedPackage p;
    p.name = name;
    p.version = Version{1, 0, 0, "", ""};
    p.kind = PackageKind::Library;
    p.checksums = {{"source", sha}};
    return p;
}

} // namespace

TEST(Deps, LockedCacheDirUsesSourceForLibrary) {
    auto p = lib("cmsis", "abcdef0123456789");
    auto dir = locked_cache_dir(p, "/cache", "linux_x86_64");
    ASSERT_TRUE(dir.has_value());
    EXPECT_EQ(*dir, fs::path("/cache") / "cmsis-1.0.0-abcdef012345");
}

TEST(Deps, LockedCacheDirUsesPlatformForToolchain) {
    LockedPackage p;
    p.name = "arm-none-eabi-gcc";
    p.version = Version{13, 2, 0, "", ""};
    p.kind = PackageKind::Toolchain;
    p.checksums = {{"linux_x86_64", "1111222233334444"}, {"darwin_arm64", "aaaa"}};
    auto dir = locked_cache_dir(p, "/cache", "linux_x86_64");
    ASSERT_TRUE(dir.has_value());
    EXPECT_EQ(*dir, fs::path("/cache") / "arm-none-eabi-gcc-13.2.0-111122223333");
}

TEST(Deps, LockedCacheDirNulloptWhenKeyMissing) {
    LockedPackage p;
    p.name = "gcc";
    p.version = Version{1, 0, 0, "", ""};
    p.kind = PackageKind::Toolchain;
    p.checksums = {{"linux_x86_64", "abcd"}};
    EXPECT_FALSE(locked_cache_dir(p, "/cache", "windows_x86_64").has_value());
}

TEST(Deps, RendersIncludeDirsForInstalledLibrary) {
    TempDir td;
    const std::string sha = "1111111111111111";
    auto dir = install_pkg(td.path(), "cmsis", "1.0.0", sha);
    touch(dir / "include" / "cmsis.h", "#pragma once\n");

    Lockfile lock;
    lock.packages.push_back(lib("cmsis", sha));

    auto out = render_deps_cmake(lock, td.path(), "linux_x86_64", "");
    EXPECT_NE(out.find((dir / "include").generic_string()), std::string::npos);
    EXPECT_NE(out.find("NAV_DEPS_INCLUDE_DIRS"), std::string::npos);
}

TEST(Deps, RendersLibraryArchivesAndLinkDir) {
    TempDir td;
    const std::string sha = "2222222222222222";
    auto dir = install_pkg(td.path(), "withlib", "1.0.0", sha);
    touch(dir / "lib" / "libfoo.a", "ar\n");

    Lockfile lock;
    lock.packages.push_back(lib("withlib", sha));

    auto out = render_deps_cmake(lock, td.path(), "linux_x86_64", "");
    EXPECT_NE(out.find((dir / "lib").generic_string()), std::string::npos);
    EXPECT_NE(out.find((dir / "lib" / "libfoo.a").generic_string()), std::string::npos);
}

TEST(Deps, RendersToolchainCompilerPath) {
    TempDir td;
    const std::string sha = "3333333333333333";
    LockedPackage p;
    p.name = "arm-none-eabi-gcc";
    p.version = Version{13, 2, 0, "", ""};
    p.kind = PackageKind::Toolchain;
    p.checksums = {{"linux_x86_64", sha}};
    auto dir = install_pkg(td.path(), p.name, "13.2.0", sha);
    touch(dir / "bin" / "arm-none-eabi-gcc", "#!/bin/sh\n");

    Lockfile lock;
    lock.packages.push_back(p);

    auto out = render_deps_cmake(lock, td.path(), "linux_x86_64", "arm-none-eabi-gcc");
    const std::string expected = (dir / "bin" / "arm-none-eabi-gcc").generic_string();
    EXPECT_NE(out.find("set(NAV_DEPS_COMPILER \"" + expected + "\")"), std::string::npos);
}

TEST(Deps, SkipsPackagesAbsentFromCache) {
    TempDir td;
    // Lock references a package that was never installed.
    Lockfile lock;
    lock.packages.push_back(lib("ghost", "9999999999999999"));

    auto out = render_deps_cmake(lock, td.path(), "linux_x86_64", "");
    // Variables are still declared (empty), but no path leaks in.
    EXPECT_NE(out.find("NAV_DEPS_INCLUDE_DIRS"), std::string::npos);
    EXPECT_EQ(out.find("ghost-1.0.0"), std::string::npos);
}
