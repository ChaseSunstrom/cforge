
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

// ANSI colors
#define COLOR_RED   "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_CYAN  "\x1b[36m"
#define COLOR_RESET "\x1b[0m"

/// Assertion macro: returns 1 on failure, 0 on success
#define test_assert(expr)                           \
    do {                                           \
        if (!(expr)) {                             \
            fprintf(stderr, COLOR_RED              \
                "Assertion failed: %s at %s:%d\n" \
                COLOR_RESET,                      \
                #expr, __FILE__, __LINE__);       \
            return 1;                             \
        }                                          \
        return 0;                                  \
    } while (0)
#define cf_assert(expr) test_assert(expr)

// TEST macro: supports TEST(name) or TEST(Category, name)
#define TEST1(name) int name()
#define TEST2(cat,name) int cat##_##name()
#define GET_TEST(_1,_2,NAME,...) NAME
#define TEST(...) GET_TEST(__VA_ARGS__,TEST2,TEST1)(__VA_ARGS__)

#endif // TEST_FRAMEWORK_H
