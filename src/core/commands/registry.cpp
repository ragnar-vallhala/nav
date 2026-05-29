// Registry-facing verbs. `add` and `search` talk to a live registry
// (default http://localhost:8081, override with NAV_REGISTRY_URL); `login`
// and `publish` remain preview stubs pending the auth workstream.
#include "nav/core/command.hpp"
#include "nav/core/cache.hpp"
#include "nav/core/config.hpp"
#include "nav/core/fetch.hpp"
#include "nav/core/http_index.hpp"
#include "nav/core/lockfile.hpp"
#include "nav/core/platform.hpp"
#include "nav/core/resolver.hpp"
#include "nav/core/semver.hpp"
#include "nav/core/ui.hpp"

#include <toml++/toml.hpp>
#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace nav::core {

namespace {

void preview_notice(const std::string& verb) {
    ui::warning("'nav " + verb + "' is coming soon — pending the registry rollout (see docs/plan.md).");
}

// Split "name" or "name@req" into a (name, requirement) pair. A bare name maps
// to "*" (any version) so the resolver picks the highest available.
std::pair<std::string, std::string> split_spec(const std::string& arg) {
    auto at = arg.find('@');
    if (at == std::string::npos) return {arg, "*"};
    return {arg.substr(0, at), arg.substr(at + 1)};
}

// Read the project's existing [dependencies] table as name -> requirement-string.
std::map<std::string, std::string> read_dependencies(const fs::path& nav_toml) {
    std::map<std::string, std::string> out;
    toml::table tbl;
    try {
        tbl = toml::parse_file(nav_toml.string());
    } catch (const toml::parse_error&) {
        return out;
    }
    if (auto deps = tbl["dependencies"].as_table()) {
        for (auto&& [k, v] : *deps) {
            if (auto s = v.value<std::string>()) out.emplace(std::string(k.str()), *s);
        }
    }
    return out;
}

// Persist a dependency into nav.toml's [dependencies] table. Note: round-trips
// through toml++ and rewrites the file, so hand-written comments are not
// preserved. Acceptable until nav.toml migrates to YAML.
bool upsert_dependency(const fs::path& nav_toml, const std::string& name, const std::string& req) {
    toml::table tbl;
    try {
        tbl = toml::parse_file(nav_toml.string());
    } catch (const toml::parse_error&) {
        return false;
    }
    if (!tbl.contains("dependencies")) {
        tbl.insert("dependencies", toml::table{});
    }
    if (auto deps = tbl["dependencies"].as_table()) {
        deps->insert_or_assign(name, req);
    } else {
        return false;
    }
    std::ofstream out(nav_toml);
    if (!out) return false;
    out << tbl << "\n";
    return out.good();
}

// Case-insensitive hex digest comparison.
bool hex_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// Download + verify + extract a set of packages into the shared cache, printing
// one line per package. Returns true only if every package ended up intact in
// the cache.
bool fetch_into_cache(IExecutionContext& ctx, const std::vector<FetchRequest>& reqs) {
    PackageFetcher fetcher(ctx, cache_root());
    bool all_ok = true;
    for (const auto& r : reqs) {
        FetchOutcome out = fetcher.fetch(r);
        const std::string label = r.name + " " + r.version;
        if (fetch_ok(out.status)) {
            ui::tool_ok(label, to_string(out.status) + " -> " + out.path.string());
        } else {
            all_ok = false;
            ui::error(label + ": " + to_string(out.status)
                      + (out.detail.empty() ? "" : " (" + out.detail + ")"));
        }
    }
    return all_ok;
}

} // namespace

int AddCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    if (args.empty()) {
        ui::error("Usage: nav add <package>[@<version-req>]");
        return 1;
    }

    auto root = find_project_root();
    if (!root) {
        ui::error("No nav.toml found in this directory or any parent.");
        return 1;
    }
    const fs::path nav_toml = *root / "nav.toml";

    auto [name, req_str] = split_spec(args[0]);
    auto req = parse_version_req(req_str);
    if (!req) {
        ui::error("Invalid version requirement '" + req_str + "' for '" + name + "'.");
        return 1;
    }

    // Assemble the full root set: existing deps + the new one.
    std::map<std::string, VersionReq> roots;
    for (const auto& [dn, dr] : read_dependencies(nav_toml)) {
        if (auto r = parse_version_req(dr)) roots.emplace(dn, *r);
    }
    roots.insert_or_assign(name, *req);  // new request wins if already present

    ui::step("Resolving", "Querying registry " + default_registry_url() + " ...");
    HttpIndexClient index(default_registry_url(), ctx);
    Resolver resolver(index);
    auto result = resolver.resolve(roots);

    if (!result.ok) {
        ui::error("Resolution failed: " + result.error.message);
        return 1;
    }

    const std::string source = "registry+" + default_registry_url();
    auto lock = to_lockfile(result, source);
    const fs::path lock_path = *root / "nav.lock";
    if (!save_lockfile(lock, lock_path)) {
        ui::error("Failed to write " + lock_path.string());
        return 1;
    }

    if (!upsert_dependency(nav_toml, name, req_str)) {
        ui::warning("Resolved and locked, but failed to update nav.toml [dependencies].");
    }

    ui::success("Added '" + name + "'. Resolved " + std::to_string(lock.packages.size()) + " package(s):");
    for (const auto& p : lock.packages) {
        std::cout << "  " << p.name << " " << to_string(p.version)
                  << " (" << to_string(p.kind) << ")\n";
    }
    ui::info("Lockfile written: " + lock_path.string());

    // Fetch the resolved closure into the shared cache. We already hold the full
    // index metadata (URLs + checksums) from resolution, so no second round-trip
    // is needed — build the requests straight from result.resolved.
    const std::string platform = host_platform_key(ctx);
    std::vector<FetchRequest> reqs;
    reqs.reserve(result.resolved.size());
    for (const auto& [pkg_name, rp] : result.resolved) {
        reqs.push_back(FetchRequest{pkg_name, to_string(rp.version.version),
                                    rp.version.kind, platform, rp.version.downloads});
    }

    ui::step("Fetching", "Downloading packages into " + cache_root().string() + " ...");
    if (!fetch_into_cache(ctx, reqs)) {
        ui::warning("Some packages could not be cached. Resolution is locked; "
                    "re-run 'nav fetch' once the issue is resolved.");
        return 1;
    }
    return 0;
}

int SearchCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    if (args.empty()) {
        ui::error("Usage: nav search <query>");
        return 1;
    }
    const std::string query = args[0];
    const std::string url = default_registry_url() + "/api/v1/search?q=" + query;

    auto res = ctx.execute({"curl", "-fsS", url}, "", true, std::chrono::seconds(30));
    if (res.exit_code != 0) {
        ui::error("Registry query failed (" + default_registry_url() + "). Is it running?");
        return 1;
    }

    json doc;
    try {
        doc = json::parse(res.output);
    } catch (const json::parse_error&) {
        ui::error("Registry returned a malformed response.");
        return 1;
    }

    if (!doc.contains("results") || !doc["results"].is_array() || doc["results"].empty()) {
        ui::info("No packages match '" + query + "'.");
        return 0;
    }

    ui::step("Search", "Results for '" + query + "':");
    for (const auto& r : doc["results"]) {
        const std::string ns      = r.value("namespace", "");
        const std::string name    = r.value("name", "");
        const std::string version = r.value("version", "");
        const std::string desc    = r.value("description", "");
        std::cout << "  " << ui::BOLD() << ns << "/" << name << ui::RESET()
                  << " " << version << "\n";
        if (!desc.empty()) std::cout << "      " << desc << "\n";
    }
    return 0;
}

int FetchCommand::run(IExecutionContext& ctx, const std::vector<std::string>& args) {
    auto root = find_project_root();
    if (!root) {
        ui::error("No nav.toml found in this directory or any parent.");
        return 1;
    }
    const fs::path lock_path = *root / "nav.lock";
    auto lock = load_lockfile(lock_path);
    if (!lock) {
        ui::error("No readable nav.lock at " + lock_path.string()
                  + ". Run 'nav add <package>' first.");
        return 1;
    }

    const std::string only = args.empty() ? "" : args[0];

    // The lockfile pins versions + checksums but not URLs (URLs aren't lock
    // material — they can change without affecting reproducibility). Re-read the
    // index for each locked package to recover the download URL for its pinned
    // version, then cross-check the index's checksum against the lock so a
    // mutated registry artifact is refused rather than silently fetched.
    HttpIndexClient index(default_registry_url(), ctx);
    const std::string platform = host_platform_key(ctx);

    std::vector<FetchRequest> reqs;
    for (const auto& lp : lock->packages) {
        if (!only.empty() && lp.name != only) continue;

        auto pkg = index.fetch(lp.name);
        if (!pkg) {
            ui::error("Registry has no index entry for '" + lp.name + "' ("
                      + default_registry_url() + "). Is it running?");
            return 1;
        }

        const std::string want = to_string(lp.version);
        const IndexVersion* match = nullptr;
        for (const auto& v : pkg->versions) {
            if (to_string(v.version) == want) { match = &v; break; }
        }
        if (!match) {
            ui::error("Registry no longer lists " + lp.name + " " + want
                      + " (pinned in nav.lock).");
            return 1;
        }

        // Refuse if the registry's checksum for the host's artifact diverges
        // from what the lock recorded.
        if (auto sel = select_download(match->downloads, match->kind, platform)) {
            auto locked = lp.checksums.find(sel->first);
            if (locked != lp.checksums.end() && !hex_equal(locked->second, sel->second.sha256)) {
                ui::error(lp.name + " " + want + ": registry checksum for '" + sel->first
                          + "' differs from nav.lock — refusing to fetch.");
                return 1;
            }
        }

        reqs.push_back(FetchRequest{lp.name, want, match->kind, platform, match->downloads});
    }

    if (!only.empty() && reqs.empty()) {
        ui::error("'" + only + "' is not present in nav.lock.");
        return 1;
    }
    if (reqs.empty()) {
        ui::info("nav.lock has no packages to fetch.");
        return 0;
    }

    ui::step("Fetching", "Downloading " + std::to_string(reqs.size())
             + " package(s) into " + cache_root().string() + " ...");
    if (!fetch_into_cache(ctx, reqs)) {
        ui::error("One or more packages could not be cached.");
        return 1;
    }
    ui::success("All packages present in the cache.");
    return 0;
}

int LoginCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& /*args*/) {
    preview_notice("login");
    return 0;
}

int PublishCommand::run(IExecutionContext& /*ctx*/, const std::vector<std::string>& /*args*/) {
    preview_notice("publish");
    return 0;
}

} // namespace nav::core
