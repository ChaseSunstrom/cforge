
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <stddef.h>

#define MAX_TESTS 256

/// Common typedef for both C and C++
typedef int (*test_fn_t)(void);
typedef struct {
    const char *name;
    test_fn_t    func;
} test_case_t;

/// Simple assertion macros
#define test_assert(expr)                                      \
    do { if (!(expr)) {                                        \
            fprintf(stderr,                                   \
                    "Assertion failed: %s at %s:%d\n",        \
                    #expr, __FILE__, __LINE__);               \
            return 1;                                         \
        }                                                     \
        return 0;                                             \
    } while (0)

#define cf_assert(expr) test_assert(expr)

#ifdef __cplusplus

  #include <vector>
  #include <string>

  namespace tst {
    /// Registry of all tests
    inline std::vector<std::pair<std::string,test_fn_t>>& all_tests() {
      static std::vector<std::pair<std::string,test_fn_t>> v;
      return v;
    }
    /// RAII‐registrar: runs before main()
    struct registrar {
      registrar(const std::string &name, test_fn_t fn) {
        all_tests().emplace_back(name, fn);
      }
    };
  }

  // Helpers to generate unique names
  #define CONCAT_INTERNAL(a,b) a##b
  #define CONCAT(a,b) CONCAT_INTERNAL(a,b)

  /// C++ TEST macro: emits a static registrar whose ctor auto‐registers
  #define TEST(cat,name)                                         \
    static int cat##_##name(void);                               \
    static ::tst::registrar                                      \
      CONCAT(_registrar_,__LINE__)(                              \
        std::string(#cat) + "." + #name,                         \
        &cat##_##name                                           \
      );                                                         \
    static int cat##_##name(void)

  /// Main runner for C++
  static int test_main(void) {
    auto &v = ::tst::all_tests();
    int failures = 0;
    for (auto &tc : v) {
      printf("[ RUN ] %s\n", tc.first.c_str());
      bool failed = (tc.second() != 0);
      printf(failed ? "[FAIL] %s\n" : "[PASS] %s\n", tc.first.c_str());
      failures += failed;
    }
    printf("Ran %zu tests: %d failures\n", v.size(), failures);
    return failures;
  }

#else  /* plain C */

  /// Put every test_case_t into a custom ELF section "test_cases"


  #if defined(__GNUC__) || defined(__clang__)
    #define TEST_EXPORT __attribute__((used, section("test_cases")))
  #else
    #error "Compiler must support section attribute for C auto‐registration"
  #endif

  /// C TEST macro: emits a test_case_t into .test_cases
  #define TEST(cat,name)                                        \
    static int cat##_##name(void);                              \
    TEST_EXPORT                                                \
    static test_case_t _tc_##cat##_##name = {                    \
      #cat "." #name,                                           \
      cat##_##name                                             \
    };                                                          \
    static int cat##_##name(void)

  /// Linker‐provided symbols bounding the array
  extern test_case_t __start_test_cases[];
  extern test_case_t __stop_test_cases[];

  /// Main runner for C
  static int test_main(void) {
    test_case_t *tc = __start_test_cases;
    int failures = 0;
    for (; tc < __stop_test_cases; ++tc) {
      printf("[ RUN ] %s\n", tc->name);
      int r = tc->func();
      if (r) {
        printf("[FAIL] %s\n", tc->name);
        ++failures;
      } else {
        printf("[PASS] %s\n", tc->name);
      }
    }
    size_t total = __stop_test_cases - __start_test_cases;
    printf("Ran %zu tests: %d failures\n", total, failures);
    return failures;
  }

#endif /* __cplusplus */

#endif /* TEST_FRAMEWORK_H */
