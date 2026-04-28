/**
 * @file bench_glob.cpp
 * @brief Microbenchmarks for the framework's glob matcher.
 *
 * cfb_glob_match_ is the same code shape as the test framework's matcher and
 * is exercised on every test/bench filter. Worth keeping fast.
 */

#include "bench_framework.h"

BENCH(Glob, ExactMatch) {
    int r = cfb_glob_match_("Math.Add", "Math.Add");
    cf_clobber_();
    (void)r;
}

BENCH(Glob, SimpleStarSuffix) {
    int r = cfb_glob_match_("Math.*", "Math.Add");
    cf_clobber_();
    (void)r;
}

BENCH(Glob, SimpleStarPrefix) {
    int r = cfb_glob_match_("*.Add", "Math.Add");
    cf_clobber_();
    (void)r;
}

BENCH(Glob, MultiStar) {
    int r = cfb_glob_match_("*.Add*", "Math.AddOne");
    cf_clobber_();
    (void)r;
}

BENCH(Glob, NoMatch) {
    int r = cfb_glob_match_("Algo.*", "Math.Add");
    cf_clobber_();
    (void)r;
}

BENCH(Glob, LongStringStarMiddle) {
    int r = cfb_glob_match_(
        "prefix*suffix",
        "prefix_lots_of_stuff_in_the_middle_with_words_suffix");
    cf_clobber_();
    (void)r;
}

BENCH(Glob, QuestionMark) {
    int r = cfb_glob_match_("Test??", "TestAB");
    cf_clobber_();
    (void)r;
}
