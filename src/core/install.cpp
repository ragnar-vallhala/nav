#include "nav/core/install.hpp"

#include "nav/core/cache.hpp"
#include "nav/core/deps.hpp"
#include "nav/core/http_index.hpp"
#include "nav/core/ui.hpp"

#include <cctype>

namespace fs = std::filesystem;

namespace nav::core {

namespace {

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

} // namespace

std::vector<EnsureOutcome>
ensure_locked_present(IExecutionContext& ctx, const fs::path& cache_root,
                      const std::string& registry_url, const Lockfile& lock,
                      const std::string& only, bool skip_cached,
                      const std::string& platform_key) {
    HttpIndexClient index(registry_url, ctx);
    PackageFetcher fetcher(ctx, cache_root);

    std::vector<EnsureOutcome> out;
    for (const auto& lp : lock.packages) {
        if (!only.empty() && lp.name != only) continue;

        EnsureOutcome eo;
        eo.name = lp.name;
        eo.version = to_string(lp.version);

        // Warm-cache fast path: a complete install needs no registry round-trip.
        if (skip_cached) {
            auto dir = locked_cache_dir(lp, cache_root, platform_key);
            if (dir && is_installed(*dir)) {
                eo.status = EnsureStatus::Ok;
                eo.fetch.status = FetchStatus::AlreadyCached;
                eo.fetch.name = lp.name;
                eo.fetch.version = eo.version;
                eo.fetch.path = *dir;
                out.push_back(std::move(eo));
                continue;
            }
        }

        auto pkg = index.fetch(lp.name);
        if (!pkg) {
            eo.status = EnsureStatus::IndexUnavailable;
            out.push_back(std::move(eo));
            continue;
        }

        const IndexVersion* match = nullptr;
        for (const auto& v : pkg->versions) {
            if (to_string(v.version) == eo.version) { match = &v; break; }
        }
        if (!match) {
            eo.status = EnsureStatus::VersionMissing;
            out.push_back(std::move(eo));
            continue;
        }

        if (auto sel = select_download(match->downloads, match->kind, platform_key)) {
            auto locked = lp.checksums.find(sel->first);
            if (locked != lp.checksums.end() && !hex_equal(locked->second, sel->second.sha256)) {
                eo.status = EnsureStatus::ChecksumDivergence;
                eo.detail = "key '" + sel->first + "'";
                out.push_back(std::move(eo));
                continue;
            }
        }

        eo.status = EnsureStatus::Ok;
        eo.fetch = fetcher.fetch(FetchRequest{lp.name, eo.version, match->kind,
                                              platform_key, match->downloads});
        out.push_back(std::move(eo));
    }
    return out;
}

bool all_present(const std::vector<EnsureOutcome>& outcomes) {
    for (const auto& o : outcomes) {
        if (o.status != EnsureStatus::Ok || !fetch_ok(o.fetch.status)) return false;
    }
    return true;
}

void report_ensure_outcomes(const std::vector<EnsureOutcome>& outcomes) {
    for (const auto& o : outcomes) {
        const std::string label = o.name + " " + o.version;
        switch (o.status) {
            case EnsureStatus::Ok:
                if (fetch_ok(o.fetch.status)) {
                    ui::tool_ok(label, to_string(o.fetch.status) + " -> " + o.fetch.path.string());
                } else {
                    ui::error(label + ": " + to_string(o.fetch.status)
                              + (o.fetch.detail.empty() ? "" : " (" + o.fetch.detail + ")"));
                }
                break;
            case EnsureStatus::IndexUnavailable:
                ui::error(label + ": registry has no index entry (is it running?)");
                break;
            case EnsureStatus::VersionMissing:
                ui::error(label + ": registry no longer lists this pinned version");
                break;
            case EnsureStatus::ChecksumDivergence:
                ui::error(label + ": registry checksum for " + o.detail
                          + " differs from nav.lock — refusing to fetch");
                break;
        }
    }
}

} // namespace nav::core
