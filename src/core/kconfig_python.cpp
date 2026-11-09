#include "nav/core/kconfig_python.hpp"

#include "nav/core/cache.hpp"
#include "nav/core/ui.hpp"

#include <cctype>
#include <chrono>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

constexpr std::chrono::milliseconds kProbeTimeout{15000};

// Last non-empty line of a command's merged output, trimmed.
std::string last_line(const std::string& out) {
    std::string line, last;
    for (char c : out + "\n") {
        if (c == '\n' || c == '\r') {
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            if (!line.empty()) last = line;
            line.clear();
        } else {
            line += c;
        }
    }
    return last;
}

// Run `python -c <code>` printing sys.executable; return it on success.
std::string probe(IExecutionContext& ctx, const std::string& python, const std::string& code) {
    auto r = ctx.execute({python, "-c", code + "import sys; print(sys.executable)"}, "",
                         /*silent=*/true, kProbeTimeout);
    if (r.exit_code != 0) return {};
    return last_line(r.output);
}

std::vector<std::string> system_pythons() {
#ifdef _WIN32
    return {"python3", "python"};
#else
    return {"python3"};
#endif
}

} // namespace

fs::path kconfig_venv_dir() {
    return nav_home_base() / "python";
}

fs::path venv_python(const fs::path& venv) {
#ifdef _WIN32
    return venv / "Scripts" / "python.exe";
#else
    return venv / "bin" / "python";
#endif
}

std::string find_kconfig_python(IExecutionContext& ctx) {
    std::vector<std::string> candidates;
    std::error_code ec;
    const fs::path vpy = venv_python(kconfig_venv_dir());
    if (fs::exists(vpy, ec)) candidates.push_back(vpy.string());
    for (const auto& p : system_pythons()) candidates.push_back(p);

    for (const auto& py : candidates) {
        std::string exe = probe(ctx, py, "import kconfiglib; ");
        if (!exe.empty()) return exe;
    }
    return {};
}

std::string ensure_kconfig_python(IExecutionContext& ctx) {
    if (std::string py = find_kconfig_python(ctx); !py.empty()) return py;

    std::string base;
    for (const auto& p : system_pythons()) {
        base = probe(ctx, p, "");
        if (!base.empty()) break;
    }
    if (base.empty()) {
        ui::error("NavHAL's configuration step needs Python 3, which was not found.");
        ui::error("Install it (or run 'nav update') and rebuild.");
        return {};
    }

    const fs::path venv = kconfig_venv_dir();
    std::error_code ec;
    fs::remove_all(venv, ec); // a half-made venv from an interrupted run
    ui::step("Configuring", "Setting up kconfiglib in " + venv.string() + " (one-time)");

    auto mk = ctx.execute({base, "-m", "venv", venv.string()}, "", /*silent=*/true);
    if (mk.exit_code != 0) {
        fs::remove_all(venv, ec);
        ui::error("Could not create a Python venv with " + base + ".");
        ui::error("On Debian/Ubuntu install the venv module: sudo apt install python3-venv");
        ui::error("Or install kconfiglib for that Python yourself: " + base +
                  " -m pip install kconfiglib");
        return {};
    }

    const std::string vpy = venv_python(venv).string();
    auto pip = ctx.execute({vpy, "-m", "pip", "install", "--disable-pip-version-check",
                            "--quiet", kKconfiglibRequirement},
                           "", /*silent=*/true);
    if (pip.exit_code != 0) {
        fs::remove_all(venv, ec);
        ui::error(std::string("Could not install ") + kKconfiglibRequirement +
                  " (offline?). NavHAL's configuration step needs it.");
        return {};
    }

    std::string exe = probe(ctx, vpy, "import kconfiglib; ");
    if (exe.empty()) {
        ui::error("kconfiglib was installed into " + venv.string() + " but cannot be imported.");
        return {};
    }
    ui::info("kconfiglib ready (" + venv.string() + ").");
    return exe;
}

} // namespace nav::core
