#include "nav/core/kconfig_python.hpp"

#include "mock_execution_context.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using nav::core::CommandResult;
using nav::test::ExecutedCall;
using nav::test::MockExecutionContext;

namespace {

bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// Fakes a machine's Python setup. NAV_HOME points at a temp dir so nav's venv
// is isolated. The system python3 answers `-c` probes; whether it can import
// kconfiglib, create a venv, or pip-install is configurable. A created venv
// gains kconfiglib once pip has run.
class KconfigPython : public ::testing::Test {
protected:
    void SetUp() override {
        home = fs::temp_directory_path() /
               ("nav_kpy_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
        fs::create_directories(home);
        ::setenv("NAV_HOME", home.c_str(), 1);
        mock.handler = [this](const ExecutedCall& c) -> CommandResult {
            const auto& a = c.argv;
            const std::string vpy = nav::core::venv_python(nav::core::kconfig_venv_dir()).string();
            if (a.size() >= 3 && a[1] == "-c") {
                const bool wants_kconfig = a[2].find("import kconfiglib") != std::string::npos;
                if (a[0] == "python3") {
                    if (!system_has_python) return {127, ""};
                    if (wants_kconfig && !system_has_kconfiglib) return {1, "ModuleNotFoundError"};
                    return {0, "/usr/bin/python3\n"};
                }
                if (a[0] == vpy) {
                    if (wants_kconfig && !venv_has_kconfiglib) return {1, "ModuleNotFoundError"};
                    return {0, vpy + "\n"};
                }
                return {127, ""};
            }
            if (has(a, "venv")) {
                if (!venv_ok) return {1, "ensurepip is not available"};
                fs::create_directories(fs::path(vpy).parent_path());
                std::ofstream(vpy) << "";
                return {0, ""};
            }
            if (has(a, "pip")) {
                if (!pip_ok) return {1, "network unreachable"};
                venv_has_kconfiglib = true;
                return {0, ""};
            }
            return {1, ""};
        };
    }
    void TearDown() override {
        ::unsetenv("NAV_HOME");
        std::error_code ec;
        fs::remove_all(home, ec);
    }

    bool any_call_with(const std::string& token) const {
        for (const auto& c : mock.calls)
            if (has(c.argv, token)) return true;
        return false;
    }

    fs::path home;
    MockExecutionContext mock;
    bool system_has_python = true;
    bool system_has_kconfiglib = false;
    bool venv_ok = true;
    bool pip_ok = true;
    bool venv_has_kconfiglib = false;
    static inline std::atomic<int> counter{0};
};

} // namespace

TEST_F(KconfigPython, UsesSystemPythonWhenItHasKconfiglib) {
    system_has_kconfiglib = true;
    EXPECT_EQ(nav::core::ensure_kconfig_python(mock), "/usr/bin/python3");
    EXPECT_FALSE(any_call_with("venv"));
    EXPECT_FALSE(any_call_with("pip"));
}

TEST_F(KconfigPython, CreatesVenvAndInstallsPinnedKconfiglib) {
    const std::string vpy = nav::core::venv_python(nav::core::kconfig_venv_dir()).string();
    EXPECT_EQ(nav::core::ensure_kconfig_python(mock), vpy);
    EXPECT_TRUE(any_call_with("venv"));
    EXPECT_TRUE(any_call_with(nav::core::kKconfiglibRequirement));
}

TEST_F(KconfigPython, ReusesExistingVenvWithoutReinstalling) {
    const std::string vpy = nav::core::venv_python(nav::core::kconfig_venv_dir()).string();
    ASSERT_EQ(nav::core::ensure_kconfig_python(mock), vpy);
    mock.calls.clear();
    EXPECT_EQ(nav::core::ensure_kconfig_python(mock), vpy);
    EXPECT_FALSE(any_call_with("venv"));
    EXPECT_FALSE(any_call_with("pip"));
}

TEST_F(KconfigPython, VenvFailureReturnsEmptyAndCleansUp) {
    venv_ok = false;
    EXPECT_TRUE(nav::core::ensure_kconfig_python(mock).empty());
    EXPECT_FALSE(fs::exists(nav::core::kconfig_venv_dir()));
}

TEST_F(KconfigPython, PipFailureReturnsEmptyAndCleansUp) {
    pip_ok = false;
    EXPECT_TRUE(nav::core::ensure_kconfig_python(mock).empty());
    EXPECT_FALSE(fs::exists(nav::core::kconfig_venv_dir()));
}

TEST_F(KconfigPython, NoPythonAtAllReturnsEmpty) {
    system_has_python = false;
    EXPECT_TRUE(nav::core::ensure_kconfig_python(mock).empty());
    EXPECT_FALSE(any_call_with("venv"));
}

TEST_F(KconfigPython, FindDoesNotInstall) {
    EXPECT_TRUE(nav::core::find_kconfig_python(mock).empty());
    EXPECT_FALSE(any_call_with("venv"));
    EXPECT_FALSE(any_call_with("pip"));
}
