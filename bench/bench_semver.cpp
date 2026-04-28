/**
 * @file bench_semver.cpp
 * @brief Microbenchmarks for cforge's semver parsing and constraint matching.
 *
 * Built with cforge's builtin BENCH() framework — no Google Benchmark or
 * nanobench dependency, no manual main(), nothing to register by hand.
 */

#include "bench_framework.h"
#include "core/version.hpp"

#include <string>
#include <vector>

using namespace cforge;

// A representative set of versions that exercise different parser paths:
// short, full, prerelease, build metadata, and the "v" prefix.
static const std::vector<std::string> kVersions = {
    "1.0.0",
    "v1.2.3",
    "0.0.1",
    "10.20.30",
    "1.2.3-alpha",
    "1.2.3-beta.1",
    "v1.0.0-rc1+build.42",
    "2.0.0-rc.2+sha.abcdef",
    "1.2",
    "1",
};

BENCH(Semver, ParseSimple) {
    auto v = semver::parse("1.2.3");
    if (!v) cf_clobber_();
}

BENCH(Semver, ParseWithPrereleaseAndBuild) {
    auto v = semver::parse("v1.2.3-rc1+build.42");
    if (!v) cf_clobber_();
}

BENCH(Semver, ParseInvalid) {
    auto v = semver::parse("not-a-version");
    if (v) cf_clobber_();
}

BENCH(Semver, ParseMixedSet) {
    for (const auto &s : kVersions) {
        auto v = semver::parse(s);
        if (!v) cf_clobber_();
    }
}

BENCH(Semver, CompareBasic) {
    auto a = semver::parse("1.2.3");
    auto b = semver::parse("1.2.4");
    bool less = (*a < *b);
    if (!less) cf_clobber_();
}

BENCH(Semver, ComparePrerelease) {
    auto a = semver::parse("1.0.0-alpha");
    auto b = semver::parse("1.0.0");
    bool less = (*a < *b);
    if (!less) cf_clobber_();
}

BENCH(Constraint, ParseCaret) {
    auto r = version_requirement::parse("^1.2.3");
    if (!r) cf_clobber_();
}

BENCH(Constraint, ParseRange) {
    auto r = version_requirement::parse(">=1.0.0,<2.0.0");
    if (!r) cf_clobber_();
}

BENCH(Constraint, SatisfiesCaret) {
    static auto req = version_requirement::parse("^1.2.3");
    bool ok = req->satisfies("1.5.0");
    if (!ok) cf_clobber_();
}

BENCH(Constraint, SatisfiesRange) {
    static auto req = version_requirement::parse(">=1.0.0,<2.0.0");
    bool ok = req->satisfies("1.5.7");
    if (!ok) cf_clobber_();
}

// Search the best matching version across a sizeable list — closer to what
// the dependency resolver actually does.
static const std::vector<std::string> kHaystack = {
    "0.5.0", "0.9.0", "1.0.0", "1.0.1", "1.0.2",
    "1.1.0", "1.1.1", "1.2.0", "1.2.1", "1.2.2",
    "1.3.0", "1.3.1", "2.0.0", "2.0.1", "2.1.0",
    "2.5.0", "3.0.0-rc1", "3.0.0", "3.1.0", "3.2.0",
};

BENCH(FindBest, Caret) {
    static auto req = version_requirement::parse("^1.0.0");
    auto best = find_best_version(kHaystack, *req);
    if (!best) cf_clobber_();
}

BENCH(FindBest, Range) {
    static auto req = version_requirement::parse(">=2.0.0,<3.0.0");
    auto best = find_best_version(kHaystack, *req);
    if (!best) cf_clobber_();
}

BENCH(FindBest, Tilde) {
    static auto req = version_requirement::parse("~1.2.0");
    auto best = find_best_version(kHaystack, *req);
    if (!best) cf_clobber_();
}
