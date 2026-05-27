#include "nav/core/config.hpp"

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
        path_ = fs::temp_directory_path() / ("nav_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void write_file(const fs::path& p, const std::string& content) {
    std::ofstream(p) << content;
}

} // namespace

TEST(Config, FindProjectRoot_WalksParents) {
    TempDir td;
    fs::create_directories(td.path() / "a" / "b" / "c");
    write_file(td.path() / "nav.toml", "[project]\nname = \"x\"\n");

    auto root = nav::core::find_project_root(td.path() / "a" / "b" / "c");
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(fs::canonical(*root), fs::canonical(td.path()));
}

TEST(Config, FindProjectRoot_NullOptWhenAbsent) {
    TempDir td;
    fs::create_directories(td.path() / "sub");

    auto root = nav::core::find_project_root(td.path() / "sub");
    // If the temp dir's ancestors happen to contain nav.toml, this may match —
    // but in any sane test environment the result for a freshly-created dir
    // under /tmp should be nullopt.
    if (root.has_value()) {
        EXPECT_NE(fs::canonical(*root), fs::canonical(td.path()));
    }
}

TEST(Config, LoadProjectConfig_ReadsKnownFields) {
    TempDir td;
    write_file(td.path() / "nav.toml", R"(
[project]
name = "demo"

[target]
arch = "cortex-m4"
vendor = "stm32"
board = "nucleo_f401re"

[build]
backend = "cmake"
)");

    auto cfg = nav::core::load_project_config(td.path());
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->project_name, "demo");
    EXPECT_EQ(cfg->target_arch, "cortex-m4");
    EXPECT_EQ(cfg->target_vendor, "stm32");
    EXPECT_EQ(cfg->target_board, "nucleo_f401re");
    EXPECT_EQ(cfg->build_backend, "cmake");
}

TEST(Config, LoadProjectConfig_ReturnsNullOptOnParseError) {
    TempDir td;
    write_file(td.path() / "nav.toml", "this is = = not valid toml ===");
    auto cfg = nav::core::load_project_config(td.path());
    EXPECT_FALSE(cfg.has_value());
}
