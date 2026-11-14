#include "nav/core/template_source.hpp"
#include "test_env.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using nav::core::template_text;

namespace {
struct TemplateDirGuard {
    fs::path dir = fs::temp_directory_path() / "nav-template-source-test";
    TemplateDirGuard() { fs::remove_all(dir); fs::create_directories(dir); }
    ~TemplateDirGuard() { unset_env("NAV_TEMPLATE_DIR"); fs::remove_all(dir); }
};
} // namespace

TEST(TemplateSource, BakedCopyWhenUnset) {
    unset_env("NAV_TEMPLATE_DIR");
    EXPECT_EQ(template_text("x.in", "baked"), "baked");
}

TEST(TemplateSource, DiskFileWinsWhenPresent) {
    TemplateDirGuard g;
    std::ofstream(g.dir / "x.in", std::ios::binary) << "from disk\n";
    set_env("NAV_TEMPLATE_DIR", g.dir.string());
    EXPECT_EQ(template_text("x.in", "baked"), "from disk\n");
}

TEST(TemplateSource, MissingFileFallsBackToBaked) {
    TemplateDirGuard g;
    set_env("NAV_TEMPLATE_DIR", g.dir.string());
    EXPECT_EQ(template_text("absent.in", "baked"), "baked");
}
