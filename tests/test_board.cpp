#include "nav/core/board.hpp"

#include <gtest/gtest.h>

using namespace nav::core;

namespace {

Board make_board() {
    Board b;
    b.id = "nucleo_f401re";
    b.name = "STM32 Nucleo-F401RE";
    b.arch = "cortex-m4";
    b.vendor = "stm32";
    b.mcu = "stm32f401re";
    b.navhal_board = "nucleo_f401re";
    b.compiler = "arm-none-eabi-gcc";
    b.flash_tool = "st-flash";
    b.flash_address = "0x08000000";
    b.compile_flags = {"-mcpu=cortex-m4", "-mthumb"};
    b.link_flags = {"-mthumb"};
    return b;
}

} // namespace

TEST(BoardCatalog, AddFindList) {
    BoardCatalog cat;
    cat.add(make_board());
    Board other = make_board();
    other.id = "atmega328p";
    cat.add(other);

    EXPECT_EQ(cat.list().size(), 2u);
    ASSERT_TRUE(cat.find("nucleo_f401re").has_value());
    EXPECT_EQ(cat.find("nucleo_f401re")->compiler, "arm-none-eabi-gcc");
    EXPECT_FALSE(cat.find("does_not_exist").has_value());
}

TEST(RenderBoardCmake, EmitsExpectedVariables) {
    const std::string out = render_board_cmake(make_board());
    EXPECT_NE(out.find("set(NAV_BOARD_ID \"nucleo_f401re\")"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_BOARD_COMPILER \"arm-none-eabi-gcc\")"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_BOARD_FLASH_TOOL \"st-flash\")"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_BOARD_FLASH_ADDRESS \"0x08000000\")"), std::string::npos);
    EXPECT_NE(out.find("set(NAV_BOARD_COMPILE_FLAGS \"-mcpu=cortex-m4\" \"-mthumb\")"), std::string::npos);
}
