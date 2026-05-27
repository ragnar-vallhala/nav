#include "nav/core/semver.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <sstream>
#include <vector>

namespace nav::core {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_ident_char(char c) {
    return is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-';
}

bool valid_dot_separated_ident(std::string_view s) {
    // Each dot-separated segment must be non-empty and ident-only.
    if (s.empty()) return false;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            if (i == start) return false;
            for (std::size_t j = start; j < i; ++j) {
                if (!is_ident_char(s[j])) return false;
            }
            start = i + 1;
        }
    }
    return true;
}

std::optional<std::uint32_t> parse_u32(std::string_view s) {
    if (s.empty()) return std::nullopt;
    // No leading zeroes (per spec) unless the whole thing is "0".
    if (s.size() > 1 && s[0] == '0') return std::nullopt;
    std::uint32_t out = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return std::nullopt;
    return out;
}

bool is_numeric_ident(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s) if (!is_digit(c)) return false;
    return true;
}

int compare_prerelease_idents(std::string_view a, std::string_view b) {
    // Both numeric: numeric compare.
    const bool an = is_numeric_ident(a);
    const bool bn = is_numeric_ident(b);
    if (an && bn) {
        // Leading zeroes shouldn't occur for numeric idents per spec.
        // Use unsigned long long for headroom.
        unsigned long long av = 0, bv = 0;
        std::from_chars(a.data(), a.data() + a.size(), av);
        std::from_chars(b.data(), b.data() + b.size(), bv);
        if (av < bv) return -1;
        if (av > bv) return 1;
        return 0;
    }
    // Numeric has lower precedence than non-numeric.
    if (an && !bn) return -1;
    if (!an && bn) return 1;
    // Both non-numeric: lexicographic.
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

std::vector<std::string_view> split_dots(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            out.emplace_back(s.data() + start, i - start);
            start = i + 1;
        }
    }
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

} // namespace

int Version::compare(const Version& other) const {
    if (major != other.major) return major < other.major ? -1 : 1;
    if (minor != other.minor) return minor < other.minor ? -1 : 1;
    if (patch != other.patch) return patch < other.patch ? -1 : 1;

    // SemVer 2.0.0 §11.3: a version with prerelease has lower precedence
    // than one without.
    const bool ap = prerelease.empty();
    const bool bp = other.prerelease.empty();
    if (ap && bp) return 0;
    if (ap && !bp) return 1;
    if (!ap && bp) return -1;

    // Both have prerelease: dot-separated identifier comparison.
    auto la = split_dots(prerelease);
    auto lb = split_dots(other.prerelease);
    const std::size_t n = std::min(la.size(), lb.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (int c = compare_prerelease_idents(la[i], lb[i]); c != 0) return c;
    }
    if (la.size() < lb.size()) return -1;
    if (la.size() > lb.size()) return 1;
    return 0;
}

std::optional<Version> parse_version(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;

    // Optional leading 'v' is NOT accepted; we want canonical SemVer.

    // Split build metadata off (+...) first.
    std::string_view build_part;
    if (auto plus = s.find('+'); plus != std::string_view::npos) {
        build_part = s.substr(plus + 1);
        s = s.substr(0, plus);
        if (!valid_dot_separated_ident(build_part)) return std::nullopt;
    }

    // Then split prerelease (-...).
    std::string_view pre_part;
    if (auto minus = s.find('-'); minus != std::string_view::npos) {
        pre_part = s.substr(minus + 1);
        s = s.substr(0, minus);
        if (!valid_dot_separated_ident(pre_part)) return std::nullopt;
        // Per spec, numeric prerelease idents must not have leading zeroes.
        for (auto ident : split_dots(pre_part)) {
            if (is_numeric_ident(ident) && ident.size() > 1 && ident[0] == '0') {
                return std::nullopt;
            }
        }
    }

    // Remaining: major.minor.patch
    auto parts = split_dots(s);
    if (parts.size() != 3) return std::nullopt;
    Version v;
    auto mj = parse_u32(parts[0]);
    auto mn = parse_u32(parts[1]);
    auto pt = parse_u32(parts[2]);
    if (!mj || !mn || !pt) return std::nullopt;
    v.major = *mj;
    v.minor = *mn;
    v.patch = *pt;
    v.prerelease = std::string(pre_part);
    v.build      = std::string(build_part);
    return v;
}

std::string to_string(const Version& v) {
    std::ostringstream os;
    os << v.major << '.' << v.minor << '.' << v.patch;
    if (!v.prerelease.empty()) os << '-' << v.prerelease;
    if (!v.build.empty())      os << '+' << v.build;
    return os.str();
}

std::optional<VersionReq> parse_version_req(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;

    if (s == "*") {
        VersionReq r;
        r.op = VersionReq::Op::Any;
        return r;
    }

    VersionReq r;
    if (s.starts_with("^")) {
        r.op = VersionReq::Op::Caret;
        s.remove_prefix(1);
    } else if (s.starts_with("~")) {
        r.op = VersionReq::Op::Tilde;
        s.remove_prefix(1);
    } else if (s.starts_with(">=")) {
        r.op = VersionReq::Op::GreaterEq;
        s.remove_prefix(2);
    } else if (s.starts_with("<=")) {
        r.op = VersionReq::Op::LessEq;
        s.remove_prefix(2);
    } else if (s.starts_with(">")) {
        r.op = VersionReq::Op::Greater;
        s.remove_prefix(1);
    } else if (s.starts_with("<")) {
        r.op = VersionReq::Op::Less;
        s.remove_prefix(1);
    } else if (s.starts_with("=")) {
        r.op = VersionReq::Op::Exact;
        s.remove_prefix(1);
    } else {
        // Bare "1.2.3" → caret semantics (Cargo / npm convention).
        r.op = VersionReq::Op::Caret;
    }

    s = trim(s);
    auto v = parse_version(s);
    if (!v) return std::nullopt;
    r.base = *v;
    return r;
}

std::string to_string(const VersionReq& r) {
    switch (r.op) {
        case VersionReq::Op::Any:       return "*";
        case VersionReq::Op::Caret:     return "^" + to_string(r.base);
        case VersionReq::Op::Tilde:     return "~" + to_string(r.base);
        case VersionReq::Op::Exact:     return "=" + to_string(r.base);
        case VersionReq::Op::GreaterEq: return ">=" + to_string(r.base);
        case VersionReq::Op::LessEq:    return "<=" + to_string(r.base);
        case VersionReq::Op::Greater:   return ">" + to_string(r.base);
        case VersionReq::Op::Less:      return "<" + to_string(r.base);
    }
    return "*";
}

bool matches(const VersionReq& req, const Version& v) {
    switch (req.op) {
        case VersionReq::Op::Any:       return true;
        case VersionReq::Op::Exact:     return v == req.base;
        case VersionReq::Op::GreaterEq: return v >= req.base;
        case VersionReq::Op::LessEq:    return v <= req.base;
        case VersionReq::Op::Greater:   return v >  req.base;
        case VersionReq::Op::Less:      return v <  req.base;

        case VersionReq::Op::Caret: {
            // Per Cargo §SemVer compatibility:
            //   ^1.2.3 → >=1.2.3, <2.0.0
            //   ^0.2.3 → >=0.2.3, <0.3.0
            //   ^0.0.3 → >=0.0.3, <0.0.4
            //   ^0.0.0 → >=0.0.0, <0.0.1
            if (v < req.base) return false;
            Version upper = req.base;
            if (req.base.major > 0) {
                upper.major += 1;
                upper.minor = 0;
                upper.patch = 0;
            } else if (req.base.minor > 0) {
                upper.minor += 1;
                upper.patch = 0;
            } else {
                upper.patch += 1;
            }
            upper.prerelease.clear();
            upper.build.clear();
            return v < upper;
        }
        case VersionReq::Op::Tilde: {
            // ~1.2.3 → >=1.2.3, <1.3.0
            // ~1.2   → >=1.2.0, <1.3.0
            // ~1     → >=1.0.0, <2.0.0  (we don't currently parse "1" alone,
            //          so this branch only covers the 3-component form)
            if (v < req.base) return false;
            Version upper = req.base;
            upper.minor += 1;
            upper.patch = 0;
            upper.prerelease.clear();
            upper.build.clear();
            return v < upper;
        }
    }
    return false;
}

} // namespace nav::core
