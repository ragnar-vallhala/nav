#include "nav/core/semver.hpp"

#include <gtest/gtest.h>

using nav::core::Version;
using nav::core::VersionReq;
using nav::core::parse_version;
using nav::core::parse_version_req;
using nav::core::matches;
using nav::core::to_string;

namespace {

Version V(std::uint32_t mj, std::uint32_t mn, std::uint32_t pt, std::string pre = "", std::string bld = "") {
    Version v{mj, mn, pt, std::move(pre), std::move(bld)};
    return v;
}

} // namespace

TEST(Semver, Parse_Simple) {
    auto v = parse_version("1.2.3");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->major, 1u);
    EXPECT_EQ(v->minor, 2u);
    EXPECT_EQ(v->patch, 3u);
    EXPECT_TRUE(v->prerelease.empty());
    EXPECT_TRUE(v->build.empty());
}

TEST(Semver, Parse_WithPrereleaseAndBuild) {
    auto v = parse_version("1.2.3-rc.1+sha.abc");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->prerelease, "rc.1");
    EXPECT_EQ(v->build, "sha.abc");
}

TEST(Semver, Parse_BuildWithoutPrerelease) {
    auto v = parse_version("0.5.0+meta");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->prerelease, "");
    EXPECT_EQ(v->build, "meta");
}

TEST(Semver, Parse_Zero) {
    auto v = parse_version("0.0.0");
    ASSERT_TRUE(v);
    EXPECT_EQ(v->major, 0u);
}

TEST(Semver, Parse_Rejects) {
    EXPECT_FALSE(parse_version("").has_value());
    EXPECT_FALSE(parse_version("1.2").has_value());
    EXPECT_FALSE(parse_version("1.2.3.4").has_value());
    EXPECT_FALSE(parse_version("1.2.x").has_value());
    EXPECT_FALSE(parse_version("01.2.3").has_value());        // leading zero
    EXPECT_FALSE(parse_version("1.02.3").has_value());
    EXPECT_FALSE(parse_version("v1.2.3").has_value());        // we don't accept 'v' prefix
    EXPECT_FALSE(parse_version("1.2.3-").has_value());        // empty prerelease
    EXPECT_FALSE(parse_version("1.2.3-01").has_value());      // numeric prerelease with leading zero
    EXPECT_FALSE(parse_version("1.2.3-?").has_value());       // invalid char
}

TEST(Semver, ToString_RoundTrips) {
    for (const char* s : {"1.2.3", "0.0.0", "10.20.30", "1.2.3-alpha", "1.2.3-alpha.1+sha"}) {
        auto v = parse_version(s);
        ASSERT_TRUE(v) << s;
        EXPECT_EQ(to_string(*v), s);
    }
}

TEST(Semver, Compare_Basic) {
    EXPECT_TRUE(V(1,2,3) <  V(1,2,4));
    EXPECT_TRUE(V(1,2,3) <  V(1,3,0));
    EXPECT_TRUE(V(1,2,3) <  V(2,0,0));
    EXPECT_TRUE(V(1,2,3) == V(1,2,3));
    EXPECT_TRUE(V(2,0,0) >  V(1,9,9));
}

TEST(Semver, Compare_PrereleaseLessThanRelease) {
    EXPECT_TRUE(V(1,0,0, "alpha") < V(1,0,0));
    EXPECT_TRUE(V(1,0,0) > V(1,0,0, "alpha"));
}

TEST(Semver, Compare_PrereleaseIdentifiers) {
    // Per SemVer §11.4 examples.
    EXPECT_TRUE(V(1,0,0,"alpha")    < V(1,0,0,"alpha.1"));
    EXPECT_TRUE(V(1,0,0,"alpha.1")  < V(1,0,0,"alpha.beta"));
    EXPECT_TRUE(V(1,0,0,"alpha.beta") < V(1,0,0,"beta"));
    EXPECT_TRUE(V(1,0,0,"beta")     < V(1,0,0,"beta.2"));
    EXPECT_TRUE(V(1,0,0,"beta.2")   < V(1,0,0,"beta.11"));
    EXPECT_TRUE(V(1,0,0,"beta.11")  < V(1,0,0,"rc.1"));
    EXPECT_TRUE(V(1,0,0,"rc.1")     < V(1,0,0));
}

TEST(Semver, Compare_BuildMetadataIgnored) {
    EXPECT_EQ(V(1,2,3,"", "abc").compare(V(1,2,3,"", "xyz")), 0);
}

TEST(Semver, ParseReq_Any) {
    auto r = parse_version_req("*");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->op, VersionReq::Op::Any);
}

TEST(Semver, ParseReq_AllOperators) {
    struct C { const char* in; VersionReq::Op op; };
    for (auto c : {
        C{"^1.2.3",  VersionReq::Op::Caret},
        C{"~1.2.3",  VersionReq::Op::Tilde},
        C{"=1.2.3",  VersionReq::Op::Exact},
        C{">=1.2.3", VersionReq::Op::GreaterEq},
        C{"<=1.2.3", VersionReq::Op::LessEq},
        C{">1.2.3",  VersionReq::Op::Greater},
        C{"<1.2.3",  VersionReq::Op::Less},
    }) {
        auto r = parse_version_req(c.in);
        ASSERT_TRUE(r) << c.in;
        EXPECT_EQ(r->op, c.op) << c.in;
        EXPECT_EQ(r->base, V(1,2,3));
    }
}

TEST(Semver, ParseReq_BareIsCaret) {
    auto r = parse_version_req("1.2.3");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->op, VersionReq::Op::Caret);
    EXPECT_EQ(r->base, V(1,2,3));
}

TEST(Semver, ParseReq_Whitespace) {
    auto r = parse_version_req("  ^1.2.3  ");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->op, VersionReq::Op::Caret);
}

TEST(Semver, ParseReq_Rejects) {
    EXPECT_FALSE(parse_version_req("").has_value());
    EXPECT_FALSE(parse_version_req("^").has_value());
    EXPECT_FALSE(parse_version_req("^x.y.z").has_value());
}

TEST(Semver, Matches_Any) {
    auto r = parse_version_req("*");
    ASSERT_TRUE(r);
    EXPECT_TRUE(matches(*r, V(0,0,0)));
    EXPECT_TRUE(matches(*r, V(99,0,0)));
}

TEST(Semver, Matches_Caret_NonZeroMajor) {
    auto r = parse_version_req("^1.2.3");
    ASSERT_TRUE(r);
    EXPECT_FALSE(matches(*r, V(1,2,2)));
    EXPECT_TRUE (matches(*r, V(1,2,3)));
    EXPECT_TRUE (matches(*r, V(1,2,9)));
    EXPECT_TRUE (matches(*r, V(1,9,9)));
    EXPECT_FALSE(matches(*r, V(2,0,0)));
}

TEST(Semver, Matches_Caret_ZeroMajor) {
    auto r = parse_version_req("^0.2.3");
    ASSERT_TRUE(r);
    EXPECT_FALSE(matches(*r, V(0,2,2)));
    EXPECT_TRUE (matches(*r, V(0,2,3)));
    EXPECT_TRUE (matches(*r, V(0,2,9)));
    EXPECT_FALSE(matches(*r, V(0,3,0)));
}

TEST(Semver, Matches_Caret_ZeroMajorZeroMinor) {
    auto r = parse_version_req("^0.0.3");
    ASSERT_TRUE(r);
    EXPECT_FALSE(matches(*r, V(0,0,2)));
    EXPECT_TRUE (matches(*r, V(0,0,3)));
    EXPECT_FALSE(matches(*r, V(0,0,4)));
}

TEST(Semver, Matches_Tilde) {
    auto r = parse_version_req("~1.2.3");
    ASSERT_TRUE(r);
    EXPECT_FALSE(matches(*r, V(1,2,2)));
    EXPECT_TRUE (matches(*r, V(1,2,3)));
    EXPECT_TRUE (matches(*r, V(1,2,99)));
    EXPECT_FALSE(matches(*r, V(1,3,0)));
    EXPECT_FALSE(matches(*r, V(2,0,0)));
}

TEST(Semver, Matches_ExactAndComparators) {
    EXPECT_TRUE (matches(*parse_version_req("=1.2.3"), V(1,2,3)));
    EXPECT_FALSE(matches(*parse_version_req("=1.2.3"), V(1,2,4)));

    EXPECT_TRUE (matches(*parse_version_req(">=1.2.3"), V(1,2,3)));
    EXPECT_TRUE (matches(*parse_version_req(">=1.2.3"), V(9,0,0)));
    EXPECT_FALSE(matches(*parse_version_req(">=1.2.3"), V(1,2,2)));

    EXPECT_TRUE (matches(*parse_version_req("<1.2.3"), V(1,2,2)));
    EXPECT_FALSE(matches(*parse_version_req("<1.2.3"), V(1,2,3)));
}

TEST(Semver, Matches_CaretRejectsBelowBase) {
    auto r = parse_version_req("^1.2.3");
    ASSERT_TRUE(r);
    EXPECT_FALSE(matches(*r, V(0,9,9)));
    EXPECT_FALSE(matches(*r, V(1,2,0)));
}
