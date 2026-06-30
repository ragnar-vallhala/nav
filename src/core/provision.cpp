#include "nav/core/provision.hpp"

#include "nav/core/cache.hpp"
#include "nav/core/registry.hpp"
#include "nav/core/toolchain.hpp"
#include "nav/core/ui.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

#ifdef _WIN32
constexpr const char* kExe = ".exe";
#else
constexpr const char* kExe = "";
#endif

fs::path completion_marker(const ToolDef& t) {
    return tool_install_dir(t) / ".nav-ok";
}

// The download entry for the host platform, or nullptr if none is declared.
const ToolDownload* host_download(const ToolDef& t) {
    auto it = t.download.find(host_platform_key());
    return it == t.download.end() ? nullptr : &it->second;
}

std::string package_manager_key(PackageManager pm) {
    switch (pm) {
        case PackageManager::Apt:    return "apt";
        case PackageManager::Dnf:    return "dnf";
        case PackageManager::Pacman: return "pacman";
        case PackageManager::Brew:   return "brew";
        case PackageManager::Winget: return "winget";
        case PackageManager::Choco:  return "choco";
        case PackageManager::Scoop:  return "scoop";
    }
    return "";
}

void run_post_steps(IExecutionContext& ctx, const ToolDef& t, const ToolDownload& dl) {
    const fs::path bindir = tool_bin_dir(t);
    for (const auto& step : dl.post) {
        if (step.rfind("alias:", 0) == 0) {
            // alias:src=dst — copy bindir/src to bindir/dst (e.g. python.exe=python3.exe)
            const std::string body = step.substr(6);
            const auto eq = body.find('=');
            if (eq == std::string::npos) continue;
            const fs::path src = bindir / body.substr(0, eq);
            const fs::path dst = bindir / body.substr(eq + 1);
            std::error_code ec;
            if (fs::exists(src, ec) && !fs::exists(dst, ec)) fs::copy_file(src, dst, ec);
        } else if (step.rfind("pip:", 0) == 0) {
            const std::string pkg = step.substr(4);
            const fs::path py = bindir / (t.binary + std::string(kExe));
            ui::step("Configuring", t.id + ": installing " + pkg);
            auto r = ctx.execute({py.string(), "-m", "pip", "install",
                                  "--no-warn-script-location", pkg}, "", /*silent=*/true);
            if (r.exit_code != 0)
                ui::warning("Could not install " + pkg + " (offline?); it may be needed later.");
        }
    }
}

} // namespace

fs::path toolchains_root() {
    return nav_home_base() / "toolchains";
}

fs::path tool_install_dir(const ToolDef& t) {
    return toolchains_root() / t.id;
}

fs::path tool_bin_dir(const ToolDef& t) {
    const fs::path base = tool_install_dir(t);
    const ToolDownload* dl = host_download(t);
    if (dl && !dl->bin_subdir.empty()) return base / dl->bin_subdir;
    return base;
}

bool tool_installed(const ToolDef& t) {
    std::error_code ec;
    return fs::exists(completion_marker(t), ec);
}

bool provision_tool(IExecutionContext& ctx, const ToolDef& t) {
    if (tool_installed(t)) return true;

    const ToolDownload* dl = host_download(t);
    if (!dl) {
        ui::error("No download for '" + t.id + "' on " + host_platform_key() + ".");
        return false;
    }

    const fs::path install_dir = tool_install_dir(t);
    std::error_code ec;
    fs::remove_all(install_dir, ec);
    fs::create_directories(install_dir, ec);
    if (ec) {
        ui::error("Could not create toolchain dir: " + install_dir.string());
        return false;
    }

    const std::string filename = dl->url.substr(dl->url.find_last_of('/') + 1);
    const fs::path archive = install_dir / filename;

    ui::step("Downloading", t.id + "  (" + dl->url + ")");
    // --progress-bar shows a live bar (no -s) so large pulls don't look frozen.
    // No stall timeout on purpose: a stuck transfer is the user's to Ctrl+C.
    auto dlres = ctx.execute({"curl", "-L", "-f", "-S", "--progress-bar", "--retry", "3",
                              "-o", archive.string(), dl->url});
    if (dlres.exit_code != 0) {
        ui::error("Download failed for " + t.id + ".");
        fs::remove_all(install_dir, ec);
        return false;
    }

    ui::step("Extracting", t.id);
    CommandResult ex;
    if (dl->archive == "targz") {
        ex = ctx.execute({"tar", "-xzf", archive.string(), "-C", install_dir.string()}, "", true);
    } else if (dl->archive == "sfxexe") {
        ex = ctx.execute({archive.string(), "-o" + install_dir.string(), "-y"}, "", true);
    } else { // "zip" (Windows bsdtar handles .zip too)
        ex = ctx.execute({"tar", "-xf", archive.string(), "-C", install_dir.string()}, "", true);
    }
    if (ex.exit_code != 0) {
        ui::error("Extraction failed for " + t.id + ".");
        std::cerr << ex.output << std::endl;
        fs::remove_all(install_dir, ec);
        return false;
    }

    if (!fs::exists(tool_bin_dir(t), ec)) {
        ui::error("Provisioned " + t.id + " but its bin dir is missing: " + tool_bin_dir(t).string());
        fs::remove_all(install_dir, ec);
        return false;
    }

    run_post_steps(ctx, t, *dl);
    fs::remove(archive, ec);
    std::ofstream(completion_marker(t)) << "ok\n";
    ui::success(t.id + " ready (" + tool_bin_dir(t).string() + ").");
    return true;
}

std::vector<fs::path> provisioned_bin_dirs() {
    std::vector<fs::path> dirs;
    for (const auto& [id, t] : registry().tools()) {
        if (tool_installed(t)) dirs.push_back(tool_bin_dir(t));
    }
    return dirs;
}

void inject_toolchains_into_path() {
    auto dirs = provisioned_bin_dirs();
    if (dirs.empty()) return;

#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string prefix;
    for (const auto& d : dirs) { prefix += d.string(); prefix += sep; }

    const char* cur = std::getenv("PATH");
    std::string updated = prefix + (cur ? cur : "");
#ifdef _WIN32
    ::_putenv_s("PATH", updated.c_str());
#else
    ::setenv("PATH", updated.c_str(), /*overwrite=*/1);
#endif
}

int install_tools(IExecutionContext& ctx, const std::vector<std::string>& binaries) {
    if (binaries.empty()) return 0;

    const Registry& reg = registry();
    auto pm = detect_package_manager();
    const std::string pmkey = pm ? package_manager_key(*pm) : "";

    std::vector<std::string> pm_packages;
    std::vector<const ToolDef*> to_download;
    std::vector<std::string> manual;

    for (const auto& bin : binaries) {
        const ToolDef* t = reg.tool_for_binary(bin);
        if (pm && t && t->packages.count(pmkey)) {
            pm_packages.push_back(t->packages.at(pmkey));
        } else if (t && t->download.count(host_platform_key())) {
            // de-dup downloads that share an archive id
            if (std::none_of(to_download.begin(), to_download.end(),
                             [&](const ToolDef* x) { return x->id == t->id; }))
                to_download.push_back(t);
        } else {
            manual.push_back(bin);
        }
    }

    bool ok = true;

    if (!pm_packages.empty()) {
        ui::step("Installing", "System package manager: " + std::to_string(pm_packages.size()) + " package(s)");
        auto res = ctx.execute(build_install_command(*pm, pm_packages));
        if (res.exit_code != 0) { ui::error("Package manager install failed."); ok = false; }
    }

    if (!to_download.empty()) {
        ui::step("Provisioning", "Downloading " + std::to_string(to_download.size()) + " portable tool(s) via nav.");
        for (const ToolDef* t : to_download)
            if (!provision_tool(ctx, *t)) ok = false;
        inject_toolchains_into_path();
    }

    for (const auto& bin : manual)
        ui::warning("No package or download known for '" + bin + "'. Install it manually.");

    return (ok && manual.empty()) ? 0 : 1;
}

} // namespace nav::core
