/**
 * @file test_runner.hpp
 * @brief Test runner class for cforge's testing system
 */

#pragma once

#include "core/test_framework.hpp"
#include "core/toml_reader.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>

namespace cforge {

/**
 * @brief Test execution options
 */
struct test_run_options {
  std::string build_config{"Debug"};
  std::string filter;
  bool native_output = false;
  bool no_build = false;
  bool list_only = false;
  bool verbose = false;
  cforge_int_t jobs = 0;
  cforge_int_t timeout_override = 0;
};

/**
 * @brief Test runner - orchestrates test discovery, building, and execution
 */
class test_runner {
public:
  /**
   * @brief Construct a test runner
   * @param project_dir Path to the project root
   * @param config Project configuration
   */
  test_runner(const std::filesystem::path &project_dir,
             const toml_reader &config);

  ~test_runner();

  /**
   * @brief Load test configuration from cforge.toml
   * @return true if configuration was loaded successfully
   */
  bool load_config();

  /**
   * @brief Get the loaded test configuration
   */
  const test_config &get_config() const { return m_test_config; }

  /**
   * @brief Discover test targets (auto + explicit)
   * @return Vector of discovered test targets
   */
  std::vector<test_target> discover_targets();

  /**
   * @brief Detect framework from source file content
   * @param source_file Path to the source file
   * @return Detected framework type
   */
  test_framework detect_framework(const std::filesystem::path &source_file);

  /**
   * @brief Build test executables
   * @param config Build configuration (Debug/Release)
   * @param verbose Show verbose output
   * @return true if build succeeded
   */
  bool build_tests(const std::string &config, bool verbose);

  /**
   * @brief Run tests with given options
   * @param options Test execution options
   * @return Test summary with results
   */
  test_summary run_tests(const test_run_options &options);

  /**
   * @brief List available tests without running them
   * @return Vector of test names
   */
  std::vector<std::string> list_tests();

  /**
   * @brief Get all test results from the last run
   */
  const std::vector<test_result> &get_results() const { return m_results; }

  /**
   * @brief Get error message if any operation failed
   */
  const std::string &get_error() const { return m_error; }

private:
  std::filesystem::path m_project_dir;
  const toml_reader &m_project_config;
  test_config m_test_config;
  std::vector<test_result> m_results;
  std::string m_error;

  // Framework adapters (lazily created)
  std::map<test_framework, std::unique_ptr<i_test_framework_adapter>> m_adapters;

  /**
   * @brief Get or create adapter for a framework
   */
  i_test_framework_adapter *get_adapter(test_framework fw);

  /**
   * @brief Generate CMakeLists.txt for test targets
   * @param target The test target to generate for
   * @return true if generation succeeded
   */
  bool generate_test_cmake(const test_target &target);

  /**
   * @brief Configure CMake for tests
   * @param target The test target
   * @param build_config Build configuration
   * @return true if configuration succeeded
   */
  bool configure_cmake(const test_target &target, const std::string &build_config);

  /**
   * @brief Build a specific test target
   * @param target The test target
   * @param build_config Build configuration
   * @return true if build succeeded
   */
  bool build_target(const test_target &target, const std::string &build_config);

  /**
   * @brief Find test executable for a target
   * @param target The test target
   * @param build_config Build configuration
   * @return Path to executable, or empty if not found
   */
  std::filesystem::path find_test_executable(const test_target &target,
                                             const std::string &build_config);

  /**
   * @brief Run a single test target
   * @param target The test target
   * @param options Execution options
   * @return Vector of test results
   */
  std::vector<test_result> run_target(const test_target &target,
                                     const test_run_options &options);

  /**
   * @brief Auto-discover tests from source files
   * @return Vector of discovered targets
   */
  std::vector<test_target> auto_discover_targets();

  /**
   * @brief Load explicitly defined test targets from config
   * @return Vector of explicit targets
   */
  std::vector<test_target> load_explicit_targets();

  /**
   * @brief Check if project should be auto-linked to tests
   */
  bool should_auto_link_project() const;

  /**
   * @brief Get project library name for linking
   */
  std::string get_project_link_target() const;

  /**
   * @brief Expand glob patterns to actual files
   * @param patterns Glob patterns
   * @param base_dir Base directory for patterns
   * @return Vector of matching file paths
   */
  std::vector<std::filesystem::path>
  expand_globs(const std::vector<std::string> &patterns,
               const std::filesystem::path &base_dir);

  /**
   * @brief Load framework-specific configuration from TOML
   * @param fw Framework type
   * @param section TOML section name (e.g., "test.gtest")
   */
  void load_framework_config(test_framework fw, const std::string &section);
};

/**
 * @brief Factory function to create framework adapters
 */
std::unique_ptr<i_test_framework_adapter> create_adapter(test_framework fw);

} // namespace cforge
