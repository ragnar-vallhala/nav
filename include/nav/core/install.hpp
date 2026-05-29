#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "nav/core/execution_context.hpp"
#include "nav/core/fetch.hpp"
#include "nav/core/lockfile.hpp"

namespace nav::core {

enum class EnsureStatus {
    Ok,                  // resolved against the index; inspect `fetch` for the result
    IndexUnavailable,    // registry has no index entry for the package
    VersionMissing,      // index no longer lists the pinned version
    ChecksumDivergence,  // index checksum differs from the lock — refused
};

struct EnsureOutcome {
    std::string  name;
    std::string  version;
    EnsureStatus status = EnsureStatus::Ok;
    FetchOutcome fetch;    // meaningful only when status == Ok
    std::string  detail;   // populated for the non-Ok statuses
};

// Ensure every package pinned in `lock` is present in the cache under
// `cache_root`, fetching any that are missing from `registry_url`. Each package
// is looked up in the index to recover the download URL for its pinned version,
// and the index checksum is cross-checked against the lock (divergence refused —
// a mutated registry artifact must not silently replace the locked one).
//
//   only:        when non-empty, restrict to that single package name.
//   skip_cached: when true, an already-installed package is reported cached
//                WITHOUT contacting the registry (the offline warm-cache path
//                `nav build` wants); when false, every selected package is
//                re-validated against the index (what `nav fetch` does).
std::vector<EnsureOutcome>
ensure_locked_present(IExecutionContext& ctx, const std::filesystem::path& cache_root,
                      const std::string& registry_url, const Lockfile& lock,
                      const std::string& only, bool skip_cached,
                      const std::string& platform_key);

// True iff every outcome left its package intact in the cache.
bool all_present(const std::vector<EnsureOutcome>& outcomes);

// Render one UI line per outcome (success via ui::tool_ok, problems via
// ui::error). Shared by `nav add`/`nav fetch`/`nav build`.
void report_ensure_outcomes(const std::vector<EnsureOutcome>& outcomes);

} // namespace nav::core
