#include "nav/core/command.hpp"
#include "nav/core/config.hpp"    // find_project_root
#include "nav/core/navconfig.hpp" // find_navhal_config, parse_kconfig, unmet_requirements
#include "nav/core/navhal.hpp"    // ensure_navhal_cached, link_navhal, kDefaultNavhalRef
#include "nav/core/ui.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

void set_env(const char* key, const std::string& val) {
#ifdef _WIN32
    ::_putenv_s(key, val.c_str());
#else
    ::setenv(key, val.c_str(), /*overwrite=*/1);
#endif
}

// Quote a path for the shell std::system() runs it through. Paths in a nav
// project don't contain double quotes, so wrapping is enough.
std::string q(const fs::path& p) { return "\"" + fs::absolute(p).string() + "\""; }

} // namespace

int ConfigCommand::run(IExecutionContext& ctx, const std::vector<std::string>& /*args*/) {
    auto root = find_project_root();
    if (!root) {
        ui::error("No nav.toml found in this directory or any parent.");
        return 1;
    }
    fs::current_path(*root);

    // NavHAL carries the Kconfig tree. Restore the link from the shared cache if
    // it's missing, exactly as `nav build` does.
    const fs::path navhal_dir = *root / "extern" / "NavHAL";
    if (!fs::exists(navhal_dir)) {
        ui::warning("NavHAL missing at extern/NavHAL — restoring from the shared cache.");
        const fs::path navhal_src = ensure_navhal_cached(ctx, kDefaultNavhalRef);
        if (navhal_src.empty() || !link_navhal(ctx, navhal_src, navhal_dir)) {
            ui::error("Could not restore NavHAL. Run 'nav build' once, then retry.");
            return 1;
        }
    }
    const fs::path kconfig = navhal_dir / "Kconfig";
    if (!fs::exists(kconfig)) {
        ui::error("No Kconfig at " + kconfig.string() + " — is NavHAL intact?");
        return 1;
    }

    const fs::path dotconfig = *root / ".config";

    // kconfiglib's menuconfig resolves `source` globs against $srctree and
    // reads/writes the file named by $KCONFIG_CONFIG. Point them at the NavHAL
    // clone and THIS project's .config, so edits land in the project (not the
    // gitignored submodule copy `nav build` derives from it).
    set_env("srctree", fs::absolute(navhal_dir).string());
    set_env("KCONFIG_CONFIG", fs::absolute(dotconfig).string());

    // High-contrast theme that adapts to the terminal's own palette instead of
    // hardcoding dark colours. Different terminals render named black/white as
    // muted greys and remap the 256-colour cube unpredictably, so the previous
    // "true black" attempts came out low-contrast or dark-blue. Instead:
    //   * body / list / help  -> terminal DEFAULT colours (empty spec). Whatever
    //     your shell already looks like — i.e. already readable for you.
    //   * bars (path/frame/separator) -> standout+bold (reverse video), which is
    //     guaranteed high-contrast on any palette.
    //   * selection -> black-on-cyan bold (cyan is the one accent that renders
    //     vividly across terminals).
    // menuconfig parses its 'default' theme first, so we reset the light
    // elements back to terminal default with an empty spec. Respect a user's own
    // MENUCONFIG_STYLE if set.
    if (!std::getenv("MENUCONFIG_STYLE")) {
        set_env("MENUCONFIG_STYLE",
                "list= body= help= show-help= inv-list= text= jump-edit= "
                "selection=fg:black,bg:cyan,bold "
                "inv-selection=fg:black,bg:cyan "
                "path=bold,standout "
                "separator=bold,standout "
                "frame=bold,standout "
                "edit=fg:black,bg:cyan");
    }

    ui::step("Config", "Opening NavHAL menuconfig — editing " + dotconfig.string());
    ui::info("Arrow keys / Enter to navigate, Space to toggle, ? for help, Q to save & quit.");

    // A full-screen curses TUI must own the real terminal, so launch it directly
    // (not through the captured execute()). python resolves from PATH — nav
    // injects its provisioned toolchains, and that python carries kconfiglib.
    const std::string cmd = "python3 -m menuconfig " + q(kconfig);
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        ui::error("menuconfig did not run. Ensure kconfiglib is installed for nav's "
                  "python (nav provisions it; otherwise: pip install kconfiglib).");
        return 1;
    }

    // Helpful nudge: if the project has library dependencies, re-check that the
    // just-saved .config still satisfies their declared capability needs, so a
    // toggled-off driver surfaces here rather than deep in the next build.
    if (auto cfg = load_project_config(*root); cfg && fs::exists(dotconfig)) {
        const auto have = parse_kconfig(dotconfig);
        bool warned = false;
        for (const auto& dep : cfg->dependencies) {
            const fs::path dep_dir = dep.is_path() ? (*root / dep.path).lexically_normal() : fs::path{};
            if (dep_dir.empty()) continue; // git deps are validated at build time
            if (auto lc = find_navhal_config(dep_dir)) {
                for (const auto& miss : unmet_requirements(have, parse_kconfig(*lc))) {
                    if (!warned) { ui::warning("Saved, but a dependency now needs capabilities your .config lacks:"); warned = true; }
                    ui::warning("  " + dep.name + " needs " + miss);
                }
            }
        }
    }

    ui::success("Configuration saved to .config. Run 'nav build' to apply it.");
    return 0;
}

} // namespace nav::core
