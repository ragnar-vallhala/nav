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
