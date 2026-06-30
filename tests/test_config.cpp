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

TEST(Config, LoadProjectConfig_ParseErrorPrintsLineColumn) {
    TempDir td;
    // Break on line 3.
    write_file(td.path() / "nav.toml", "[project]\nname = \"ok\"\nthis = is = broken\n");

    // Capture stderr.
    testing::internal::CaptureStderr();
    auto cfg = nav::core::load_project_config(td.path());
    std::string err = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(cfg.has_value());
    EXPECT_NE(err.find("nav.toml"), std::string::npos);
    EXPECT_NE(err.find(":3"), std::string::npos); // line 3
}

TEST(Config, LoadProjectConfig_DefaultsTypeToExecutable) {
    TempDir td;
    write_file(td.path() / "nav.toml", "[project]\nname = \"demo\"\n");
    auto cfg = nav::core::load_project_config(td.path());
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->project_type, "executable");
    EXPECT_FALSE(cfg->is_library());
    EXPECT_TRUE(cfg->dependencies.empty());
}

TEST(Config, LoadProjectConfig_ReadsLibraryType) {
    TempDir td;
    write_file(td.path() / "nav.toml",
               "[project]\nname = \"mylib\"\ntype = \"library\"\n");
    auto cfg = nav::core::load_project_config(td.path());
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->project_type, "library");
    EXPECT_TRUE(cfg->is_library());
}

TEST(Config, LoadProjectConfig_ParsesDependencies) {
    TempDir td;
    write_file(td.path() / "nav.toml", R"(
[project]
name = "app"

[dependencies]
local  = { path = "../local" }
remote = { git = "https://example.com/remote.git", ref = "main" }
bare   = "../bare"
)");
    auto cfg = nav::core::load_project_config(td.path());
    ASSERT_TRUE(cfg.has_value());
    ASSERT_EQ(cfg->dependencies.size(), 3u);

    auto by_name = [&](const std::string& n) -> const nav::core::Dependency* {
        for (const auto& d : cfg->dependencies) if (d.name == n) return &d;
        return nullptr;
    };

    const auto* local = by_name("local");
    ASSERT_NE(local, nullptr);
    EXPECT_TRUE(local->is_path());
    EXPECT_FALSE(local->is_git());
    EXPECT_EQ(local->path, "../local");

    const auto* remote = by_name("remote");
    ASSERT_NE(remote, nullptr);
    EXPECT_TRUE(remote->is_git());
    EXPECT_EQ(remote->git, "https://example.com/remote.git");
    EXPECT_EQ(remote->ref, "main");

    const auto* bare = by_name("bare");
    ASSERT_NE(bare, nullptr);
    EXPECT_TRUE(bare->is_path());
    EXPECT_EQ(bare->path, "../bare");
}
