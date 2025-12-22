#include "test_framework.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>

int Lockfile_LoadMissing();
int Lockfile_LoadValid();
int Lockfile_SaveAndReload();
int Lockfile_RemoveDependency();
int Lockfile_Clear();
int Lockfile_Exists();
int Lockfile_QuotedValues();
int Lockfile_SkipsComments();
int Lockfile_MultipleDependencies();
int Lockfile_GetNonexistent();
int Lockfile_VcpkgWithTriplet();
int Math_Add();
int Math_Subtract();
int Divide();
int Version_ParseSimple();
int Version_ParseWithV();
int Version_ParseWithPrerelease();
int Version_ParseWithBuild();
int Version_ParseFull();
int Version_ParseTwoParts();
int Version_ParseOnePart();
int Version_ParseInvalid();
int Version_CompareMajor();
int Version_CompareMinor();
int Version_ComparePatch();
int Version_CompareEqual();
int Version_ComparePrerelease();
int Constraint_Exact();
int Constraint_GreaterThan();
int Constraint_GreaterThanOrEqual();
int Constraint_LessThan();
int Constraint_LessThanOrEqual();
int Constraint_Range();
int Constraint_Caret();
int Constraint_CaretZero();
int Constraint_Tilde();
int Constraint_Any();
int Version_FindBest();
int Version_FindBestRange();
int Version_FindBestNone();

struct test_entry { const char* full; int (*fn)(); };
static test_entry tests[] = {
  {"Lockfile.LoadMissing", Lockfile_LoadMissing},
  {"Lockfile.LoadValid", Lockfile_LoadValid},
  {"Lockfile.SaveAndReload", Lockfile_SaveAndReload},
  {"Lockfile.RemoveDependency", Lockfile_RemoveDependency},
  {"Lockfile.Clear", Lockfile_Clear},
  {"Lockfile.Exists", Lockfile_Exists},
  {"Lockfile.QuotedValues", Lockfile_QuotedValues},
  {"Lockfile.SkipsComments", Lockfile_SkipsComments},
  {"Lockfile.MultipleDependencies", Lockfile_MultipleDependencies},
  {"Lockfile.GetNonexistent", Lockfile_GetNonexistent},
  {"Lockfile.VcpkgWithTriplet", Lockfile_VcpkgWithTriplet},
  {"Math.Add", Math_Add},
  {"Math.Subtract", Math_Subtract},
  {"Divide", Divide},
  {"Version.ParseSimple", Version_ParseSimple},
  {"Version.ParseWithV", Version_ParseWithV},
  {"Version.ParseWithPrerelease", Version_ParseWithPrerelease},
  {"Version.ParseWithBuild", Version_ParseWithBuild},
  {"Version.ParseFull", Version_ParseFull},
  {"Version.ParseTwoParts", Version_ParseTwoParts},
  {"Version.ParseOnePart", Version_ParseOnePart},
  {"Version.ParseInvalid", Version_ParseInvalid},
  {"Version.CompareMajor", Version_CompareMajor},
  {"Version.CompareMinor", Version_CompareMinor},
  {"Version.ComparePatch", Version_ComparePatch},
  {"Version.CompareEqual", Version_CompareEqual},
  {"Version.ComparePrerelease", Version_ComparePrerelease},
  {"Constraint.Exact", Constraint_Exact},
  {"Constraint.GreaterThan", Constraint_GreaterThan},
  {"Constraint.GreaterThanOrEqual", Constraint_GreaterThanOrEqual},
  {"Constraint.LessThan", Constraint_LessThan},
  {"Constraint.LessThanOrEqual", Constraint_LessThanOrEqual},
  {"Constraint.Range", Constraint_Range},
  {"Constraint.Caret", Constraint_Caret},
  {"Constraint.CaretZero", Constraint_CaretZero},
  {"Constraint.Tilde", Constraint_Tilde},
  {"Constraint.Any", Constraint_Any},
  {"Version.FindBest", Version_FindBest},
  {"Version.FindBestRange", Version_FindBestRange},
  {"Version.FindBestNone", Version_FindBestNone},
};

int main(int argc, char** argv) {
  std::string category;
  std::vector<std::string> test_filters;
  // Positional args: first is category (optional), rest are test names
  for (int i = 1; i < argc; ++i) {
    if (i == 1) category = argv[i]; else test_filters.emplace_back(argv[i]);
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
