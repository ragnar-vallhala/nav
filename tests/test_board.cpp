#include "nav/core/board.hpp"

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
        path_ = fs::temp_directory_path() / ("nav_board_test_" + std::to_string(::getpid())
                                            + "_" + std::to_string(counter_++));
        fs::create_directories(path_);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path_, ec); }
    const fs::path& path() const { return path_; }
private:
    fs::path path_;
    static inline std::atomic<int> counter_{0};
};

void write(const fs::path& p, const std::string& s) { std::ofstream(p) << s; }

constexpr const char* kFullBoard = R"(
id     = "test_full"
name   = "Test Full"
arch   = "cortex-m4"
vendor = "stm32"
mcu    = "stm32f401re"

[cpu]
compile_flags = ["-mcpu=cortex-m4", "-mthumb"]
link_flags    = ["-mthumb"]

[toolchain]
compiler = "arm-none-eabi-gcc"

[flashing]
tool    = "st-flash"
address = "0x08000000"

[framework]
default   = "navhal"
supported = ["navhal", "stm32cube"]
)";

constexpr const char* kMinimalBoard = R"(
id   = "test_min"
arch = "rv32"
)";

constexpr const char* kBoardMissingId = R"(
arch = "x"
)";

constexpr const char* kBoardMissingArch = R"(
id = "test_no_arch"
)";

} // namespace

TEST(Board, ParseFullSchema) {
    TempDir td;
    write(td.path() / "b.toml", kFullBoard);
    auto b = nav::core::parse_board_file(td.path() / "b.toml");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->id, "test_full");
    EXPECT_EQ(b->name, "Test Full");
    EXPECT_EQ(b->arch, "cortex-m4");
    EXPECT_EQ(b->vendor, "stm32");
    EXPECT_EQ(b->mcu, "stm32f401re");
    EXPECT_EQ(b->compiler, "arm-none-eabi-gcc");
    EXPECT_EQ(b->flash_tool, "st-flash");
    EXPECT_EQ(b->flash_address, "0x08000000");
    EXPECT_EQ(b->default_framework, "navhal");
    EXPECT_EQ(b->compile_flags, (std::vector<std::string>{"-mcpu=cortex-m4", "-mthumb"}));
    EXPECT_EQ(b->link_flags, (std::vector<std::string>{"-mthumb"}));
    EXPECT_EQ(b->supported_frameworks, (std::vector<std::string>{"navhal", "stm32cube"}));
}

TEST(Board, ParseMinimal) {
    TempDir td;
    write(td.path() / "m.toml", kMinimalBoard);
    auto b = nav::core::parse_board_file(td.path() / "m.toml");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->id, "test_min");
    EXPECT_EQ(b->arch, "rv32");
    EXPECT_TRUE(b->vendor.empty());
    EXPECT_TRUE(b->compile_flags.empty());
}

TEST(Board, RejectsMissingRequiredFields) {
    TempDir td;
    write(td.path() / "no_id.toml", kBoardMissingId);
    write(td.path() / "no_arch.toml", kBoardMissingArch);
    EXPECT_FALSE(nav::core::parse_board_file(td.path() / "no_id.toml").has_value());
    EXPECT_FALSE(nav::core::parse_board_file(td.path() / "no_arch.toml").has_value());
}

TEST(Board, CatalogAddSearchPathLoadsAllValidFiles) {
    TempDir td;
    write(td.path() / "a.toml", kFullBoard);
    write(td.path() / "b.toml", kMinimalBoard);
    write(td.path() / "skip.txt", "not toml");
    write(td.path() / "bad.toml", kBoardMissingId);

    nav::core::BoardCatalog cat;
    cat.add_search_path(td.path());

    EXPECT_EQ(cat.list().size(), 2u);
    EXPECT_TRUE(cat.find("test_full").has_value());
    EXPECT_TRUE(cat.find("test_min").has_value());
    EXPECT_FALSE(cat.find("doesnt-exist").has_value());
}

TEST(Board, CatalogFirstAddWinsOnIdCollision) {
    TempDir td1, td2;
    write(td1.path() / "a.toml", kFullBoard);

    // Same id, different arch — should be ignored because td1 wins.
    write(td2.path() / "a.toml", R"(
id   = "test_full"
arch = "different-arch"
)");

    nav::core::BoardCatalog cat;
    cat.add_search_path(td1.path());
    cat.add_search_path(td2.path());

    auto b = cat.find("test_full");
    ASSERT_TRUE(b.has_value());
    EXPECT_EQ(b->arch, "cortex-m4");
}

TEST(Board, RenderBoardCmake_EmitsAllFields) {
    nav::core::Board b;
    b.id            = "pico";
    b.name          = "Raspberry Pi Pico (RP2040)";
    b.arch          = "cortex-m0plus";
    b.vendor        = "raspberrypi";
    b.mcu           = "rp2040";
    b.compiler      = "arm-none-eabi-gcc";
    b.compile_flags = {"-mcpu=cortex-m0plus", "-mthumb"};
    b.link_flags    = {"-mcpu=cortex-m0plus", "-mthumb"};
    b.flash_tool    = "picotool";
    b.flash_address = "0x10000000";

    auto cmake = nav::core::render_board_cmake(b);
    EXPECT_NE(cmake.find("set(NAV_BOARD_ID \"pico\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_ARCH \"cortex-m0plus\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_VENDOR \"raspberrypi\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_MCU \"rp2040\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_COMPILER \"arm-none-eabi-gcc\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_COMPILE_FLAGS \"-mcpu=cortex-m0plus\" \"-mthumb\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_LINK_FLAGS \"-mcpu=cortex-m0plus\" \"-mthumb\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_FLASH_TOOL \"picotool\")"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_FLASH_ADDRESS \"0x10000000\")"), std::string::npos);
    EXPECT_NE(cmake.find("share/nav/boards/pico.toml"), std::string::npos);
}

TEST(Board, RenderBoardCmake_EmptyListsAreEmptySet) {
    nav::core::Board b;
    b.id   = "min";
    b.arch = "rv32";
    auto cmake = nav::core::render_board_cmake(b);
    EXPECT_NE(cmake.find("set(NAV_BOARD_COMPILE_FLAGS)"), std::string::npos);
    EXPECT_NE(cmake.find("set(NAV_BOARD_LINK_FLAGS)"), std::string::npos);
}

TEST(Board, WriteBoardCmake_CreatesParentDirs) {
    TempDir td;
    nav::core::Board b;
    b.id   = "x";
    b.arch = "any";
    auto out = td.path() / "nested" / "deep" / "nav-board.cmake";
    ASSERT_TRUE(nav::core::write_board_cmake(b, out));
    ASSERT_TRUE(fs::exists(out));
    std::ifstream f(out);
    std::string contents((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("set(NAV_BOARD_ID \"x\")"), std::string::npos);
}

TEST(Board, DefaultCatalogRespectsNavBoardPathEnv) {
    TempDir td;
    write(td.path() / "p.toml", kMinimalBoard);

    ::setenv("NAV_BOARD_PATH", td.path().string().c_str(), 1);
    auto cat = nav::core::default_catalog();
    ::unsetenv("NAV_BOARD_PATH");

    EXPECT_TRUE(cat.find("test_min").has_value());
}
