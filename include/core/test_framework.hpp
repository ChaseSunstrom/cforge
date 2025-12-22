/**
 * @file test_framework.hpp
 * @brief Core data structures and interfaces for cforge's testing system
 */

#pragma once

#include "types.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Supported test frameworks
 */
enum class test_framework {
  Auto,      // Auto-detect from source
  Builtin,   // cforge's simple TEST() macro
  GTest,     // Google Test
  Catch2,    // Catch2 v3
  Doctest,   // doctest
  BoostTest  // Boost.Test
};

/**
 * @brief Test execution status
 */
enum class test_status {
  PENDING,   // Not yet run
  RUNNING,   // Currently executing
  PASSED,    // Test passed
  FAILED,    // Test failed
  SKIPPED,   // Test was skipped
  TIMEOUT    // Test timed out
};

/**
 * @brief Individual test result
 */
struct test_result {
  std::string name;           // Full test name (e.g., "Math.Addition")
  std::string suite;          // Test suite/group name (e.g., "Math")
  std::string test_name;      // Individual test name (e.g., "Addition")
  test_status status = test_status::PENDING;
  std::chrono::milliseconds duration{0};

  // Source location (for failures)
  std::string file_path;
  int line_number = 0;
  int column_number = 0;

  // Failure details
  std::string failure_message;
  std::string assertion_expr;
  std::string expected_value;
  std::string actual_value;

  // Output capture
  std::vector<std::string> stdout_lines;
  std::vector<std::string> stderr_lines;
  std::vector<std::string> notes;
};

/**
 * @brief Test target configuration (from cforge.toml)
 */
struct test_target {
  std::string name;
  std::vector<std::string> sources;       // Glob patterns
  test_framework framework = test_framework::Auto;
  int timeout_seconds = 300;
  std::vector<std::string> dependencies;  // Link dependencies
  std::vector<std::string> defines;
  std::vector<std::string> includes;
  bool enabled = true;

  // Computed paths
  std::filesystem::path executable_path;
  std::vector<std::filesystem::path> source_files;
};

/**
 * @brief Global test configuration (from [test] section)
 */
struct test_config {
  std::filesystem::path directory{"tests"};
  test_framework default_framework = test_framework::Auto;
  int default_timeout = 300;
  int jobs = 0;  // 0 = auto-detect
  bool auto_link_project = true;
  bool cargo_style_output = true;
  std::string discovery_mode{"both"};  // "auto", "explicit", "both"

  // Framework-specific settings
  struct FrameworkConfig {
    bool fetch = true;
    std::string version;
    std::map<std::string, std::string> options;
  };
  std::map<test_framework, FrameworkConfig> framework_configs;

  // Discovered/explicit targets
  std::vector<test_target> targets;
};

/**
 * @brief Test run summary statistics
 */
struct test_summary {
  int total = 0;
  int passed = 0;
  int failed = 0;
  int skipped = 0;
  int timeout = 0;
  std::chrono::milliseconds total_duration{0};

  // FAILED test names for summary
  std::vector<std::string> failed_tests;
};

/**
 * @brief Convert TestFramework enum to string
 */
inline std::string test_framework_to_string(test_framework fw) {
  switch (fw) {
  case test_framework::Auto:
    return "auto";
  case test_framework::Builtin:
    return "builtin";
  case test_framework::GTest:
    return "gtest";
  case test_framework::Catch2:
    return "catch2";
  case test_framework::Doctest:
    return "doctest";
  case test_framework::BoostTest:
    return "boost";
  default:
    return "unknown";
  }
}

/**
 * @brief Convert string to TestFramework enum
 */
inline test_framework string_to_test_framework(const std::string &str) {
  if (str == "auto")
    return test_framework::Auto;
  if (str == "builtin" || str == "cforge")
    return test_framework::Builtin;
  if (str == "gtest" || str == "googletest" || str == "google")
    return test_framework::GTest;
  if (str == "catch2" || str == "catch")
    return test_framework::Catch2;
  if (str == "doctest")
    return test_framework::Doctest;
  if (str == "boost" || str == "boost_test" || str == "boosttest")
    return test_framework::BoostTest;
  return test_framework::Auto;
}

/**
 * @brief Convert TestStatus enum to string
 */
inline std::string test_status_to_string(test_status status) {
  switch (status) {
  case test_status::PENDING:
    return "pending";
  case test_status::RUNNING:
    return "running";
  case test_status::PASSED:
    return "ok";
  case test_status::FAILED:
    return "FAILED";
  case test_status::SKIPPED:
    return "skipped";
  case test_status::TIMEOUT:
    return "TIMEOUT";
  default:
    return "unknown";
  }
}

/**
 * @brief Abstract interface for framework-specific operations
 */
class i_test_framework_adapter {
public:
  virtual ~i_test_framework_adapter() = default;

  /**
   * @brief Get the framework type this adapter handles
   */
  virtual test_framework get_framework() const = 0;

  /**
   * @brief Detect if source file uses this framework
   * @param source_content The content of a source file
   * @return true if this framework is detected
   */
  virtual bool detect_from_source(const std::string &source_content) const = 0;

  /**
   * @brief Generate CMake code to fetch/configure the framework
   * @param config Framework-specific configuration
   * @return CMake code as string
   */
  virtual std::string
  generate_cmake_setup(const test_config::FrameworkConfig &config) const = 0;

  /**
   * @brief Get the CMake target name to link against
   * @return Target name (e.g., "GTest::gtest_main")
   */
  virtual std::string get_cmake_target() const = 0;

  /**
   * @brief Parse framework output into TestResults
   * @param output Raw output from test execution
   * @return Vector of test results
   */
  virtual std::vector<test_result>
  parse_output(const std::string &output) const = 0;

  /**
   * @brief Get command-line args to list available tests
   * @return Vector of arguments
   */
  virtual std::vector<std::string> get_list_args() const = 0;

  /**
   * @brief Get command-line args for filtering tests
   * @param filter The filter pattern
   * @return Vector of arguments
   */
  virtual std::vector<std::string>
  get_filter_args(const std::string &filter) const = 0;

  /**
   * @brief Get command-line args for native verbose output
   * @return Vector of arguments
   */
  virtual std::vector<std::string> get_verbose_args() const = 0;

  /**
   * @brief Parse test list output to extract test names
   * @param output Output from running with list args
   * @return Vector of test names
   */
  virtual std::vector<std::string>
  parse_test_list(const std::string &output) const = 0;
};

} // namespace cforge
