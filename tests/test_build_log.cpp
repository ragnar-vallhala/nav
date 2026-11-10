#include "nav/core/build_log.hpp"

#include <gtest/gtest.h>

using nav::core::extract_build_warnings;

TEST(BuildLog, NoWarningsInCleanOutput) {
    auto w = extract_build_warnings("[ 10%] Building C object a.o\n[100%] Linking\n");
    EXPECT_TRUE(w.blocks.empty());
    EXPECT_EQ(w.total, 0);
}

TEST(BuildLog, KeepsContextUntilNextDiagnosticAndDeduplicates) {
    const std::string out =
        "[ 10%] Building C object a.o\n"
        "/p/src/main.c: In function 'main':\n"
        "/p/src/main.c:7:9: warning: unused variable 'x' [-Wunused-variable]\n"
        "    7 |     int x;\n"
        "      |         ^\n"
        "[ 20%] Building C object b.o\n"
        "/p/inc/h.h:3:1: warning: 'f' defined but not used [-Wunused-function]\n"
        "    3 | static int f(void) { return 0; }\n"
        "      | ^~~~~~\n"
        "/p/inc/h.h:3:1: note: declared here\n"
        "[ 30%] Building C object c.o\n"
        "/p/inc/h.h:3:1: warning: 'f' defined but not used [-Wunused-function]\n"
        "    3 | static int f(void) { return 0; }\n"
        "      | ^~~~~~\n"
        "/p/inc/h.h:3:1: note: declared here\n"
        "[100%] Linking C executable app.elf\n";
    auto w = extract_build_warnings(out);
    ASSERT_EQ(w.blocks.size(), 2u);
    EXPECT_EQ(w.total, 3);
    EXPECT_EQ(w.blocks[0],
              "/p/src/main.c:7:9: warning: unused variable 'x' [-Wunused-variable]\n"
              "    7 |     int x;\n"
              "      |         ^");
    EXPECT_NE(w.blocks[1].find("note: declared here"), std::string::npos);
}

TEST(BuildLog, ErrorEndsABlockAndIndentedWarningTextIsContext) {
    const std::string out =
        "a.c:1:1: warning: first\n"
        "    1 | warning: in source\n"
        "a.c:2:1: error: boom\n"
        "    2 | x\n";
    auto w = extract_build_warnings(out);
    ASSERT_EQ(w.blocks.size(), 1u);
    EXPECT_EQ(w.total, 1);
    EXPECT_EQ(w.blocks[0], "a.c:1:1: warning: first\n    1 | warning: in source");
}

TEST(BuildLog, LinkerWarningWithoutLineNumber) {
    auto w = extract_build_warnings("ld: warning: foo.elf has a LOAD segment with RWX permissions\n");
    ASSERT_EQ(w.blocks.size(), 1u);
}

TEST(BuildLog, LinkerMissingEndGroupIsAWarningWithoutSwallowingTheNextLines) {
    const std::string out =
        "[ 90%] Linking C executable app.elf\n"
        "/opt/arm/bin/ld: missing --end-group; added as last command line option\n"
        "Memory region         Used Size  Region Size  %age Used\n"
        "[100%] Built target app\n";
    auto w = extract_build_warnings(out);
    ASSERT_EQ(w.blocks.size(), 1u);
    EXPECT_EQ(w.total, 1);
    EXPECT_EQ(w.blocks[0], "/opt/arm/bin/ld: missing --end-group; added as last command line option");
}

TEST(BuildLog, MissingEndGroupWordsInsideSourceContextAreNotAWarning) {
    auto w = extract_build_warnings(
        "    1 | // missing --end-group; added as last command line option\n");
    EXPECT_TRUE(w.blocks.empty());
}
