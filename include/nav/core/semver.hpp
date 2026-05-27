#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nav::core {

// SemVer 2.0.0 version (major.minor.patch[-prerelease][+build]).
struct Version {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string   prerelease;   // empty when absent; "alpha.1" etc.
    std::string   build;        // build metadata; ignored for precedence per spec

    // Three-way comparison per SemVer 2.0.0 §11:
    //   1. major, minor, patch lexicographic
    //   2. when major.minor.patch equal: a version with prerelease has LOWER
    //      precedence than one without
    //   3. when both have prerelease: compare dot-separated identifiers,
    //      numeric < non-numeric, numerically vs lexicographically as
    //      appropriate
    //   build metadata is NEVER consulted for precedence
    int compare(const Version& other) const;

    bool operator==(const Version& o) const { return compare(o) == 0; }
    bool operator!=(const Version& o) const { return compare(o) != 0; }
    bool operator< (const Version& o) const { return compare(o) <  0; }
    bool operator<=(const Version& o) const { return compare(o) <= 0; }
    bool operator> (const Version& o) const { return compare(o) >  0; }
    bool operator>=(const Version& o) const { return compare(o) >= 0; }
};

// Parse a SemVer string. Returns nullopt for any malformed input.
// Accepted forms: "1.2.3", "1.2.3-alpha", "1.2.3+build", "1.2.3-rc.1+sha".
std::optional<Version> parse_version(std::string_view s);

// Stringify in canonical form.
std::string to_string(const Version& v);

// Version requirement.
//
// Supported syntax:
//   =1.2.3   — exact match (also the meaning of bare "1.2.3" with no operator)
//   ^1.2.3   — compatible: >=1.2.3, <2.0.0 (or <0.3.0 for "^0.2.x", etc.)
//   ~1.2.3   — tilde:      >=1.2.3, <1.3.0
//   >=1.2.3, <=1.2.3, >1.2.3, <1.2.3 — direct comparison
//   *        — any version
//
// Bare "1.2.3" is treated as "=1.2.3" (exact). This is the conservative
// default — when a user or generator writes a bare version, we take it
// literally rather than inferring a compatibility range.
struct VersionReq {
    enum class Op {
        Any,         // *
        Caret,       // ^
        Tilde,       // ~
        Exact,       // =
        GreaterEq,   // >=
        LessEq,      // <=
        Greater,     // >
        Less,        // <
    };
    Op op = Op::Any;
    Version base;     // unused when op == Any
};

std::optional<VersionReq> parse_version_req(std::string_view s);

std::string to_string(const VersionReq& r);

// Does this concrete version satisfy the requirement?
bool matches(const VersionReq& req, const Version& v);

} // namespace nav::core
