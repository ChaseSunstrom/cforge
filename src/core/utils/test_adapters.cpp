/**
 * @file test_adapters.cpp
 * @brief Implementation of framework-specific test adapters
 */

#include "core/test_adapters.hpp"
#include <algorithm>
#include <regex>
#include <sstream>

namespace cforge {

// ============================================================================
// Built-in Test Framework Adapter
// ============================================================================

bool builtin_test_adapter::detect_from_source(const std::string &source_content) const {
  // Look for cforge's test_framework.h or TEST() macro with int return
  std::regex include_regex(R"(#include\s*[<"]test_framework\.h[">])");
  std::regex test_macro_regex(R"(\bTEST\s*\(\s*\w+\s*(?:,\s*\w+\s*)?\)\s*\{)");

  return std::regex_search(source_content, include_regex) ||
         (std::regex_search(source_content, test_macro_regex) &&
          source_content.find("gtest") == std::string::npos &&
          source_content.find("catch") == std::string::npos &&
          source_content.find("doctest") == std::string::npos);
}

std::string builtin_test_adapter::generate_cmake_setup(
    const test_config::FrameworkConfig &) const {
  // Built-in framework doesn't need any CMake setup
  return "";
}

std::string builtin_test_adapter::get_cmake_target() const {
  return ""; // No additional target needed
}

std::vector<test_result> builtin_test_adapter::parse_output(
    const std::string &output) const {
  std::vector<test_result> results;

  // Parse: [RUN] TestName
  //        [PASS] TestName or [FAIL] TestName
  std::regex run_regex(R"(\[RUN\]\s+(\S+))");
  std::regex pass_regex(R"(\[PASS\]\s+(\S+))");
  std::regex fail_regex(R"(\[FAIL\]\s+(\S+))");
  std::regex assert_regex(R"(Assertion failed:\s*(.+)\s*at\s*([^:]+):(\d+))");

  std::istringstream stream(output);
  std::string line;
  std::map<std::string, test_result> test_map;
  std::string current_test;

  while (std::getline(stream, line)) {
    std::smatch match;

    if (std::regex_search(line, match, run_regex)) {
      current_test = match[1].str();
      test_result result;
      result.name = current_test;
      result.status = test_status::RUNNING;

      // Parse suite.test format
      auto dot = current_test.find('.');
      if (dot != std::string::npos) {
        result.suite = current_test.substr(0, dot);
        result.test_name = current_test.substr(dot + 1);
      } else {
        result.test_name = current_test;
      }
      test_map[current_test] = result;
    } else if (std::regex_search(line, match, pass_regex)) {
      std::string name = match[1].str();
      if (test_map.count(name)) {
        test_map[name].status = test_status::PASSED;
      }
    } else if (std::regex_search(line, match, fail_regex)) {
      std::string name = match[1].str();
      if (test_map.count(name)) {
        test_map[name].status = test_status::FAILED;
      }
    } else if (std::regex_search(line, match, assert_regex)) {
      if (!current_test.empty() && test_map.count(current_test)) {
        test_map[current_test].failure_message = match[1].str();
        test_map[current_test].file_path = match[2].str();
        test_map[current_test].line_number = std::stoi(match[3].str());
      }
    }
  }

  for (auto &[name, result] : test_map) {
    results.push_back(std::move(result));
  }

  return results;
}

std::vector<std::string> builtin_test_adapter::get_list_args() const {
  return {"--list"};
}

std::vector<std::string> builtin_test_adapter::get_filter_args(
    const std::string &filter) const {
  return {filter};
}

std::vector<std::string> builtin_test_adapter::get_verbose_args() const {
  return {};
}

std::vector<std::string> builtin_test_adapter::parse_test_list(
    const std::string &output) const {
  std::vector<std::string> tests;
  std::istringstream stream(output);
  std::string line;

  while (std::getline(stream, line)) {
    // Trim whitespace
    line.erase(0, line.find_first_not_of(" \t"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (!line.empty() && line[0] != '#') {
      tests.push_back(line);
    }
  }

  return tests;
}

// ============================================================================
// Google Test Adapter
// ============================================================================

bool gtest_adapter::detect_from_source(const std::string &source_content) const {
  // Look for gtest includes or macros
  std::regex include_regex(R"(#include\s*[<"]gtest/gtest\.h[">])");
  std::regex test_macro_regex(R"(\bTEST(_F|_P)?\s*\()");
  std::regex gmock_regex(R"(#include\s*[<"]gmock/gmock\.h[">])");

  return std::regex_search(source_content, include_regex) ||
         std::regex_search(source_content, gmock_regex) ||
         (std::regex_search(source_content, test_macro_regex) &&
          source_content.find("gtest") != std::string::npos);
}

std::string gtest_adapter::generate_cmake_setup(
    const test_config::FrameworkConfig &config) const {
  std::ostringstream ss;

  if (config.fetch) {
    std::string version = config.version.empty() ? "v1.14.0" : config.version;
    if (version[0] != 'v') version = "v" + version;

    ss << "# Fetch Google Test\n"
       << "include(FetchContent)\n"
       << "FetchContent_Declare(\n"
       << "  googletest\n"
       << "  GIT_REPOSITORY https://github.com/google/googletest.git\n"
       << "  GIT_TAG " << version << "\n"
       << ")\n"
       << "# Prevent overriding parent project's compiler/linker settings on Windows\n"
       << "set(gtest_force_shared_crt ON CACHE BOOL \"\" FORCE)\n";

    // Check for GMock option
    auto it = config.options.find("BUILD_GMOCK");
    if (it != config.options.end()) {
      ss << "set(BUILD_GMOCK " << it->second << " CACHE BOOL \"\" FORCE)\n";
    }

    ss << "FetchContent_MakeAvailable(googletest)\n\n";
  } else {
    ss << "# Find Google Test (must be installed)\n"
       << "find_package(GTest REQUIRED)\n\n";
  }

  return ss.str();
}

std::string gtest_adapter::get_cmake_target() const {
  return "GTest::gtest_main";
}

std::vector<test_result> gtest_adapter::parse_output(
    const std::string &output) const {
  std::vector<test_result> results;

  // Parse GTest output format
  // [ RUN      ] Suite.TestName
  // [       OK ] Suite.TestName (0 ms)
  // [  FAILED  ] Suite.TestName (0 ms)
  std::regex run_regex(R"(\[\s*RUN\s*\]\s+(\S+))");
  std::regex ok_regex(R"(\[\s*OK\s*\]\s+(\S+)\s*\((\d+)\s*ms\))");
  std::regex fail_regex(R"(\[\s*FAILED\s*\]\s+(\S+)\s*\((\d+)\s*ms\))");
  std::regex skip_regex(R"(\[\s*SKIPPED\s*\]\s+(\S+))");

  // Failure details: file:line: Failure
  std::regex failure_loc_regex(R"(([^:]+):(\d+):\s*Failure)");
  std::regex expected_regex(R"(Expected:\s*(.+))");
  std::regex actual_regex(R"((?:Actual|Which is):\s*(.+))");

  std::istringstream stream(output);
  std::string line;
  std::map<std::string, test_result> test_map;
  std::string current_test;

  while (std::getline(stream, line)) {
    std::smatch match;

    if (std::regex_search(line, match, run_regex)) {
      current_test = match[1].str();
      test_result result;
      result.name = current_test;
      result.status = test_status::RUNNING;

      auto dot = current_test.find('.');
      if (dot != std::string::npos) {
        result.suite = current_test.substr(0, dot);
        result.test_name = current_test.substr(dot + 1);
      } else {
        result.test_name = current_test;
      }
      test_map[current_test] = result;
    } else if (std::regex_search(line, match, ok_regex)) {
      std::string name = match[1].str();
      if (test_map.count(name)) {
        test_map[name].status = test_status::PASSED;
        test_map[name].duration = std::chrono::milliseconds(std::stoi(match[2].str()));
      }
    } else if (std::regex_search(line, match, fail_regex)) {
      std::string name = match[1].str();
      if (test_map.count(name)) {
        test_map[name].status = test_status::FAILED;
        test_map[name].duration = std::chrono::milliseconds(std::stoi(match[2].str()));
      }
    } else if (std::regex_search(line, match, skip_regex)) {
      std::string name = match[1].str();
      if (test_map.count(name)) {
        test_map[name].status = test_status::SKIPPED;
      }
    } else if (std::regex_search(line, match, failure_loc_regex)) {
      if (!current_test.empty() && test_map.count(current_test)) {
        test_map[current_test].file_path = match[1].str();
        test_map[current_test].line_number = std::stoi(match[2].str());
      }
    } else if (std::regex_search(line, match, expected_regex)) {
      if (!current_test.empty() && test_map.count(current_test)) {
        test_map[current_test].expected_value = match[1].str();
      }
    } else if (std::regex_search(line, match, actual_regex)) {
      if (!current_test.empty() && test_map.count(current_test)) {
        test_map[current_test].actual_value = match[1].str();
      }
    }
  }

  for (auto &[name, result] : test_map) {
    results.push_back(std::move(result));
  }

  return results;
}

std::vector<std::string> gtest_adapter::get_list_args() const {
  return {"--gtest_list_tests"};
}

std::vector<std::string> gtest_adapter::get_filter_args(
    const std::string &filter) const {
  return {"--gtest_filter=" + filter};
}

std::vector<std::string> gtest_adapter::get_verbose_args() const {
  return {"--gtest_print_time=1"};
}

std::vector<std::string> gtest_adapter::parse_test_list(
    const std::string &output) const {
  std::vector<std::string> tests;
  std::istringstream stream(output);
  std::string line;
  std::string current_suite;

  while (std::getline(stream, line)) {
    // Suite names end with '.'
    // Test names start with spaces
    if (!line.empty() && line.back() == '.') {
      // This is a suite name
      current_suite = line.substr(0, line.length() - 1);
    } else if (!line.empty() && (line[0] == ' ' || line[0] == '\t')) {
      // This is a test name
      line.erase(0, line.find_first_not_of(" \t"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);
      // Remove parameter info if present
      auto paren = line.find("  #");
      if (paren != std::string::npos) {
        line = line.substr(0, paren);
      }
      if (!line.empty() && !current_suite.empty()) {
        tests.push_back(current_suite + "." + line);
      }
    }
  }

  return tests;
}

// ============================================================================
// Catch2 Adapter
// ============================================================================

bool catch2_adapter::detect_from_source(const std::string &source_content) const {
  std::regex include_regex(R"(#include\s*[<"]catch2?/catch[^>]*[">])");
  std::regex test_case_regex(R"(\bTEST_CASE\s*\()");
  std::regex scenario_regex(R"(\bSCENARIO\s*\()");

  return std::regex_search(source_content, include_regex) ||
         std::regex_search(source_content, test_case_regex) ||
         std::regex_search(source_content, scenario_regex);
}

std::string catch2_adapter::generate_cmake_setup(
    const test_config::FrameworkConfig &config) const {
  std::ostringstream ss;

  if (config.fetch) {
    std::string version = config.version.empty() ? "v3.5.0" : config.version;
    if (version[0] != 'v') version = "v" + version;

    ss << "# Fetch Catch2\n"
       << "include(FetchContent)\n"
       << "FetchContent_Declare(\n"
       << "  Catch2\n"
       << "  GIT_REPOSITORY https://github.com/catchorg/Catch2.git\n"
       << "  GIT_TAG " << version << "\n"
       << ")\n"
       << "FetchContent_MakeAvailable(Catch2)\n"
       << "list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)\n\n";
  } else {
    ss << "# Find Catch2 (must be installed)\n"
       << "find_package(Catch2 3 REQUIRED)\n\n";
  }

  return ss.str();
}

std::string catch2_adapter::get_cmake_target() const {
  return "Catch2::Catch2WithMain";
}

std::vector<test_result> catch2_adapter::parse_output(
    const std::string &output) const {
  std::vector<test_result> results;

  // Parse Catch2 console output
  // Test case start: test_name
  // file:line: PASSED/FAILED:
  std::regex test_case_regex(R"(^(\S.+?)$)");  // Test case names
  std::regex passed_regex(R"(All tests passed)");
  std::regex failed_regex(R"((\d+) test cases? failed)");
  std::regex assertion_regex(R"(([^:]+):(\d+):\s*(PASSED|FAILED):)");
  std::regex require_regex(R"((REQUIRE|CHECK|REQUIRE_FALSE|CHECK_FALSE)\s*\(\s*(.+)\s*\))");

  // Catch2 verbose output: file:line: FAILED:
  std::istringstream stream(output);
  std::string line;
  test_result current_result;
  bool in_test = false;

  while (std::getline(stream, line)) {
    std::smatch match;

    // Look for test case markers in verbose output
    if (line.find("---------------") != std::string::npos) {
      // Section separator
      if (in_test && !current_result.name.empty()) {
        if (current_result.status == test_status::RUNNING) {
          current_result.status = test_status::PASSED;
        }
        results.push_back(current_result);
        current_result = test_result();
      }
      in_test = false;
    } else if (std::regex_search(line, match, assertion_regex)) {
      current_result.file_path = match[1].str();
      current_result.line_number = std::stoi(match[2].str());
      std::string result_str = match[3].str();

      if (result_str == "FAILED") {
        current_result.status = test_status::FAILED;
      }
      in_test = true;
    } else if (std::regex_search(line, match, require_regex)) {
      current_result.assertion_expr = match[1].str() + "(" + match[2].str() + ")";
    }
  }

  // Push last test if any
  if (!current_result.name.empty()) {
    if (current_result.status == test_status::RUNNING) {
      current_result.status = test_status::PASSED;
    }
    results.push_back(current_result);
  }

  return results;
}

std::vector<std::string> catch2_adapter::get_list_args() const {
  return {"--list-tests"};
}

std::vector<std::string> catch2_adapter::get_filter_args(
    const std::string &filter) const {
  return {filter};
}

std::vector<std::string> catch2_adapter::get_verbose_args() const {
  return {"-s", "-d", "yes"};  // -s for success, -d for durations
}

std::vector<std::string> catch2_adapter::parse_test_list(
    const std::string &output) const {
  std::vector<std::string> tests;
  std::istringstream stream(output);
  std::string line;

  while (std::getline(stream, line)) {
    // Catch2 lists tests one per line
    line.erase(0, line.find_first_not_of(" \t"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (!line.empty() && line.find("All available") == std::string::npos &&
        line.find("test case") == std::string::npos) {
      tests.push_back(line);
    }
  }

  return tests;
}

// ============================================================================
// doctest Adapter
// ============================================================================

bool doctest_adapter::detect_from_source(const std::string &source_content) const {
  std::regex include_regex(R"(#include\s*[<"]doctest/doctest\.h[">])");
  std::regex doctest_regex(R"(#define\s+DOCTEST_CONFIG_IMPLEMENT)");

  return std::regex_search(source_content, include_regex) ||
         std::regex_search(source_content, doctest_regex);
}

std::string doctest_adapter::generate_cmake_setup(
    const test_config::FrameworkConfig &config) const {
  std::ostringstream ss;

  if (config.fetch) {
    std::string version = config.version.empty() ? "v2.4.11" : config.version;
    if (version[0] != 'v') version = "v" + version;

    ss << "# Fetch doctest\n"
       << "include(FetchContent)\n"
       << "FetchContent_Declare(\n"
       << "  doctest\n"
       << "  GIT_REPOSITORY https://github.com/doctest/doctest.git\n"
       << "  GIT_TAG " << version << "\n"
       << ")\n"
       << "FetchContent_MakeAvailable(doctest)\n\n";
  } else {
    ss << "# Find doctest (must be installed)\n"
       << "find_package(doctest REQUIRED)\n\n";
  }

  return ss.str();
}

std::string doctest_adapter::get_cmake_target() const {
  return "doctest::doctest";
}

std::vector<test_result> doctest_adapter::parse_output(
    const std::string &output) const {
  std::vector<test_result> results;

  // doctest output format is similar to Catch2
  // [doctest] Test case file:line PASSED | FAILED
  std::regex test_regex(R"(\[doctest\]\s+(?:TEST CASE|SUBCASE):\s+(.+))");
  std::regex pass_regex(R"(SUCCESS!)");
  std::regex fail_regex(R"(FAILED!)");
  std::regex loc_regex(R"(([^:]+):(\d+):)");

  std::istringstream stream(output);
  std::string line;
  test_result current_result;

  while (std::getline(stream, line)) {
    std::smatch match;

    if (std::regex_search(line, match, test_regex)) {
      if (!current_result.name.empty()) {
        results.push_back(current_result);
      }
      current_result = test_result();
      current_result.name = match[1].str();
      current_result.status = test_status::RUNNING;
    } else if (std::regex_search(line, pass_regex)) {
      if (current_result.status == test_status::RUNNING) {
        current_result.status = test_status::PASSED;
      }
    } else if (std::regex_search(line, fail_regex)) {
      current_result.status = test_status::FAILED;
    } else if (std::regex_search(line, match, loc_regex)) {
      current_result.file_path = match[1].str();
      current_result.line_number = std::stoi(match[2].str());
    }
  }

  if (!current_result.name.empty()) {
    results.push_back(current_result);
  }

  return results;
}

std::vector<std::string> doctest_adapter::get_list_args() const {
  return {"--list-test-cases"};
}

std::vector<std::string> doctest_adapter::get_filter_args(
    const std::string &filter) const {
  return {"--test-case=" + filter};
}

std::vector<std::string> doctest_adapter::get_verbose_args() const {
  return {"--success=true", "--duration=true"};
}

std::vector<std::string> doctest_adapter::parse_test_list(
    const std::string &output) const {
  std::vector<std::string> tests;
  std::istringstream stream(output);
  std::string line;

  while (std::getline(stream, line)) {
    line.erase(0, line.find_first_not_of(" \t"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    if (!line.empty() && line[0] != '[') {
      tests.push_back(line);
    }
  }

  return tests;
}

// ============================================================================
// Boost.Test Adapter
// ============================================================================

bool bost_test_adapter::detect_from_source(const std::string &source_content) const {
  std::regex include_regex(R"(#include\s*[<"]boost/test/[^>]+[">])");
  std::regex test_regex(R"(\bBOOST_AUTO_TEST_CASE\s*\()");
  std::regex suite_regex(R"(\bBOOST_AUTO_TEST_SUITE\s*\()");

  return std::regex_search(source_content, include_regex) ||
         std::regex_search(source_content, test_regex) ||
         std::regex_search(source_content, suite_regex);
}

std::string bost_test_adapter::generate_cmake_setup(
    const test_config::FrameworkConfig &) const {
  std::ostringstream ss;

  // Boost.Test is typically installed, not fetched
  ss << "# Find Boost.Test\n"
     << "find_package(Boost REQUIRED COMPONENTS unit_test_framework)\n\n";

  return ss.str();
}

std::string bost_test_adapter::get_cmake_target() const {
  return "Boost::unit_test_framework";
}

std::vector<test_result> bost_test_adapter::parse_output(
    const std::string &output) const {
  std::vector<test_result> results;

  // Boost.Test output format:
  // RUNNING X test cases...
  // file(line): error: in "test_name": check ... failed
  // *** No errors detected
  // *** X failures detected
  std::regex test_regex(R"(in\s*\"([^\"]+)\":)");
  std::regex error_regex(R"(([^\(]+)\((\d+)\):\s*(error|fatal error):\s*in\s*\"([^\"]+)\":\s*(.+))");
  std::regex pass_regex(R"(\*\*\*\s*No errors detected)");
  std::regex fail_regex(R"(\*\*\*\s*(\d+)\s*failures?\s*detected)");

  std::istringstream stream(output);
  std::string line;
  std::map<std::string, test_result> test_map;

  while (std::getline(stream, line)) {
    std::smatch match;

    if (std::regex_search(line, match, error_regex)) {
      std::string test_name = match[4].str();
      if (!test_map.count(test_name)) {
        test_result result;
        result.name = test_name;
        result.test_name = test_name;
        test_map[test_name] = result;
      }
      test_map[test_name].status = test_status::FAILED;
      test_map[test_name].file_path = match[1].str();
      test_map[test_name].line_number = std::stoi(match[2].str());
      test_map[test_name].failure_message = match[5].str();
    }
  }

  for (auto &[name, result] : test_map) {
    results.push_back(std::move(result));
  }

  return results;
}

std::vector<std::string> bost_test_adapter::get_list_args() const {
  return {"--list_content"};
}

std::vector<std::string> bost_test_adapter::get_filter_args(
    const std::string &filter) const {
  return {"--run_test=" + filter};
}

std::vector<std::string> bost_test_adapter::get_verbose_args() const {
  return {"--log_level=test_suite", "--report_level=detailed"};
}

std::vector<std::string> bost_test_adapter::parse_test_list(
    const std::string &output) const {
  std::vector<std::string> tests;
  std::istringstream stream(output);
  std::string line;
  std::string current_suite;

  while (std::getline(stream, line)) {
    // Parse Boost.Test list format
    // Look for test case names
    if (line.find("*") != std::string::npos) {
      // This is a test case marker
      auto pos = line.find("*");
      std::string name = line.substr(pos + 1);
      name.erase(0, name.find_first_not_of(" \t"));
      name.erase(name.find_last_not_of(" \t\r\n") + 1);
      if (!name.empty()) {
        tests.push_back(name);
      }
    }
  }

  return tests;
}

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<i_test_framework_adapter> create_adapter(test_framework fw) {
  switch (fw) {
  case test_framework::Builtin:
    return std::make_unique<builtin_test_adapter>();
  case test_framework::GTest:
    return std::make_unique<gtest_adapter>();
  case test_framework::Catch2:
    return std::make_unique<catch2_adapter>();
  case test_framework::Doctest:
    return std::make_unique<doctest_adapter>();
  case test_framework::BoostTest:
    return std::make_unique<bost_test_adapter>();
  default:
    return std::make_unique<builtin_test_adapter>();
  }
}

} // namespace cforge
