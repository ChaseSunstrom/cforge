#include "test_framework.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

int Math_Add();
int Math_Subtract();
int Divide();

struct test_entry { const char* full; int (*fn)(); };
static test_entry tests[] = {
  {"Math.Add", Math_Add},
  {"Math.Subtract", Math_Subtract},
  {"Divide", Divide},
};

int main(int argc, char** argv) {
  std::string category;
  std::vector<std::string> test_filters;
  // Positional args: first is category (optional), rest are test names
  for (int i = 1; i < argc; ++i) {
    if (i == 1) category = argv[i]; else test_filters.push_back(argv[i]);
  }
  int failures = 0;
  size_t run_count = 0;
  for (auto& tc : tests) {
    std::string full(tc.full);
    std::string cat, name; auto pos = full.find('.');
    if (pos == std::string::npos) { name = full; } else { cat = full.substr(0,pos); name = full.substr(pos+1); }
    if (!category.empty() && cat != category) continue;
    if (!test_filters.empty() && std::find(test_filters.begin(), test_filters.end(), name) == test_filters.end()) continue;
    ++run_count;
    printf(COLOR_CYAN "[RUN] %s" COLOR_RESET "\n", tc.full);
    int res = tc.fn();
    if (res) { printf(COLOR_RED "[FAIL] %s" COLOR_RESET "\n", tc.full); ++failures; } else { printf(COLOR_GREEN "[PASS] %s" COLOR_RESET "\n", tc.full); }
  }
  printf("Ran %zu tests: %d failures\n", run_count, failures);
  return failures;
}
