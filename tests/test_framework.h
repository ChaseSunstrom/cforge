/**
 * @file test_framework.h
 * @brief Zero-config, auto-registering test framework for cforge projects
 *
 * Single header. Works in C and C++. No manual main(), no manual registration.
 * Just write TEST(...) and run.
 *
 *   #include "test_framework.h"
 *
 *   TEST(Math, Add) {
 *       cf_assert(1 + 1 == 2);
 *       return 0;
 *   }
 *
 *   TEST(Trivial) {
 *       cf_assert(42 == 42);
 *       return 0;
 *   }
 *
 * cforge auto-generates the main() when it builds a builtin-framework target,
 * so you do not need to write one. If you want one anyway:
 *
 *   int main(int argc, char** argv) { return cf_run_tests(argc, argv); }
 *
 * CLI accepted by cf_run_tests:
 *   --list                       Print test names, one per line, then exit
 *   -f / --filter <glob>         Only run tests whose name matches the glob
 *                                (supports * and ?, case-sensitive). May be
 *                                given multiple times; matches are OR-ed.
 *   -v / --verbose               More chatty output
 *   --no-color                   Disable ANSI colors
 *   <positional...>              Treated as additional --filter patterns
 *
 * Output format is line-oriented and stable; cforge's test_runner parses it.
 *
 * SOURCE OF TRUTH: this file is mirrored byte-for-byte by an embedded copy in
 * src/core/commands/command_test.cpp. Update both together.
 */

#ifndef CFORGE_TEST_FRAMEWORK_H
#define CFORGE_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Portable cross-TU "static initializer" — registers tests before main()
 * ============================================================================ */

#if defined(__cplusplus)
  /* In C++ we use a static object whose constructor runs at startup. */
#elif defined(_MSC_VER)
  /* MSVC: place a function pointer in .CRT$XCU so the CRT calls it before
     main(). Force the linker to keep the symbol via /include:. */
  #pragma section(".CRT$XCU", read)
  #ifdef _WIN64
    #define CF_INCLUDE_PREFIX ""
  #else
    #define CF_INCLUDE_PREFIX "_"
  #endif
  #define CF_CTOR_(fn, line)                                                    \
      static void fn(void);                                                     \
      __pragma(section(".CRT$XCU",read))                                        \
      __declspec(allocate(".CRT$XCU")) void (*fn##_ptr_##line)(void) = fn;      \
      __pragma(comment(linker,"/include:" CF_INCLUDE_PREFIX #fn "_ptr_" #line)) \
      static void fn(void)
  #define CF_CTOR_X(fn, line) CF_CTOR_(fn, line)
  #define CF_CTOR(fn) CF_CTOR_X(fn, __LINE__)
#elif defined(__GNUC__) || defined(__clang__)
  #define CF_CTOR(fn) static void __attribute__((constructor)) fn(void)
#else
  #error "Unsupported compiler: please use MSVC, GCC, or Clang"
#endif

/* selectany-equivalent: lets us define globals in this header without
   multiple-definition errors at link time.
   On PE/COFF (Windows), use __declspec(selectany) — even with GCC/Clang —
   because GCC's `__attribute__((weak))` puts the variable in the wrong COMDAT
   group when it's referenced from an inline constructor, producing
   "multiple definition of .weak.foo._ZN..." link errors. mingw and clang both
   accept __declspec(selectany) on Windows targets. */
#if defined(_WIN32)
  #define CF_SELECTANY __declspec(selectany)
#elif defined(_MSC_VER)
  #define CF_SELECTANY __declspec(selectany)
#elif defined(__GNUC__) || defined(__clang__)
  #define CF_SELECTANY __attribute__((weak))
#else
  #define CF_SELECTANY
#endif

/* Suppress "unused static function" warnings in TUs that include the header
   but never call the helpers — common when each test file gets its own copy. */
#if defined(__GNUC__) || defined(__clang__)
  #define CF_MAYBE_UNUSED __attribute__((unused))
#else
  #define CF_MAYBE_UNUSED
#endif

/* ============================================================================
 * Cross-language registry (C linkage so C and C++ TUs share the same symbol)
 * ============================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*cf_test_func_t)(void);

typedef struct cf_test_node {
    const char* name;
    cf_test_func_t func;
    struct cf_test_node* next;
} cf_test_node_t;

CF_SELECTANY cf_test_node_t* cf_test_head_ = (cf_test_node_t*)0;
CF_SELECTANY int cf_color_enabled_ = 1;

static CF_MAYBE_UNUSED void cf_test_register_node_(cf_test_node_t* node) {
    node->next = cf_test_head_;
    cf_test_head_ = node;
}

/* Optional: explicit registration for code that can't use the macros. */
static CF_MAYBE_UNUSED void cf_register_test(const char* name, cf_test_func_t func) {
    cf_test_node_t* node = (cf_test_node_t*)malloc(sizeof(cf_test_node_t));
    if (!node) return;
    node->name = name;
    node->func = func;
    cf_test_register_node_(node);
}

static CF_MAYBE_UNUSED const char* cf_red_(void)    { return cf_color_enabled_ ? "\x1b[31m" : ""; }
static CF_MAYBE_UNUSED const char* cf_green_(void)  { return cf_color_enabled_ ? "\x1b[32m" : ""; }
static CF_MAYBE_UNUSED const char* cf_yellow_(void) { return cf_color_enabled_ ? "\x1b[33m" : ""; }
static CF_MAYBE_UNUSED const char* cf_cyan_(void)   { return cf_color_enabled_ ? "\x1b[36m" : ""; }
static CF_MAYBE_UNUSED const char* cf_dim_(void)    { return cf_color_enabled_ ? "\x1b[2m"  : ""; }
static CF_MAYBE_UNUSED const char* cf_reset_(void)  { return cf_color_enabled_ ? "\x1b[0m"  : ""; }

#ifdef __cplusplus
} /* extern "C" */
#endif

/* Legacy color macros — old tests that splice these into format strings keep
   working. They emit ANSI unconditionally; modern Windows handles this fine
   once cf_run_tests has called the VT-enable path. */
#define CF_COLOR_RED     "\x1b[31m"
#define CF_COLOR_GREEN   "\x1b[32m"
#define CF_COLOR_YELLOW  "\x1b[33m"
#define CF_COLOR_CYAN    "\x1b[36m"
#define CF_COLOR_RESET   "\x1b[0m"

/* ============================================================================
 * VT mode enable on Windows so ANSI colors render in cmd/PowerShell hosts
 * ============================================================================ */

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  static CF_MAYBE_UNUSED void cf_enable_vt_(void) {
      HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
      DWORD mode = 0;
      if (h == INVALID_HANDLE_VALUE || h == NULL) { cf_color_enabled_ = 0; return; }
      if (!GetConsoleMode(h, &mode)) {
          /* Not a console (redirected to file/pipe) — leave colors as-is. */
          return;
      }
      if (!SetConsoleMode(h, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */)) {
          cf_color_enabled_ = 0;
      }
  }
#else
  static CF_MAYBE_UNUSED void cf_enable_vt_(void) { /* POSIX terminals support ANSI natively. */ }
#endif

/* ============================================================================
 * Assertion macros (return non-zero on failure)
 * ============================================================================ */

#define cf_assert(expr)                                                         \
    do {                                                                        \
        if (!(expr)) {                                                          \
            fprintf(stderr, "%sAssertion failed: %s at %s:%d%s\n",              \
                cf_red_(), #expr, __FILE__, __LINE__, cf_reset_());             \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define cf_assert_eq(expected, actual)                                          \
    do {                                                                        \
        if ((expected) != (actual)) {                                           \
            fprintf(stderr, "%sAssertion failed: %s == %s at %s:%d%s\n",        \
                cf_red_(), #expected, #actual, __FILE__, __LINE__, cf_reset_());\
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define cf_assert_ne(a, b)                                                      \
    do {                                                                        \
        if ((a) == (b)) {                                                       \
            fprintf(stderr, "%sAssertion failed: %s != %s at %s:%d%s\n",        \
                cf_red_(), #a, #b, __FILE__, __LINE__, cf_reset_());            \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define cf_assert_null(ptr)                                                     \
    do {                                                                        \
        if ((ptr) != NULL) {                                                    \
            fprintf(stderr, "%sAssertion failed: %s is not null at %s:%d%s\n",  \
                cf_red_(), #ptr, __FILE__, __LINE__, cf_reset_());              \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define cf_assert_not_null(ptr)                                                 \
    do {                                                                        \
        if ((ptr) == NULL) {                                                    \
            fprintf(stderr, "%sAssertion failed: %s is null at %s:%d%s\n",      \
                cf_red_(), #ptr, __FILE__, __LINE__, cf_reset_());              \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/* Legacy alias kept for old call sites. */
#define test_assert(expr) cf_assert(expr)

/* ============================================================================
 * Glob match: '*' matches any run, '?' matches one char.
 * ============================================================================ */

static CF_MAYBE_UNUSED int cf_glob_match_(const char* pat, const char* s) {
    while (*pat && *s) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            if (*pat == 0) return 1;
            while (*s) {
                if (cf_glob_match_(pat, s)) return 1;
                s++;
            }
            return 0;
        } else if (*pat == '?' || *pat == *s) {
            pat++; s++;
        } else {
            return 0;
        }
    }
    while (*pat == '*') pat++;
    return *pat == 0 && *s == 0;
}

/* ============================================================================
 * TEST macro — works the same way in C and C++
 *
 * TEST(name)              -> registers as "name"
 * TEST(suite, name)       -> registers as "suite.name"
 * ============================================================================ */

#define CF_CONCAT_(a, b) a##b
#define CF_CONCAT(a, b) CF_CONCAT_(a, b)
#define CF_UNIQUE(prefix) CF_CONCAT(prefix, __LINE__)

#ifdef __cplusplus

namespace cf_test_internal {
struct Registrar {
    cf_test_node_t node;
    Registrar(const char* name, cf_test_func_t fn) {
        node.name = name;
        node.func = fn;
        node.next = nullptr;
        cf_test_register_node_(&node);
    }
};
} /* namespace cf_test_internal */

#define CF_TEST1(name)                                                          \
    static int name(void);                                                      \
    static ::cf_test_internal::Registrar CF_UNIQUE(cf_reg_)(#name, name);       \
    static int name(void)

#define CF_TEST2(suite, name)                                                   \
    static int CF_CONCAT(suite, CF_CONCAT(_, name))(void);                      \
    static ::cf_test_internal::Registrar CF_UNIQUE(cf_reg_)(                    \
        #suite "." #name, CF_CONCAT(suite, CF_CONCAT(_, name)));                \
    static int CF_CONCAT(suite, CF_CONCAT(_, name))(void)

#else /* C path: use a constructor function to register before main(). */

/* Bake the test name into the registrar symbol so the MSVC pointer-into-CRT$XCU
   trick produces a unique extern symbol per test (line-only would collide
   across translation units when two TESTs land on the same line). */
#define CF_TEST1(name)                                                          \
    static int name(void);                                                      \
    static cf_test_node_t CF_CONCAT(cf_node_, name) = { #name, name, 0 };       \
    CF_CTOR(CF_CONCAT(cf_reg_, name)) {                                         \
        cf_test_register_node_(&CF_CONCAT(cf_node_, name));                     \
    }                                                                           \
    static int name(void)

#define CF_TEST2(suite, name)                                                   \
    static int CF_CONCAT(suite, CF_CONCAT(_, name))(void);                      \
    static cf_test_node_t                                                       \
        CF_CONCAT(cf_node_, CF_CONCAT(suite, CF_CONCAT(_, name))) = {           \
            #suite "." #name,                                                   \
            CF_CONCAT(suite, CF_CONCAT(_, name)), 0 };                          \
    CF_CTOR(CF_CONCAT(cf_reg_, CF_CONCAT(suite, CF_CONCAT(_, name)))) {         \
        cf_test_register_node_(                                                 \
            &CF_CONCAT(cf_node_, CF_CONCAT(suite, CF_CONCAT(_, name))));        \
    }                                                                           \
    static int CF_CONCAT(suite, CF_CONCAT(_, name))(void)

#endif

#define CF_EXPAND(x) x
#define CF_GET_MACRO(_1, _2, NAME, ...) NAME
#define CF_TEST_CHOOSER(...) CF_EXPAND(CF_GET_MACRO(__VA_ARGS__, CF_TEST2, CF_TEST1))
#define TEST(...) CF_EXPAND(CF_TEST_CHOOSER(__VA_ARGS__)(__VA_ARGS__))

/* ============================================================================
 * Runner
 * ============================================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/* Reverse the linked list to give declaration order. */
static CF_MAYBE_UNUSED cf_test_node_t* cf_test_collect_in_order_(int* out_count) {
    cf_test_node_t* prev = (cf_test_node_t*)0;
    cf_test_node_t* curr = cf_test_head_;
    int count = 0;
    while (curr) {
        cf_test_node_t* next = curr->next;
        curr->next = prev;
        prev = curr;
        curr = next;
        count++;
    }
    cf_test_head_ = prev;
    if (out_count) *out_count = count;
    return prev;
}

static CF_MAYBE_UNUSED int cf_filter_matches_(const char* name,
                                              const char* const* filters, int filter_count) {
    int i;
    if (filter_count <= 0) return 1;
    for (i = 0; i < filter_count; i++) {
        const char* f = filters[i];
        size_t flen;
        if (!f) continue;
        if (cf_glob_match_(f, name)) return 1;
        if (strchr(f, '*') == 0 && strchr(f, '?') == 0) {
            if (strcmp(f, name) == 0) return 1;
            flen = strlen(f);
            /* "Suite" matches anything in "Suite.*" */
            if (strncmp(f, name, flen) == 0 && name[flen] == '.') return 1;
            if (strstr(name, f) != 0) return 1;
        }
    }
    return 0;
}

/**
 * @brief Run registered tests, optionally with CLI args.
 *
 * Pass argc/argv from main() (or 0/NULL for "run everything").
 * Returns 0 on success, 1 if any test failed.
 */
static CF_MAYBE_UNUSED int cf_run_tests(int argc, char** argv) {
    const char* filters[64];
    int filter_count = 0;
    int list_only = 0;
    int verbose = 0;
    int i;
    int total = 0;
    int passed = 0, failed = 0, skipped = 0;
    cf_test_node_t* head;
    cf_test_node_t* n;

    cf_enable_vt_();

    for (i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "--list") == 0) {
            list_only = 1;
        } else if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(a, "--no-color") == 0) {
            cf_color_enabled_ = 0;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--filter") == 0) {
            if (i + 1 < argc && filter_count < 64) {
                filters[filter_count++] = argv[++i];
            }
        } else if (a[0] != '-') {
            if (filter_count < 64) filters[filter_count++] = a;
        }
        /* unknown flags ignored so external runners can pass extras through */
    }

    head = cf_test_collect_in_order_(&total);

    if (list_only) {
        for (n = head; n != 0; n = n->next) {
            if (cf_filter_matches_(n->name, filters, filter_count)) {
                printf("%s\n", n->name);
            }
        }
        fflush(stdout);
        return 0;
    }

    printf("\n");
    for (n = head; n != 0; n = n->next) {
        int rc;
        if (!cf_filter_matches_(n->name, filters, filter_count)) {
            if (verbose) {
                printf("%s[SKIP]%s %s\n", cf_yellow_(), cf_reset_(), n->name);
            }
            skipped++;
            continue;
        }
        printf("%s[RUN]%s %s\n", cf_cyan_(), cf_reset_(), n->name);
        fflush(stdout);
        rc = n->func ? n->func() : 1;
        if (rc == 0) {
            printf("%s[PASS]%s %s\n", cf_green_(), cf_reset_(), n->name);
            passed++;
        } else {
            printf("%s[FAIL]%s %s\n", cf_red_(), cf_reset_(), n->name);
            failed++;
        }
        fflush(stdout);
    }

    printf("\n");
    printf("==============================\n");
    if (failed == 0) {
        printf("%sAll %d tests passed%s\n",
               cf_green_(), passed, cf_reset_());
    } else {
        printf("%s%d of %d tests failed%s\n",
               cf_red_(), failed, passed + failed, cf_reset_());
    }
    if (skipped && verbose) {
        printf("%s%d skipped%s\n", cf_dim_(), skipped, cf_reset_());
    }
    printf("==============================\n");
    fflush(stdout);

    return failed > 0 ? 1 : 0;
}

/* Convenience for legacy "no args" call sites. */
static CF_MAYBE_UNUSED int cf_run_tests_default(void) {
    return cf_run_tests(0, (char**)0);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* CFORGE_TEST_FRAMEWORK_H */
