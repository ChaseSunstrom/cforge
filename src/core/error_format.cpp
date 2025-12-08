/**
 * @file error_format.cpp
 * @brief Implementation of error formatting utilities
 */

#include "core/error_format.hpp"
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
    ss << fmt::format(fg(fmt::color::cyan), "  --> ");
    ss << diag.file_path;
    if (diag.line_number > 0) {
      ss << ":" << diag.line_number;
      if (diag.column_number > 0) {
        ss << ":" << diag.column_number;
      }
    }
    ss << "\n";
  }

  // Code snippet with line numbers
  if (!diag.line_content.empty() && diag.line_number > 0) {
    // Calculate gutter width based on line number
    int gutter_width = std::to_string(diag.line_number).length();
    if (gutter_width < 2)
      gutter_width = 2;

    // Empty line before code
    ss << fmt::format(fg(fmt::color::cyan), "{:>{}} |\n", "", gutter_width);

    // The actual code line
    ss << fmt::format(fg(fmt::color::cyan), "{:>{}} | ", diag.line_number,
                      gutter_width);
    ss << diag.line_content << "\n";

    // The error pointer line
    ss << fmt::format(fg(fmt::color::cyan), "{:>{}} | ", "", gutter_width);

    if (diag.column_number > 0) {
      size_t col = static_cast<size_t>(diag.column_number - 1);
      size_t token_length = 1;

      // Try to find the token length
      if (col < diag.line_content.length()) {
        if (std::isalnum(diag.line_content[col]) ||
            diag.line_content[col] == '_') {
          size_t start = col;
          size_t end = col;

          // Find token boundaries
          while (end < diag.line_content.length() &&
                 (std::isalnum(diag.line_content[end]) ||
                  diag.line_content[end] == '_')) {
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
    for (size_t i = 0; i < diag.fixes.size() && i < 3;
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
          int current_line = 0;

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
          int current_line = 0;

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
        int code_num = std::stoi(error_code.substr(3));
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
        int ref_line = matches[2].matched ? std::stoi(matches[2].str()) : 0;

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
  int instantiation_depth = 0;
  const int MAX_INSTANTIATION_DEPTH = 3; // Only show top 3 levels

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
      int code_num = std::stoi(diag.code.substr(1));
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
          size_t last_slash = file.find_last_of("/\\");
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
        size_t pos = base_code.rfind("-");
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
        int count = existing.occurrence_count + 1;
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
  std::map<std::string, int> category_counts;

  for (const auto &diag : diagnostics) {
    int count = diag.occurrence_count;

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
  size_t at_pos = clean_symbol.find('@');
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
  size_t template_start = type_name.find('<');
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
static int levenshtein_distance(const std::string &s1, const std::string &s2) {
  const size_t m = s1.size();
  const size_t n = s2.size();

  if (m == 0)
    return static_cast<int>(n);
  if (n == 0)
    return static_cast<int>(m);

  std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));

  for (size_t i = 0; i <= m; ++i)
    dp[i][0] = static_cast<int>(i);
  for (size_t j = 0; j <= n; ++j)
    dp[0][j] = static_cast<int>(j);

  for (size_t i = 1; i <= m; ++i) {
    for (size_t j = 1; j <= n; ++j) {
      int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
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
                         int max_distance) {

  std::vector<std::pair<std::string, int>> matches;

  for (const auto &candidate : available_identifiers) {
    // Skip if length difference is too large
    int len_diff = std::abs(static_cast<int>(unknown_identifier.length()) -
                            static_cast<int>(candidate.length()));
    if (len_diff > max_distance)
      continue;

    int distance = levenshtein_distance(unknown_identifier, candidate);
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
      size_t comment_pos = line.find("//");
      if (comment_pos != std::string::npos) {
        line = line.substr(0, comment_pos);
      }

      // Trim trailing whitespace
      size_t end = line.find_last_not_of(" \t");
      if (end != std::string::npos) {
        fix.start_column =
            static_cast<int>(end + 2); // After last non-space char
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

  return suggestions;
}

} // namespace cforge