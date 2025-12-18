/**
 * @file test_adapters.hpp
 * @brief Framework-specific test adapters for cforge testing system
 */

#pragma once

#include "core/test_framework.hpp"
#include <regex>

namespace cforge {

/**
 * @brief Built-in test framework adapter (cforge's TEST() macro)
 */
class BuiltinTestAdapter : public ITestFrameworkAdapter {
public:
  TestFramework get_framework() const override { return TestFramework::Builtin; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const TestConfig::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<TestResult> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief Google Test framework adapter
 */
class GTestAdapter : public ITestFrameworkAdapter {
public:
  TestFramework get_framework() const override { return TestFramework::GTest; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const TestConfig::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<TestResult> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief Catch2 framework adapter
 */
class Catch2Adapter : public ITestFrameworkAdapter {
public:
  TestFramework get_framework() const override { return TestFramework::Catch2; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const TestConfig::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<TestResult> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief doctest framework adapter
 */
class DoctestAdapter : public ITestFrameworkAdapter {
public:
  TestFramework get_framework() const override { return TestFramework::Doctest; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const TestConfig::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<TestResult> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief Boost.Test framework adapter
 */
class BoostTestAdapter : public ITestFrameworkAdapter {
public:
  TestFramework get_framework() const override { return TestFramework::BoostTest; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const TestConfig::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<TestResult> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

} // namespace cforge
