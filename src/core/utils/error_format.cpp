/**
 * @file error_format.cpp
 * @brief Implementation of error formatting utilities
 */

#include "core/error_format.hpp"
#include "core/types.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>

// Add header-only mode for fmt library
// Use these headers in this specific order
#include "fmt/color.h"
#include "fmt/core.h"
#include "fmt/format.h"

namespace cforge {

// Use simple foreground colors instead of custom text_styles
const auto ERROR_COLOR = fmt::fg(fmt::color::crimson);
const auto WARNING_COLOR = fmt::fg(fmt::color::gold);
const auto NOTE_COLOR = fmt::fg(fmt::color::steel_blue);
const auto HELP_COLOR = fmt::fg(fmt::color::medium_sea_green);
const auto CODE_COLOR = fmt::fg(fmt::color::slate_gray);
const auto LOCATION_COLOR = fmt::fg(fmt::color::light_blue);
const auto HIGHLIGHT_COLOR = fmt::fg(fmt::color::red);
const auto CARET_COLOR = fmt::fg(fmt::color::orange_red);

// Define error code prefixes for different tools to use when original codes
// aren't available
namespace error_code_prefix {
const std::string GCC_CLANG = "GCC";
const std::string MSVC = "MSVC";
const std::string CMAKE = "CM";
const std::string NINJA = "NJ";
const std::string LINKER = "LNK";
const std::string GENERIC = "ERR";
const std::string CPACK = "CP"; // Add CPack prefix
} // namespace error_code_prefix

std::string format_build_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics = extract_diagnostics(error_output);

  // Skip formatting if no diagnostics found
  if (diagnostics.empty()) {
    return "";
  }

  // Filter out CMake noise, keeping only relevant errors
  std::vector<diagnostic> filtered_diagnostics;

  for (const auto &diag : diagnostics) {
    // Skip CMake configuration/generation messages that aren't errors
    if (diag.message.find("CMake is re-running") != std::string::npos ||
        diag.message.find("Selecting Windows SDK") != std::string::npos ||
        diag.message.find("Building with") != std::string::npos ||
        diag.message.find("Configuring done") != std::string::npos ||
        diag.message.find("Generating done") != std::string::npos ||
        diag.message.find("Build files have been written") !=
            std::string::npos) {
      continue;
    }

    // Keep compiler errors, warnings, linker errors, etc.
    filtered_diagnostics.push_back(diag);
  }

  // If after filtering we have no diagnostics, return empty string
  if (filtered_diagnostics.empty()) {
    return "";
  }

  // Deduplicate similar errors (especially linker errors)
  filtered_diagnostics =
      deduplicate_diagnostics(std::move(filtered_diagnostics));

  // Add library suggestions to linker errors and generate fix suggestions for
  // all
  for (auto &diag : filtered_diagnostics) {
    if (diag.code.find("LNK") != std::string::npos ||
        diag.code.find("UNDEFINED") != std::string::npos) {
      // Try to extract symbol and suggest library
      std::regex symbol_regex(
          R"((?:undefined|unresolved)[^`'\"]*[`'\"]([^`'\"]+)[`'\"])");
      std::smatch match;
      if (std::regex_search(diag.message, match, symbol_regex)) {
        std::string suggested_lib = suggest_library_for_symbol(match[1].str());
        if (!suggested_lib.empty()) {
          diag.notes.push_back("try linking: " + suggested_lib);
        }
      }
    }

    // Generate fix suggestions for this diagnostic
    auto fixes = generate_fix_suggestions(diag);
    diag.fixes.insert(diag.fixes.end(), fixes.begin(), fixes.end());
  }

  // Format the diagnostics to a string
  std::stringstream ss;
  for (const auto &diag : filtered_diagnostics) {
    // Add occurrence count to message if > 1
    if (diag.occurrence_count > 1) {
      diagnostic diag_copy = diag;
      diag_copy.message = diag.message + " (" +
                          std::to_string(diag.occurrence_count) +
                          " occurrences)";
      ss << format_diagnostic_to_string(diag_copy);
    } else {
      ss << format_diagnostic_to_string(diag);
    }
  }

  // Calculate and append summary
  error_summary summary = calculate_error_summary(filtered_diagnostics);
  std::string summary_str = format_error_summary(summary);
  if (!summary_str.empty()) {
    ss << "\n" << summary_str;
  }

  return ss.str();
}

// Helper function to read a specific line from a file
static std::string read_line_from_file(const std::string &file_path, cforge_int_t line_number) {
  if (file_path.empty() || line_number <= 0) {
    return "";
  }

  try {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      return "";
    }

    std::string line;
    cforge_int_t current_line = 0;
    while (std::getline(file, line) && current_line < line_number) {
      current_line++;
      if (current_line == line_number) {
        return line;
      }
    }
  } catch (...) {
    // Ignore file read errors
  }

  return "";
}

// Format a diagnostic in Cargo/Rust style
std::string format_diagnostic_to_string(const diagnostic &diag) {
  std::stringstream ss;

  // Determine the level-specific formatting
  std::string level_str;
  fmt::color level_color = fmt::color::white; // Default initialization

  switch (diag.level) {
  case diagnostic_level::ERROR:
    level_str = "error";
    level_color = fmt::color::red;
    break;
  case diagnostic_level::WARNING:
    level_str = "warning";
    level_color = fmt::color::yellow;
    break;
  case diagnostic_level::NOTE:
    level_str = "note";
    level_color = fmt::color::cyan;
    break;
  case diagnostic_level::HELP:
    level_str = "help";
    level_color = fmt::color::green;
    break;
  default:
    level_str = "unknown";
    level_color = fmt::color::white;
    break;
  }

  // Cargo-style header: "error[E0425]: cannot find value `x`"
  ss << fmt::format(fg(level_color) | fmt::emphasis::bold, "{}", level_str);
  if (!diag.code.empty()) {
    ss << fmt::format(fg(level_color) | fmt::emphasis::bold, "[{}]", diag.code);
  }
  ss << fmt::format(fg(fmt::color::white) | fmt::emphasis::bold, ": {}\n",
                    diag.message);

  // File location: " --> src/main.cpp:10:5"
  if (!diag.file_path.empty()) {
    // Shorten long paths for display
    std::string display_path = diag.file_path;
    if (display_path.length() > 60) {
      // Use ... for middle of path
      display_path = display_path.substr(0, 25) + "..." +
                     display_path.substr(display_path.length() - 32);
    }

    ss << fmt::format(fg(fmt::color::cyan), "  --> ");
    ss << display_path;
    if (diag.line_number > 0) {
      ss << ":" << diag.line_number;
      if (diag.column_number > 0) {
        ss << ":" << diag.column_number;
      }
    }
    ss << "\n";
  }

  // Get line content - either from diagnostic or by reading the file
  std::string line_content = diag.line_content;
  if (line_content.empty() && !diag.file_path.empty() && diag.line_number > 0) {
    line_content = read_line_from_file(diag.file_path, diag.line_number);
  }

  // Code snippet with line numbers
  if (!line_content.empty() && diag.line_number > 0) {
    // Calculate gutter width based on line number
    cforge_int_t gutter_width = std::to_string(diag.line_number).length();
    if (gutter_width < 2)
      gutter_width = 2;

    // Empty line before code
    ss << fmt::format(fg(fmt::color::cyan), "{:>{}} |\n", "", gutter_width);

    // The actual code line
    ss << fmt::format(fg(fmt::color::cyan), "{:>{}} | ", diag.line_number,
                      gutter_width);
    ss << line_content << "\n";

    // The error pointer line
    ss << fmt::format(fg(fmt::color::cyan), "{:>{}} | ", "", gutter_width);

    if (diag.column_number > 0) {
      cforge_size_t col = static_cast<cforge_size_t>(diag.column_number - 1);
      cforge_size_t token_length = 1;

      // Try to find the token length
      if (col < line_content.length()) {
        if (std::isalnum(line_content[col]) ||
            line_content[col] == '_') {
          cforge_size_t start = col;
          cforge_size_t end = col;

          // Find token boundaries
          while (end < line_content.length() &&
                 (std::isalnum(line_content[end]) ||
                  line_content[end] == '_')) {
            end++;
          }
          token_length = end - start;
          if (token_length == 0)
            token_length = 1;
        }
      }

      // Print spaces then carets
      ss << std::string(col, ' ');
      ss << fmt::format(fg(level_color) | fmt::emphasis::bold, "{}\n",
                        std::string(token_length, '^'));
    } else {
      ss << fmt::format(fg(level_color) | fmt::emphasis::bold, "^\n");
    }
  }

  // Notes
  for (const auto &note : diag.notes) {
    ss << fmt::format(fg(fmt::color::cyan) | fmt::emphasis::bold,
                      "   = note: ");
    ss << note << "\n";
  }

  // Help text
  std::string help = diag.help.empty() ? diag.help_text : diag.help;
  if (!help.empty()) {
    ss << fmt::format(fg(fmt::color::green) | fmt::emphasis::bold,
                      "   = help: ");
    ss << help << "\n";
  }

  // Fix suggestions
  if (!diag.fixes.empty()) {
    for (cforge_size_t i = 0; i < diag.fixes.size() && i < 3;
         ++i) { // Limit to 3 suggestions
      const auto &fix = diag.fixes[i];
      ss << fmt::format(fg(fmt::color::magenta) | fmt::emphasis::bold,
                        "   = fix: ");
      ss << fix.description;
      if (!fix.replacement.empty() && fix.replacement.length() < 40) {
        ss << fmt::format(fg(fmt::color::gray), " -> ");
        ss << fmt::format(fg(fmt::color::green), "`{}`", fix.replacement);
      }
      ss << "\n";
    }
    if (diag.fixes.size() > 3) {
      ss << fmt::format(fg(fmt::color::gray),
                        "   = ... and {} more suggestion(s)\n",
                        diag.fixes.size() - 3);
    }
  }

  ss << "\n";
  return ss.str();
}

void print_diagnostic(const diagnostic &diag) {
  // Simply print the formatted string to stderr
  std::string formatted = format_diagnostic_to_string(diag);
  fmt::print(stderr, "{}", formatted);
}

std::vector<diagnostic> extract_diagnostics(const std::string &error_output) {
  std::vector<diagnostic> all_diagnostics;

  // Try parsing with each error parser
  auto compiler_diags = parse_compiler_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), compiler_diags.begin(),
                         compiler_diags.end());

  auto gcc_clang_diags = parse_gcc_clang_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), gcc_clang_diags.begin(),
                         gcc_clang_diags.end());

  auto msvc_diags = parse_msvc_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), msvc_diags.begin(),
                         msvc_diags.end());

  auto cmake_diags = parse_cmake_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), cmake_diags.begin(),
                         cmake_diags.end());

  auto ninja_diags = parse_ninja_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), ninja_diags.begin(),
                         ninja_diags.end());

  auto linker_diags = parse_linker_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), linker_diags.begin(),
                         linker_diags.end());

  auto cpack_diags = parse_cpack_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), cpack_diags.begin(),
                         cpack_diags.end());

  // Parse template errors (these are often missed by standard parsers)
  auto template_diags = parse_template_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), template_diags.begin(),
                         template_diags.end());

  // Parse preprocessor errors (#error, macro issues)
  auto preprocessor_diags = parse_preprocessor_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), preprocessor_diags.begin(),
                         preprocessor_diags.end());

  // Parse sanitizer output (ASan, UBSan, TSan)
  auto sanitizer_diags = parse_sanitizer_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), sanitizer_diags.begin(),
                         sanitizer_diags.end());

  // Parse assertion failures
  auto assertion_diags = parse_assertion_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), assertion_diags.begin(),
                         assertion_diags.end());

  // Parse C++20 module errors
  auto module_diags = parse_module_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), module_diags.begin(),
                         module_diags.end());

  // Parse runtime errors
  auto runtime_diags = parse_runtime_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), runtime_diags.begin(),
                         runtime_diags.end());

  // Parse test framework errors (GTest, Catch2, Boost.Test)
  auto test_diags = parse_test_framework_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), test_diags.begin(),
                         test_diags.end());

  // Parse static analysis tool output (clang-tidy, cppcheck)
  auto analysis_diags = parse_static_analysis_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), analysis_diags.begin(),
                         analysis_diags.end());

  // Parse C++20 concept constraint errors
  auto concept_diags = parse_concept_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), concept_diags.begin(),
                         concept_diags.end());

  // Parse constexpr evaluation errors
  auto constexpr_diags = parse_constexpr_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), constexpr_diags.begin(),
                         constexpr_diags.end());

  // Parse C++20 coroutine errors
  auto coroutine_diags = parse_coroutine_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), coroutine_diags.begin(),
                         coroutine_diags.end());

  // Parse C++20 ranges library errors
  auto ranges_diags = parse_ranges_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), ranges_diags.begin(),
                         ranges_diags.end());

  // Parse CUDA/HIP GPU compiler errors
  auto cuda_diags = parse_cuda_hip_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), cuda_diags.begin(),
                         cuda_diags.end());

  // Parse Intel ICC/ICX compiler errors
  auto intel_diags = parse_intel_compiler_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), intel_diags.begin(),
                         intel_diags.end());

  // Parse precompiled header (PCH) errors
  auto pch_diags = parse_pch_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), pch_diags.begin(),
                         pch_diags.end());

  // Parse cross-compilation and ABI mismatch errors
  auto abi_diags = parse_abi_errors(error_output);
  all_diagnostics.insert(all_diagnostics.end(), abi_diags.begin(),
                         abi_diags.end());

  return all_diagnostics;
}

std::vector<diagnostic> parse_compiler_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Regular expressions for common compiler error patterns
  std::regex missing_header_regex(
      R"(fatal error: ([^:]+): No such file or directory)");
  std::regex include_error_regex(
      R"(fatal error: ([^:]+): Cannot open include file)");
  std::regex syntax_error_regex(R"(error: expected ([^:]+) before ([^:]+))");
  std::regex undefined_reference_regex(R"(undefined reference to `([^']+)')");
  std::regex redefinition_regex(R"(redefinition of '([^']+)')");
  std::regex type_mismatch_regex(
      R"(error: cannot convert '([^']+)' to '([^']+)')");
  std::regex undeclared_identifier_regex(
      R"(error: '([^']+)' was not declared in this scope)");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;
    diagnostic diag;

    // Check for missing header files
    if (std::regex_search(line, matches, missing_header_regex)) {
      diag.level = diagnostic_level::ERROR;
      diag.code = "COMPILER-MISSING-HEADER";
      diag.message = "Missing header file: " + matches[1].str();
      diag.help_text =
          "Make sure the header file exists and is in the include path. "
          "You may need to add the directory to your include paths in "
          "cforge.toml.";
      diagnostics.push_back(diag);
      continue;
    }

    // Check for include errors
    if (std::regex_search(line, matches, include_error_regex)) {
      diag.level = diagnostic_level::ERROR;
      diag.code = "COMPILER-INCLUDE-ERROR";
      diag.message = "Cannot open include file: " + matches[1].str();
      diag.help_text =
          "Check that the include file exists and is accessible. "
          "Verify include paths in your cforge.toml configuration.";
      diagnostics.push_back(diag);
      continue;
    }

    // Check for syntax errors
    if (std::regex_search(line, matches, syntax_error_regex)) {
      diag.level = diagnostic_level::ERROR;
      diag.code = "COMPILER-SYNTAX-ERROR";
      diag.message =
          "Expected " + matches[1].str() + " before " + matches[2].str();
      diag.help_text = "Check your syntax and make sure all brackets, "
                       "parentheses, and semicolons are properly matched.";
      diagnostics.push_back(diag);
      continue;
    }

    // Check for undefined references
    if (std::regex_search(line, matches, undefined_reference_regex)) {
      diag.level = diagnostic_level::ERROR;
      diag.code = "COMPILER-UNDEFINED-REFERENCE";
      diag.message = "Undefined reference to: " + matches[1].str();
      diag.help_text =
          "Make sure the function or variable is defined and linked properly. "
          "Check that all required libraries are linked in your cforge.toml.";
      diagnostics.push_back(diag);
      continue;
    }

    // Check for redefinitions
    if (std::regex_search(line, matches, redefinition_regex)) {
      diag.level = diagnostic_level::ERROR;
      diag.code = "COMPILER-REDEFINITION";
      diag.message = "Redefinition of: " + matches[1].str();
      diag.help_text = "The symbol is defined more than once. Check for "
                       "duplicate definitions "
                       "or missing include guards in header files.";
      diagnostics.push_back(diag);
      continue;
    }

    // Check for type mismatches
    if (std::regex_search(line, matches, type_mismatch_regex)) {
      diag.level = diagnostic_level::ERROR;
      diag.code = "COMPILER-TYPE-MISMATCH";
      diag.message =
          "Cannot convert from " + matches[1].str() + " to " + matches[2].str();
      diag.help_text =
          "Check that the types match in your assignment or function call. "
          "You may need to add an explicit cast or use the correct type.";
      diagnostics.push_back(diag);
      continue;
    }

    // Check for undeclared identifiers
    if (std::regex_search(line, matches, undeclared_identifier_regex)) {
      diag.level = diagnostic_level::ERROR;
      diag.code = "COMPILER-UNDECLARED";
      diag.message = "Undeclared identifier: " + matches[1].str();
      diag.help_text = "Make sure the identifier is declared before use. "
                       "Check for missing includes, typos, or scope issues.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

std::vector<diagnostic>
parse_gcc_clang_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Regular expression to match GCC/Clang error format
  // Example: file.cpp:10:15: error: 'foo' was not declared in this scope
  // Also capture compiler-specific error codes like [-Wunused-variable]
  std::regex error_regex(
      R"(([^:]+):(\d+):(\d+):\s+(error|warning|note):\s+([^:]+)(?:\[([-\w]+)\])?)"
      R"(|([^:]+):(\d+):\s+(error|warning|note):\s+([^:]+)(?:\[([-\w]+)\])?)");

  // Also try to match more specific error codes that might be present
  std::regex error_code_regex(R"(error\s+(\d+):)"); // For GCC error numbers

  std::string line;
  std::istringstream stream(error_output);
  std::map<std::string, std::string> file_contents;

  while (std::getline(stream, line)) {
    std::smatch matches;
    if (std::regex_search(line, matches, error_regex)) {
      diagnostic diag;

      // Determine which pattern matched and extract fields accordingly
      if (matches[4].matched) { // First pattern with column number
        diag.file_path = matches[1].str();
        diag.line_number = std::stoi(matches[2].str());
        diag.column_number = std::stoi(matches[3].str());

        std::string level_str = matches[4].str();
        diag.message = matches[5].str();

        // Use the actual compiler warning/error code if provided
        if (matches[6].matched && !matches[6].str().empty()) {
          // Extract the warning code (like -Wunused-variable)
          diag.code = matches[6].str();
        } else {
          // Look for error code in the message
          std::smatch code_match;
          if (std::regex_search(diag.message, code_match, error_code_regex)) {
            diag.code = "E" + code_match[1].str();
          } else {
            // Use a prefix based on the message content
            diag.code = error_code_prefix::GCC_CLANG;

            // Add a more specific suffix based on the message
            if (diag.message.find("expected") != std::string::npos) {
              diag.code += "-SYNTAX"; // syntax error - expected something
            } else if (diag.message.find("undeclared") != std::string::npos ||
                       diag.message.find("not declared") != std::string::npos) {
              diag.code += "-UNDECL"; // undeclared identifier
            } else if (diag.message.find("undefined") != std::string::npos) {
              diag.code += "-UNDEF"; // undefined reference/symbol
            } else if (diag.message.find("cannot convert") !=
                           std::string::npos ||
                       diag.message.find("invalid conversion") !=
                           std::string::npos) {
              diag.code += "-CONV"; // type conversion error
            } else if (diag.message.find("no matching") != std::string::npos) {
              diag.code += "-NOMATCH"; // no matching function/method
            } else if (diag.message.find("redefinition") != std::string::npos ||
                       diag.message.find("already defined") !=
                           std::string::npos) {
              diag.code += "-REDEF"; // redefinition error
            }
          }
        }

        if (level_str == "error") {
          diag.level = diagnostic_level::ERROR;
        } else if (level_str == "warning") {
          diag.level = diagnostic_level::WARNING;

          // If we don't have an actual warning code, use the content
          if (diag.code == error_code_prefix::GCC_CLANG) {
            if (diag.message.find("unused") != std::string::npos) {
              diag.code += "-UNUSED"; // unused variable/function
            } else if (diag.message.find("implicit") != std::string::npos) {
              diag.code += "-IMPLICIT"; // implicit conversion
            } else if (diag.message.find("deprecated") != std::string::npos) {
              diag.code += "-DEPR"; // deprecated feature
            }
          }
        } else if (level_str == "note") {
          diag.level = diagnostic_level::NOTE;
        }
      }
      // ... similar changes for the second pattern without column number
      else if (matches[9].matched) { // Second pattern without column number
        diag.file_path = matches[7].str();
        diag.line_number = std::stoi(matches[8].str());
        diag.column_number = 0;

        std::string level_str = matches[9].str();
        diag.message = matches[10].str();

        // Use the actual compiler warning/error code if provided
        if (matches[11].matched && !matches[11].str().empty()) {
          // Extract the warning code (like -Wunused-variable)
          diag.code = matches[11].str();
        } else {
          // Look for error code in the message
          std::smatch code_match;
          if (std::regex_search(diag.message, code_match, error_code_regex)) {
            diag.code = "E" + code_match[1].str();
          } else {
            // Use a prefix based on the message content
            diag.code = error_code_prefix::GCC_CLANG;

            // Add a more specific suffix based on the message
            if (diag.message.find("expected") != std::string::npos) {
              diag.code += "-SYNTAX"; // syntax error - expected something
            } else if (diag.message.find("undeclared") != std::string::npos ||
                       diag.message.find("not declared") != std::string::npos) {
              diag.code += "-UNDECL"; // undeclared identifier
            } else if (diag.message.find("undefined") != std::string::npos) {
              diag.code += "-UNDEF"; // undefined reference/symbol
            } else if (diag.message.find("cannot convert") !=
                           std::string::npos ||
                       diag.message.find("invalid conversion") !=
                           std::string::npos) {
              diag.code += "-CONV"; // type conversion error
            } else if (diag.message.find("no matching") != std::string::npos) {
              diag.code += "-NOMATCH"; // no matching function/method
            } else if (diag.message.find("redefinition") != std::string::npos ||
                       diag.message.find("already defined") !=
                           std::string::npos) {
              diag.code += "-REDEF"; // redefinition error
            }
          }
        }

        if (level_str == "error") {
          diag.level = diagnostic_level::ERROR;
        } else if (level_str == "warning") {
          diag.level = diagnostic_level::WARNING;

          // If we don't have an actual warning code, use the content
          if (diag.code == error_code_prefix::GCC_CLANG) {
            if (diag.message.find("unused") != std::string::npos) {
              diag.code += "-UNUSED"; // unused variable/function
            } else if (diag.message.find("implicit") != std::string::npos) {
              diag.code += "-IMPLICIT"; // implicit conversion
            } else if (diag.message.find("deprecated") != std::string::npos) {
              diag.code += "-DEPR"; // deprecated feature
            }
          }
        } else if (level_str == "note") {
          diag.level = diagnostic_level::NOTE;
        }
      }

      // Try to extract line content if possible
      if (!diag.file_path.empty() && diag.line_number > 0) {
        // Check if we already have this file's contents
        if (file_contents.find(diag.file_path) == file_contents.end()) {
          try {
            std::ifstream file(diag.file_path);
            if (file.is_open()) {
              std::stringstream buffer;
              buffer << file.rdbuf();
              file_contents[diag.file_path] = buffer.str();
            }
          } catch (...) {
            // Ignore errors reading the file
          }
        }

        // Extract the line content if we have the file
        if (file_contents.find(diag.file_path) != file_contents.end()) {
          std::istringstream file_stream(file_contents[diag.file_path]);
          std::string file_line;
          cforge_int_t current_line = 0;

          while (std::getline(file_stream, file_line) &&
                 current_line < diag.line_number) {
            current_line++;
            if (current_line == diag.line_number) {
              diag.line_content = file_line;
              break;
            }
          }
        }
      }

      // Add help text with suggestions if appropriate
      if (diag.message.find("undeclared") != std::string::npos) {
        diag.help_text =
            "Check for typos or make sure to include the appropriate header";
      } else if (diag.message.find("expected") != std::string::npos) {
        diag.help_text = "Check for missing syntax elements";
      }

      diagnostics.push_back(diag);
    }
  }

  return diagnostics;
}

std::vector<diagnostic> parse_msvc_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Regular expression for MSVC error format
  // Example: C:\path\to\file.cpp(10,15): error C2065: 'foo': undeclared
  // identifier
  std::regex error_regex(
      R"(([^(]+)\((\d+)(?:,(\d+))?\):\s+(error|warning|note)\s+([A-Z]\d+):\s+(.+))");

  // Parse follow-up context lines that sometimes appear after the main error
  std::regex context_regex(R"((?:\s{2,}|\t+)(.+))");

  std::string line;
  std::istringstream stream(error_output);
  std::map<std::string, std::string> file_contents;

  // Keep track of the current diagnostic for adding context
  diagnostic *current_diag = nullptr;

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Match main error/warning line
    if (std::regex_search(line, matches, error_regex)) {
      diagnostic diag;

      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = matches[3].matched ? std::stoi(matches[3].str()) : 0;

      std::string level_str = matches[4].str();
      diag.code =
          matches[5].str(); // Use the actual MSVC error code (e.g., C2065)
      diag.message = matches[6].str();

      if (level_str == "error") {
        diag.level = diagnostic_level::ERROR;
      } else if (level_str == "warning") {
        diag.level = diagnostic_level::WARNING;
      } else if (level_str == "note") {
        diag.level = diagnostic_level::NOTE;
      }

      // Try to extract line content directly from the file
      if (!diag.file_path.empty() && diag.line_number > 0) {
        if (file_contents.find(diag.file_path) == file_contents.end()) {
          try {
            std::ifstream file(diag.file_path);
            if (file.is_open()) {
              std::stringstream buffer;
              buffer << file.rdbuf();
              file_contents[diag.file_path] = buffer.str();
            }
          } catch (...) {
            // Ignore errors reading the file
          }
        }

        if (file_contents.find(diag.file_path) != file_contents.end()) {
          std::istringstream file_stream(file_contents[diag.file_path]);
          std::string file_line;
          cforge_int_t current_line = 0;

          // Read until we reach the target line
          while (std::getline(file_stream, file_line) &&
                 current_line < diag.line_number) {
            current_line++;
            if (current_line == diag.line_number) {
              // Remove any trailing whitespace from the line
              while (!file_line.empty() && isspace(file_line.back())) {
                file_line.pop_back();
              }
              diag.line_content = file_line;
              break;
            }
          }
        }
      }

      // Add MSVC-specific help text based on error code
      switch (std::stoi(
          diag.code.substr(1))) { // Remove the C prefix and convert to int
      case 2065:                  // C2065: 'identifier': undeclared identifier
        diag.help_text = "The identifier is not declared in this scope. Check "
                         "for typos or missing includes.";
        break;
      case 2146: // C2146: syntax error: missing ';' before identifier
        diag.help_text = "Add a semicolon after the previous statement.";
        break;
      case 2143: // C2143: syntax error: missing ';' before 'type'
        diag.help_text = "Check for missing semicolons or unmatched braces.";
        break;
      case 3861: // C3861: 'identifier': identifier not found
        diag.help_text =
            "Function not found. Check for typos, missing includes, or if the "
            "function needs to be declared before use.";
        break;
      case 4430: // C4430: missing type specifier - int assumed
        diag.help_text = "C++ requires a type specifier for all declarations. "
                         "Add the appropriate type.";
        break;
      case 2059: // C2059: syntax error: 'token'
        diag.help_text = "Check for syntax errors like missing braces, "
                         "parentheses, or misplaced tokens.";
        break;
      case 2664: // C2664: cannot convert argument
        diag.help_text = "The types of arguments don't match the function "
                         "parameters. Check parameter types.";
        break;
      case 2782: // C2782: template parameter not used in parameter types
        diag.help_text = "Specify the template arguments explicitly or adjust "
                         "your code to use the template parameter.";
        break;
      default:
        // General guidance for other error codes
        if (diag.message.find("syntax error") != std::string::npos) {
          diag.help_text = "Check syntax around this line. Look for missing "
                           "punctuation or mismatched braces.";
        } else if (diag.message.find("undeclared") != std::string::npos) {
          diag.help_text = "Make sure this identifier is declared before use "
                           "or check for typos.";
        }
        break;
      }

      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
    }
    // Match context lines that might follow the main error
    else if (current_diag != nullptr &&
             std::regex_search(line, matches, context_regex)) {
      std::string context = matches[1].str();

      // If the line contains a source code snippet, add it as help text
      if (context.find("see declaration of") != std::string::npos ||
          context.find("see reference to") != std::string::npos) {
        if (!current_diag->help_text.empty()) {
          current_diag->help_text += " " + context;
        } else {
          current_diag->help_text = context;
        }
      }
    } else {
      // Reset current diagnostic pointer if we've moved to a new error
      current_diag = nullptr;
    }
  }

  return diagnostics;
}

std::vector<diagnostic> parse_cmake_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Regular expression for CMake error format
  // Example: CMake Error at CMakeLists.txt:10 (add_executable): Target
  // "my_target" already exists.
  std::regex error_regex(
      R"(CMake\s+(Error|Warning)(?:\s+at\s+([^:]+):(\d+)\s+\(([^)]+)\))?:\s+(.+))");
  // Multiline CMake error header without inline message
  std::regex error_at_regex(
      R"(CMake\s+(Error|Warning)\s+at\s+([^:]+):(\d+)\s+\(([^)]+)\):\s*$)");

  // Also try to match error codes in the format "CMake Error: Error 0x... " or
  // similar
  std::regex cmake_error_code_regex(R"(Error\s+(\w+\d+))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    // Handle multiline error blocks (header line followed by indented message
    // lines)
    std::smatch match_at;
    if (std::regex_search(line, match_at, error_at_regex)) {
      diagnostic diag;
      // Level and location
      std::string level_str = match_at[1].str();
      diag.level = (level_str == "Error") ? diagnostic_level::ERROR
                                          : diagnostic_level::WARNING;
      diag.file_path = match_at[2].str();
      diag.line_number = std::stoi(match_at[3].str());
      // Next line is the error message
      std::string msg_line;
      if (std::getline(stream, msg_line)) {
        msg_line.erase(0, msg_line.find_first_not_of(" \t"));
        diag.message = msg_line;
      }
      // Collect subsequent help text lines until empty
      std::string help_text;
      std::string help_line;
      while (std::getline(stream, help_line)) {
        std::string trimmed = help_line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (trimmed.empty())
          break;
        if (!help_text.empty())
          help_text += " ";
        help_text += trimmed;
      }
      diag.help_text = help_text;
      diagnostics.push_back(diag);
      continue;
    }

    std::smatch matches;
    if (std::regex_search(line, matches, error_regex)) {
      diagnostic diag;

      std::string level_str = matches[1].str();
      std::string message = matches[5].str();

      // Try to extract error code from message if present
      std::smatch code_match;
      if (std::regex_search(message, code_match, cmake_error_code_regex)) {
        diag.code = code_match[1].str();
      } else {
        diag.code = error_code_prefix::CMAKE;
      }

      if (level_str == "Error") {
        diag.level = diagnostic_level::ERROR;
        if (diag.code == error_code_prefix::CMAKE) {
          diag.code += "-ERROR";
        }
      } else if (level_str == "Warning") {
        diag.level = diagnostic_level::WARNING;
        if (diag.code == error_code_prefix::CMAKE) {
          diag.code += "-WARN";
        }
      }

      if (matches[2].matched) {
        diag.file_path = matches[2].str();
        diag.line_number = std::stoi(matches[3].str());
        std::string command = matches[4].str();
        diag.message = message + " (in " + command + ")";

        // Add command context to the error code
        if (diag.code == error_code_prefix::CMAKE + "-ERROR" ||
            diag.code == error_code_prefix::CMAKE + "-WARN") {
          diag.code += "-" + command;
        }
      } else {
        // For errors not associated with a specific file
        diag.message = message;

        // Add more context to the error code based on message content
        if (diag.code == error_code_prefix::CMAKE + "-ERROR") {
          if (message.find("Could not find") != std::string::npos ||
              message.find("not found") != std::string::npos) {
            diag.code += "-NOTFOUND";
          } else if (message.find("already exists") != std::string::npos) {
            diag.code += "-DUPLICATE";
          } else if (message.find("syntax error") != std::string::npos) {
            diag.code += "-SYNTAX";
          }
        } else if (diag.code == error_code_prefix::CMAKE + "-WARN") {
          if (message.find("deprecated") != std::string::npos) {
            diag.code += "-DEPR";
          } else if (message.find("unused") != std::string::npos) {
            diag.code += "-UNUSED";
          }
        }
      }

      // Add help text
      diag.help_text = "Check your CMake configuration files for correctness";

      diagnostics.push_back(diag);
    }
  }

  return diagnostics;
}

std::vector<diagnostic> parse_ninja_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Regular expression for Ninja error format
  // Example: ninja: error: build.ninja:10: syntax error
  std::regex error_regex(
      R"(ninja:\s+(error|warning):\s+(?:([^:]+):(\d+):\s+)?(.+))");

  // Try to extract error codes if present in messages
  std::regex ninja_code_regex(R"(error\s+(\w+\d+):)");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;
    if (std::regex_search(line, matches, error_regex)) {
      diagnostic diag;

      std::string level_str = matches[1].str();
      diag.message = matches[4].str();

      // Try to extract error code from the message
      std::smatch code_match;
      if (std::regex_search(diag.message, code_match, ninja_code_regex)) {
        diag.code = code_match[1].str();
      } else {
        diag.code = error_code_prefix::NINJA;

        if (level_str == "error") {
          diag.level = diagnostic_level::ERROR;
          diag.code += "-ERROR";

          // Add specific context based on the message
          std::string message = diag.message;
          if (message.find("syntax error") != std::string::npos) {
            diag.code += "-SYNTAX";
          } else if (message.find("multiple rules") != std::string::npos) {
            diag.code += "-MULTIPLE";
          } else if (message.find("missing") != std::string::npos) {
            diag.code += "-MISSING";
          } else if (message.find("stopping") != std::string::npos ||
                     message.find("failed") != std::string::npos) {
            diag.code += "-FAILED";
          } else if (message.find("unknown") != std::string::npos) {
            diag.code += "-UNKNOWN";
          }
        } else if (level_str == "warning") {
          diag.level = diagnostic_level::WARNING;
          diag.code += "-WARN";

          // Add specific context based on the message
          std::string message = diag.message;
          if (message.find("duplicate") != std::string::npos) {
            diag.code += "-DUPLICATE";
          } else if (message.find("deprecated") != std::string::npos) {
            diag.code += "-DEPR";
          }
        }
      }

      if (matches[2].matched) {
        diag.file_path = matches[2].str();
        diag.line_number = std::stoi(matches[3].str());
      }

      // Add help text
      diag.help_text = "Check your build configuration";

      diagnostics.push_back(diag);
    }
  }

  return diagnostics;
}

std::vector<diagnostic> parse_linker_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  
  // Regular expressions for various linker error formats
  

  // LLD-style linker error: "lld-link: error: undefined symbol: ..."
  std::regex lld_error_regex(R"(lld-link:\s*error:\s*(.*))");

  // ld-style linker error: "ld: undefined symbol: ..." or "/usr/bin/ld: error:
  // ..."
  std::regex ld_error_regex(
      R"((?:/[^\s:]+/)?ld(?:\.\S+)?:\s*(?:error:\s*)?(.*))");

  // MSVC linker error from LINK.exe: "LINK : fatal error LNK1181: ..."
  std::regex msvc_link_error_regex(
      R"(LINK\s*:\s*(?:fatal\s*)?error\s*(LNK\d+):\s*(.*))");

  // MSVC linker error from object files: "file.obj : error LNK2019: ..."
  // This is a very common format that was previously missed
  std::regex msvc_obj_error_regex(
      R"(([^\s:]+\.obj)\s*:\s*(?:fatal\s*)?error\s*(LNK\d+):\s*(.*))");

  // MSVC linker error with function context: "file.obj : error LNK2019: ... in
  // function ..."
  std::regex msvc_function_context_regex(
      R"(function\s+[\"']?([^\"'\s]+)[\"']?)");

  // GCC/Clang undefined reference: "file.o:(.text+0x...): undefined reference
  // to `symbol'"
  std::regex gcc_undefined_ref_regex(
      R"(([^\s:]+\.o(?:bj)?)\s*:\s*(?:\([^)]+\)\s*:\s*)?undefined reference to [`']([^'`]+)[`'])");

  // GCC/Clang undefined reference (simpler format): "undefined reference to
  // `symbol'"
  std::regex simple_undefined_ref_regex(
      R"(undefined reference to [`']([^'`]+)[`'])");

  // collect2 error: "collect2: error: ld returned 1 exit status"
  std::regex collect2_error_regex(R"(collect2:\s*error:\s*(.*))");

  // Clang linker error: "clang: error: linker command failed..."
  std::regex clang_linker_error_regex(
      R"(clang(?:\+\+)?:\s*error:\s*(linker.*))");

  // Generic reference pattern: ">>> referenced by file.obj" or "referenced by
  // file.cpp:123"
  std::regex reference_regex(
      R"((?:>>>)?\s*referenced by\s*([^:\n]+)(?::(\d+))?)");

  // Symbol extraction for help text
  std::regex symbol_extract_regex(
      R"((?:unresolved external symbol|undefined symbol|undefined reference to)\s*[\"'`]?([^\"'`\s\(]+))");

  std::string line;
  std::istringstream stream(error_output);
  diagnostic *current_diag = nullptr;

  // Helper to add linker-specific help text
  auto add_linker_help = [](diagnostic &diag, const std::string &error_code) {
    // Parse the error code number if it's an LNK error
    if (error_code.find("LNK") == 0) {
      try {
        cforge_int_t code_num = std::stoi(error_code.substr(3));
        switch (code_num) {
        case 1104:
          diag.help_text = "The file name specified could not be found. Check "
                           "that the library path is correct.";
          break;
        case 1120:
          diag.help_text = "One or more external symbols are unresolved. Make "
                           "sure all required libraries are linked.";
          break;
        case 1181:
          diag.help_text = "Cannot open the specified input file. Verify the "
                           "file exists and the path is correct.";
          break;
        case 2001:
          diag.help_text =
              "Unresolved external symbol. The symbol is declared but not "
              "defined. Check:\n"
              "   - Is the library containing this symbol linked?\n"
              "   - Is the symbol exported from a DLL correctly?\n"
              "   - Are you missing a lib file in your link dependencies?";
          break;
        case 2005:
          diag.help_text =
              "Symbol is already defined in another object. Check for:\n"
              "   - Duplicate definitions in multiple source files\n"
              "   - Missing 'inline' on header-defined functions\n"
              "   - Missing include guards";
          break;
        case 2019:
          diag.help_text =
              "Unresolved external symbol referenced in function. The function "
              "calls something that isn't defined. Check:\n"
              "   - Is the required library linked in cforge.toml?\n"
              "   - For Windows API, add the appropriate .lib (e.g., "
              "user32.lib, kernel32.lib)\n"
              "   - For third-party libs, verify include and library paths";
          break;
        case 2038:
          diag.help_text = "Runtime library mismatch detected. All modules "
                           "must use the same runtime library variant.";
          break;
        default:
          diag.help_text = "Check that all required libraries are linked and "
                           "symbols are correctly exported.";
          break;
        }
      } catch (...) {
        diag.help_text = "Check that all required libraries are linked.";
      }
    } else {
      // Generic linker help
      if (diag.message.find("undefined") != std::string::npos ||
          diag.message.find("unresolved") != std::string::npos) {
        diag.help_text =
            "Symbol not found during linking. Ensure:\n"
            "   - All required libraries are linked in cforge.toml\n"
            "   - Library paths are correct\n"
            "   - The symbol is actually defined (not just declared)";
      } else if (diag.message.find("multiple definition") !=
                     std::string::npos ||
                 diag.message.find("duplicate") != std::string::npos) {
        diag.help_text = "Symbol defined multiple times. Check for:\n"
                         "   - Duplicate definitions in source files\n"
                         "   - Functions in headers missing 'inline' keyword\n"
                         "   - Missing include guards in headers";
      } else {
        diag.help_text =
            "Check your linker configuration and library dependencies.";
      }
    }
  };

  // Helper to extract symbol name from error message for display
  auto extract_symbol_name =
      [&symbol_extract_regex](const std::string &msg) -> std::string {
    std::smatch match;
    if (std::regex_search(msg, match, symbol_extract_regex)) {
      return match[1].str();
    }
    return "";
  };

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Skip empty lines
    if (line.empty() ||
        line.find_first_not_of(" \t\r\n") == std::string::npos) {
      continue;
    }

    
    // MSVC object file linker errors (most common on Windows)
    
    if (std::regex_search(line, matches, msvc_obj_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.code = matches[2].str();
      diag.message = matches[3].str();
      diag.line_number = 0;
      diag.column_number = 0;

      // Extract the symbol name for a cleaner message
      std::string symbol = extract_symbol_name(diag.message);
      if (!symbol.empty()) {
        diag.notes.push_back("Missing symbol: " + symbol);
      }

      // Check for function context in the message
      std::smatch func_match;
      if (std::regex_search(diag.message, func_match,
                            msvc_function_context_regex)) {
        diag.notes.push_back("Referenced in function: " + func_match[1].str());
      }

      add_linker_help(diag, diag.code);
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    
    // MSVC LINK.exe errors
    
    if (std::regex_search(line, matches, msvc_link_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = matches[1].str();
      diag.message = matches[2].str();
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;

      add_linker_help(diag, diag.code);
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }


    // LLD linker errors

    if (std::regex_search(line, matches, lld_error_regex)) {
      std::string message = matches[1].str();

      // Skip if message is empty or only whitespace
      if (message.empty() || message.find_first_not_of(" \t\r\n") == std::string::npos) {
        continue;
      }

      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.message = message;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;

      // Determine error code based on message
      if (message.find("undefined symbol") != std::string::npos) {
        diag.code = "LNK-UNDEFINED";
        std::string symbol = extract_symbol_name(message);
        if (!symbol.empty()) {
          diag.notes.push_back("Missing symbol: " + symbol);
        }
      } else if (message.find("duplicate symbol") != std::string::npos) {
        diag.code = "LNK-DUPLICATE";
      } else if (message.find("cannot open") != std::string::npos) {
        diag.code = "LNK-NOTFOUND";
      } else if (message.find("unresolved") != std::string::npos) {
        diag.code = "LNK-UNRESOLVED";
      } else {
        diag.code = "LNK";
      }

      add_linker_help(diag, diag.code);
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    
    // GCC/Clang undefined reference with file context
    
    if (std::regex_search(line, matches, gcc_undefined_ref_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "LNK-UNDEFINED";
      diag.file_path = matches[1].str();
      std::string symbol = matches[2].str();
      diag.message = "undefined reference to `" + symbol + "'";
      diag.notes.push_back("Missing symbol: " + symbol);
      diag.line_number = 0;
      diag.column_number = 0;

      add_linker_help(diag, diag.code);
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    
    // Simple undefined reference (without file context)
    
    if (std::regex_search(line, matches, simple_undefined_ref_regex)) {
      // Only create a new diagnostic if we don't already have one for this
      bool already_captured = false;
      std::string symbol = matches[1].str();

      for (const auto &existing : diagnostics) {
        if (existing.message.find(symbol) != std::string::npos) {
          already_captured = true;
          break;
        }
      }

      if (!already_captured) {
        diagnostic diag;
        diag.level = diagnostic_level::ERROR;
        diag.code = "LNK-UNDEFINED";
        diag.message = "undefined reference to `" + symbol + "'";
        diag.notes.push_back("Missing symbol: " + symbol);
        diag.file_path = "";
        diag.line_number = 0;
        diag.column_number = 0;

        add_linker_help(diag, diag.code);
        diagnostics.push_back(diag);
        current_diag = &diagnostics.back();
      }
      continue;
    }


    // Clang linker wrapper errors

    if (std::regex_search(line, matches, clang_linker_error_regex)) {
      std::string message = matches[1].str();

      // Skip if message is empty or only whitespace
      if (message.empty() || message.find_first_not_of(" \t\r\n") == std::string::npos) {
        continue;
      }

      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "LNK-CLANG";
      diag.message = message;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;

      diag.help_text = "The linker failed. Check the errors above for details "
                       "about missing symbols or libraries.";
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }


    // collect2 errors (GCC linker wrapper)

    if (std::regex_search(line, matches, collect2_error_regex)) {
      std::string message = matches[1].str();

      // Skip if message is empty or only whitespace
      if (message.empty() || message.find_first_not_of(" \t\r\n") == std::string::npos) {
        continue;
      }

      // collect2 is a summary error; we prefer the more specific errors above
      // Only add if we haven't captured any linker errors yet
      if (diagnostics.empty()) {
        diagnostic diag;
        diag.level = diagnostic_level::ERROR;
        diag.code = "LNK-LD";
        diag.message = message;
        diag.file_path = "";
        diag.line_number = 0;
        diag.column_number = 0;

        diag.help_text = "The linker failed. Check for undefined references or "
                         "missing libraries above.";
        diagnostics.push_back(diag);
        current_diag = &diagnostics.back();
      }
      continue;
    }


    // ld linker errors

    if (std::regex_search(line, matches, ld_error_regex)) {
      std::string message = matches[1].str();

      // Skip if message is empty or only whitespace
      if (message.empty() || message.find_first_not_of(" \t\r\n") == std::string::npos) {
        continue;
      }

      // Skip if it's just a warning or note
      if (message.find("warning") != std::string::npos &&
          message.find("error") == std::string::npos) {
        continue;
      }

      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.message = message;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;

      if (message.find("undefined reference") != std::string::npos ||
          message.find("undefined symbol") != std::string::npos) {
        diag.code = "LNK-UNDEFINED";
      } else if (message.find("duplicate symbol") != std::string::npos ||
                 message.find("multiple definition") != std::string::npos) {
        diag.code = "LNK-DUPLICATE";
      } else if (message.find("cannot find") != std::string::npos ||
                 message.find("cannot open") != std::string::npos ||
                 message.find("not found") != std::string::npos) {
        diag.code = "LNK-NOTFOUND";
      } else {
        diag.code = "LNK";
      }

      add_linker_help(diag, diag.code);
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    
    // Reference context lines (add to current diagnostic)

    if (std::regex_search(line, matches, reference_regex)) {
      if (current_diag != nullptr) {
        std::string file_ref = matches[1].str();
        cforge_int_t ref_line = matches[2].matched ? std::stoi(matches[2].str()) : 0;

        if (current_diag->file_path.empty()) {
          current_diag->file_path = file_ref;
          if (ref_line > 0) {
            current_diag->line_number = ref_line;
          }
        } else {
          std::string note = "Also referenced in: " + file_ref;
          if (ref_line > 0) {
            note += ":" + std::to_string(ref_line);
          }
          current_diag->notes.push_back(note);
        }
      }
      continue;
    }
  }

  return diagnostics;
}

// Add function to parse CPack errors
std::vector<diagnostic> parse_cpack_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Regular expression for CPack error format
  // Example: CPack Error: Error when generating package: project_name
  std::regex cpack_error_regex(R"(CPack\s+(Error|Warning):\s+(.+))");

  // Try to match errors in CMake install files that reference CPack
  std::regex cmake_cpack_error_regex(
      R"(CMake\s+Error\s+at\s+([^:]+):(\d+)\s+\(([^)]+)\):\s+CPack\s+Error:\s+(.+))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // First try to match direct CPack errors
    if (std::regex_search(line, matches, cpack_error_regex)) {
      diagnostic diag;

      std::string level_str = matches[1].str();
      std::string message = matches[2].str();

      diag.code = error_code_prefix::CPACK;
      diag.message = message;

      if (level_str == "Error") {
        diag.level = diagnostic_level::ERROR;
        diag.code += "-ERROR";

        // Add more specific error code suffix based on message
        if (message.find("generating package") != std::string::npos) {
          diag.code += "-GEN";
          diag.help_text = "Check your package configuration in cforge.toml. "
                           "Make sure to specify valid generators.";
        } else if (message.find("file exists") != std::string::npos) {
          diag.code += "-EXISTS";
          diag.help_text =
              "Remove existing package files or use a different package name.";
        } else if (message.find("could not find") != std::string::npos ||
                   message.find("not found") != std::string::npos) {
          diag.code += "-NOTFOUND";
          diag.help_text =
              "Check that all required dependencies and files are available.";
        }
      } else if (level_str == "Warning") {
        diag.level = diagnostic_level::WARNING;
        diag.code += "-WARN";
      }

      diagnostics.push_back(diag);
    }
    // Then try to match CPack errors embedded in CMake errors
    else if (std::regex_search(line, matches, cmake_cpack_error_regex)) {
      diagnostic diag;

      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      std::string cmake_command = matches[3].str();
      std::string message = matches[4].str();

      diag.code = error_code_prefix::CPACK;

      // If this is from cmake_install.cmake, it's likely a packaging error
      if (diag.file_path.find("cmake_install.cmake") != std::string::npos) {
        diag.code += "-INSTALL";
      } else {
        diag.code += "-" + cmake_command;
      }

      diag.message = message;
      diag.level = diagnostic_level::ERROR;

      // Add specific help based on the error message
      if (message.find("Error when generating package") != std::string::npos) {
        diag.help_text =
            "Check that you have specified valid generators in your package "
            "configuration. For Windows, try using 'ZIP' generator.";
      } else if (message.find("Could not find") != std::string::npos) {
        diag.help_text =
            "Make sure all required files and dependencies are available.";
      }

      diagnostics.push_back(diag);
    }
  }

  return diagnostics;
}


// Template Error Parsing


std::vector<diagnostic> parse_template_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // MSVC template errors
  // Example: error C2784: 'bool std::operator <(const std::vector<_Ty,_Alloc>
  // &,const std::vector<_Ty,_Alloc> &)': could not deduce template argument
  std::regex msvc_template_regex(
      R"(([^(]+)\((\d+)(?:,(\d+))?\):\s*error\s+(C2\d{3}):\s*(.+template.+))");

  // MSVC "required from" / "see reference to" chains
  std::regex msvc_instantiation_regex(
      R"(([^(]+)\((\d+)\):\s*(?:note|see reference to|see declaration))");

  // GCC/Clang template errors
  // Example: error: no matching function for call to 'func<int>()'
  std::regex gcc_template_regex(
      R"(([^:]+):(\d+):(\d+):\s*error:\s*(.*(?:template|instantiat|no matching|candidate|deduced).*))",
      std::regex::icase);

  // GCC/Clang "required from here" / "in instantiation of"
  std::regex gcc_instantiation_regex(
      R"(([^:]+):(\d+):(\d+):\s*(?:note|required from|in instantiation of)\s*(.*))");

  // "candidate:" lines that follow template errors
  std::regex candidate_regex(
      R"(([^:]+):(\d+):(\d+):\s*note:\s*candidate:\s*(.*))");

  // Simplify deeply nested template types for readability
  auto simplify_template_type = [](const std::string &type) -> std::string {
    std::string result = type;

    // Replace std::basic_string<char, ...> with std::string
    std::regex basic_string_regex(R"(std::basic_string<char[^>]*>)");
    result = std::regex_replace(result, basic_string_regex, "std::string");

    // Replace std::basic_ostream<char, ...> with std::ostream
    std::regex basic_ostream_regex(R"(std::basic_ostream<char[^>]*>)");
    result = std::regex_replace(result, basic_ostream_regex, "std::ostream");

    // Replace std::basic_istream<char, ...> with std::istream
    std::regex basic_istream_regex(R"(std::basic_istream<char[^>]*>)");
    result = std::regex_replace(result, basic_istream_regex, "std::istream");

    // Replace allocator details
    std::regex allocator_regex(R"(,\s*std::allocator<[^>]+>)");
    result = std::regex_replace(result, allocator_regex, "");

    // Simplify __cxx11:: namespace
    std::regex cxx11_regex(R"(__cxx11::)");
    result = std::regex_replace(result, cxx11_regex, "");

    return result;
  };

  std::string line;
  std::istringstream stream(error_output);
  diagnostic *current_template_error = nullptr;
  cforge_int_t instantiation_depth = 0;
  const cforge_int_t MAX_INSTANTIATION_DEPTH = 3; // Only show top 3 levels

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Check for MSVC template errors
    if (std::regex_search(line, matches, msvc_template_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = matches[3].matched ? std::stoi(matches[3].str()) : 0;
      diag.code = matches[4].str();
      diag.message = simplify_template_type(matches[5].str());

      // Add help based on error code
      cforge_int_t code_num = std::stoi(diag.code.substr(1));
      switch (code_num) {
      case 2782:
        diag.help_text = "Template argument deduction failed. Try specifying "
                         "template arguments explicitly.";
        break;
      case 2783:
        diag.help_text = "Could not deduce template argument. Check that the "
                         "argument types match the template parameters.";
        break;
      case 2784:
        diag.help_text = "Template argument deduction failed for a function "
                         "template. Ensure argument types are compatible.";
        break;
      case 2893:
        diag.help_text = "Failed to specialize function template. Check "
                         "template parameter constraints.";
        break;
      case 2913:
        diag.help_text = "Template instantiation is ambiguous. Try using "
                         "explicit template arguments.";
        break;
      case 2977:
        diag.help_text = "Too many template arguments provided.";
        break;
      default:
        diag.help_text =
            "Template error. Check template parameters and argument types.";
        break;
      }

      diagnostics.push_back(diag);
      current_template_error = &diagnostics.back();
      instantiation_depth = 0;
      continue;
    }

    // Check for GCC/Clang template errors
    if (std::regex_search(line, matches, gcc_template_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = std::stoi(matches[3].str());
      diag.message = simplify_template_type(matches[4].str());
      diag.code = "TEMPLATE";

      // Categorize the error
      std::string msg_lower = diag.message;
      std::transform(msg_lower.begin(), msg_lower.end(), msg_lower.begin(),
                     ::tolower);

      if (msg_lower.find("no matching") != std::string::npos) {
        diag.code = "TEMPLATE-NOMATCH";
        diag.help_text = "No matching function or template found. Check:\n"
                         "   - Argument types match expected parameters\n"
                         "   - Required headers are included\n"
                         "   - Template arguments are correct";
      } else if (msg_lower.find("ambiguous") != std::string::npos) {
        diag.code = "TEMPLATE-AMBIGUOUS";
        diag.help_text = "Multiple templates match. Use explicit template "
                         "arguments to disambiguate.";
      } else if (msg_lower.find("incomplete type") != std::string::npos) {
        diag.code = "TEMPLATE-INCOMPLETE";
        diag.help_text = "Type is incomplete (forward declared only). Include "
                         "the full definition.";
      } else if (msg_lower.find("deduced") != std::string::npos ||
                 msg_lower.find("deduce") != std::string::npos) {
        diag.code = "TEMPLATE-DEDUCTION";
        diag.help_text = "Template argument deduction failed. Specify template "
                         "arguments explicitly.";
      } else {
        diag.help_text = "Template instantiation error. Review template "
                         "parameters and argument types.";
      }

      diagnostics.push_back(diag);
      current_template_error = &diagnostics.back();
      instantiation_depth = 0;
      continue;
    }

    // Check for instantiation context (limit depth to avoid noise)
    if (current_template_error != nullptr &&
        instantiation_depth < MAX_INSTANTIATION_DEPTH) {
      if (std::regex_search(line, matches, gcc_instantiation_regex) ||
          std::regex_search(line, matches, msvc_instantiation_regex)) {
        std::string context = matches[matches.size() > 4 ? 4 : 0].str();
        if (!context.empty()) {
          context = simplify_template_type(context);
          std::string file = matches[1].str();
          std::string line_num = matches[2].str();

          // Extract just the filename for brevity
          cforge_size_t last_slash = file.find_last_of("/\\");
          if (last_slash != std::string::npos) {
            file = file.substr(last_slash + 1);
          }

          current_template_error->notes.push_back("instantiated from " + file +
                                                  ":" + line_num);
          instantiation_depth++;
        }
        continue;
      }

      // Check for candidate functions
      if (std::regex_search(line, matches, candidate_regex)) {
        if (instantiation_depth < MAX_INSTANTIATION_DEPTH) {
          std::string candidate = simplify_template_type(matches[4].str());
          current_template_error->notes.push_back("candidate: " + candidate);
          instantiation_depth++;
        }
        continue;
      }
    }

    // Reset context on empty lines or unrelated content
    if (line.empty() ||
        line.find_first_not_of(" \t\r\n") == std::string::npos) {
      current_template_error = nullptr;
      instantiation_depth = 0;
    }
  }

  return diagnostics;
}


// Error Deduplication


std::vector<diagnostic>
deduplicate_diagnostics(std::vector<diagnostic> diagnostics) {
  if (diagnostics.empty()) {
    return diagnostics;
  }

  std::vector<diagnostic> deduplicated;

  // Helper to extract key parts for comparison
  auto get_dedup_key = [](const diagnostic &d) -> std::string {
    // For linker errors, dedupe by the symbol name
    if (d.code.find("LNK") != std::string::npos ||
        d.code.find("UNDEFINED") != std::string::npos) {
      // Extract symbol name from message
      std::regex symbol_regex(
          R"((?:undefined|unresolved)[^`'\"]*[`'\"]([^`'\"]+)[`'\"])");
      std::smatch match;
      if (std::regex_search(d.message, match, symbol_regex)) {
        return "LINKER::" + match[1].str();
      }
    }

    // For undeclared identifier errors, dedupe by the identifier name
    // This handles both "Undeclared identifier: foo" and "'foo' was not declared"
    if (d.code.find("UNDECL") != std::string::npos ||
        d.code.find("UNDECLARED") != std::string::npos ||
        d.message.find("not declared") != std::string::npos ||
        d.message.find("undeclared") != std::string::npos) {
      // Try to extract the identifier name
      std::regex id_regex1(R"(Undeclared identifier:\s*(\w+))");
      std::regex id_regex2(R"('(\w+)'\s*was not declared)");
      std::regex id_regex3(R"('(\w+)':\s*undeclared)");
      std::smatch match;
      if (std::regex_search(d.message, match, id_regex1) ||
          std::regex_search(d.message, match, id_regex2) ||
          std::regex_search(d.message, match, id_regex3)) {
        return "UNDECLARED::" + match[1].str();
      }
    }

    // For errors with file/line info, dedupe by location + error type
    if (!d.file_path.empty() && d.line_number > 0) {
      // Normalize the code to a base type for deduplication
      std::string base_code = d.code;
      if (base_code.find("-") != std::string::npos) {
        // Extract the suffix after the last hyphen as the error type
        cforge_size_t pos = base_code.rfind("-");
        base_code = base_code.substr(pos + 1);
      }
      return d.file_path + ":" + std::to_string(d.line_number) + "::" + base_code;
    }

    // For other errors, dedupe by code + message
    return d.code + "::" + d.message;
  };

  std::map<std::string, size_t> seen_errors; // key -> index in deduplicated

  for (auto &diag : diagnostics) {
    std::string key = get_dedup_key(diag);

    auto it = seen_errors.find(key);
    if (it != seen_errors.end()) {
      // Already seen this error
      auto &existing = deduplicated[it->second];

      // Prefer the diagnostic with more detailed location info
      bool new_has_better_info = !diag.file_path.empty() && diag.line_number > 0 &&
                                  (existing.file_path.empty() || existing.line_number == 0);

      if (new_has_better_info) {
        // Replace with the more detailed diagnostic but keep occurrence count
        cforge_int_t count = existing.occurrence_count + 1;
        auto old_notes = std::move(existing.notes);
        existing = std::move(diag);
        existing.occurrence_count = count;
        // Merge notes
        for (auto &note : old_notes) {
          if (existing.notes.size() < 5) {
            existing.notes.push_back(std::move(note));
          }
        }
      } else {
        existing.occurrence_count++;

        // Add file reference as a note if different location
        if (!diag.file_path.empty() && diag.file_path != existing.file_path) {
          std::string note = "also in: " + diag.file_path;
          if (diag.line_number > 0) {
            note += ":" + std::to_string(diag.line_number);
          }
          // Only add if we haven't exceeded note limit
          if (existing.notes.size() < 5) {
            existing.notes.push_back(note);
          } else if (existing.notes.size() == 5) {
            existing.notes.push_back("... and more");
          }
        }
      }
    } else {
      // New error
      seen_errors[key] = deduplicated.size();
      deduplicated.push_back(std::move(diag));
    }
  }

  return deduplicated;
}


// Error Summary


error_summary
calculate_error_summary(const std::vector<diagnostic> &diagnostics) {
  error_summary summary;
  std::map<std::string, cforge_int_t> category_counts;

  for (const auto &diag : diagnostics) {
    cforge_int_t count = diag.occurrence_count;

    switch (diag.level) {
    case diagnostic_level::ERROR:
      summary.total_errors += count;
      break;
    case diagnostic_level::WARNING:
      summary.total_warnings += count;
      break;
    case diagnostic_level::NOTE:
      summary.total_notes += count;
      break;
    case diagnostic_level::HELP:
      break;
    }

    // Categorize by error source
    if (diag.code.find("LNK") != std::string::npos ||
        diag.code.find("LD") != std::string::npos ||
        diag.code.find("UNDEFINED") != std::string::npos ||
        diag.code.find("DUPLICATE") != std::string::npos) {
      summary.linker_errors += count;
      category_counts["linker"] += count;
    } else if (diag.code.find("TEMPLATE") != std::string::npos ||
               diag.code.find("C278") != std::string::npos ||
               diag.code.find("C289") != std::string::npos) {
      summary.template_errors += count;
      category_counts["template"] += count;
    } else if (diag.code.find("CM") != std::string::npos ||
               diag.code.find("CMAKE") != std::string::npos) {
      summary.cmake_errors += count;
      category_counts["cmake"] += count;
    } else if (diag.level == diagnostic_level::ERROR) {
      summary.compiler_errors += count;
      category_counts["compiler"] += count;
    }

    // Also track by specific error type
    if (diag.message.find("undefined") != std::string::npos ||
        diag.message.find("unresolved") != std::string::npos) {
      category_counts["undefined symbol"] += count;
    } else if (diag.message.find("multiple definition") != std::string::npos ||
               diag.message.find("already defined") != std::string::npos) {
      category_counts["duplicate symbol"] += count;
    } else if (diag.message.find("No such file") != std::string::npos ||
               diag.message.find("not found") != std::string::npos ||
               diag.message.find("cannot find") != std::string::npos) {
      category_counts["file not found"] += count;
    }
  }

  // Convert category counts to sorted vector
  for (const auto &pair : category_counts) {
    summary.error_categories.push_back(pair);
  }
  std::sort(summary.error_categories.begin(), summary.error_categories.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  return summary;
}

std::string format_error_summary(const error_summary &summary) {
  if (summary.total_errors == 0 && summary.total_warnings == 0) {
    return "";
  }

  std::stringstream ss;

  // Main summary line
  if (summary.total_errors > 0) {
    ss << fmt::format(fg(fmt::color::red) | fmt::emphasis::bold, "error");
    ss << fmt::format(fg(fmt::color::white) | fmt::emphasis::bold,
                      ": build failed\n");
  }

  // Breakdown by source
  ss << fmt::format(fg(fmt::color::cyan), "   |\n");

  if (summary.compiler_errors > 0) {
    ss << fmt::format(fg(fmt::color::cyan), "   = ");
    ss << fmt::format(fg(fmt::color::red), "{} compiler error{}\n",
                      summary.compiler_errors,
                      summary.compiler_errors == 1 ? "" : "s");
  }

  if (summary.linker_errors > 0) {
    ss << fmt::format(fg(fmt::color::cyan), "   = ");
    ss << fmt::format(fg(fmt::color::red), "{} linker error{}\n",
                      summary.linker_errors,
                      summary.linker_errors == 1 ? "" : "s");
  }

  if (summary.template_errors > 0) {
    ss << fmt::format(fg(fmt::color::cyan), "   = ");
    ss << fmt::format(fg(fmt::color::red), "{} template error{}\n",
                      summary.template_errors,
                      summary.template_errors == 1 ? "" : "s");
  }

  if (summary.cmake_errors > 0) {
    ss << fmt::format(fg(fmt::color::cyan), "   = ");
    ss << fmt::format(fg(fmt::color::red), "{} CMake error{}\n",
                      summary.cmake_errors,
                      summary.cmake_errors == 1 ? "" : "s");
  }

  if (summary.total_warnings > 0) {
    ss << fmt::format(fg(fmt::color::cyan), "   = ");
    ss << fmt::format(fg(fmt::color::yellow), "{} warning{}\n",
                      summary.total_warnings,
                      summary.total_warnings == 1 ? "" : "s");
  }

  return ss.str();
}


// Library Suggestions for Common Symbols


std::string suggest_library_for_symbol(const std::string &symbol) {
  // Windows API function to library mappings
  static const std::map<std::string, std::string> windows_libs = {
      // User32.lib
      {"MessageBox", "user32.lib"},
      {"CreateWindow", "user32.lib"},
      {"DefWindowProc", "user32.lib"},
      {"RegisterClass", "user32.lib"},
      {"GetMessage", "user32.lib"},
      {"TranslateMessage", "user32.lib"},
      {"DispatchMessage", "user32.lib"},
      {"PostQuitMessage", "user32.lib"},
      {"ShowWindow", "user32.lib"},
      {"UpdateWindow", "user32.lib"},
      {"SetWindowText", "user32.lib"},
      {"GetWindowText", "user32.lib"},
      {"SendMessage", "user32.lib"},
      {"PostMessage", "user32.lib"},

      // Kernel32.lib
      {"CreateFile", "kernel32.lib"},
      {"ReadFile", "kernel32.lib"},
      {"WriteFile", "kernel32.lib"},
      {"CloseHandle", "kernel32.lib"},
      {"GetLastError", "kernel32.lib"},
      {"CreateThread", "kernel32.lib"},
      {"WaitForSingleObject", "kernel32.lib"},
      {"Sleep", "kernel32.lib"},
      {"GetModuleHandle", "kernel32.lib"},
      {"LoadLibrary", "kernel32.lib"},
      {"GetProcAddress", "kernel32.lib"},
      {"VirtualAlloc", "kernel32.lib"},
      {"VirtualFree", "kernel32.lib"},
      {"HeapAlloc", "kernel32.lib"},
      {"HeapFree", "kernel32.lib"},

      // Gdi32.lib
      {"CreateDC", "gdi32.lib"},
      {"DeleteDC", "gdi32.lib"},
      {"SelectObject", "gdi32.lib"},
      {"CreateFont", "gdi32.lib"},
      {"CreateBrush", "gdi32.lib"},
      {"CreatePen", "gdi32.lib"},
      {"BitBlt", "gdi32.lib"},
      {"TextOut", "gdi32.lib"},

      // Shell32.lib
      {"ShellExecute", "shell32.lib"},
      {"SHGetFolderPath", "shell32.lib"},
      {"SHBrowseForFolder", "shell32.lib"},

      // Ws2_32.lib (Winsock)
      {"socket", "ws2_32.lib"},
      {"connect", "ws2_32.lib"},
      {"send", "ws2_32.lib"},
      {"recv", "ws2_32.lib"},
      {"bind", "ws2_32.lib"},
      {"listen", "ws2_32.lib"},
      {"accept", "ws2_32.lib"},
      {"closesocket", "ws2_32.lib"},
      {"WSAStartup", "ws2_32.lib"},
      {"WSACleanup", "ws2_32.lib"},
      {"gethostbyname", "ws2_32.lib"},
      {"inet_addr", "ws2_32.lib"},
      {"htons", "ws2_32.lib"},
      {"ntohs", "ws2_32.lib"},

      // Ole32.lib / OleAut32.lib
      {"CoInitialize", "ole32.lib"},
      {"CoUninitialize", "ole32.lib"},
      {"CoCreateInstance", "ole32.lib"},
      {"SysAllocString", "oleaut32.lib"},
      {"SysFreeString", "oleaut32.lib"},

      // Advapi32.lib
      {"RegOpenKey", "advapi32.lib"},
      {"RegCloseKey", "advapi32.lib"},
      {"RegQueryValue", "advapi32.lib"},
      {"RegSetValue", "advapi32.lib"},
      {"OpenProcessToken", "advapi32.lib"},

      // Winmm.lib
      {"PlaySound", "winmm.lib"},
      {"timeGetTime", "winmm.lib"},
      {"mciSendString", "winmm.lib"},

      // OpenGL
      {"glBegin", "opengl32.lib"},
      {"glEnd", "opengl32.lib"},
      {"glVertex", "opengl32.lib"},
      {"glClear", "opengl32.lib"},
      {"wglCreateContext", "opengl32.lib"},
      {"wglMakeCurrent", "opengl32.lib"},
  };

  // Unix library mappings
  static const std::map<std::string, std::string> unix_libs = {
      // pthread
      {"pthread_create", "-lpthread"},
      {"pthread_join", "-lpthread"},
      {"pthread_mutex_init", "-lpthread"},
      {"pthread_mutex_lock", "-lpthread"},

      // math
      {"sin", "-lm"},
      {"cos", "-lm"},
      {"tan", "-lm"},
      {"sqrt", "-lm"},
      {"pow", "-lm"},
      {"log", "-lm"},
      {"exp", "-lm"},
      {"floor", "-lm"},
      {"ceil", "-lm"},

      // dl
      {"dlopen", "-ldl"},
      {"dlsym", "-ldl"},
      {"dlclose", "-ldl"},

      // rt
      {"clock_gettime", "-lrt"},
      {"timer_create", "-lrt"},
      {"shm_open", "-lrt"},

      // z (zlib)
      {"compress", "-lz"},
      {"uncompress", "-lz"},
      {"deflate", "-lz"},
      {"inflate", "-lz"},

      // ssl
      {"SSL_new", "-lssl -lcrypto"},
      {"SSL_connect", "-lssl -lcrypto"},
      {"SSL_read", "-lssl -lcrypto"},
      {"SSL_write", "-lssl -lcrypto"},

      // curl
      {"curl_easy_init", "-lcurl"},
      {"curl_easy_perform", "-lcurl"},
      {"curl_easy_cleanup", "-lcurl"},
  };

  // Clean up the symbol name (remove decorations)
  std::string clean_symbol = symbol;

  // Remove MSVC decorations (__imp_, @N suffix, etc.)
  if (clean_symbol.find("__imp_") == 0) {
    clean_symbol = clean_symbol.substr(6);
  }
  cforge_size_t at_pos = clean_symbol.find('@');
  if (at_pos != std::string::npos) {
    clean_symbol = clean_symbol.substr(0, at_pos);
  }

  // Remove leading underscore (C decoration)
  if (!clean_symbol.empty() && clean_symbol[0] == '_') {
    clean_symbol = clean_symbol.substr(1);
  }

  // Check Windows libs first
  for (const auto &pair : windows_libs) {
    if (clean_symbol.find(pair.first) != std::string::npos) {
      return pair.second;
    }
  }

  // Check Unix libs
  for (const auto &pair : unix_libs) {
    if (clean_symbol.find(pair.first) != std::string::npos) {
      return pair.second;
    }
  }

  // Check for common C++ standard library symbols that need explicit linking
  if (clean_symbol.find("std::filesystem") != std::string::npos) {
    return "-lstdc++fs (GCC < 9) or built-in (GCC 9+)";
  }

  return "";
}


// Fix Suggestions


std::string suggest_include_for_type(const std::string &type_name) {
  // Map of common types to their headers
  static const std::map<std::string, std::string> type_to_header = {
      // Containers
      {"vector", "<vector>"},
      {"std::vector", "<vector>"},
      {"map", "<map>"},
      {"std::map", "<map>"},
      {"unordered_map", "<unordered_map>"},
      {"std::unordered_map", "<unordered_map>"},
      {"set", "<set>"},
      {"std::set", "<set>"},
      {"unordered_set", "<unordered_set>"},
      {"std::unordered_set", "<unordered_set>"},
      {"list", "<list>"},
      {"std::list", "<list>"},
      {"deque", "<deque>"},
      {"std::deque", "<deque>"},
      {"array", "<array>"},
      {"std::array", "<array>"},
      {"queue", "<queue>"},
      {"std::queue", "<queue>"},
      {"stack", "<stack>"},
      {"std::stack", "<stack>"},
      {"priority_queue", "<queue>"},
      {"std::priority_queue", "<queue>"},

      // Strings
      {"string", "<string>"},
      {"std::string", "<string>"},
      {"wstring", "<string>"},
      {"std::wstring", "<string>"},
      {"string_view", "<string_view>"},
      {"std::string_view", "<string_view>"},

      // I/O
      {"cout", "<iostream>"},
      {"std::cout", "<iostream>"},
      {"cin", "<iostream>"},
      {"std::cin", "<iostream>"},
      {"cerr", "<iostream>"},
      {"std::cerr", "<iostream>"},
      {"endl", "<iostream>"},
      {"std::endl", "<iostream>"},
      {"ifstream", "<fstream>"},
      {"std::ifstream", "<fstream>"},
      {"ofstream", "<fstream>"},
      {"std::ofstream", "<fstream>"},
      {"fstream", "<fstream>"},
      {"std::fstream", "<fstream>"},
      {"stringstream", "<sstream>"},
      {"std::stringstream", "<sstream>"},
      {"ostringstream", "<sstream>"},
      {"std::ostringstream", "<sstream>"},
      {"istringstream", "<sstream>"},
      {"std::istringstream", "<sstream>"},
      {"iomanip", "<iomanip>"},

      // Memory
      {"unique_ptr", "<memory>"},
      {"std::unique_ptr", "<memory>"},
      {"shared_ptr", "<memory>"},
      {"std::shared_ptr", "<memory>"},
      {"weak_ptr", "<memory>"},
      {"std::weak_ptr", "<memory>"},
      {"make_unique", "<memory>"},
      {"std::make_unique", "<memory>"},
      {"make_shared", "<memory>"},
      {"std::make_shared", "<memory>"},

      // Utilities
      {"pair", "<utility>"},
      {"std::pair", "<utility>"},
      {"make_pair", "<utility>"},
      {"std::make_pair", "<utility>"},
      {"tuple", "<tuple>"},
      {"std::tuple", "<tuple>"},
      {"optional", "<optional>"},
      {"std::optional", "<optional>"},
      {"variant", "<variant>"},
      {"std::variant", "<variant>"},
      {"any", "<any>"},
      {"std::any", "<any>"},
      {"function", "<functional>"},
      {"std::function", "<functional>"},
      {"bind", "<functional>"},
      {"std::bind", "<functional>"},

      // Algorithms
      {"sort", "<algorithm>"},
      {"std::sort", "<algorithm>"},
      {"find", "<algorithm>"},
      {"std::find", "<algorithm>"},
      {"copy", "<algorithm>"},
      {"std::copy", "<algorithm>"},
      {"transform", "<algorithm>"},
      {"std::transform", "<algorithm>"},
      {"for_each", "<algorithm>"},
      {"std::for_each", "<algorithm>"},
      {"min", "<algorithm>"},
      {"std::min", "<algorithm>"},
      {"max", "<algorithm>"},
      {"std::max", "<algorithm>"},
      {"accumulate", "<numeric>"},
      {"std::accumulate", "<numeric>"},

      // Threading
      {"thread", "<thread>"},
      {"std::thread", "<thread>"},
      {"mutex", "<mutex>"},
      {"std::mutex", "<mutex>"},
      {"lock_guard", "<mutex>"},
      {"std::lock_guard", "<mutex>"},
      {"unique_lock", "<mutex>"},
      {"std::unique_lock", "<mutex>"},
      {"condition_variable", "<condition_variable>"},
      {"std::condition_variable", "<condition_variable>"},
      {"future", "<future>"},
      {"std::future", "<future>"},
      {"promise", "<promise>"},
      {"std::promise", "<promise>"},
      {"async", "<future>"},
      {"std::async", "<future>"},
      {"atomic", "<atomic>"},
      {"std::atomic", "<atomic>"},

      // Filesystem
      {"filesystem", "<filesystem>"},
      {"std::filesystem", "<filesystem>"},
      {"path", "<filesystem>"},
      {"std::filesystem::path", "<filesystem>"},

      // Time
      {"chrono", "<chrono>"},
      {"std::chrono", "<chrono>"},
      {"system_clock", "<chrono>"},
      {"steady_clock", "<chrono>"},
      {"high_resolution_clock", "<chrono>"},

      // Regex
      {"regex", "<regex>"},
      {"std::regex", "<regex>"},
      {"smatch", "<regex>"},
      {"std::smatch", "<regex>"},

      // Random
      {"random_device", "<random>"},
      {"std::random_device", "<random>"},
      {"mt19937", "<random>"},
      {"std::mt19937", "<random>"},
      {"uniform_int_distribution", "<random>"},
      {"uniform_real_distribution", "<random>"},

      // Type traits
      {"is_same", "<type_traits>"},
      {"std::is_same", "<type_traits>"},
      {"enable_if", "<type_traits>"},
      {"std::enable_if", "<type_traits>"},
      {"decay", "<type_traits>"},
      {"std::decay", "<type_traits>"},

      // C library
      {"size_t", "<cstddef>"},
      {"std::size_t", "<cstddef>"},
      {"nullptr_t", "<cstddef>"},
      {"uint8_t", "<cstdint>"},
      {"uint16_t", "<cstdint>"},
      {"uint32_t", "<cstdint>"},
      {"uint64_t", "<cstdint>"},
      {"int8_t", "<cstdint>"},
      {"int16_t", "<cstdint>"},
      {"int32_t", "<cstdint>"},
      {"int64_t", "<cstdint>"},
      {"FILE", "<cstdio>"},
      {"printf", "<cstdio>"},
      {"sprintf", "<cstdio>"},
      {"malloc", "<cstdlib>"},
      {"free", "<cstdlib>"},
      {"exit", "<cstdlib>"},
      {"memcpy", "<cstring>"},
      {"memset", "<cstring>"},
      {"strlen", "<cstring>"},
      {"strcmp", "<cstring>"},
      {"assert", "<cassert>"},
  };

  // Try exact match
  auto it = type_to_header.find(type_name);
  if (it != type_to_header.end()) {
    return it->second;
  }

  // Try without std:: prefix
  if (type_name.find("std::") == 0) {
    std::string without_std = type_name.substr(5);
    it = type_to_header.find(without_std);
    if (it != type_to_header.end()) {
      return it->second;
    }
  }

  // Try extracting template base (e.g., "vector<int>" -> "vector")
  cforge_size_t template_start = type_name.find('<');
  if (template_start != std::string::npos) {
    std::string base_type = type_name.substr(0, template_start);
    it = type_to_header.find(base_type);
    if (it != type_to_header.end()) {
      return it->second;
    }
  }

  return "";
}

// Simple Levenshtein distance for typo detection
static cforge_int_t levenshtein_distance(const std::string &s1, const std::string &s2) {
  const cforge_size_t m = s1.size();
  const cforge_size_t n = s2.size();

  if (m == 0)
    return static_cast<cforge_int_t>(n);
  if (n == 0)
    return static_cast<cforge_int_t>(m);

  std::vector<std::vector<cforge_int_t>> dp(m + 1, std::vector<cforge_int_t>(n + 1));

  for (cforge_size_t i = 0; i <= m; ++i)
    dp[i][0] = static_cast<cforge_int_t>(i);
  for (cforge_size_t j = 0; j <= n; ++j)
    dp[0][j] = static_cast<cforge_int_t>(j);

  for (cforge_size_t i = 1; i <= m; ++i) {
    for (cforge_size_t j = 1; j <= n; ++j) {
      cforge_int_t cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
      dp[i][j] = std::min({
          dp[i - 1][j] + 1,       // deletion
          dp[i][j - 1] + 1,       // insertion
          dp[i - 1][j - 1] + cost // substitution
      });
    }
  }

  return dp[m][n];
}

std::vector<std::string>
find_similar_identifiers(const std::string &unknown_identifier,
                         const std::vector<std::string> &available_identifiers,
                         cforge_int_t max_distance) {

  std::vector<std::pair<std::string, cforge_int_t>> matches;

  for (const auto &candidate : available_identifiers) {
    // Skip if length difference is too large
    cforge_int_t len_diff = std::abs(static_cast<cforge_int_t>(unknown_identifier.length()) -
                            static_cast<cforge_int_t>(candidate.length()));
    if (len_diff > max_distance)
      continue;

    cforge_int_t distance = levenshtein_distance(unknown_identifier, candidate);
    if (distance <= max_distance && distance > 0) {
      matches.push_back({candidate, distance});
    }
  }

  // Sort by distance (closest first)
  std::sort(matches.begin(), matches.end(),
            [](const auto &a, const auto &b) { return a.second < b.second; });

  // Extract just the identifiers
  std::vector<std::string> result;
  for (const auto &match : matches) {
    result.push_back(match.first);
    if (result.size() >= 3)
      break; // Limit to top 3 suggestions
  }

  return result;
}

std::vector<fix_suggestion> generate_fix_suggestions(const diagnostic &diag) {
  std::vector<fix_suggestion> suggestions;

  std::string msg_lower = diag.message;
  std::transform(msg_lower.begin(), msg_lower.end(), msg_lower.begin(),
                 ::tolower);

  
  // Missing semicolon
  
  if (msg_lower.find("expected ';'") != std::string::npos ||
      msg_lower.find("expected ';' ") != std::string::npos ||
      msg_lower.find("missing ';'") != std::string::npos ||
      diag.code == "C2143" || diag.code == "C2146") {

    fix_suggestion fix;
    fix.description = "Add missing semicolon";
    fix.replacement = ";";
    fix.is_insertion = true;

    // Try to determine where to insert
    if (!diag.line_content.empty()) {
      // Find the end of the statement (before any comment)
      std::string line = diag.line_content;
      cforge_size_t comment_pos = line.find("//");
      if (comment_pos != std::string::npos) {
        line = line.substr(0, comment_pos);
      }

      // Trim trailing whitespace
      cforge_size_t end = line.find_last_not_of(" \t");
      if (end != std::string::npos) {
        fix.start_column =
            static_cast<cforge_int_t>(end + 2); // After last non-space char
      }
    }

    suggestions.push_back(fix);
  }

  
  // Missing closing brace
  
  if (msg_lower.find("expected '}'") != std::string::npos ||
      msg_lower.find("missing '}'") != std::string::npos ||
      diag.code == "C2059") {

    fix_suggestion fix;
    fix.description = "Add missing closing brace";
    fix.replacement = "}";
    fix.is_insertion = true;
    suggestions.push_back(fix);
  }

  
  // Missing closing parenthesis
  
  if (msg_lower.find("expected ')'") != std::string::npos ||
      msg_lower.find("missing ')'") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Add missing closing parenthesis";
    fix.replacement = ")";
    fix.is_insertion = true;
    suggestions.push_back(fix);
  }

  
  // Undeclared identifier - suggest include
  
  if (msg_lower.find("undeclared") != std::string::npos ||
      msg_lower.find("not declared") != std::string::npos ||
      msg_lower.find("unknown type") != std::string::npos ||
      msg_lower.find("does not name a type") != std::string::npos ||
      diag.code == "C2065" || diag.code == "C3861") {

    // Extract the identifier from the message
    std::regex identifier_regex(R"(['"`]([^'"`]+)['"`])");
    std::smatch match;
    if (std::regex_search(diag.message, match, identifier_regex)) {
      std::string identifier = match[1].str();

      // Try to suggest an include
      std::string header = suggest_include_for_type(identifier);
      if (!header.empty()) {
        fix_suggestion fix;
        fix.description = "Add #include " + header;
        fix.replacement = "#include " + header + "\n";
        fix.is_insertion = true;
        fix.start_line = 1; // Insert at top of file
        fix.start_column = 1;
        suggestions.push_back(fix);
      }
    }
  }

  
  // Unused variable - suggest [[maybe_unused]] or removal
  
  if (msg_lower.find("unused variable") != std::string::npos ||
      msg_lower.find("unused parameter") != std::string::npos ||
      diag.code.find("-Wunused") != std::string::npos) {

    // Extract variable name
    std::regex var_regex(R"(['"`]([^'"`]+)['"`])");
    std::smatch match;
    if (std::regex_search(diag.message, match, var_regex)) {
      std::string var_name = match[1].str();

      // Suggest [[maybe_unused]]
      fix_suggestion fix1;
      fix1.description = "Add [[maybe_unused]] attribute";
      fix1.replacement = "[[maybe_unused]] ";
      fix1.is_insertion = true;
      suggestions.push_back(fix1);

      // Suggest casting to void
      fix_suggestion fix2;
      fix2.description = "Cast to void to suppress warning";
      fix2.replacement = "(void)" + var_name + ";";
      fix2.is_insertion = true;
      suggestions.push_back(fix2);

      // For parameters, suggest commenting out the name
      if (msg_lower.find("parameter") != std::string::npos) {
        fix_suggestion fix3;
        fix3.description = "Comment out parameter name";
        fix3.replacement = "/*" + var_name + "*/";
        suggestions.push_back(fix3);
      }
    }
  }

  
  // Missing return statement
  
  if (msg_lower.find("no return statement") != std::string::npos ||
      msg_lower.find("missing return") != std::string::npos ||
      msg_lower.find("control reaches end") != std::string::npos ||
      msg_lower.find("not all control paths return") != std::string::npos ||
      diag.code == "C4715" || diag.code == "C4716") {

    fix_suggestion fix;
    fix.description = "Add return statement";
    fix.replacement = "return {};  // TODO: Add proper return value";
    fix.is_insertion = true;
    suggestions.push_back(fix);
  }

  
  // Comparison between signed and unsigned
  
  if (msg_lower.find("signed and unsigned") != std::string::npos ||
      msg_lower.find("comparison between signed") != std::string::npos ||
      diag.code.find("-Wsign-compare") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Use static_cast to match types";
    fix.replacement = "static_cast<size_t>(...)"; // Generic suggestion
    suggestions.push_back(fix);
  }

  
  // Implicit conversion / narrowing
  
  if (msg_lower.find("implicit conversion") != std::string::npos ||
      msg_lower.find("narrowing conversion") != std::string::npos ||
      msg_lower.find("possible loss of data") != std::string::npos ||
      diag.code == "C4244" || diag.code == "C4267") {

    fix_suggestion fix;
    fix.description = "Add explicit cast to acknowledge conversion";
    fix.replacement = "static_cast<TargetType>(value)";
    suggestions.push_back(fix);
  }

  
  // Missing default in switch
  
  if (msg_lower.find("default label") != std::string::npos ||
      msg_lower.find("not handled in switch") != std::string::npos ||
      diag.code.find("-Wswitch") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Add default case to switch";
    fix.replacement = "default:\n    break;";
    fix.is_insertion = true;
    suggestions.push_back(fix);
  }

  
  // Use of = instead of ==
  
  if (msg_lower.find("suggest parentheses") != std::string::npos ||
      msg_lower.find("assignment in conditional") != std::string::npos ||
      msg_lower.find("using the result of an assignment") !=
          std::string::npos) {

    fix_suggestion fix1;
    fix1.description = "Change = to == for comparison";
    fix1.replacement = "==";
    suggestions.push_back(fix1);

    fix_suggestion fix2;
    fix2.description = "Add parentheses if assignment is intentional";
    fix2.replacement = "((assignment))";
    suggestions.push_back(fix2);
  }

  
  // Null pointer dereference potential
  
  if (msg_lower.find("null pointer") != std::string::npos ||
      msg_lower.find("nullptr") != std::string::npos ||
      msg_lower.find("may be null") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Add null check before use";
    fix.replacement = "if (ptr != nullptr) { /* use ptr */ }";
    suggestions.push_back(fix);
  }

  
  // Typo suggestions from compiler (MSVC style: "did you mean")
  
  std::regex did_you_mean_regex(R"(did you mean ['"`]([^'"`]+)['"`])");
  std::smatch did_you_mean_match;
  if (std::regex_search(diag.message, did_you_mean_match, did_you_mean_regex)) {
    fix_suggestion fix;
    fix.description = "Change to '" + did_you_mean_match[1].str() + "'";
    fix.replacement = did_you_mean_match[1].str();
    suggestions.push_back(fix);
  }


  // GCC/Clang note about similar names

  for (const auto &note : diag.notes) {
    std::string note_lower = note;
    std::transform(note_lower.begin(), note_lower.end(), note_lower.begin(),
                   ::tolower);

    if (note_lower.find("similar") != std::string::npos ||
        note_lower.find("did you mean") != std::string::npos) {
      std::regex suggestion_regex(R"(['"`]([^'"`]+)['"`])");
      std::smatch suggestion_match;
      if (std::regex_search(note, suggestion_match, suggestion_regex)) {
        fix_suggestion fix;
        fix.description = "Change to '" + suggestion_match[1].str() + "'";
        fix.replacement = suggestion_match[1].str();
        suggestions.push_back(fix);
      }
    }
  }


  // Virtual destructor missing

  if (msg_lower.find("has virtual functions but non-virtual destructor") != std::string::npos ||
      msg_lower.find("destructor is not virtual") != std::string::npos ||
      diag.code.find("-Wnon-virtual-dtor") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Add virtual destructor";
    fix.replacement = "virtual ~ClassName() = default;";
    fix.is_insertion = true;
    suggestions.push_back(fix);
  }


  // Override keyword missing

  if (msg_lower.find("hides overloaded virtual function") != std::string::npos ||
      msg_lower.find("suggest override") != std::string::npos ||
      diag.code.find("-Woverloaded-virtual") != std::string::npos ||
      diag.code.find("-Wsuggest-override") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Add 'override' keyword";
    fix.replacement = " override";
    fix.is_insertion = true;
    suggestions.push_back(fix);
  }


  // Multiple definition - suggest inline or move to .cpp

  if (msg_lower.find("multiple definition") != std::string::npos ||
      msg_lower.find("already defined") != std::string::npos ||
      msg_lower.find("duplicate symbol") != std::string::npos) {

    fix_suggestion fix1;
    fix1.description = "Mark as inline if in header file";
    fix1.replacement = "inline ";
    fix1.is_insertion = true;
    suggestions.push_back(fix1);

    fix_suggestion fix2;
    fix2.description = "Move definition to a .cpp file";
    suggestions.push_back(fix2);

    fix_suggestion fix3;
    fix3.description = "Check for missing include guards or #pragma once";
    suggestions.push_back(fix3);
  }


  // Use after move

  if (msg_lower.find("use after move") != std::string::npos ||
      msg_lower.find("moved from") != std::string::npos ||
      diag.code.find("-Wuse-after-move") != std::string::npos) {

    fix_suggestion fix1;
    fix1.description = "Remove use of moved-from object";
    suggestions.push_back(fix1);

    fix_suggestion fix2;
    fix2.description = "Use a copy instead of move";
    suggestions.push_back(fix2);
  }


  // Dangling reference/pointer

  if (msg_lower.find("dangling") != std::string::npos ||
      msg_lower.find("lifetime") != std::string::npos ||
      msg_lower.find("stack memory") != std::string::npos ||
      diag.code.find("-Wreturn-local-addr") != std::string::npos ||
      diag.code.find("-Wdangling") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Return by value or extend object lifetime";
    suggestions.push_back(fix);
  }


  // Deprecated feature usage

  if (msg_lower.find("deprecated") != std::string::npos ||
      diag.code.find("-Wdeprecated") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Update to use the non-deprecated alternative";
    suggestions.push_back(fix);
  }


  // Missing #pragma once / include guard

  if (msg_lower.find("included multiple times") != std::string::npos ||
      msg_lower.find("recursive include") != std::string::npos) {

    fix_suggestion fix1;
    fix1.description = "Add #pragma once at the top of the header";
    fix1.replacement = "#pragma once\n";
    fix1.is_insertion = true;
    suggestions.push_back(fix1);

    fix_suggestion fix2;
    fix2.description = "Consider using forward declarations instead of #include";
    suggestions.push_back(fix2);
  }


  // Thread safety issues

  if (msg_lower.find("thread") != std::string::npos &&
      (msg_lower.find("race") != std::string::npos ||
       msg_lower.find("unsafe") != std::string::npos)) {

    fix_suggestion fix;
    fix.description = "Add synchronization (mutex, atomic, etc.)";
    suggestions.push_back(fix);
  }


  // Memory leak potential

  if (msg_lower.find("memory leak") != std::string::npos ||
      msg_lower.find("not freed") != std::string::npos) {

    fix_suggestion fix;
    fix.description = "Use smart pointers (std::unique_ptr or std::shared_ptr)";
    suggestions.push_back(fix);
  }

  return suggestions;
}

// ============================================================================
// Preprocessor Error Parser
// ============================================================================

std::vector<diagnostic> parse_preprocessor_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // #error directive: file.h:10:2: error: #error "message"
  std::regex error_directive_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*#error\s*[\"']?([^\"'\n]+)[\"']?)");

  // Macro expansion errors
  std::regex macro_error_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:in expansion of macro|expanding macro)\s*[`'\"](\w+)[`'\"])");

  // Include guard warning
  std::regex include_guard_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*warning:\s*.*include guard.*\s*\[([^\]]+)\])");

  // Pragma error/warning
  std::regex pragma_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*(error|warning):\s*(?:#pragma|_Pragma)\s*(.*))");

  // Conditional compilation errors
  std::regex conditional_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:unterminated|unbalanced)\s*(#if|#ifdef|#ifndef|#else|#endif))");

  // Macro redefinition
  std::regex redef_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*warning:\s*[\"'](\w+)[\"']\s*(?:redefined|macro redefinition))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // #error directive
    if (std::regex_search(line, matches, error_directive_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "PP-ERROR";
      diag.message = "#error: " + matches[3].str();
      diag.help_text = "This #error directive was triggered intentionally. "
                       "Check the preprocessor conditions that led here.";
      diagnostics.push_back(diag);
      continue;
    }

    // Macro expansion errors
    if (std::regex_search(line, matches, macro_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "PP-MACRO";
      diag.message = "Error expanding macro '" + matches[3].str() + "'";
      diag.help_text = "Check the macro definition and arguments. "
                       "Ensure all macro parameters are properly escaped.";
      diagnostics.push_back(diag);
      continue;
    }

    // Conditional compilation errors
    if (std::regex_search(line, matches, conditional_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "PP-COND";
      diag.message = "Unterminated or unbalanced " + matches[3].str();
      diag.help_text = "Make sure all #if/#ifdef/#ifndef have matching #endif. "
                       "Check for missing #endif at end of file.";
      diagnostics.push_back(diag);
      continue;
    }

    // Macro redefinition
    if (std::regex_search(line, matches, redef_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::WARNING;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "PP-REDEF";
      diag.message = "Macro '" + matches[3].str() + "' redefined";
      diag.help_text = "The macro is defined multiple times. Use #undef before redefining, "
                       "or wrap in #ifndef to avoid redefinition.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Sanitizer Error Parser (ASan, UBSan, TSan, MSan)
// ============================================================================

std::vector<diagnostic> parse_sanitizer_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // AddressSanitizer errors
  // ==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
  std::regex asan_error_regex(
      R"(==\d+==ERROR:\s*AddressSanitizer:\s*([^\n]+))");

  // ASan stack trace location
  // #0 0x... in function_name file.cpp:123
  std::regex asan_frame_regex(
      R"(#(\d+)\s+0x[0-9a-fA-F]+\s+in\s+(\S+)\s+([^:]+):(\d+))");

  // UndefinedBehaviorSanitizer
  // file.cpp:123:45: runtime error: signed integer overflow
  std::regex ubsan_regex(
      R"(([^:]+):(\d+):(\d+):\s*runtime error:\s*(.+))");

  // ThreadSanitizer
  // WARNING: ThreadSanitizer: data race (pid=12345)
  std::regex tsan_regex(
      R"(WARNING:\s*ThreadSanitizer:\s*([^\(]+))");

  // MemorySanitizer
  // ==12345==WARNING: MemorySanitizer: use-of-uninitialized-value
  std::regex msan_regex(
      R"(==\d+==WARNING:\s*MemorySanitizer:\s*([^\n]+))");

  // LeakSanitizer
  // ==12345==ERROR: LeakSanitizer: detected memory leaks
  std::regex lsan_regex(
      R"(==\d+==ERROR:\s*LeakSanitizer:\s*([^\n]+))");

  std::string line;
  std::istringstream stream(error_output);
  diagnostic *current_diag = nullptr;

  while (std::getline(stream, line)) {
    std::smatch matches;

    // AddressSanitizer error header
    if (std::regex_search(line, matches, asan_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "ASAN";
      std::string error_type = matches[1].str();
      diag.message = "AddressSanitizer: " + error_type;

      // Add specific help based on error type
      if (error_type.find("heap-buffer-overflow") != std::string::npos) {
        diag.help_text = "Writing or reading beyond allocated heap memory. "
                         "Check array bounds and pointer arithmetic.";
      } else if (error_type.find("stack-buffer-overflow") != std::string::npos) {
        diag.help_text = "Writing or reading beyond stack-allocated array. "
                         "Check array indices and buffer sizes.";
      } else if (error_type.find("heap-use-after-free") != std::string::npos) {
        diag.help_text = "Accessing memory after it was freed. "
                         "Use smart pointers or carefully manage object lifetimes.";
      } else if (error_type.find("double-free") != std::string::npos) {
        diag.help_text = "Memory freed twice. Use smart pointers to prevent this.";
      } else if (error_type.find("stack-use-after-return") != std::string::npos) {
        diag.help_text = "Using a pointer to local variable after function returned. "
                         "Don't return pointers/references to local variables.";
      } else if (error_type.find("null-dereference") != std::string::npos ||
                 error_type.find("SEGV") != std::string::npos) {
        diag.help_text = "Dereferencing null or invalid pointer. Add null checks.";
      } else {
        diag.help_text = "Memory error detected. Check pointer/array operations.";
      }

      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    // ASan stack frame - add to current diagnostic
    if (current_diag && std::regex_search(line, matches, asan_frame_regex)) {
      if (matches[1].str() == "0") { // First frame is usually the error location
        current_diag->file_path = matches[3].str();
        current_diag->line_number = std::stoi(matches[4].str());
        current_diag->notes.push_back("in function: " + matches[2].str());
      }
      continue;
    }

    // UndefinedBehaviorSanitizer
    if (std::regex_search(line, matches, ubsan_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = std::stoi(matches[3].str());
      diag.code = "UBSAN";
      diag.message = "UndefinedBehavior: " + matches[4].str();

      std::string msg = matches[4].str();
      if (msg.find("signed integer overflow") != std::string::npos) {
        diag.help_text = "Signed integer overflow is undefined behavior. "
                         "Use unsigned types or check for overflow before operations.";
      } else if (msg.find("shift") != std::string::npos) {
        diag.help_text = "Invalid shift operation. Check shift amount is within bounds.";
      } else if (msg.find("null pointer") != std::string::npos) {
        diag.help_text = "Null pointer dereference. Add null checks before use.";
      } else if (msg.find("division by zero") != std::string::npos) {
        diag.help_text = "Division by zero. Check divisor before dividing.";
      } else {
        diag.help_text = "Undefined behavior detected. Review the operation.";
      }

      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    // ThreadSanitizer
    if (std::regex_search(line, matches, tsan_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "TSAN";
      diag.message = "ThreadSanitizer: " + matches[1].str();
      diag.help_text = "Data race detected. Use mutexes, atomics, or other "
                       "synchronization primitives to protect shared data.";
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    // MemorySanitizer
    if (std::regex_search(line, matches, msan_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "MSAN";
      diag.message = "MemorySanitizer: " + matches[1].str();
      diag.help_text = "Use of uninitialized memory detected. "
                       "Initialize all variables before use.";
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }

    // LeakSanitizer
    if (std::regex_search(line, matches, lsan_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "LSAN";
      diag.message = "LeakSanitizer: " + matches[1].str();
      diag.help_text = "Memory leak detected. Use smart pointers (unique_ptr, shared_ptr) "
                       "or ensure all allocations have corresponding deallocations.";
      diagnostics.push_back(diag);
      current_diag = &diagnostics.back();
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Assertion Error Parser
// ============================================================================

std::vector<diagnostic> parse_assertion_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // static_assert failure (GCC/Clang)
  // file.cpp:123: error: static assertion failed: message
  std::regex static_assert_gcc_regex(
      R"(([^:]+):(\d+):\s*error:\s*static assertion failed(?::\s*(.+))?)");

  // static_assert failure (MSVC)
  // file.cpp(123): error C2338: static_assert failed 'message'
  std::regex static_assert_msvc_regex(
      R"(([^\(]+)\((\d+)\):\s*error\s+C2338:\s*static_assert failed\s*'?([^']*)'?)");

  // Runtime assert failure (standard library)
  // Assertion failed: expression, file filename, line N
  std::regex assert_failure_regex(
      R"(Assertion failed:\s*([^,]+),\s*file\s+([^,]+),\s*line\s+(\d+))");

  // GCC/Clang runtime assert
  // file:line: function: Assertion `expr' failed.
  std::regex gcc_assert_regex(
      R"(([^:]+):(\d+):\s*(\S+):\s*Assertion\s*[`']([^'`]+)[`']\s*failed)");

  // MSVC runtime assert
  // Debug Assertion Failed! ... File: file.cpp, Line: 123
  std::regex msvc_assert_regex(
      R"(Debug Assertion Failed.*File:\s*([^,]+),\s*Line:\s*(\d+))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // static_assert (GCC/Clang)
    if (std::regex_search(line, matches, static_assert_gcc_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "STATIC-ASSERT";
      diag.message = "static_assert failed";
      if (matches[3].matched && !matches[3].str().empty()) {
        diag.message += ": " + matches[3].str();
      }
      diag.help_text = "A compile-time assertion failed. Check the condition "
                       "and ensure template parameters/constants meet requirements.";
      diagnostics.push_back(diag);
      continue;
    }

    // static_assert (MSVC)
    if (std::regex_search(line, matches, static_assert_msvc_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "C2338";
      diag.message = "static_assert failed";
      if (!matches[3].str().empty()) {
        diag.message += ": " + matches[3].str();
      }
      diag.help_text = "A compile-time assertion failed. Check the condition "
                       "and ensure template parameters/constants meet requirements.";
      diagnostics.push_back(diag);
      continue;
    }

    // Runtime assert (standard)
    if (std::regex_search(line, matches, assert_failure_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[2].str();
      diag.line_number = std::stoi(matches[3].str());
      diag.column_number = 0;
      diag.code = "ASSERT";
      diag.message = "Assertion failed: " + matches[1].str();
      diag.help_text = "A runtime assertion failed. The condition was expected to be true. "
                       "Check the program state and input data.";
      diagnostics.push_back(diag);
      continue;
    }

    // GCC/Clang runtime assert
    if (std::regex_search(line, matches, gcc_assert_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "ASSERT";
      diag.message = "Assertion `" + matches[4].str() + "' failed";
      diag.notes.push_back("in function: " + matches[3].str());
      diag.help_text = "A runtime assertion failed. Check the condition and inputs.";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC runtime assert
    if (std::regex_search(line, matches, msvc_assert_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "ASSERT";
      diag.message = "Debug Assertion Failed";
      diag.help_text = "A debug assertion failed. This usually indicates a bug "
                       "or invalid program state. Check preconditions.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// C++20 Module Error Parser
// ============================================================================

std::vector<diagnostic> parse_module_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Module not found
  std::regex module_not_found_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*(?:fatal\s+)?error:\s*(?:module|import)\s*['\"]?(\S+)['\"]?\s*not found)");

  // Module interface error
  std::regex module_interface_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:cannot|failed to)\s*(?:compile|build)\s*module\s*['\"]?(\S+)['\"]?)");

  // Module partition error
  std::regex partition_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*module partition\s*['\"]?(\S+)['\"]?)");

  // GCC module error: importing module X requires first compiling its interface
  std::regex gcc_module_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*failed to read compiled module:\s*(.+))");

  // MSVC module error: cannot open module file 'X.ifc'
  std::regex msvc_module_regex(
      R"(([^\(]+)\((\d+)\):\s*error\s+C\d+:\s*cannot open module\s+(?:interface\s+)?file\s*'([^']+)')");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Module not found
    if (std::regex_search(line, matches, module_not_found_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "MODULE-NOTFOUND";
      diag.message = "Module '" + matches[3].str() + "' not found";
      diag.help_text = "The imported module was not found. Ensure:\n"
                       "   - The module interface file exists\n"
                       "   - The module was compiled before this translation unit\n"
                       "   - Module search paths are correctly configured";
      diagnostics.push_back(diag);
      continue;
    }

    // Module interface error
    if (std::regex_search(line, matches, module_interface_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "MODULE-INTERFACE";
      diag.message = "Failed to compile module interface '" + matches[3].str() + "'";
      diag.help_text = "Module interface compilation failed. Check for errors in the module interface unit.";
      diagnostics.push_back(diag);
      continue;
    }

    // GCC module read error
    if (std::regex_search(line, matches, gcc_module_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "MODULE-READ";
      diag.message = "Failed to read compiled module: " + matches[3].str();
      diag.help_text = "Could not read the compiled module. Ensure the module was built "
                       "before importing it, and check module cache paths.";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC module file error
    if (std::regex_search(line, matches, msvc_module_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "MODULE-FILE";
      diag.message = "Cannot open module file '" + matches[3].str() + "'";
      diag.help_text = "Module interface file (.ifc) not found. Compile the module "
                       "interface before importing it.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Runtime Error Parser (Segfaults, Exceptions, etc.)
// ============================================================================

std::vector<diagnostic> parse_runtime_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Segmentation fault
  std::regex segfault_regex(R"(Segmentation fault|SIGSEGV)");

  // Stack overflow
  std::regex stackoverflow_regex(R"(Stack overflow|SIGSTKFLT|stack smashing)");

  // Floating point exception
  std::regex fpe_regex(R"(Floating point exception|SIGFPE)");

  // Abort
  std::regex abort_regex(R"(Aborted|SIGABRT)");

  // Bus error
  std::regex bus_regex(R"(Bus error|SIGBUS)");

  // Illegal instruction
  std::regex illegal_regex(R"(Illegal instruction|SIGILL)");

  // C++ exception
  std::regex cpp_exception_regex(
      R"(terminate called after throwing.*'(\w+)'|exception of type '([^']+)'|what\(\):\s*(.+))");

  // Uncaught exception with stack trace (GCC)
  std::regex uncaught_regex(
      R"(terminate called after throwing an instance of '([^']+)')");

  // Windows exception
  std::regex windows_exception_regex(
      R"(Exception Code:\s*(0x[0-9A-Fa-f]+)|Access Violation|STATUS_ACCESS_VIOLATION)");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Segmentation fault
    if (std::regex_search(line, matches, segfault_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "SEGFAULT";
      diag.message = "Segmentation fault (invalid memory access)";
      diag.help_text = "The program tried to access invalid memory. Common causes:\n"
                       "   - Dereferencing null pointer\n"
                       "   - Array/buffer overflow\n"
                       "   - Use after free\n"
                       "   - Stack overflow from infinite recursion\n"
                       "Run with AddressSanitizer (-fsanitize=address) for details.";
      diagnostics.push_back(diag);
      continue;
    }

    // Stack overflow
    if (std::regex_search(line, matches, stackoverflow_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "STACKOVERFLOW";
      diag.message = "Stack overflow detected";
      diag.help_text = "The stack exceeded its limit. Common causes:\n"
                       "   - Infinite recursion\n"
                       "   - Very large stack allocations (large arrays)\n"
                       "   - Deep call chains\n"
                       "Consider using heap allocation or increasing stack size.";
      diagnostics.push_back(diag);
      continue;
    }

    // Floating point exception
    if (std::regex_search(line, matches, fpe_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "FPE";
      diag.message = "Floating point exception";
      diag.help_text = "Invalid floating point operation. Common causes:\n"
                       "   - Division by zero\n"
                       "   - Invalid operation (sqrt of negative, etc.)\n"
                       "   - Overflow/underflow\n"
                       "Add checks before mathematical operations.";
      diagnostics.push_back(diag);
      continue;
    }

    // Abort
    if (std::regex_search(line, matches, abort_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "ABORT";
      diag.message = "Program aborted";
      diag.help_text = "The program was terminated. This usually indicates:\n"
                       "   - Failed assertion\n"
                       "   - Uncaught exception\n"
                       "   - Explicit abort() call\n"
                       "   - Memory allocation failure";
      diagnostics.push_back(diag);
      continue;
    }

    // C++ exception
    if (std::regex_search(line, matches, uncaught_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "EXCEPTION";
      diag.message = "Uncaught exception of type '" + matches[1].str() + "'";
      diag.help_text = "An exception was thrown but not caught. Add appropriate "
                       "try-catch blocks or fix the underlying issue.";
      diagnostics.push_back(diag);
      continue;
    }

    // Windows exception
    if (std::regex_search(line, matches, windows_exception_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "WIN-EXCEPTION";
      diag.message = "Windows exception: Access Violation";
      diag.help_text = "The program tried to access invalid memory on Windows. "
                       "Check for null pointers and buffer overflows.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Test Framework Error Parser (Google Test, Catch2, Boost.Test)
// ============================================================================

std::vector<diagnostic> parse_test_framework_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Google Test failure
  // file.cpp:123: Failure
  // Expected equality of these values:
  //   x
  //     Which is: 1
  //   y
  //     Which is: 2
  std::regex gtest_failure_regex(
      R"(([^:]+):(\d+):\s*Failure)");

  // Google Test EXPECT/ASSERT failure
  // file.cpp:123: error: Expected: (x) == (y), actual: 1 vs 2
  std::regex gtest_expect_regex(
      R"(([^:]+):(\d+):\s*error:\s*(.+))");

  // Google Test death test
  std::regex gtest_death_regex(
      R"(Death test:\s*(.+))");

  // Catch2 failure
  // file.cpp:123: FAILED:
  //   REQUIRE( x == y )
  // with expansion:
  //   1 == 2
  std::regex catch2_failure_regex(
      R"(([^:]+):(\d+):\s*FAILED:)");

  // Catch2 assertion
  std::regex catch2_assertion_regex(
      R"(([^:]+):(\d+):\s*(REQUIRE|CHECK|REQUIRE_FALSE|CHECK_FALSE)\s*\(\s*(.+)\s*\))");

  // Boost.Test failure
  // file.cpp(123): error: in "test_name": check x == y has failed
  std::regex boost_test_regex(
      R"(([^\(]+)\((\d+)\):\s*(error|fatal error):\s*in\s*\"([^\"]+)\":\s*(.+))");

  // Generic test failure pattern
  // [  FAILED  ] TestSuite.TestName (123 ms)
  std::regex gtest_failed_test_regex(
      R"(\[\s*FAILED\s*\]\s*(\S+)\s*\((\d+)\s*ms\))");

  std::string line;
  std::istringstream stream(error_output);
  std::vector<std::string> context_lines;

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Google Test failure
    if (std::regex_search(line, matches, gtest_failure_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "GTEST";
      diag.message = "Test assertion failed";

      // Collect next few lines for context
      std::string next_line;
      for (cforge_int_t i = 0; i < 6 && std::getline(stream, next_line); ++i) {
        if (!next_line.empty() && next_line[0] != '[') {
          diag.notes.push_back(next_line);
        }
      }

      diag.help_text = "A test assertion failed. Check expected vs actual values above.";
      diagnostics.push_back(diag);
      continue;
    }

    // Google Test EXPECT/ASSERT error
    if (std::regex_search(line, matches, gtest_expect_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "GTEST";
      diag.message = matches[3].str();
      diag.help_text = "A test assertion failed.";
      diagnostics.push_back(diag);
      continue;
    }

    // Catch2 failure
    if (std::regex_search(line, matches, catch2_failure_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CATCH2";
      diag.message = "Test assertion failed";

      // Collect context
      std::string next_line;
      for (cforge_int_t i = 0; i < 5 && std::getline(stream, next_line); ++i) {
        if (!next_line.empty()) {
          diag.notes.push_back(next_line);
        }
      }

      diag.help_text = "A Catch2 assertion failed. See expansion above.";
      diagnostics.push_back(diag);
      continue;
    }

    // Boost.Test failure
    if (std::regex_search(line, matches, boost_test_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "BOOST-TEST";
      diag.message = "In test '" + matches[4].str() + "': " + matches[5].str();
      diag.help_text = "A Boost.Test assertion failed.";
      diagnostics.push_back(diag);
      continue;
    }

    // Failed test summary line
    if (std::regex_search(line, matches, gtest_failed_test_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.code = "TEST-FAILED";
      diag.message = "Test failed: " + matches[1].str();
      diag.notes.push_back("Duration: " + matches[2].str() + " ms");
      diag.help_text = "This test case failed. See detailed output above.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Static Analysis Error Parser (clang-tidy, cppcheck)
// ============================================================================

std::vector<diagnostic> parse_static_analysis_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // clang-tidy format
  // file.cpp:123:45: warning: ... [check-name]
  std::regex clang_tidy_regex(
      R"(([^:]+):(\d+):(\d+):\s*(warning|error|note):\s*([^\[]+)\[([^\]]+)\])");

  // cppcheck format
  // [file.cpp:123]: (error) Message
  std::regex cppcheck_regex(
      R"(\[([^\]:]+):(\d+)\]:\s*\((error|warning|style|performance|portability|information)\)\s*(.+))");

  // cppcheck XML format attribute extraction
  std::regex cppcheck_xml_regex(
      R"(file=\"([^\"]+)\"\s+line=\"(\d+)\".*severity=\"([^\"]+)\".*msg=\"([^\"]+)\")");

  // PVS-Studio format
  // file.cpp:123:1: error: V501 Message
  std::regex pvs_regex(
      R"(([^:]+):(\d+):(\d+):\s*(error|warning|note):\s*(V\d+)\s*(.+))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // clang-tidy
    if (std::regex_search(line, matches, clang_tidy_regex)) {
      diagnostic diag;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = std::stoi(matches[3].str());

      std::string level = matches[4].str();
      if (level == "error") {
        diag.level = diagnostic_level::ERROR;
      } else if (level == "warning") {
        diag.level = diagnostic_level::WARNING;
      } else {
        diag.level = diagnostic_level::NOTE;
      }

      diag.code = matches[6].str(); // The check name like "modernize-use-nullptr"
      diag.message = matches[5].str();

      // Add help based on check category
      std::string check = diag.code;
      if (check.find("modernize") == 0) {
        diag.help_text = "Consider updating to modern C++ idioms.";
      } else if (check.find("bugprone") == 0) {
        diag.help_text = "This pattern may indicate a bug.";
      } else if (check.find("performance") == 0) {
        diag.help_text = "This could impact performance.";
      } else if (check.find("readability") == 0) {
        diag.help_text = "This affects code readability.";
      } else if (check.find("cppcoreguidelines") == 0) {
        diag.help_text = "Violates C++ Core Guidelines.";
      } else {
        diag.help_text = "Static analysis finding.";
      }

      diagnostics.push_back(diag);
      continue;
    }

    // cppcheck
    if (std::regex_search(line, matches, cppcheck_regex)) {
      diagnostic diag;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;

      std::string severity = matches[3].str();
      if (severity == "error") {
        diag.level = diagnostic_level::ERROR;
        diag.code = "CPPCHECK-ERR";
      } else if (severity == "warning") {
        diag.level = diagnostic_level::WARNING;
        diag.code = "CPPCHECK-WARN";
      } else if (severity == "performance") {
        diag.level = diagnostic_level::WARNING;
        diag.code = "CPPCHECK-PERF";
      } else {
        diag.level = diagnostic_level::NOTE;
        diag.code = "CPPCHECK-" + severity;
      }

      diag.message = matches[4].str();
      diag.help_text = "Static analysis finding from cppcheck.";
      diagnostics.push_back(diag);
      continue;
    }

    // PVS-Studio
    if (std::regex_search(line, matches, pvs_regex)) {
      diagnostic diag;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = std::stoi(matches[3].str());

      std::string level = matches[4].str();
      if (level == "error") {
        diag.level = diagnostic_level::ERROR;
      } else if (level == "warning") {
        diag.level = diagnostic_level::WARNING;
      } else {
        diag.level = diagnostic_level::NOTE;
      }

      diag.code = matches[5].str(); // V-code like V501
      diag.message = matches[6].str();
      diag.help_text = "Static analysis finding from PVS-Studio.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// C++20 Concept Constraint Error Parser
// ============================================================================

std::vector<diagnostic> parse_concept_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // GCC/Clang concept constraint not satisfied
  // file.cpp:123:45: error: no matching function for call to 'foo'
  // note: constraints not satisfied
  std::regex constraint_not_satisfied_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*note:\s*constraints?\s*not\s*satisfied)");

  // GCC concept error
  // file.cpp:123:45: error: template constraint failure
  std::regex gcc_concept_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:template\s+)?(?:constraint|concept)\s*(?:failure|not\s*satisfied))");

  // Clang concept error
  // file.cpp:123:45: error: constraints not satisfied for ...
  std::regex clang_concept_regex(
      R"(([^:]+):(\d+):(\d+):\s*error:\s*constraints\s*not\s*satisfied\s*(?:for|in)\s*(.+))");

  // MSVC concept error
  // file.cpp(123): error C7602: 'concept_name': the associated constraints are not satisfied
  std::regex msvc_concept_regex(
      R"(([^\(]+)\((\d+)\):\s*error\s+C7602:\s*'([^']+)':\s*(.+))");

  // Requires clause error
  std::regex requires_clause_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*(?:error|note):\s*(?:in|within)\s*requires.clause)");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Clang concept constraint error
    if (std::regex_search(line, matches, clang_concept_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = std::stoi(matches[3].str());
      diag.code = "CONCEPT";
      diag.message = "Constraints not satisfied for " + matches[4].str();
      diag.help_text = "The type does not satisfy the concept requirements. "
                       "Check that all required member functions and operators exist.";
      diagnostics.push_back(diag);
      continue;
    }

    // GCC concept error
    if (std::regex_search(line, matches, gcc_concept_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CONCEPT";
      diag.message = "Concept constraint not satisfied";
      diag.help_text = "The template argument does not satisfy the concept. "
                       "Check type requirements.";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC concept error
    if (std::regex_search(line, matches, msvc_concept_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "C7602";
      diag.message = "Concept '" + matches[3].str() + "': " + matches[4].str();
      diag.help_text = "The associated constraints are not satisfied. "
                       "Verify the type meets all concept requirements.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Constexpr Evaluation Error Parser
// ============================================================================

std::vector<diagnostic> parse_constexpr_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // GCC/Clang constexpr evaluation error
  // file.cpp:123:45: error: 'foo' is not a constant expression
  std::regex not_constexpr_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*'?([^']+)'?\s*is\s*not\s*a\s*constant\s*expression)");

  // Constexpr function call error
  // in 'constexpr' expansion of 'foo()'
  std::regex constexpr_expansion_regex(
      R"(in\s*'constexpr'\s*expansion\s*of\s*'([^']+)')");

  // Non-constexpr function call
  std::regex non_constexpr_call_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*call\s*to\s*non-'?constexpr'?\s*function\s*'([^']+)')");

  // consteval error
  std::regex consteval_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:call\s*to|cannot\s*call)\s*(?:immediate|consteval)\s*function)");

  // MSVC constexpr error
  // file.cpp(123): error C2131: expression did not evaluate to a constant
  std::regex msvc_constexpr_regex(
      R"(([^\(]+)\((\d+)\):\s*error\s+C2131:\s*(.+))");

  // Undefined behavior in constant evaluation
  std::regex ub_constexpr_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:undefined behavior|UB)\s*in\s*constant\s*expression)");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Not a constant expression
    if (std::regex_search(line, matches, not_constexpr_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CONSTEXPR";
      diag.message = "'" + matches[3].str() + "' is not a constant expression";
      diag.help_text = "This expression cannot be evaluated at compile time. "
                       "Check for:\n"
                       "   - Calls to non-constexpr functions\n"
                       "   - Dynamic memory allocation\n"
                       "   - Undefined behavior\n"
                       "   - Non-literal types";
      diagnostics.push_back(diag);
      continue;
    }

    // Non-constexpr function call
    if (std::regex_search(line, matches, non_constexpr_call_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CONSTEXPR";
      diag.message = "Call to non-constexpr function '" + matches[3].str() + "'";
      diag.help_text = "Cannot call a non-constexpr function in a constant expression. "
                       "Mark the function as constexpr if possible.";
      diagnostics.push_back(diag);
      continue;
    }

    // consteval error
    if (std::regex_search(line, matches, consteval_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CONSTEVAL";
      diag.message = "Invalid call to consteval/immediate function";
      diag.help_text = "consteval functions must be called at compile time. "
                       "Ensure all arguments are constant expressions.";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC constexpr error
    if (std::regex_search(line, matches, msvc_constexpr_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "C2131";
      diag.message = matches[3].str();
      diag.help_text = "Expression did not evaluate to a constant. "
                       "Check for non-constant subexpressions.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// C++20 Coroutine Error Parser
// ============================================================================

std::vector<diagnostic> parse_coroutine_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // co_await requires awaitable type
  // file.cpp:123:5: error: no member named 'await_ready' in 'MyType'
  std::regex await_ready_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*no\s*member\s*named\s*'await_ready'\s*in\s*'([^']+)')");

  // co_await with non-awaitable
  // error: no viable 'co_await' for 'value' of type 'int'
  std::regex non_awaitable_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:no\s*viable\s*)?'?co_await'?\s*(?:for|on|of)\s*'?([^']+)'?)");

  // Missing coroutine_handle or promise_type
  std::regex missing_promise_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:no\s*type\s*named\s*)?'promise_type'\s*in)");

  // co_return type mismatch
  std::regex co_return_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:cannot\s*convert|no\s*viable\s*conversion).*co_return)");

  // coroutine_traits specialization not found
  std::regex traits_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*(?:no|could\s*not\s*find)\s*(?:type|member)\s*.*coroutine_traits)");

  // Missing <coroutine> header
  std::regex header_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*(?:error|fatal\s*error):\s*'?(?:coroutine|experimental/coroutine)'?\s*(?:file\s*not\s*found|No\s*such\s*file))");

  // co_yield errors
  std::regex co_yield_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*co_yield.*(?:no\s*member|cannot|invalid))");

  // Coroutine in non-coroutine function
  std::regex non_coroutine_func_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*'co_(?:await|yield|return)'\s*cannot\s*be\s*used\s*in)");

  // MSVC coroutine errors
  // error C3312: no callable 'await_resume' function found for type 'T'
  std::regex msvc_await_regex(
      R"(([^\(]+)\((\d+)\):\s*error\s+C3312:\s*(.+))");

  // error C3313: 'identifier': variable cannot have type 'coroutine type'
  std::regex msvc_coro_type_regex(
      R"(([^\(]+)\((\d+)\):\s*error\s+C3313:\s*(.+))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Missing await_ready
    if (std::regex_search(line, matches, await_ready_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "COROUTINE";
      diag.message = "Type '" + matches[3].str() + "' is not awaitable (missing await_ready)";
      diag.help_text = "To make a type awaitable, implement:\n"
                       "   - bool await_ready() const\n"
                       "   - void await_suspend(std::coroutine_handle<>)\n"
                       "   - T await_resume()";
      diagnostics.push_back(diag);
      continue;
    }

    // Non-awaitable type
    if (std::regex_search(line, matches, non_awaitable_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "COROUTINE";
      diag.message = "Cannot co_await on '" + matches[3].str() + "'";
      diag.help_text = "The type must be awaitable. Either:\n"
                       "   - Implement await_ready/await_suspend/await_resume\n"
                       "   - Implement operator co_await()\n"
                       "   - Specialize std::coroutine_traits";
      diagnostics.push_back(diag);
      continue;
    }

    // Missing promise_type
    if (std::regex_search(line, matches, missing_promise_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "COROUTINE";
      diag.message = "Missing promise_type in coroutine return type";
      diag.help_text = "Define a nested promise_type in your coroutine return type:\n"
                       "   struct MyCoroutine {\n"
                       "     struct promise_type {\n"
                       "       MyCoroutine get_return_object();\n"
                       "       std::suspend_always initial_suspend();\n"
                       "       std::suspend_always final_suspend() noexcept;\n"
                       "       void return_void(); // or return_value(T)\n"
                       "       void unhandled_exception();\n"
                       "     };\n"
                       "   };";
      diagnostics.push_back(diag);
      continue;
    }

    // Missing coroutine header
    if (std::regex_search(line, matches, header_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "COROUTINE";
      diag.message = "Missing <coroutine> header";
      diag.help_text = "Add #include <coroutine> (C++20) or\n"
                       "#include <experimental/coroutine> (C++17 with compiler support)";
      diagnostics.push_back(diag);
      continue;
    }

    // coroutine_traits not found
    if (std::regex_search(line, matches, traits_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "COROUTINE";
      diag.message = "coroutine_traits specialization not found";
      diag.help_text = "Define promise_type in your return type, or specialize "
                       "std::coroutine_traits for your return type.";
      diagnostics.push_back(diag);
      continue;
    }

    // co_await/co_yield/co_return in non-coroutine
    if (std::regex_search(line, matches, non_coroutine_func_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "COROUTINE";
      diag.message = "Coroutine keyword used in non-coroutine function";
      diag.help_text = "co_await, co_yield, and co_return can only be used in coroutines. "
                       "Ensure the function's return type has a valid promise_type.";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC await errors
    if (std::regex_search(line, matches, msvc_await_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "C3312";
      diag.message = matches[3].str();
      diag.help_text = "The awaitable type must have await_resume() member function.";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC coroutine type errors
    if (std::regex_search(line, matches, msvc_coro_type_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "C3313";
      diag.message = matches[3].str();
      diag.help_text = "Check coroutine type requirements and promise_type definition.";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// C++20 Ranges Library Error Parser
// ============================================================================

std::vector<diagnostic> parse_ranges_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Range concept not satisfied
  // error: type 'T' does not satisfy 'range'
  std::regex range_concept_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*(?:type\s*)?'([^']+)'\s*does\s*not\s*satisfy\s*'?(ranges?::)?(\w+)'?)");

  // Missing begin/end for range
  std::regex begin_end_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:\s*no\s*(?:viable\s*)?(?:member|function)\s*(?:named\s*)?'(begin|end)')");

  // views:: pipe operator errors
  std::regex pipe_operator_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*(?:invalid|no\s*match).*operator\|.*(?:views|ranges))");

  // Iterator requirements not met
  std::regex iterator_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*(?:type\s*)?'([^']+)'\s*does\s*not\s*satisfy\s*'?(input|output|forward|bidirectional|random_access|contiguous)_iterator'?)");

  // Sentinel requirements
  std::regex sentinel_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*(?:type\s*)?'([^']+)'\s*does\s*not\s*satisfy\s*'?sentinel_for'?)");

  // view_interface errors
  std::regex view_interface_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*view_interface.*(?:incomplete|requires|missing))");

  // Common range issues
  std::regex common_range_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*common_range.*(?:not\s*satisfied|requires))");

  // Projection callable errors
  std::regex projection_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*(?:projection|callable).*(?:not\s*invocable|cannot\s*call))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Range concept not satisfied
    if (std::regex_search(line, matches, range_concept_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "RANGES";
      std::string concept_name = matches[5].str();
      diag.message = "Type '" + matches[3].str() + "' does not satisfy '" + concept_name + "'";

      // Provide specific help based on the concept
      if (concept_name == "range" || concept_name == "input_range") {
        diag.help_text = "To satisfy the range concept, the type must have:\n"
                         "   - begin() returning an iterator\n"
                         "   - end() returning a sentinel";
      } else if (concept_name == "view") {
        diag.help_text = "To satisfy view, the type must:\n"
                         "   - Be a range\n"
                         "   - Be movable\n"
                         "   - Have O(1) copy/move/assignment (or be non-copyable)";
      } else if (concept_name == "viewable_range") {
        diag.help_text = "The type must be either:\n"
                         "   - A view, or\n"
                         "   - An lvalue reference to a range";
      } else {
        diag.help_text = "Ensure the type meets all requirements for the " + concept_name + " concept.";
      }
      diagnostics.push_back(diag);
      continue;
    }

    // Missing begin/end
    if (std::regex_search(line, matches, begin_end_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "RANGES";
      diag.message = "Missing '" + matches[3].str() + "' for range type";
      diag.help_text = "Add a " + matches[3].str() + "() member function or a free function "
                       "findable via ADL. Consider using std::ranges::begin/end customization points.";
      diagnostics.push_back(diag);
      continue;
    }

    // Pipe operator errors
    if (std::regex_search(line, matches, pipe_operator_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "RANGES";
      diag.message = "Invalid ranges pipe operation";
      diag.help_text = "When using the | operator with views:\n"
                       "   - Left side must be a viewable_range\n"
                       "   - Right side must be a range adaptor\n"
                       "   - Include <ranges> header";
      diagnostics.push_back(diag);
      continue;
    }

    // Iterator concept errors
    if (std::regex_search(line, matches, iterator_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "RANGES";
      std::string iter_type = matches[4].str();
      diag.message = "Type '" + matches[3].str() + "' does not satisfy '" + iter_type + "_iterator'";
      diag.help_text = "Iterator requirements for " + iter_type + "_iterator:\n"
                       "   - Implement required operators (++, *, etc.)\n"
                       "   - Define iterator_traits or use iterator tag\n"
                       "   - Satisfy weaker iterator concepts first";
      diagnostics.push_back(diag);
      continue;
    }

    // Sentinel errors
    if (std::regex_search(line, matches, sentinel_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "RANGES";
      diag.message = "Type '" + matches[3].str() + "' does not satisfy sentinel_for";
      diag.help_text = "A sentinel must be:\n"
                       "   - Semiregular (default constructible, copyable)\n"
                       "   - Comparable with the iterator type (operator==)";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// CUDA/HIP GPU Compiler Error Parser
// ============================================================================

std::vector<diagnostic> parse_cuda_hip_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // NVCC errors: file.cu(123): error: identifier "foo" is undefined
  std::regex nvcc_error_regex(
      R"(([^\(]+)\((\d+)\):\s*error:\s*(.+))");

  // NVCC warnings
  std::regex nvcc_warning_regex(
      R"(([^\(]+)\((\d+)\):\s*warning:\s*(.+))");

  // CUDA kernel launch errors
  std::regex kernel_launch_regex(
      R"(([^\(]+)\((\d+)\):\s*error:.*(?:kernel|__global__|<<<|>>>).*)");

  // Device code errors (calling host from device)
  std::regex device_host_regex(
      R"(([^\(]+)\((\d+)\):\s*error:\s*(?:calling\s*a\s*__host__\s*function.*from\s*a\s*__(?:device|global)__|identifier.*is\s*undefined\s*in\s*device\s*code))");

  // Shared memory errors
  std::regex shared_memory_regex(
      R"(([^\(]+)\((\d+)\):\s*error:.*__shared__\s*(?:variable|memory|allocation))");

  // HIP errors (similar to NVCC but with HIP naming)
  std::regex hip_error_regex(
      R"(([^:]+):(\d+):(?:\d+:)?\s*error:.*(?:hip|HIP|__hip_|hipLaunch))");

  // CUDA architecture mismatch
  std::regex arch_regex(
      R"((?:error|warning):.*(?:sm_\d+|compute_\d+|arch=|gencode).*)");

  // Memory copy direction errors
  std::regex memcpy_regex(
      R"(([^\(]+)\((\d+)\):\s*error:.*(?:cudaMemcpy|hipMemcpy).*(?:direction|kind|invalid))");

  // PTX assembly errors
  std::regex ptx_regex(
      R"(ptxas\s*(?:error|fatal):?\s*(.+))");

  // CUDA runtime API errors
  std::regex cuda_api_regex(
      R"(([^\(]+)\((\d+)\):\s*error:.*(?:cuda(?:Error|GetLastError|DeviceSynchronize|Malloc|Free)|hip(?:Error|GetLastError)))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Device/host boundary errors (higher priority)
    if (std::regex_search(line, matches, device_host_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CUDA";
      diag.message = "Device/host boundary violation";
      diag.help_text = "Cannot call __host__ functions from __device__ or __global__ code.\n"
                       "Solutions:\n"
                       "   - Mark the function with __device__ or __host__ __device__\n"
                       "   - Use device-compatible alternatives\n"
                       "   - Move the call outside kernel code";
      diagnostics.push_back(diag);
      continue;
    }

    // Kernel launch errors
    if (std::regex_search(line, matches, kernel_launch_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CUDA";
      diag.message = "Kernel launch error";
      diag.help_text = "Check kernel launch syntax: kernel<<<grid, block>>>(args)\n"
                       "   - grid: number of blocks (dim3 or int)\n"
                       "   - block: threads per block (dim3 or int)\n"
                       "   - Ensure function is declared __global__";
      diagnostics.push_back(diag);
      continue;
    }

    // Shared memory errors
    if (std::regex_search(line, matches, shared_memory_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CUDA";
      diag.message = "Shared memory error";
      diag.help_text = "__shared__ memory restrictions:\n"
                       "   - Must be declared inside __device__ or __global__ function\n"
                       "   - Static size must be known at compile time (or use extern)\n"
                       "   - Limited to GPU shared memory size (typically 48KB)";
      diagnostics.push_back(diag);
      continue;
    }

    // Architecture mismatch
    if (std::regex_search(line, matches, arch_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::WARNING;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "CUDA-ARCH";
      diag.message = "CUDA architecture configuration issue";
      diag.help_text = "Ensure the target architecture matches your GPU:\n"
                       "   - Use -arch=sm_XX where XX matches your GPU\n"
                       "   - Common: sm_60 (Pascal), sm_70 (Volta), sm_80 (Ampere)\n"
                       "   - Check with: nvidia-smi --query-gpu=compute_cap --format=csv";
      diagnostics.push_back(diag);
      continue;
    }

    // PTX errors
    if (std::regex_search(line, matches, ptx_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "CUDA-PTX";
      diag.message = "PTX assembly error: " + matches[1].str();
      diag.help_text = "PTX errors usually indicate:\n"
                       "   - Register pressure (too many variables)\n"
                       "   - Invalid inline assembly\n"
                       "   - Architecture-incompatible features";
      diagnostics.push_back(diag);
      continue;
    }

    // Generic NVCC errors
    if (std::regex_search(line, matches, nvcc_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CUDA";
      diag.message = matches[3].str();
      diagnostics.push_back(diag);
      continue;
    }

    // HIP errors
    if (std::regex_search(line, matches, hip_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "HIP";
      diag.message = line;
      diag.help_text = "HIP is AMD's GPU programming interface. "
                       "Similar to CUDA but with hip* prefixes.";
      diagnostics.push_back(diag);
      continue;
    }

    // NVCC warnings
    if (std::regex_search(line, matches, nvcc_warning_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::WARNING;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "CUDA";
      diag.message = matches[3].str();
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Intel ICC/ICX Compiler Error Parser
// ============================================================================

std::vector<diagnostic> parse_intel_compiler_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // Classic ICC error format: file.cpp(123): error: message
  std::regex icc_error_regex(
      R"(([^\(]+)\((\d+)\):\s*error(?:\s*#(\d+))?:\s*(.+))");

  // Classic ICC warning format
  std::regex icc_warning_regex(
      R"(([^\(]+)\((\d+)\):\s*warning(?:\s*#(\d+))?:\s*(.+))");

  // ICX (Intel oneAPI) uses Clang-like format: file.cpp:123:45: error: message
  std::regex icx_error_regex(
      R"(([^:]+):(\d+):(\d+):\s*error:\s*(.+))");

  // ICX warning
  std::regex icx_warning_regex(
      R"(([^:]+):(\d+):(\d+):\s*warning:\s*(.+))");

  // Intel-specific remarks (optimization reports)
  std::regex remark_regex(
      R"(([^\(]+)\((\d+)\):\s*remark(?:\s*#(\d+))?:\s*(.+))");

  // Vectorization reports
  std::regex vec_report_regex(
      R"(([^\(]+)\((\d+)\):\s*(?:remark|warning).*(?:LOOP\s*WAS|vectoriz|unroll))");

  // OpenMP errors (common with Intel compilers)
  std::regex openmp_regex(
      R"(([^\(]+)\((\d+)\):\s*error.*(?:omp|OMP|openmp|OpenMP)\s*(.+))");

  // Intel MKL/IPP linking errors
  std::regex mkl_regex(
      R"((?:error|undefined).*(?:mkl_|MKL_|ipp|IPP)\w+)");

  // SIMD intrinsics errors
  std::regex simd_regex(
      R"(([^\(]+)\((\d+)\):\s*error:.*(?:_mm|__m\d+|_mm\d+|avx|AVX|sse|SSE))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // OpenMP errors (higher priority)
    if (std::regex_search(line, matches, openmp_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "ICC-OMP";
      diag.message = "OpenMP error: " + matches[3].str();
      diag.help_text = "Common OpenMP issues:\n"
                       "   - Ensure -fopenmp (ICX) or -qopenmp (ICC) flag is used\n"
                       "   - Check pragma syntax: #pragma omp parallel for\n"
                       "   - Verify variable scoping (private, shared, reduction)";
      diagnostics.push_back(diag);
      continue;
    }

    // SIMD errors
    if (std::regex_search(line, matches, simd_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "ICC-SIMD";
      diag.message = "SIMD intrinsic error";
      diag.help_text = "For Intel SIMD intrinsics:\n"
                       "   - Include <immintrin.h> for all intrinsics\n"
                       "   - Ensure target architecture supports the instruction set\n"
                       "   - Use -march=native or specific -mavx2, -mavx512f, etc.";
      diagnostics.push_back(diag);
      continue;
    }

    // MKL/IPP linking
    if (std::regex_search(line, matches, mkl_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "ICC-LIB";
      diag.message = "Intel library linking error";
      diag.help_text = "For Intel MKL/IPP:\n"
                       "   - Use Intel Link Advisor for correct libraries\n"
                       "   - Set MKLROOT or IPPROOT environment variables\n"
                       "   - Try: -lmkl_intel_lp64 -lmkl_sequential -lmkl_core";
      diagnostics.push_back(diag);
      continue;
    }

    // Vectorization reports (as notes)
    if (std::regex_search(line, matches, vec_report_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::NOTE;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = "ICC-VEC";
      diag.message = "Vectorization report";
      diag.help_text = "Use -qopt-report for detailed optimization reports.";
      diagnostics.push_back(diag);
      continue;
    }

    // Classic ICC errors
    if (std::regex_search(line, matches, icc_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = matches[3].matched ? ("ICC-" + matches[3].str()) : "ICC";
      diag.message = matches[4].str();
      diagnostics.push_back(diag);
      continue;
    }

    // ICX errors (Clang-like)
    if (std::regex_search(line, matches, icx_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = std::stoi(matches[3].str());
      diag.code = "ICX";
      diag.message = matches[4].str();
      diagnostics.push_back(diag);
      continue;
    }

    // ICC warnings
    if (std::regex_search(line, matches, icc_warning_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::WARNING;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = 0;
      diag.code = matches[3].matched ? ("ICC-" + matches[3].str()) : "ICC";
      diag.message = matches[4].str();
      diagnostics.push_back(diag);
      continue;
    }

    // ICX warnings
    if (std::regex_search(line, matches, icx_warning_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::WARNING;
      diag.file_path = matches[1].str();
      diag.line_number = std::stoi(matches[2].str());
      diag.column_number = std::stoi(matches[3].str());
      diag.code = "ICX";
      diag.message = matches[4].str();
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Precompiled Header (PCH) Error Parser
// ============================================================================

std::vector<diagnostic> parse_pch_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // GCC/Clang PCH errors
  // error: pch file built from a different branch
  std::regex pch_mismatch_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*error:.*(?:pch|PCH|precompiled\s*header).*(?:built\s*from|different|mismatch|invalid|corrupt))");

  // PCH file not found
  std::regex pch_not_found_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|fatal\s*error):.*(?:cannot\s*find|not\s*found|no\s*such).*(?:\.pch|\.gch|precompiled))");

  // PCH was created with different compiler version
  std::regex pch_version_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*error:.*(?:pch|precompiled).*(?:version|compiler))");

  // PCH created with different options
  std::regex pch_options_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*error:.*(?:pch|precompiled).*(?:option|flag|setting))");

  // MSVC PCH errors
  // error C1859: unexpected precompiled header error
  std::regex msvc_pch_error_regex(
      R"(([^\(]+)(?:\((\d+)\))?:\s*error\s+(C1[89]\d\d):\s*(.+))");

  // MSVC: cannot find pch
  std::regex msvc_pch_not_found_regex(
      R"(([^\(]+)(?:\((\d+)\))?:\s*fatal\s*error\s+C1083:.*(?:pch|\.pch))");

  // Clang: -include-pch errors
  std::regex clang_include_pch_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*error:.*-include-pch.*)");

  // stdafx.h / pch.h missing (common Windows pattern)
  std::regex stdafx_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|fatal\s*error):.*(?:stdafx\.h|pch\.h).*(?:not\s*found|No\s*such|cannot\s*open))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // stdafx/pch.h not found (most common)
    if (std::regex_search(line, matches, stdafx_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "PCH";
      diag.message = "Precompiled header file not found";
      diag.help_text = "Common solutions:\n"
                       "   - Create stdafx.h/pch.h with common includes\n"
                       "   - Disable PCH in project settings\n"
                       "   - MSVC: Properties > C/C++ > Precompiled Headers > Not Using";
      diagnostics.push_back(diag);
      continue;
    }

    // PCH version/compiler mismatch
    if (std::regex_search(line, matches, pch_version_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "PCH";
      diag.message = "Precompiled header version mismatch";
      diag.help_text = "The PCH was built with a different compiler version.\n"
                       "Solutions:\n"
                       "   - Clean and rebuild the project\n"
                       "   - Delete .pch/.gch files manually\n"
                       "   - Rebuild PCH: MSVC uses /Yc, GCC/Clang auto-regenerate";
      diagnostics.push_back(diag);
      continue;
    }

    // PCH compiler options mismatch
    if (std::regex_search(line, matches, pch_options_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "PCH";
      diag.message = "Precompiled header built with different options";
      diag.help_text = "PCH must be built with same compiler options.\n"
                       "Ensure consistent:\n"
                       "   - Optimization level (-O0/-O2/-O3)\n"
                       "   - C++ standard (-std=c++17/20)\n"
                       "   - Include paths\n"
                       "   - Preprocessor definitions";
      diagnostics.push_back(diag);
      continue;
    }

    // PCH file not found
    if (std::regex_search(line, matches, pch_not_found_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "PCH";
      diag.message = "Precompiled header file not found";
      diag.help_text = "The .pch/.gch file doesn't exist.\n"
                       "   - Build PCH first (MSVC: /Yc flag on stdafx.cpp)\n"
                       "   - Check the PCH output path\n"
                       "   - Ensure PCH file is generated before other files compile";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC PCH errors
    if (std::regex_search(line, matches, msvc_pch_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = matches[3].str();
      diag.message = matches[4].str();

      // Add specific help for known MSVC PCH error codes
      std::string code = matches[3].str();
      if (code == "C1859") {
        diag.help_text = "Unexpected PCH error. Try:\n"
                         "   - Clean solution and rebuild\n"
                         "   - Delete .pch files in intermediate directory\n"
                         "   - Check for header file corruption";
      } else if (code == "C1850" || code == "C1851" || code == "C1852" || code == "C1853") {
        diag.help_text = "PCH file is corrupt or incompatible.\n"
                         "   - Delete the .pch file\n"
                         "   - Rebuild the project";
      } else {
        diag.help_text = "MSVC precompiled header error.\n"
                         "   - Try disabling PCH temporarily\n"
                         "   - Check PCH settings in project properties";
      }
      diagnostics.push_back(diag);
      continue;
    }

    // General PCH mismatch
    if (std::regex_search(line, matches, pch_mismatch_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "PCH";
      diag.message = "Precompiled header mismatch or corruption";
      diag.help_text = "Delete PCH files and rebuild:\n"
                       "   - GCC: delete *.gch files\n"
                       "   - MSVC: delete *.pch files\n"
                       "   - Clang: delete *.pch files";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

// ============================================================================
// Cross-Compilation and ABI Mismatch Error Parser
// ============================================================================

std::vector<diagnostic> parse_abi_errors(const std::string &error_output) {
  std::vector<diagnostic> diagnostics;

  // ABI version mismatch
  // warning: ABI version mismatch
  std::regex abi_version_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|warning):.*ABI\s*(?:version\s*)?(?:mismatch|incompatib|differ))");

  // C++ ABI tag issues (GCC)
  std::regex abi_tag_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|warning):.*(?:_GLIBCXX_USE_CXX11_ABI|abi_tag))");

  // Symbol visibility issues
  std::regex visibility_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|warning):.*(?:visibility|hidden|default).*)");

  // Architecture mismatch
  std::regex arch_mismatch_regex(
      R"((?:error|warning):.*(?:incompatible|mismatch).*(?:architecture|arch|x86_64|i386|arm|aarch64|32.bit|64.bit))");

  // Cross-compile sysroot errors
  std::regex sysroot_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|fatal\s*error):.*(?:sysroot|--sysroot|cannot\s*find.*target))");

  // Target triple issues
  std::regex triple_regex(
      R"((?:error|warning):.*(?:target\s*triple|unknown\s*target|-target))");

  // Linking objects from different ABIs
  std::regex link_abi_regex(
      R"((?:error|warning):.*(?:linking|link).*(?:different|incompatible).*(?:ABI|standard|libstdc\+\+|libc\+\+))");

  // _ITERATOR_DEBUG_LEVEL mismatch (MSVC)
  std::regex iterator_debug_regex(
      R"((?:error|warning).*_ITERATOR_DEBUG_LEVEL.*(?:mismatch|different|inconsistent))");

  // Runtime library mismatch (MSVC /MD /MT /MDd /MTd)
  std::regex runtime_lib_regex(
      R"((?:error|warning).*/M[DT]d?\s*.*(?:mismatch|conflict|inconsistent))");

  // libstdc++ vs libc++ mixing
  std::regex stdlib_regex(
      R"((?:error|warning):.*(?:libstdc\+\+|libc\+\+).*(?:mismatch|incompatible|undefined))");

  // Calling convention mismatch
  std::regex calling_conv_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|warning):.*(?:calling\s*convention|__cdecl|__stdcall|__fastcall|__vectorcall))");

  // Structure packing/alignment issues
  std::regex packing_regex(
      R"(([^:]+):(?:(\d+):)?(?:\d+:)?\s*(?:error|warning):.*(?:#pragma\s*pack|__attribute__.*packed|alignment|sizeof.*differ))");

  std::string line;
  std::istringstream stream(error_output);

  while (std::getline(stream, line)) {
    std::smatch matches;

    // C++11 ABI tag (GCC dual ABI)
    if (std::regex_search(line, matches, abi_tag_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "ABI";
      diag.message = "C++ ABI compatibility issue (_GLIBCXX_USE_CXX11_ABI)";
      diag.help_text = "GCC uses dual ABI since GCC 5.1 for std::string/std::list.\n"
                       "Solutions:\n"
                       "   - Ensure all libraries use same ABI:\n"
                       "     #define _GLIBCXX_USE_CXX11_ABI 1 (new ABI, default)\n"
                       "     #define _GLIBCXX_USE_CXX11_ABI 0 (old ABI)\n"
                       "   - Rebuild all dependent libraries with same setting\n"
                       "   - Check if prebuilt libraries were built with old ABI";
      diagnostics.push_back(diag);
      continue;
    }

    // Architecture mismatch
    if (std::regex_search(line, matches, arch_mismatch_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "ABI-ARCH";
      diag.message = "Architecture mismatch";
      diag.help_text = "Cannot mix 32-bit and 64-bit code.\n"
                       "   - Check all libraries match target architecture\n"
                       "   - Use -m32 or -m64 consistently\n"
                       "   - Cross-compiling: set correct --target\n"
                       "   - MSVC: check Platform setting (Win32/x64/ARM64)";
      diagnostics.push_back(diag);
      continue;
    }

    // MSVC runtime library mismatch
    if (std::regex_search(line, matches, runtime_lib_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "ABI-MSVC";
      diag.message = "MSVC runtime library mismatch";
      diag.help_text = "All code must use the same runtime:\n"
                       "   /MD  - Dynamic release (msvcrt.dll)\n"
                       "   /MDd - Dynamic debug (msvcrtd.dll)\n"
                       "   /MT  - Static release\n"
                       "   /MTd - Static debug\n"
                       "Rebuild all libraries with consistent setting.";
      diagnostics.push_back(diag);
      continue;
    }

    // Iterator debug level (MSVC)
    if (std::regex_search(line, matches, iterator_debug_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "ABI-MSVC";
      diag.message = "_ITERATOR_DEBUG_LEVEL mismatch";
      diag.help_text = "Debug iterator settings must match:\n"
                       "   _ITERATOR_DEBUG_LEVEL=0 (Release)\n"
                       "   _ITERATOR_DEBUG_LEVEL=2 (Debug)\n"
                       "Don't mix Debug and Release libraries.";
      diagnostics.push_back(diag);
      continue;
    }

    // libstdc++ vs libc++
    if (std::regex_search(line, matches, stdlib_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "ABI";
      diag.message = "C++ standard library mismatch (libstdc++/libc++)";
      diag.help_text = "Cannot mix libstdc++ and libc++ in same binary.\n"
                       "   - Use -stdlib=libstdc++ or -stdlib=libc++ consistently\n"
                       "   - Rebuild all libraries with same stdlib\n"
                       "   - macOS default is libc++, Linux default is libstdc++";
      diagnostics.push_back(diag);
      continue;
    }

    // Cross-compilation sysroot
    if (std::regex_search(line, matches, sysroot_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "CROSS";
      diag.message = "Cross-compilation sysroot error";
      diag.help_text = "Set the correct sysroot for cross-compiling:\n"
                       "   --sysroot=/path/to/target/sysroot\n"
                       "   - Contains target system headers and libraries\n"
                       "   - CMAKE_SYSROOT in CMake toolchain file";
      diagnostics.push_back(diag);
      continue;
    }

    // Target triple
    if (std::regex_search(line, matches, triple_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "CROSS";
      diag.message = "Invalid or unknown target triple";
      diag.help_text = "Target triple format: arch-vendor-os[-env]\n"
                       "Examples:\n"
                       "   x86_64-unknown-linux-gnu\n"
                       "   aarch64-linux-android\n"
                       "   arm-none-eabi (bare metal)\n"
                       "Use: clang -target <triple> or --target=<triple>";
      diagnostics.push_back(diag);
      continue;
    }

    // Calling convention
    if (std::regex_search(line, matches, calling_conv_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "ABI";
      diag.message = "Calling convention mismatch";
      diag.help_text = "Function calling conventions must match:\n"
                       "   __cdecl    - C default, caller cleans stack\n"
                       "   __stdcall  - Win32 API, callee cleans stack\n"
                       "   __fastcall - Uses registers\n"
                       "Check function declarations in headers match libraries.";
      diagnostics.push_back(diag);
      continue;
    }

    // Structure packing
    if (std::regex_search(line, matches, packing_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::WARNING;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "ABI";
      diag.message = "Structure alignment/packing issue";
      diag.help_text = "Structure layout must match across boundaries:\n"
                       "   - Use consistent #pragma pack settings\n"
                       "   - Explicit alignment: alignas(N)\n"
                       "   - Check sizeof() matches expected values\n"
                       "   - Binary protocols may need packed structures";
      diagnostics.push_back(diag);
      continue;
    }

    // Generic ABI version mismatch
    if (std::regex_search(line, matches, abi_version_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "ABI";
      diag.message = "ABI version mismatch";
      diag.help_text = "Binary interface versions don't match.\n"
                       "   - Rebuild all components with same compiler version\n"
                       "   - Check library versions are compatible\n"
                       "   - Ensure consistent compiler flags";
      diagnostics.push_back(diag);
      continue;
    }

    // Link-time ABI mismatch
    if (std::regex_search(line, matches, link_abi_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;
      diag.code = "ABI";
      diag.message = "Linking objects with incompatible ABIs";
      diag.help_text = "Object files were compiled with different ABIs.\n"
                       "   - Rebuild all objects with same compiler/settings\n"
                       "   - Check third-party library compatibility\n"
                       "   - Don't mix Debug/Release builds";
      diagnostics.push_back(diag);
      continue;
    }

    // Symbol visibility
    if (std::regex_search(line, matches, visibility_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::WARNING;
      diag.file_path = matches[1].str();
      diag.line_number = matches[2].matched ? std::stoi(matches[2].str()) : 0;
      diag.column_number = 0;
      diag.code = "ABI";
      diag.message = "Symbol visibility issue";
      diag.help_text = "Symbol visibility affects linking:\n"
                       "   - __attribute__((visibility(\"default\"))) - exported\n"
                       "   - __attribute__((visibility(\"hidden\"))) - internal\n"
                       "   - -fvisibility=hidden with explicit exports is best practice";
      diagnostics.push_back(diag);
      continue;
    }
  }

  return diagnostics;
}

} // namespace cforge