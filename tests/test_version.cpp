/**
 * @file test_version.cpp
 * @brief Unit tests for version parsing and constraint matching
 */

#include "test_framework.h"
#include "core/version.hpp"

using namespace cforge;

// ============================================================================
// Semver parsing tests
// ============================================================================

TEST(Version, ParseSimple) {
    auto v = semver::parse("1.2.3");
    cf_assert(v.has_value());
    cf_assert(v->major == 1);
    cf_assert(v->minor == 2);
    cf_assert(v->patch == 3);
    cf_assert(v->prerelease.empty());
    return 0;
}

TEST(Version, ParseWithV) {
    auto v = semver::parse("v1.2.3");
    cf_assert(v.has_value());
    cf_assert(v->major == 1);
    cf_assert(v->minor == 2);
    cf_assert(v->patch == 3);
    return 0;
}

TEST(Version, ParseWithPrerelease) {
    auto v = semver::parse("1.2.3-beta");
    cf_assert(v.has_value());
    cf_assert(v->major == 1);
    cf_assert(v->minor == 2);
    cf_assert(v->patch == 3);
    cf_assert(v->prerelease == "beta");
    return 0;
}

TEST(Version, ParseWithBuild) {
    auto v = semver::parse("1.2.3+build123");
    cf_assert(v.has_value());
    cf_assert(v->build == "build123");
    return 0;
}

TEST(Version, ParseFull) {
    auto v = semver::parse("v1.2.3-rc1+build456");
    cf_assert(v.has_value());
    cf_assert(v->major == 1);
    cf_assert(v->minor == 2);
    cf_assert(v->patch == 3);
    cf_assert(v->prerelease == "rc1");
    cf_assert(v->build == "build456");
    return 0;
}

TEST(Version, ParseTwoParts) {
    auto v = semver::parse("1.2");
    cf_assert(v.has_value());
    cf_assert(v->major == 1);
    cf_assert(v->minor == 2);
    cf_assert(v->patch == 0);
    return 0;
}

TEST(Version, ParseOnePart) {
    auto v = semver::parse("1");
    cf_assert(v.has_value());
    cf_assert(v->major == 1);
    cf_assert(v->minor == 0);
    cf_assert(v->patch == 0);
    return 0;
}

TEST(Version, ParseInvalid) {
    cf_assert(!semver::parse("").has_value());
    cf_assert(!semver::parse("abc").has_value());
    cf_assert(!semver::parse("1.2.abc").has_value());
    return 0;
}

// ============================================================================
// Version comparison tests
// ============================================================================

TEST(Version, CompareMajor) {
    auto v1 = semver::parse("1.0.0");
    auto v2 = semver::parse("2.0.0");
    cf_assert(v1.has_value() && v2.has_value());
    cf_assert(*v1 < *v2);
    cf_assert(*v2 > *v1);
    return 0;
}

TEST(Version, CompareMinor) {
    auto v1 = semver::parse("1.1.0");
    auto v2 = semver::parse("1.2.0");
    cf_assert(v1.has_value() && v2.has_value());
    cf_assert(*v1 < *v2);
    return 0;
}

TEST(Version, ComparePatch) {
    auto v1 = semver::parse("1.0.1");
    auto v2 = semver::parse("1.0.2");
    cf_assert(v1.has_value() && v2.has_value());
    cf_assert(*v1 < *v2);
    return 0;
}

TEST(Version, CompareEqual) {
    auto v1 = semver::parse("1.2.3");
    auto v2 = semver::parse("1.2.3");
    cf_assert(v1.has_value() && v2.has_value());
    cf_assert(*v1 == *v2);
    return 0;
}

TEST(Version, ComparePrerelease) {
    // Version without prerelease is greater than with prerelease
    auto v1 = semver::parse("1.0.0-beta");
    auto v2 = semver::parse("1.0.0");
    cf_assert(v1.has_value() && v2.has_value());
    cf_assert(*v1 < *v2);
    return 0;
}

// ============================================================================
// Version constraint tests
// ============================================================================

TEST(Constraint, Exact) {
    auto req = version_requirement::parse("1.2.3");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("1.2.3"));
    cf_assert(!req->satisfies("1.2.4"));
    cf_assert(!req->satisfies("1.2.2"));
    return 0;
}

TEST(Constraint, GreaterThan) {
    auto req = version_requirement::parse(">1.0.0");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("1.0.1"));
    cf_assert(req->satisfies("2.0.0"));
    cf_assert(!req->satisfies("1.0.0"));
    cf_assert(!req->satisfies("0.9.9"));
    return 0;
}

TEST(Constraint, GreaterThanOrEqual) {
    auto req = version_requirement::parse(">=1.0.0");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("1.0.0"));
    cf_assert(req->satisfies("1.0.1"));
    cf_assert(req->satisfies("2.0.0"));
    cf_assert(!req->satisfies("0.9.9"));
    return 0;
}

TEST(Constraint, LessThan) {
    auto req = version_requirement::parse("<2.0.0");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("1.9.9"));
    cf_assert(req->satisfies("1.0.0"));
    cf_assert(!req->satisfies("2.0.0"));
    cf_assert(!req->satisfies("2.0.1"));
    return 0;
}

TEST(Constraint, LessThanOrEqual) {
    auto req = version_requirement::parse("<=2.0.0");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("2.0.0"));
    cf_assert(req->satisfies("1.9.9"));
    cf_assert(!req->satisfies("2.0.1"));
    return 0;
}

TEST(Constraint, Range) {
    auto req = version_requirement::parse(">=1.0.0,<2.0.0");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("1.0.0"));
    cf_assert(req->satisfies("1.5.0"));
    cf_assert(req->satisfies("1.9.9"));
    cf_assert(!req->satisfies("0.9.9"));
    cf_assert(!req->satisfies("2.0.0"));
    return 0;
}

TEST(Constraint, Caret) {
    // ^1.2.3 means >=1.2.3 and <2.0.0
    auto req = version_requirement::parse("^1.2.3");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("1.2.3"));
    cf_assert(req->satisfies("1.9.9"));
    cf_assert(!req->satisfies("1.2.2"));
    cf_assert(!req->satisfies("2.0.0"));
    return 0;
}

TEST(Constraint, CaretZero) {
    // ^0.2.3 means >=0.2.3 and <0.3.0 (special case for 0.x)
    auto req = version_requirement::parse("^0.2.3");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("0.2.3"));
    cf_assert(req->satisfies("0.2.9"));
    cf_assert(!req->satisfies("0.3.0"));
    cf_assert(!req->satisfies("0.2.2"));
    return 0;
}

TEST(Constraint, Tilde) {
    // ~1.2.3 means >=1.2.3 and <1.3.0
    auto req = version_requirement::parse("~1.2.3");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("1.2.3"));
    cf_assert(req->satisfies("1.2.9"));
    cf_assert(!req->satisfies("1.3.0"));
    cf_assert(!req->satisfies("1.2.2"));
    return 0;
}

TEST(Constraint, Any) {
    auto req = version_requirement::parse("*");
    cf_assert(req.has_value());
    cf_assert(req->satisfies("0.0.1"));
    cf_assert(req->satisfies("1.0.0"));
    cf_assert(req->satisfies("999.999.999"));
    return 0;
}

// ============================================================================
// Find best version tests
// ============================================================================

TEST(Version, FindBest) {
    std::vector<std::string> available = {"1.0.0", "1.1.0", "1.2.0", "2.0.0", "2.1.0"};

    auto req = version_requirement::parse("^1.0.0");
    cf_assert(req.has_value());

    auto best = find_best_version(available, *req);
    cf_assert(best.has_value());
    cf_assert(*best == "1.2.0"); // Highest 1.x.x version
    return 0;
}

TEST(Version, FindBestRange) {
    std::vector<std::string> available = {"1.0.0", "1.5.0", "2.0.0", "2.5.0", "3.0.0"};

    auto req = version_requirement::parse(">=1.5.0,<3.0.0");
    cf_assert(req.has_value());

    auto best = find_best_version(available, *req);
    cf_assert(best.has_value());
    cf_assert(*best == "2.5.0");
    return 0;
}

TEST(Version, FindBestNone) {
    std::vector<std::string> available = {"1.0.0", "1.1.0"};

    auto req = version_requirement::parse(">=2.0.0");
    cf_assert(req.has_value());

    auto best = find_best_version(available, *req);
    cf_assert(!best.has_value());
    return 0;
}
