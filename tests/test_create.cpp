#include "nav/core/project_name.hpp"

#include <gtest/gtest.h>

using nav::core::validate_project_name;

TEST(ProjectName, AcceptsValidIdentifiers) {
    EXPECT_EQ(validate_project_name("my-project"), "");
    EXPECT_EQ(validate_project_name("nav_demo"), "");
    EXPECT_EQ(validate_project_name("v0.1.0"), "");
    EXPECT_EQ(validate_project_name("a"), "");
    EXPECT_EQ(validate_project_name("Hello-World_42"), "");
}

TEST(ProjectName, RejectsEmpty) {
    EXPECT_FALSE(validate_project_name("").empty());
}

TEST(ProjectName, RejectsPathSeparators) {
    EXPECT_FALSE(validate_project_name("../foo").empty());
    EXPECT_FALSE(validate_project_name("/tmp/foo").empty());
    EXPECT_FALSE(validate_project_name("a/b").empty());
    EXPECT_FALSE(validate_project_name("a\\b").empty());
}

TEST(ProjectName, RejectsLeadingDotOrDash) {
    EXPECT_FALSE(validate_project_name(".hidden").empty());
    EXPECT_FALSE(validate_project_name("-flag-looking").empty());
}

TEST(ProjectName, RejectsSpacesAndPunctuation) {
    EXPECT_FALSE(validate_project_name("a b").empty());
    EXPECT_FALSE(validate_project_name("hello!").empty());
    EXPECT_FALSE(validate_project_name("rm$(whoami)").empty());
}

TEST(ProjectName, RejectsOverlyLongNames) {
    std::string long_name(200, 'a');
    EXPECT_FALSE(validate_project_name(long_name).empty());
}
