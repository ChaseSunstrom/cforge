/**
 * @file test_framework.h
 * @brief Lightweight, self-contained test framework for cforge projects
 *
 * This is a header-only test framework that outputs in a format parseable by
 * cforge's test runner. It has no external dependencies beyond the C++ standard library.
 *
 * Usage:
 *   #include "test_framework.h"
 *
 *   TEST(MyTest) {
 *       cf_assert(1 + 1 == 2);
 *       cf_assert_eq(2, 2);
 *       return 0;  // 0 = pass, non-zero = fail
 *   }
 *
 *   TEST(Category, SpecificTest) {
 *       cf_assert(true);
 *       return 0;
 *   }
 *
 *   int main() {
 *       return cf_run_tests();
 *   }
 */

#ifndef CFORGE_TEST_FRAMEWORK_H
#define CFORGE_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#include <vector>
#include <string>
#include <functional>
#endif

/* ============================================================================
 * ANSI Color Codes (disabled on Windows unless ANSI is enabled)
 * ============================================================================ */

#if defined(_WIN32) && !defined(CF_FORCE_COLORS)
#define CF_COLOR_RED     ""
#define CF_COLOR_GREEN   ""
#define CF_COLOR_YELLOW  ""
#define CF_COLOR_CYAN    ""
#define CF_COLOR_RESET   ""
#else
#define CF_COLOR_RED     "\x1b[31m"
#define CF_COLOR_GREEN   "\x1b[32m"
#define CF_COLOR_YELLOW  "\x1b[33m"
#define CF_COLOR_CYAN    "\x1b[36m"
#define CF_COLOR_RESET   "\x1b[0m"
#endif

/* ============================================================================
 * Assertion Macros
 * ============================================================================ */

/**
 * @brief Basic assertion - returns 1 (failure) if expression is false
 */
#define cf_assert(expr)                                                         \
    do {                                                                        \
        if (!(expr)) {                                                          \
            fprintf(stderr, CF_COLOR_RED                                        \
                "Assertion failed: %s at %s:%d\n"                               \
                CF_COLOR_RESET,                                                 \
                #expr, __FILE__, __LINE__);                                     \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/**
 * @brief Equality assertion with value printing
 */
#define cf_assert_eq(expected, actual)                                          \
    do {                                                                        \
        if ((expected) != (actual)) {                                           \
            fprintf(stderr, CF_COLOR_RED                                        \
                "Assertion failed: %s == %s at %s:%d\n"                         \
                CF_COLOR_RESET,                                                 \
                #expected, #actual, __FILE__, __LINE__);                        \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/**
 * @brief Not-equal assertion
 */
#define cf_assert_ne(a, b)                                                      \
    do {                                                                        \
        if ((a) == (b)) {                                                       \
            fprintf(stderr, CF_COLOR_RED                                        \
                "Assertion failed: %s != %s at %s:%d\n"                         \
                CF_COLOR_RESET,                                                 \
                #a, #b, __FILE__, __LINE__);                                    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/**
 * @brief Null assertion
 */
#define cf_assert_null(ptr)                                                     \
    do {                                                                        \
        if ((ptr) != NULL) {                                                    \
            fprintf(stderr, CF_COLOR_RED                                        \
                "Assertion failed: %s is not null at %s:%d\n"                   \
                CF_COLOR_RESET,                                                 \
                #ptr, __FILE__, __LINE__);                                      \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/**
 * @brief Not-null assertion
 */
#define cf_assert_not_null(ptr)                                                 \
    do {                                                                        \
        if ((ptr) == NULL) {                                                    \
            fprintf(stderr, CF_COLOR_RED                                        \
                "Assertion failed: %s is null at %s:%d\n"                       \
                CF_COLOR_RESET,                                                 \
                #ptr, __FILE__, __LINE__);                                      \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/* Legacy alias */
#define test_assert(expr) cf_assert(expr)

/* ============================================================================
 * C++ Test Registry (header-only implementation)
 * ============================================================================ */

#ifdef __cplusplus

namespace cf_test {

/**
 * @brief Represents a single test case
 */
struct TestCase {
    std::string name;
    std::function<int()> func;

    TestCase(const std::string& n, std::function<int()> f)
        : name(n), func(f) {}
};

/**
 * @brief Global test registry - singleton pattern
 */
inline std::vector<TestCase>& get_test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

/**
 * @brief Registers a test at static initialization time
 */
struct TestRegistrar {
    TestRegistrar(const char* name, std::function<int()> func) {
        get_test_registry().emplace_back(name, func);
    }

    TestRegistrar(const char* category, const char* name, std::function<int()> func) {
        std::string full_name = std::string(category) + "." + name;
        get_test_registry().emplace_back(full_name, func);
    }
};

/**
 * @brief Runs all registered tests and prints results
 * @return 0 if all tests pass, 1 if any test fails
 */
inline int run_all_tests() {
    auto& tests = get_test_registry();

    int passed = 0;
    int failed = 0;

    printf("\n");

    for (const auto& test : tests) {
        // Print [RUN] marker - this is what cforge's builtin adapter parses
        printf("[RUN] %s\n", test.name.c_str());
        fflush(stdout);

        int result = test.func();

        if (result == 0) {
            printf(CF_COLOR_GREEN "[PASS] %s" CF_COLOR_RESET "\n", test.name.c_str());
            passed++;
        } else {
            printf(CF_COLOR_RED "[FAIL] %s" CF_COLOR_RESET "\n", test.name.c_str());
            failed++;
        }
        fflush(stdout);
    }

    // Print summary
    printf("\n");
    printf("==============================\n");
    if (failed == 0) {
        printf(CF_COLOR_GREEN "All %d tests passed!" CF_COLOR_RESET "\n", passed);
    } else {
        printf(CF_COLOR_RED "%d of %d tests failed" CF_COLOR_RESET "\n",
               failed, passed + failed);
    }
    printf("==============================\n");

    return failed > 0 ? 1 : 0;
}

} // namespace cf_test

/**
 * @brief Runs all registered tests (C++ entry point)
 */
inline int cf_run_tests() {
    return cf_test::run_all_tests();
}

/* ============================================================================
 * TEST Macro Definitions (C++)
 * ============================================================================ */

/**
 * @brief Helper macros for overloading TEST() based on argument count
 */
#define CF_TEST_CONCAT_(a, b) a##b
#define CF_TEST_CONCAT(a, b) CF_TEST_CONCAT_(a, b)
#define CF_TEST_UNIQUE_NAME CF_TEST_CONCAT(cf_test_registrar_, __LINE__)

/**
 * @brief Single-argument TEST(name) - just a test name
 * Note: Functions are NOT static so they can be linked from other translation units
 */
#define CF_TEST1(name)                                                          \
    int name();                                                                 \
    static cf_test::TestRegistrar CF_TEST_UNIQUE_NAME(#name, name);             \
    int name()

/**
 * @brief Two-argument TEST(category, name) - category.name format
 * Note: Functions are NOT static so they can be linked from other translation units
 */
#define CF_TEST2(category, name)                                                \
    int category##_##name();                                                    \
    static cf_test::TestRegistrar CF_TEST_UNIQUE_NAME(#category, #name,         \
                                                       category##_##name);      \
    int category##_##name()

/**
 * @brief Macro overloading trick to pick TEST1 or TEST2
 * MSVC requires extra expansion step with EXPAND
 */
#define CF_EXPAND(x) x
#define CF_GET_MACRO(_1, _2, NAME, ...) NAME
#define CF_TEST_CHOOSER(...) CF_EXPAND(CF_GET_MACRO(__VA_ARGS__, CF_TEST2, CF_TEST1))
#define TEST(...) CF_EXPAND(CF_TEST_CHOOSER(__VA_ARGS__)(__VA_ARGS__))

#else /* !__cplusplus - Pure C implementation */

/* ============================================================================
 * C Test Registry (requires manual registration)
 * ============================================================================ */

typedef int (*cf_test_func_t)(void);

typedef struct {
    const char* name;
    cf_test_func_t func;
} cf_test_case_t;

/* Maximum number of tests (adjust as needed) */
#define CF_MAX_TESTS 256

/* Global test array - defined in exactly one translation unit */
#ifndef CF_TEST_IMPL
extern cf_test_case_t cf_test_registry[CF_MAX_TESTS];
extern int cf_test_count;
#else
cf_test_case_t cf_test_registry[CF_MAX_TESTS];
int cf_test_count = 0;
#endif

/**
 * @brief Register a test (call before running tests)
 */
static inline void cf_register_test(const char* name, cf_test_func_t func) {
    if (cf_test_count < CF_MAX_TESTS) {
        cf_test_registry[cf_test_count].name = name;
        cf_test_registry[cf_test_count].func = func;
        cf_test_count++;
    }
}

/**
 * @brief Run all registered tests
 */
static inline int cf_run_tests(void) {
    int passed = 0;
    int failed = 0;
    int i;

    printf("\n");

    for (i = 0; i < cf_test_count; i++) {
        printf("[RUN] %s\n", cf_test_registry[i].name);
        fflush(stdout);

        int result = cf_test_registry[i].func();

        if (result == 0) {
            printf(CF_COLOR_GREEN "[PASS] %s" CF_COLOR_RESET "\n",
                   cf_test_registry[i].name);
            passed++;
        } else {
            printf(CF_COLOR_RED "[FAIL] %s" CF_COLOR_RESET "\n",
                   cf_test_registry[i].name);
            failed++;
        }
        fflush(stdout);
    }

    printf("\n");
    printf("==============================\n");
    if (failed == 0) {
        printf(CF_COLOR_GREEN "All %d tests passed!" CF_COLOR_RESET "\n", passed);
    } else {
        printf(CF_COLOR_RED "%d of %d tests failed" CF_COLOR_RESET "\n",
               failed, passed + failed);
    }
    printf("==============================\n");

    return failed > 0 ? 1 : 0;
}

/**
 * @brief TEST macro for C - declares function, auto-registration not available
 */
#define CF_C_TEST1(name) int name(void)
#define CF_C_TEST2(cat, name) int cat##_##name(void)

#define CF_C_EXPAND(x) x
#define CF_C_GET_MACRO(_1, _2, NAME, ...) NAME
#define CF_C_TEST_CHOOSER(...) CF_C_EXPAND(CF_C_GET_MACRO(__VA_ARGS__, CF_C_TEST2, CF_C_TEST1))
#define TEST(...) CF_C_EXPAND(CF_C_TEST_CHOOSER(__VA_ARGS__)(__VA_ARGS__))

#endif /* __cplusplus */

#endif /* CFORGE_TEST_FRAMEWORK_H */
