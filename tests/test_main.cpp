#include "test_framework.h"
#include <stdio.h>

extern int Add();
extern int Subtract();
extern int Divide();

struct test_entry { const char* full; int (*fn)(); };
static test_entry tests[] = {
  {"Add", Add},
  {"Subtract", Subtract},
  {"Divide", Divide},
};

int main(int argc, char** argv) {
  (void)argc; (void)argv;
  int failures = 0;
  for (auto& tc : tests) {
    printf(COLOR_CYAN "[RUNNING] %s" COLOR_RESET "\n", tc.full);
  }
  for (auto& tc : tests) {
    int res = tc.fn();
    if (res) {
      printf(COLOR_RED "[FAIL] %s" COLOR_RESET "\n", tc.full);
      ++failures;
    } else {
      printf(COLOR_GREEN "[PASS] %s" COLOR_RESET "\n", tc.full);
    }
  }
  printf("Ran %zu tests: %d failures\n", sizeof(tests)/sizeof(tests[0]), failures);
  return failures;
}
