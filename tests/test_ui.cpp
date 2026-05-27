#include "nav/core/ui.hpp"

#include <gtest/gtest.h>

namespace ui = nav::core::ui;

TEST(Ui, ForceNeverEmptyEscapes) {
    ui::set_color_mode(ui::ColorMode::Never);
    EXPECT_FALSE(ui::color_enabled());
    EXPECT_EQ(ui::RED(), "");
    EXPECT_EQ(ui::RESET(), "");
    EXPECT_EQ(ui::BOLD(), "");
}

TEST(Ui, ForceAlwaysProducesEscapes) {
    ui::set_color_mode(ui::ColorMode::Always);
    EXPECT_TRUE(ui::color_enabled());
    EXPECT_EQ(ui::RED(), "\033[31m");
    EXPECT_EQ(ui::RESET(), "\033[0m");
}

TEST(Ui, AutoModeRespectsNoColorEnv) {
    // Manually flip back to auto and re-resolve with NO_COLOR set.
    ::setenv("NO_COLOR", "1", 1);
    ui::set_color_mode(ui::ColorMode::Auto);
    EXPECT_FALSE(ui::color_enabled());
    ::unsetenv("NO_COLOR");
}

TEST(Ui, ToolHelpersRouteThroughColorGate) {
    ui::set_color_mode(ui::ColorMode::Never);

    testing::internal::CaptureStdout();
    ui::tool_ok("cmake", "v3.30");
    ui::tool_missing_critical("arm-none-eabi-gcc");
    ui::tool_missing_optional("st-flash");
    std::string out = testing::internal::GetCapturedStdout();

    EXPECT_NE(out.find("cmake"), std::string::npos);
    EXPECT_NE(out.find("v3.30"), std::string::npos);
    EXPECT_NE(out.find("arm-none-eabi-gcc"), std::string::npos);
    EXPECT_NE(out.find("NOT FOUND (CRITICAL)"), std::string::npos);
    EXPECT_NE(out.find("st-flash"), std::string::npos);
    EXPECT_NE(out.find("Optional"), std::string::npos);
    // With color disabled, no ANSI escapes.
    EXPECT_EQ(out.find('\033'), std::string::npos);
}
