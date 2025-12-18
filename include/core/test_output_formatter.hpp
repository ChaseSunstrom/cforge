/**
 * @file test_output_formatter.hpp
 * @brief Cargo/Rust-style output formatting for test results
 */

#pragma once

#include "core/test_framework.hpp"
#include <string>

namespace cforge {

/**
 * @brief Output formatter for test results
 *
 * Provides Cargo/Rust-style formatted output for tests with colors,
 * source context, and detailed failure information.
 */
class TestOutputFormatter {
public:
  /**
   * @brief Output style options
   */
  enum class Style {
    Cargo,  // Rust/Cargo style (default)
    Native  // Pass through framework native output
  };

  /**
   * @brief Construct formatter with specified style
   * @param style Output style to use
   */
  explicit TestOutputFormatter(Style style = Style::Cargo);

  /**
   * @brief Set output style
   */
  void set_style(Style style) { m_style = style; }

  /**
   * @brief Get current output style
   */
  Style get_style() const { return m_style; }

  // === Formatting Methods (return strings) ===

  /**
   * @brief Format test run start header
   * @param total_tests Total number of tests to run
   * @return Formatted string
   */
  std::string format_run_start(int total_tests);

  /**
   * @brief Format test build start
   * @param target_name Name of test target being built
   * @param framework Framework being used
   * @return Formatted string
   */
  std::string format_build_start(const std::string &target_name,
                                  TestFramework framework);

  /**
   * @brief Format test execution start
   * @param executable_path Path to test executable
   * @return Formatted string
   */
  std::string format_execution_start(const std::string &executable_path);

  /**
   * @brief Format individual test result
   * @param result The test result
   * @return Formatted string
   */
  std::string format_test_result(const TestResult &result);

  /**
   * @brief Format detailed failure information
   * @param result The failed test result
   * @return Formatted string with source context
   */
  std::string format_failure_details(const TestResult &result);

  /**
   * @brief Format final summary
   * @param summary Test run summary
   * @return Formatted string
   */
  std::string format_summary(const TestSummary &summary);

  /**
   * @brief Format test list
   * @param tests List of test names
   * @return Formatted string
   */
  std::string format_test_list(const std::vector<std::string> &tests);

  // === Printing Methods (output to console with colors) ===

  /**
   * @brief Print test run start header
   */
  void print_run_start(int total_tests);

  /**
   * @brief Print test build start
   */
  void print_build_start(const std::string &target_name,
                         TestFramework framework);

  /**
   * @brief Print test execution start
   */
  void print_execution_start(const std::string &executable_path);

  /**
   * @brief Print individual test result
   */
  void print_test_result(const TestResult &result);

  /**
   * @brief Print detailed failure information
   */
  void print_failure_details(const TestResult &result);

  /**
   * @brief Print all failure details for failed tests
   */
  void print_all_failures(const std::vector<TestResult> &results);

  /**
   * @brief Print final summary
   */
  void print_summary(const TestSummary &summary);

  /**
   * @brief Print test list
   */
  void print_test_list(const std::vector<std::string> &tests);

  /**
   * @brief Print native framework output (passthrough)
   */
  void print_native_output(const std::string &output);

private:
  Style m_style;

  /**
   * @brief Read source line from file for context
   * @param file_path Path to source file
   * @param line_number Line number to read (1-based)
   * @return Source line content, or empty if unavailable
   */
  std::string read_source_line(const std::string &file_path, int line_number);

  /**
   * @brief Shorten path for display
   * @param path Full file path
   * @param max_length Maximum display length
   * @return Shortened path
   */
  std::string shorten_path(const std::string &path, size_t max_length = 50);

  /**
   * @brief Format duration for display
   * @param duration Duration in milliseconds
   * @return Formatted string (e.g., "1.23s" or "45ms")
   */
  std::string format_duration(std::chrono::milliseconds duration);

  /**
   * @brief Group tests by suite for display
   * @param tests Flat list of test names
   * @return Map of suite -> test names
   */
  std::map<std::string, std::vector<std::string>>
  group_tests_by_suite(const std::vector<std::string> &tests);
};

} // namespace cforge
