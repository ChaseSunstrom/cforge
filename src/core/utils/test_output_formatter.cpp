/**
 * @file test_output_formatter.cpp
 * @brief Cargo/Rust-style output formatting for test results
 */

#include "core/test_output_formatter.hpp"
#include "core/types.h"
#include <algorithm>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace cforge {

namespace fs = std::filesystem;

// ============================================================================
// Constructor
// ============================================================================

test_output_formatter::test_output_formatter(style style) : m_style(style) {}

// ============================================================================
// Formatting Methods (return strings)
// ============================================================================

std::string test_output_formatter::format_run_start(cforge_int_t total_tests) {
  std::ostringstream ss;
  ss << "\nrunning " << total_tests << " tests\n";
  return ss.str();
}

std::string test_output_formatter::format_build_start(const std::string &target_name,
                                                     test_framework framework) {
  std::ostringstream ss;
  ss << "    Building test " << target_name
     << " (" << test_framework_to_string(framework) << ")\n";
  return ss.str();
}

std::string test_output_formatter::format_execution_start(
    const std::string &executable_path) {
  std::ostringstream ss;
  ss << "     Running " << shorten_path(executable_path) << "\n";
  return ss.str();
}

std::string test_output_formatter::format_test_result(const test_result &result) {
  std::ostringstream ss;

  ss << "test " << result.name << " ... ";

  switch (result.status) {
    case test_status::PASSED:
      ss << "ok";
      break;
    case test_status::FAILED:
      ss << "FAILED";
      break;
    case test_status::SKIPPED:
      ss << "ignored";
      break;
    case test_status::TIMEOUT:
      ss << "TIMEOUT";
      break;
    default:
      ss << "???";
      break;
  }

  if (result.duration.count() > 0) {
    ss << " (" << format_duration(result.duration) << ")";
  }

  ss << "\n";
  return ss.str();
}

std::string test_output_formatter::format_failure_details(const test_result &result) {
  if (result.status != test_status::FAILED && result.status != test_status::TIMEOUT) {
    return "";
  }

  std::ostringstream ss;

  ss << "\n---- " << result.name << " stdout ----\n";

  // Print captured stdout
  for (const auto &line : result.stdout_lines) {
    ss << line << "\n";
  }

  // Format error in Rust-style
  if (!result.failure_message.empty() || !result.file_path.empty()) {
    ss << "error[TEST]: ";
    if (!result.failure_message.empty()) {
      ss << result.failure_message;
    } else {
      ss << "assertion failed";
    }
    ss << "\n";

    if (!result.file_path.empty()) {
      ss << "  --> " << shorten_path(result.file_path);
      if (result.line_number > 0) {
        ss << ":" << result.line_number;
        if (result.column_number > 0) {
          ss << ":" << result.column_number;
        }
      }
      ss << "\n";

      // Show source context
      if (result.line_number > 0) {
        std::string source_line = read_source_line(result.file_path, result.line_number);
        if (!source_line.empty()) {
          // Line number gutter
          cforge_int_t gutter_width = 4;
          ss << "   |\n";
          ss << std::setw(gutter_width) << result.line_number << " |   " << source_line << "\n";
          ss << "   |   ";

          // Add carets under the line (pointing to assertion)
          if (!result.assertion_expr.empty()) {
            ss << "^";
            for (cforge_size_t i = 1; i < source_line.length() && i < 40; ++i) {
              ss << "^";
            }
          }
          ss << "\n";
        }
      }

      ss << "   |\n";
    }

    // Show expected vs actual
    if (!result.expected_value.empty() || !result.actual_value.empty()) {
      if (!result.expected_value.empty()) {
        ss << "   = expected: " << result.expected_value << "\n";
      }
      if (!result.actual_value.empty()) {
        ss << "   = actual: " << result.actual_value << "\n";
      }
    }

    // Show notes
    for (const auto &note : result.notes) {
      ss << "   = note: " << note << "\n";
    }
  }

  // Print captured stderr
  if (!result.stderr_lines.empty()) {
    ss << "\n---- " << result.name << " stderr ----\n";
    for (const auto &line : result.stderr_lines) {
      ss << line << "\n";
    }
  }

  return ss.str();
}

std::string test_output_formatter::format_summary(const test_summary &summary) {
  std::ostringstream ss;

  // Summary line
  if (!summary.failed_tests.empty()) {
    ss << "\nfailures:\n";
    for (const auto &test : summary.failed_tests) {
      ss << "    " << test << "\n";
    }
  }

  ss << "\ntest result: ";

  if (summary.failed > 0 || summary.timeout > 0) {
    ss << "FAILED";
  } else {
    ss << "ok";
  }

  ss << ". " << summary.passed << " passed; "
     << summary.failed << " failed";

  if (summary.skipped > 0) {
    ss << "; " << summary.skipped << " ignored";
  }

  if (summary.timeout > 0) {
    ss << "; " << summary.timeout << " timed out";
  }

  ss << "; finished in " << format_duration(summary.total_duration) << "\n";

  return ss.str();
}

std::string test_output_formatter::format_test_list(
    const std::vector<std::string> &tests) {
  auto grouped = group_tests_by_suite(tests);

  std::ostringstream ss;
  ss << "\nAvailable tests:\n\n";

  for (const auto &[suite, suite_tests] : grouped) {
    if (!suite.empty()) {
      ss << suite << "::\n";
      for (const auto &test : suite_tests) {
        ss << "  " << test << "\n";
      }
      ss << "\n";
    } else {
      for (const auto &test : suite_tests) {
        ss << test << "\n";
      }
    }
  }

  ss << "Total: " << tests.size() << " tests\n";
  return ss.str();
}

// ============================================================================
// Printing Methods (output with colors)
// ============================================================================

void test_output_formatter::print_run_start(cforge_int_t total_tests) {
  fmt::print("\nrunning {} tests\n", total_tests);
}

void test_output_formatter::print_build_start(const std::string &target_name,
                                             test_framework framework) {
  fmt::print(fmt::emphasis::bold | fg(fmt::color::green),
             "    Building");
  fmt::print(" test {} ({})\n", target_name, test_framework_to_string(framework));
}

void test_output_formatter::print_execution_start(const std::string &executable_path) {
  fmt::print(fmt::emphasis::bold | fg(fmt::color::green),
             "     Running");
  fmt::print(" {}\n", shorten_path(executable_path));
}

void test_output_formatter::print_test_result(const test_result &result) {
  fmt::print("test {} ... ", result.name);

  switch (result.status) {
    case test_status::PASSED:
      fmt::print(fg(fmt::color::green), "ok");
      break;
    case test_status::FAILED:
      fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "FAILED");
      break;
    case test_status::SKIPPED:
      fmt::print(fg(fmt::color::yellow), "ignored");
      break;
    case test_status::TIMEOUT:
      fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "TIMEOUT");
      break;
    default:
      fmt::print("???");
      break;
  }

  if (result.duration.count() > 0) {
    fmt::print(fg(fmt::color::dim_gray), " ({})", format_duration(result.duration));
  }

  fmt::print("\n");
}

void test_output_formatter::print_failure_details(const test_result &result) {
  if (result.status != test_status::FAILED && result.status != test_status::TIMEOUT) {
    return;
  }

  fmt::print("\n---- {} stdout ----\n", result.name);

  // Print captured stdout
  for (const auto &line : result.stdout_lines) {
    fmt::print("{}\n", line);
  }

  // Format error in Rust-style
  if (!result.failure_message.empty() || !result.file_path.empty()) {
    fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "error");
    fmt::print("[TEST]: ");
    if (!result.failure_message.empty()) {
      fmt::print("{}", result.failure_message);
    } else {
      fmt::print("assertion failed");
    }
    fmt::print("\n");

    if (!result.file_path.empty()) {
      fmt::print(fg(fmt::color::cyan), "  --> ");
      fmt::print("{}", shorten_path(result.file_path));
      if (result.line_number > 0) {
        fmt::print(":{}", result.line_number);
        if (result.column_number > 0) {
          fmt::print(":{}", result.column_number);
        }
      }
      fmt::print("\n");

      // Show source context
      if (result.line_number > 0) {
        std::string source_line = read_source_line(result.file_path, result.line_number);
        if (!source_line.empty()) {
          fmt::print(fg(fmt::color::cyan), "   |\n");
          fmt::print(fg(fmt::color::cyan), "{:4} |   ", result.line_number);
          fmt::print("{}\n", source_line);
          fmt::print(fg(fmt::color::cyan), "   |   ");

          // Add carets under the line
          fmt::print(fg(fmt::color::red), "");
          for (cforge_size_t i = 0; i < source_line.length() && i < 40; ++i) {
            fmt::print(fg(fmt::color::red), "^");
          }
          fmt::print("\n");
        }
      }

      fmt::print(fg(fmt::color::cyan), "   |\n");
    }

    // Show expected vs actual
    if (!result.expected_value.empty() || !result.actual_value.empty()) {
      if (!result.expected_value.empty()) {
        fmt::print(fg(fmt::color::cyan), "   = ");
        fmt::print("expected: {}\n", result.expected_value);
      }
      if (!result.actual_value.empty()) {
        fmt::print(fg(fmt::color::cyan), "   = ");
        fmt::print("actual: {}\n", result.actual_value);
      }
    }

    // Show notes
    for (const auto &note : result.notes) {
      fmt::print(fg(fmt::color::cyan), "   = ");
      fmt::print("note: {}\n", note);
    }
  }

  // Print captured stderr
  if (!result.stderr_lines.empty()) {
    fmt::print("\n---- {} stderr ----\n", result.name);
    for (const auto &line : result.stderr_lines) {
      fmt::print("{}\n", line);
    }
  }
}

void test_output_formatter::print_all_failures(const std::vector<test_result> &results) {
  bool has_failures = false;
  for (const auto &result : results) {
    if (result.status == test_status::FAILED || result.status == test_status::TIMEOUT) {
      has_failures = true;
      break;
    }
  }

  if (!has_failures) return;

  fmt::print("\n");
  fmt::print(fmt::emphasis::bold, "failures:\n\n");

  for (const auto &result : results) {
    print_failure_details(result);
  }
}

void test_output_formatter::print_summary(const test_summary &summary) {
  // List failed tests
  if (!summary.failed_tests.empty()) {
    fmt::print("\n");
    fmt::print(fmt::emphasis::bold, "failures:\n");
    for (const auto &test : summary.failed_tests) {
      fmt::print("    {}\n", test);
    }
  }

  fmt::print("\ntest result: ");

  if (summary.failed > 0 || summary.timeout > 0) {
    fmt::print(fmt::emphasis::bold | fg(fmt::color::red), "FAILED");
  } else {
    fmt::print(fmt::emphasis::bold | fg(fmt::color::green), "ok");
  }

  fmt::print(". ");
  fmt::print(fg(fmt::color::green), "{} passed", summary.passed);
  fmt::print("; ");
  if (summary.failed > 0) {
    fmt::print(fg(fmt::color::red), "{} failed", summary.failed);
  } else {
    fmt::print("{} failed", summary.failed);
  }

  if (summary.skipped > 0) {
    fmt::print("; ");
    fmt::print(fg(fmt::color::yellow), "{} ignored", summary.skipped);
  }

  if (summary.timeout > 0) {
    fmt::print("; ");
    fmt::print(fg(fmt::color::red), "{} timed out", summary.timeout);
  }

  fmt::print("; finished in {}\n", format_duration(summary.total_duration));
}

void test_output_formatter::print_test_list(const std::vector<std::string> &tests) {
  auto grouped = group_tests_by_suite(tests);

  fmt::print("\n");
  fmt::print(fmt::emphasis::bold, "Available tests:\n\n");

  for (const auto &[suite, suite_tests] : grouped) {
    if (!suite.empty()) {
      fmt::print(fg(fmt::color::cyan), "{}::\n", suite);
      for (const auto &test : suite_tests) {
        fmt::print("  {}\n", test);
      }
      fmt::print("\n");
    } else {
      for (const auto &test : suite_tests) {
        fmt::print("{}\n", test);
      }
    }
  }

  fmt::print("\nTotal: {} tests\n", tests.size());
}

void test_output_formatter::print_native_output(const std::string &output) {
  fmt::print("{}", output);
}

// ============================================================================
// Helper Methods
// ============================================================================

std::string test_output_formatter::read_source_line(const std::string &file_path,
                                                   cforge_int_t line_number) {
  if (line_number <= 0) return "";

  std::ifstream file(file_path);
  if (!file) return "";

  std::string line;
  cforge_int_t current_line = 0;
  while (std::getline(file, line)) {
    current_line++;
    if (current_line == line_number) {
      // Trim leading whitespace but preserve some indent
      cforge_size_t first_non_space = line.find_first_not_of(" \t");
      if (first_non_space != std::string::npos && first_non_space > 0) {
        line = line.substr(first_non_space);
      }
      return line;
    }
  }

  return "";
}

std::string test_output_formatter::shorten_path(const std::string &path,
                                               cforge_size_t max_length) {
  if (path.length() <= max_length) {
    return path;
  }

  // Try to get relative path from current directory
  try {
    fs::path abs_path = fs::absolute(path);
    fs::path rel_path = fs::relative(abs_path);
    std::string rel_str = rel_path.string();
    if (rel_str.length() < path.length()) {
      if (rel_str.length() <= max_length) {
        return rel_str;
      }
      // Shorten with ellipsis
      return "..." + rel_str.substr(rel_str.length() - max_length + 3);
    }
  } catch (...) {
    // Fallback to original path
  }

  // Just truncate with ellipsis at beginning
  return "..." + path.substr(path.length() - max_length + 3);
}

std::string test_output_formatter::format_duration(std::chrono::milliseconds duration) {
  auto ms = duration.count();

  if (ms < 1000) {
    return std::to_string(ms) + "ms";
  } else if (ms < 60000) {
    cforge_double_t secs = ms / 1000.0;
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << secs << "s";
    return ss.str();
  } else {
    cforge_int_t mins = static_cast<cforge_int_t>(ms / 60000);
    cforge_int_t secs = static_cast<cforge_int_t>((ms % 60000) / 1000);
    std::ostringstream ss;
    ss << mins << "m " << secs << "s";
    return ss.str();
  }
}

std::map<std::string, std::vector<std::string>>
test_output_formatter::group_tests_by_suite(const std::vector<std::string> &tests) {
  std::map<std::string, std::vector<std::string>> grouped;

  for (const auto &test : tests) {
    // Find suite separator (::, ., or /)
    cforge_size_t sep = test.find("::");
    if (sep == std::string::npos) {
      sep = test.find('.');
    }
    if (sep == std::string::npos) {
      sep = test.find('/');
    }

    if (sep != std::string::npos) {
      std::string suite = test.substr(0, sep);
      std::string name = test.substr(sep + (test[sep] == ':' ? 2 : 1));
      grouped[suite].push_back(name);
    } else {
      grouped[""].push_back(test);
    }
  }

  return grouped;
}

} // namespace cforge
