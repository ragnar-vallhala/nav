#include "nav/core/index.hpp"

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
        path_ = fs::temp_directory_path() / ("nav_index_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void write_file(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << s;
}

constexpr const char* kLibraryEntry = R"({
  "name": "nav-hal",
  "versions": [
    {
      "version": "0.5.0",
      "kind": "library",
      "downloads": {
        "source": {
          "url": "https://example.com/nav-hal-0.5.0.tar.gz",
          "sha256": "deadbeef"
        }
      },
      "dependencies": {
        "cmsis": "^6.0.0"
      }
    },
    {
      "version": "0.4.2",
      "kind": "library",
      "downloads": {
        "source": {
          "url": "https://example.com/nav-hal-0.4.2.tar.gz",
          "sha256": "cafebabe"
        }
      }
    }
  ]
})";

constexpr const char* kToolchainEntry = R"({
  "name": "arm-none-eabi-gcc",
  "versions": [
    {
      "version": "13.2.0",
      "kind": "toolchain",
      "toolchain_binaries": ["arm-none-eabi-gcc", "arm-none-eabi-objcopy"],
      "downloads": {
        "linux_x86_64": {
          "url": "https://registry.navrobotec.com/arm-none-eabi-gcc-13.2-linux-x86_64.tar.xz",
          "sha256": "aaaa"
        },
        "darwin_arm64": {
          "url": "https://registry.navrobotec.com/arm-none-eabi-gcc-13.2-darwin-arm64.tar.xz",
          "sha256": "bbbb"
        }
      }
    }
  ]
})";

} // namespace

TEST(Index, ParseLibrary) {
    TempDir td;
    auto path = td.path() / "lib.json";
    write_file(path, kLibraryEntry);
    auto pkg = nav::core::parse_index_file(path);
    ASSERT_TRUE(pkg.has_value());
    EXPECT_EQ(pkg->name, "nav-hal");

    ASSERT_EQ(pkg->versions.size(), 2u);
    EXPECT_EQ(pkg->versions[0].version, (nav::core::Version{0,4,2,"",""}));
    EXPECT_EQ(pkg->versions[1].version, (nav::core::Version{0,5,0,"",""}));

    const auto& latest = pkg->versions[1];
    EXPECT_EQ(latest.kind, nav::core::PackageKind::Library);
    EXPECT_EQ(latest.downloads.at("source").url, "https://example.com/nav-hal-0.5.0.tar.gz");
    EXPECT_EQ(latest.downloads.at("source").sha256, "deadbeef");
    ASSERT_EQ(latest.dependencies.size(), 1u);
    EXPECT_EQ(latest.dependencies.at("cmsis").op, nav::core::VersionReq::Op::Caret);
}

TEST(Index, ParseToolchain) {
    TempDir td;
    auto path = td.path() / "tc.json";
    write_file(path, kToolchainEntry);
    auto pkg = nav::core::parse_index_file(path);
    ASSERT_TRUE(pkg.has_value());
    EXPECT_EQ(pkg->name, "arm-none-eabi-gcc");

    ASSERT_EQ(pkg->versions.size(), 1u);
    const auto& v = pkg->versions[0];
    EXPECT_EQ(v.kind, nav::core::PackageKind::Toolchain);
    EXPECT_EQ(v.downloads.size(), 2u);
    EXPECT_EQ(v.downloads.at("linux_x86_64").sha256, "aaaa");
    EXPECT_EQ(v.downloads.at("darwin_arm64").sha256, "bbbb");
    EXPECT_EQ(v.toolchain_binaries.size(), 2u);
    EXPECT_EQ(v.toolchain_binaries[0], "arm-none-eabi-gcc");
}

TEST(Index, MissingNameRejected) {
    TempDir td;
    auto path = td.path() / "broken.json";
    write_file(path, R"({"versions":[{"version":"0.1.0"}]})");
    EXPECT_FALSE(nav::core::parse_index_file(path).has_value());
}

TEST(Index, MissingVersionsRejected) {
    TempDir td;
    auto path = td.path() / "broken.json";
    write_file(path, R"({"name":"x"})");
    EXPECT_FALSE(nav::core::parse_index_file(path).has_value());
}

TEST(Index, MalformedJsonRejected) {
    TempDir td;
    auto path = td.path() / "broken.json";
    write_file(path, "this is not json");
    EXPECT_FALSE(nav::core::parse_index_file(path).has_value());
}

TEST(Index, ParseFromStringMatchesFile) {
    auto from_string = nav::core::parse_index_string(kLibraryEntry);
    ASSERT_TRUE(from_string.has_value());
    EXPECT_EQ(from_string->name, "nav-hal");
    ASSERT_EQ(from_string->versions.size(), 2u);
    EXPECT_EQ(from_string->versions[1].version, (nav::core::Version{0,5,0,"",""}));
}

TEST(Index, ParseStringMalformedRejected) {
    EXPECT_FALSE(nav::core::parse_index_string("not json").has_value());
    EXPECT_FALSE(nav::core::parse_index_string("[]").has_value());        // array, not object
    EXPECT_FALSE(nav::core::parse_index_string(R"({"name":"x"})").has_value()); // no versions
}

TEST(Index, ParseKindHelpers) {
    EXPECT_EQ(nav::core::parse_kind("library"),   nav::core::PackageKind::Library);
    EXPECT_EQ(nav::core::parse_kind("toolchain"), nav::core::PackageKind::Toolchain);
    EXPECT_FALSE(nav::core::parse_kind("framework").has_value());
    EXPECT_EQ(nav::core::to_string(nav::core::PackageKind::Library),   "library");
    EXPECT_EQ(nav::core::to_string(nav::core::PackageKind::Toolchain), "toolchain");
}

TEST(LocalIndexClient, ShardedPath) {
    nav::core::LocalIndexClient cli("/tmp/idx");
    EXPECT_EQ(cli.index_path("nav-hal").string(),           "/tmp/idx/na/nav-hal.json");
    EXPECT_EQ(cli.index_path("arm-none-eabi-gcc").string(), "/tmp/idx/ar/arm-none-eabi-gcc.json");
    EXPECT_EQ(cli.index_path("imu-driver").string(),        "/tmp/idx/im/imu-driver.json");
    EXPECT_EQ(cli.index_path("x").string(),                 "/tmp/idx/x/x.json");
}

TEST(LocalIndexClient, ShardedFetchSuccess) {
    TempDir td;
    write_file(td.path() / "na" / "nav-hal.json", kLibraryEntry);
    nav::core::LocalIndexClient cli(td.path());
    auto pkg = cli.fetch("nav-hal");
    ASSERT_TRUE(pkg.has_value());
    EXPECT_EQ(pkg->name, "nav-hal");
}

TEST(LocalIndexClient, FetchMissingReturnsNullopt) {
    TempDir td;
    nav::core::LocalIndexClient cli(td.path());
    EXPECT_FALSE(cli.fetch("nonexistent").has_value());
}

TEST(LocalIndexClient, FetchWrongShardReturnsNullopt) {
    TempDir td;
    write_file(td.path() / "zz" / "nav-hal.json", kLibraryEntry);
    nav::core::LocalIndexClient cli(td.path());
    EXPECT_FALSE(cli.fetch("nav-hal").has_value());
}
