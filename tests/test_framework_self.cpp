/**
 * @file test_framework_self.cpp
 * @brief Tests the cforge test framework itself — proves that:
 *   - TEST() auto-registration works across multiple .cpp files
 *   - The glob matcher behaves correctly on the patterns the runner uses
 *   - The filter-matching shorthand (suite-prefix, substring) works
 */

#include "test_framework.h"
#include <string>

// ----------------------------------------------------------------------------
// Glob matcher
// ----------------------------------------------------------------------------

TEST(Glob, ExactMatch) {
    cf_assert(cf_glob_match_("foo", "foo") == 1);
    cf_assert(cf_glob_match_("foo", "bar") == 0);
    return 0;
}

TEST(Glob, SuffixWildcard) {
    cf_assert(cf_glob_match_("Math.*", "Math.Add") == 1);
    cf_assert(cf_glob_match_("Math.*", "Math.") == 1);
    cf_assert(cf_glob_match_("Math.*", "Math") == 0);
    cf_assert(cf_glob_match_("Math.*", "Algo.Add") == 0);
    return 0;
}

TEST(Glob, PrefixWildcard) {
    cf_assert(cf_glob_match_("*.Add", "Math.Add") == 1);
    cf_assert(cf_glob_match_("*.Add", "Algo.Add") == 1);
    cf_assert(cf_glob_match_("*.Add", "Add") == 0);
    return 0;
}

TEST(Glob, MultipleStars) {
    cf_assert(cf_glob_match_("*foo*bar*", "xfoozbar") == 1);
    cf_assert(cf_glob_match_("*foo*bar*", "xfoobarz") == 1);
    cf_assert(cf_glob_match_("*foo*bar*", "barfoo") == 0);
    return 0;
}

TEST(Glob, QuestionMark) {
    cf_assert(cf_glob_match_("a?c", "abc") == 1);
    cf_assert(cf_glob_match_("a?c", "abbc") == 0);
    cf_assert(cf_glob_match_("a?c", "ac") == 0);
    return 0;
}

TEST(Glob, EmptyAndEdge) {
    cf_assert(cf_glob_match_("", "") == 1);
    cf_assert(cf_glob_match_("*", "") == 1);
    cf_assert(cf_glob_match_("*", "anything") == 1);
    cf_assert(cf_glob_match_("", "x") == 0);
    return 0;
}

// ----------------------------------------------------------------------------
// Filter shorthand (what cf_run_tests uses to match positional/--filter args)
// ----------------------------------------------------------------------------

TEST(Filter, NoFiltersAcceptsAll) {
    const char* const* none = nullptr;
    cf_assert(cf_filter_matches_("Math.Add", none, 0) == 1);
    return 0;
}

TEST(Filter, ExactMatch) {
    const char* filters[] = {"Math.Add"};
    cf_assert(cf_filter_matches_("Math.Add", filters, 1) == 1);
    cf_assert(cf_filter_matches_("Math.Subtract", filters, 1) == 0);
    return 0;
}

TEST(Filter, SuiteShorthand) {
    // "Math" should match Math.* tests but not Algo.*
    const char* filters[] = {"Math"};
    cf_assert(cf_filter_matches_("Math.Add", filters, 1) == 1);
    cf_assert(cf_filter_matches_("Math.Subtract", filters, 1) == 1);
    cf_assert(cf_filter_matches_("Algo.Sort", filters, 1) == 0);
    return 0;
}

TEST(Filter, GlobPattern) {
    const char* filters[] = {"*.Add*"};
    cf_assert(cf_filter_matches_("Math.Add", filters, 1) == 1);
    cf_assert(cf_filter_matches_("Math.AddOne", filters, 1) == 1);
    cf_assert(cf_filter_matches_("Math.Subtract", filters, 1) == 0);
    return 0;
}

TEST(Filter, MultipleFiltersOred) {
    const char* filters[] = {"Math.Add", "Algo.Sort"};
    cf_assert(cf_filter_matches_("Math.Add", filters, 2) == 1);
    cf_assert(cf_filter_matches_("Algo.Sort", filters, 2) == 1);
    cf_assert(cf_filter_matches_("Crypto.Hash", filters, 2) == 0);
    return 0;
}

TEST(Filter, SubstringFallback) {
    // Non-glob, non-prefix filters fall through to substring matching so
    // partial typing like "Lock" finds Lockfile.* entries.
    const char* filters[] = {"Lock"};
    cf_assert(cf_filter_matches_("Lockfile.LoadValid", filters, 1) == 1);
    cf_assert(cf_filter_matches_("Math.Add", filters, 1) == 0);
    return 0;
}

// ----------------------------------------------------------------------------
// Smoke test for assertions — confirms cf_assert_eq/ne fire correctly.
// ----------------------------------------------------------------------------

TEST(Assert, EqAndNe) {
    cf_assert_eq(2 + 2, 4);
    cf_assert_ne(2 + 2, 5);
    return 0;
}

TEST(Assert, NullChecks) {
    int x = 42;
    int* p = &x;
    int* np = nullptr;
    cf_assert_not_null(p);
    cf_assert_null(np);
    return 0;
}
