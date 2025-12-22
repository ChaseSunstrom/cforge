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
class builtin_test_adapter : public i_test_framework_adapter {
public:
  test_framework get_framework() const override { return test_framework::Builtin; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const test_config::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<test_result> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief Google Test framework adapter
 */
class gtest_adapter : public i_test_framework_adapter {
public:
  test_framework get_framework() const override { return test_framework::GTest; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const test_config::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<test_result> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief Catch2 framework adapter
 */
class catch2_adapter : public i_test_framework_adapter {
public:
  test_framework get_framework() const override { return test_framework::Catch2; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const test_config::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<test_result> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief doctest framework adapter
 */
class doctest_adapter : public i_test_framework_adapter {
public:
  test_framework get_framework() const override { return test_framework::Doctest; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const test_config::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<test_result> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

/**
 * @brief Boost.Test framework adapter
 */
class bost_test_adapter : public i_test_framework_adapter {
public:
  test_framework get_framework() const override { return test_framework::BoostTest; }

  bool detect_from_source(const std::string &source_content) const override;
  std::string generate_cmake_setup(const test_config::FrameworkConfig &config) const override;
  std::string get_cmake_target() const override;
  std::vector<test_result> parse_output(const std::string &output) const override;
  std::vector<std::string> get_list_args() const override;
  std::vector<std::string> get_filter_args(const std::string &filter) const override;
  std::vector<std::string> get_verbose_args() const override;
  std::vector<std::string> parse_test_list(const std::string &output) const override;
};

} // namespace cforge
