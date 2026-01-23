/**
 * @file command_test.cpp
 * @brief Implementation of the 'test' command to run project tests
 *
 * Supports multiple test frameworks (GTest, Catch2, doctest, Boost.Test, Builtin)
 * with CARGO/Rust-style output formatting by default.
 * Supports workspace-level test execution across all projects.
 */

#include "cforge/log.hpp"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/test_output_formatter.hpp"
#include "core/test_runner.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
#include "core/workspace.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

/**
 * @brief Generate the builtin test framework header if it doesn't exist
 */
void ensure_test_framework_header(const std::filesystem::path &tests_dir) {
  namespace fs = std::filesystem;

  fs::path header_path = tests_dir / "test_framework.h";
  if (fs::exists(header_path)) {
    return;
  }

  cforge::logger::print_action("Generating", "test framework header: " + header_path.string());

  std::ofstream file(header_path);
  file << R"(/**
 * @file test_framework.h
 * @brief Lightweight, self-contained test framework for cforge projects
 *
 * Usage:
 *   #include "test_framework.h"
 *
 *   TEST(MyTest) {
 *       cf_assert(1 + 1 == 2);
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

/* ANSI Color Codes (disabled on Windows unless CF_FORCE_COLORS is defined) */
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

/* Assertion Macros */
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

/* Legacy alias */
#define test_assert(expr) cf_assert(expr)

#ifdef __cplusplus

namespace cf_test {

struct TestCase {
    std::string name;
    std::function<int()> func;
    TestCase(const std::string& n, std::function<int()> f) : name(n), func(f) {}
};

inline std::vector<TestCase>& get_test_registry() {
    static std::vector<TestCase> registry;
    return registry;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<int()> func) {
        get_test_registry().emplace_back(name, func);
    }
    TestRegistrar(const char* category, const char* name, std::function<int()> func) {
        std::string full_name = std::string(category) + "." + name;
        get_test_registry().emplace_back(full_name, func);
    }
};

inline int run_all_tests() {
    auto& tests = get_test_registry();
    int passed = 0, failed = 0;
    printf("\n");
    for (const auto& test : tests) {
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
    printf("\n==============================\n");
    if (failed == 0) {
        printf(CF_COLOR_GREEN "All %d tests passed!" CF_COLOR_RESET "\n", passed);
    } else {
        printf(CF_COLOR_RED "%d of %d tests failed" CF_COLOR_RESET "\n", failed, passed + failed);
    }
    printf("==============================\n");
    return failed > 0 ? 1 : 0;
}

} // namespace cf_test

inline int cf_run_tests() { return cf_test::run_all_tests(); }

/* TEST Macro Definitions (C++) - MSVC compatible */
#define CF_TEST_CONCAT_(a, b) a##b
#define CF_TEST_CONCAT(a, b) CF_TEST_CONCAT_(a, b)
#define CF_TEST_UNIQUE_NAME CF_TEST_CONCAT(cf_test_registrar_, __LINE__)

#define CF_TEST1(name)                                                          \
    int name();                                                                 \
    static cf_test::TestRegistrar CF_TEST_UNIQUE_NAME(#name, name);             \
    int name()

#define CF_TEST2(category, name)                                                \
    int category##_##name();                                                    \
    static cf_test::TestRegistrar CF_TEST_UNIQUE_NAME(#category, #name,         \
                                                       category##_##name);      \
    int category##_##name()

#define CF_EXPAND(x) x
#define CF_GET_MACRO(_1, _2, NAME, ...) NAME
#define CF_TEST_CHOOSER(...) CF_EXPAND(CF_GET_MACRO(__VA_ARGS__, CF_TEST2, CF_TEST1))
#define TEST(...) CF_EXPAND(CF_TEST_CHOOSER(__VA_ARGS__)(__VA_ARGS__))

#else /* Pure C implementation */

typedef int (*cf_test_func_t)(void);
typedef struct { const char* name; cf_test_func_t func; } cf_test_case_t;
#define CF_MAX_TESTS 256

#ifndef CF_TEST_IMPL
extern cf_test_case_t cf_test_registry[CF_MAX_TESTS];
extern int cf_test_count;
#else
cf_test_case_t cf_test_registry[CF_MAX_TESTS];
int cf_test_count = 0;
#endif

static inline void cf_register_test(const char* name, cf_test_func_t func) {
    if (cf_test_count < CF_MAX_TESTS) {
        cf_test_registry[cf_test_count].name = name;
        cf_test_registry[cf_test_count].func = func;
        cf_test_count++;
    }
}

static inline int cf_run_tests(void) {
    int passed = 0, failed = 0, i;
    printf("\n");
    for (i = 0; i < cf_test_count; i++) {
        printf("[RUN] %s\n", cf_test_registry[i].name);
        fflush(stdout);
        int result = cf_test_registry[i].func();
        if (result == 0) {
            printf(CF_COLOR_GREEN "[PASS] %s" CF_COLOR_RESET "\n", cf_test_registry[i].name);
            passed++;
        } else {
            printf(CF_COLOR_RED "[FAIL] %s" CF_COLOR_RESET "\n", cf_test_registry[i].name);
            failed++;
        }
        fflush(stdout);
    }
    printf("\n==============================\n");
    if (failed == 0) { printf(CF_COLOR_GREEN "All %d tests passed!" CF_COLOR_RESET "\n", passed); }
    else { printf(CF_COLOR_RED "%d of %d tests failed" CF_COLOR_RESET "\n", failed, passed + failed); }
    printf("==============================\n");
    return failed > 0 ? 1 : 0;
}

#define CF_C_TEST1(name) int name(void)
#define CF_C_TEST2(cat, name) int cat##_##name(void)
#define CF_C_EXPAND(x) x
#define CF_C_GET_MACRO(_1, _2, NAME, ...) NAME
#define CF_C_TEST_CHOOSER(...) CF_C_EXPAND(CF_C_GET_MACRO(__VA_ARGS__, CF_C_TEST2, CF_C_TEST1))
#define TEST(...) CF_C_EXPAND(CF_C_TEST_CHOOSER(__VA_ARGS__)(__VA_ARGS__))

#endif /* __cplusplus */

#endif /* CFORGE_TEST_FRAMEWORK_H */
)";
}

/**
 * @brief Parse test command options from context
 */
struct TestOptions {
  std::string build_config = "Debug";
  std::string filter;
  bool native_output = false;
  bool no_build = false;
  bool list_only = false;
  bool verbose = false;
  cforge_int_t jobs = 0;
  cforge_int_t timeout = 0;
};

TestOptions parse_test_options(const cforge_context_t *ctx) {
  TestOptions opts;

  // Get config from ctx->args.config
  if (ctx->args.config && std::strlen(ctx->args.config) > 0) {
    opts.build_config = ctx->args.config;
  }

  // Parse additional arguments
  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];

    if (arg == "--native") {
      opts.native_output = true;
    } else if (arg == "--no-build") {
      opts.no_build = true;
    } else if (arg == "--list") {
      opts.list_only = true;
    } else if (arg == "-v" || arg == "--verbose") {
      opts.verbose = true;
    } else if ((arg == "-f" || arg == "--filter") && i + 1 < ctx->args.arg_count) {
      opts.filter = ctx->args.args[++i];
    } else if ((arg == "-j" || arg == "--jobs") && i + 1 < ctx->args.arg_count) {
      opts.jobs = std::stoi(ctx->args.args[++i]);
    } else if (arg == "--timeout" && i + 1 < ctx->args.arg_count) {
      opts.timeout = std::stoi(ctx->args.args[++i]);
    } else if (arg == "-c" || arg == "--config") {
      // Skip - handled by ctx->args.config
      if (i + 1 < ctx->args.arg_count) ++i;
    } else if (arg[0] != '-' && opts.filter.empty()) {
      // Positional argument is a filter
      opts.filter = arg;
    }
  }

  // Check verbosity from context
  if (cforge::logger::get_verbosity() == cforge::log_verbosity::VERBOSITY_VERBOSE) {
    opts.verbose = true;
  }

  return opts;
}

/**
 * @brief Run tests for a single project
 *
 * @param project_dir Project directory
 * @param opts Test options
 * @param summary_out Output parameter for test summary
 * @param results_out Output parameter for test results
 * @return int Exit code (0 for success)
 */
cforge_int_t run_tests_for_project(const std::filesystem::path &project_dir,
                          const TestOptions &opts,
                          cforge::test_summary &summary_out,
                          std::vector<cforge::test_result> &results_out) {
  namespace fs = std::filesystem;

  // Load project configuration
  cforge::toml_reader cfg;
  if (!cfg.load((project_dir / CFORGE_FILE).string())) {
    cforge::logger::print_error("Failed to load " CFORGE_FILE " in " + project_dir.string());
    return 1;
  }

  // Get project name
  std::string project_name = cfg.get_string("project.name", "");
  if (project_name.empty()) {
    cforge::logger::print_error("project.name must be set in " CFORGE_FILE);
    return 1;
  }

  // Determine test directory
  std::string test_dir = cfg.get_string("test.directory", "tests");
  fs::path tests_dir = project_dir / test_dir;

  // Create test directory if it doesn't exist
  if (!fs::exists(tests_dir)) {
    cforge::logger::print_action("Creating", "test directory: " + tests_dir.string());
    fs::create_directories(tests_dir);
  }

  // Ensure builtin test framework header exists
  ensure_test_framework_header(tests_dir);

  // Create test runner
  cforge::test_runner runner(project_dir, cfg);
  if (!runner.load_config()) {
    cforge::logger::print_error("Failed to load test configuration for " + project_name);
    return 1;
  }

  // Discover test targets
  auto targets = runner.discover_targets();
  if (targets.empty()) {
    cforge::logger::print_verbose("No test targets found in " + project_name);
    return 0; // Not an error - project just has no tests
  }

  // List mode
  if (opts.list_only) {
    auto tests = runner.list_tests();
    cforge::test_output_formatter formatter(cforge::test_output_formatter::style::CARGO);
    formatter.print_test_list(tests);
    return 0;
  }

  // Run tests
  cforge::test_run_options run_opts;
  run_opts.build_config = opts.build_config;
  run_opts.filter = opts.filter;
  run_opts.native_output = opts.native_output;
  run_opts.no_build = opts.no_build;
  run_opts.list_only = opts.list_only;
  run_opts.verbose = opts.verbose;
  run_opts.jobs = opts.jobs;
  run_opts.timeout_override = opts.timeout;

  // Execute tests
  summary_out = runner.run_tests(run_opts);
  results_out = runner.get_results();

  // Return appropriate exit code
  if (summary_out.failed > 0 || summary_out.timeout > 0) {
    return 1;
  }

  return 0;
}

} // anonymous namespace

/**
 * @brief Handle the 'test' command
 *
 * Usage: cforge test [OPTIONS] [FILTER]
 *
 * OPTIONS:
 *   -c, --config <CONFIG>    Build configuration (Debug/Release)
 *   -j, --jobs <N>           Parallel test jobs
 *   -f, --filter <PATTERN>   Filter tests by pattern
 *   --list                   List tests without running
 *   --native                 Use framework's native output
 *   --no-build               Skip build step
 *   --timeout <SECONDS>      Override test timeout
 *
 * FILTER:
 *   Positional filter, e.g., "math::*" or "Math.Add*"
 *
 * When run from a workspace root, tests are executed for all projects
 * in the workspace in dependency order.
 */
cforge_int_t cforge_cmd_test(const cforge_context_t *ctx) {
  // Check for help flag first
  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-h" || arg == "--help") {
      cforge::command_registry::instance().print_command_help("test");
      return 0;
    }
  }

  namespace fs = std::filesystem;

  fs::path current_dir = fs::absolute(ctx->working_dir);

  // Parse options first
  TestOptions opts = parse_test_options(ctx);

  // Check if we're in a workspace
  auto [is_ws, workspace_dir] = cforge::is_in_workspace(current_dir);

  // If we're at the workspace root, run tests for all projects
  if (is_ws && current_dir == workspace_dir) {
    cforge::workspace ws;
    if (!ws.load(workspace_dir)) {
      cforge::logger::print_error("Failed to load workspace configuration");
      return 1;
    }

    cforge::logger::print_header("Running tests for workspace: " + ws.get_name());

    // Get projects in build order (respects dependencies)
    auto build_order = ws.get_build_order();
    auto projects = ws.get_projects();

    // Aggregate results across all projects
    cforge::test_summary total_summary{};
    std::vector<cforge::test_result> all_results;
    cforge_int_t projects_tested = 0;
    cforge_int_t projects_failed = 0;

    // Create formatter for output
    cforge::test_output_formatter formatter(
        opts.native_output ? cforge::test_output_formatter::style::NATIVE
                           : cforge::test_output_formatter::style::CARGO);

    for (const auto &project_name : build_order) {
      // Find the project
      auto it = std::find_if(projects.begin(), projects.end(),
                             [&project_name](const cforge::workspace_project &p) {
                               return p.name == project_name;
                             });

      if (it == projects.end()) {
        continue;
      }

      const auto &project = *it;

      // Check if project has cforge.toml
      if (!fs::exists(project.path / CFORGE_FILE)) {
        cforge::logger::print_verbose("Skipping " + project.name + " (no cforge.toml)");
        continue;
      }

      // Check if project has tests directory
      cforge::toml_reader proj_cfg;
      if (proj_cfg.load((project.path / CFORGE_FILE).string())) {
        std::string test_dir = proj_cfg.get_string("test.directory", "tests");
        if (!fs::exists(project.path / test_dir)) {
          cforge::logger::print_verbose("Skipping " + project.name + " (no tests directory)");
          continue;
        }
      }

      cforge::logger::print_action("Testing", project.name);

      cforge::test_summary project_summary{};
      std::vector<cforge::test_result> project_results;

      cforge_int_t result = run_tests_for_project(project.path, opts, project_summary, project_results);

      // Aggregate results
      total_summary.passed += project_summary.passed;
      total_summary.failed += project_summary.failed;
      total_summary.skipped += project_summary.skipped;
      total_summary.timeout += project_summary.timeout;
      total_summary.total_duration += project_summary.total_duration;

      // Copy results with project prefix
      for (auto &r : project_results) {
        r.name = project.name + "::" + r.name;
        all_results.push_back(r);
      }

      projects_tested++;
      if (result != 0) {
        projects_failed++;
      }
    }

    // Print aggregated results
    if (!opts.native_output && !opts.list_only) {
      cforge::logger::print_blank();
      cforge::logger::print_header("Workspace Test Summary");

      // Print header with actual test count
      formatter.print_run_start(static_cast<cforge_int_t>(all_results.size()));

      for (const auto &result : all_results) {
        formatter.print_test_result(result);
      }

      // Print failure details
      formatter.print_all_failures(all_results);

      // Print summary
      formatter.print_summary(total_summary);

      cforge::logger::print_blank();
      cforge::logger::print_plain("Projects tested: " + std::to_string(projects_tested) +
                                  ", Projects with failures: " + std::to_string(projects_failed));
    }

    // Return failure if any tests failed
    if (total_summary.failed > 0 || total_summary.timeout > 0) {
      return 1;
    }

    return 0;
  }

  // Single project mode - original behavior
  fs::path project_dir = current_dir;
  cforge::toml_reader cfg;
  if (!cfg.load((project_dir / CFORGE_FILE).string())) {
    cforge::logger::print_error("Failed to load " CFORGE_FILE);
    return 1;
  }

  // Get project name
  std::string project_name = cfg.get_string("project.name", "");
  if (project_name.empty()) {
    cforge::logger::print_error("project.name must be set in " CFORGE_FILE);
    return 1;
  }

  // Determine test directory
  std::string test_dir = cfg.get_string("test.directory", "tests");
  fs::path tests_dir = project_dir / test_dir;

  // Create test directory if it doesn't exist
  if (!fs::exists(tests_dir)) {
    cforge::logger::print_action("Creating", "test directory: " + tests_dir.string());
    fs::create_directories(tests_dir);
  }

  // Ensure builtin test framework header exists
  ensure_test_framework_header(tests_dir);

  // Create test runner
  cforge::test_runner runner(project_dir, cfg);
  if (!runner.load_config()) {
    cforge::logger::print_error("Failed to load test configuration");
    return 1;
  }

  // Create output formatter
  cforge::test_output_formatter formatter(
      opts.native_output ? cforge::test_output_formatter::style::NATIVE
                         : cforge::test_output_formatter::style::CARGO);

  // Discover test targets
  auto targets = runner.discover_targets();
  if (targets.empty()) {
    cforge::logger::print_warning("No test targets found");
    cforge::logger::print_status("Create test files in '" + test_dir + "/' directory");
    cforge::logger::print_status("Or add [[test.targets]] to cforge.toml");
    return 0;
  }

  // List mode
  if (opts.list_only) {
    auto tests = runner.list_tests();
    formatter.print_test_list(tests);
    return 0;
  }

  // Run tests
  cforge::test_run_options run_opts;
  run_opts.build_config = opts.build_config;
  run_opts.filter = opts.filter;
  run_opts.native_output = opts.native_output;
  run_opts.no_build = opts.no_build;
  run_opts.list_only = opts.list_only;
  run_opts.verbose = opts.verbose;
  run_opts.jobs = opts.jobs;
  run_opts.timeout_override = opts.timeout;

  // Execute tests
  cforge::test_summary summary = runner.run_tests(run_opts);
  const auto &results = runner.get_results();

  // Print results (unless native output, which prints as it runs)
  if (!opts.native_output) {
    // Print header with actual test count
    formatter.print_run_start(static_cast<cforge_int_t>(results.size()));

    for (const auto &result : results) {
      formatter.print_test_result(result);
    }

    // Print failure details
    formatter.print_all_failures(results);

    // Print summary
    formatter.print_summary(summary);
  }

  // Return appropriate exit code
  if (summary.failed > 0 || summary.timeout > 0) {
    return 1;
  }

  return 0;
}
