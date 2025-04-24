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
#define FMT_HEADER_ONLY
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

  // Format the diagnostics to a string
  std::stringstream ss;
  for (const auto &diag : filtered_diagnostics) {
    // Format diagnostic to string instead of printing directly
    ss << format_diagnostic_to_string(diag);
  }

  return ss.str();
}

// Add a new function to format a diagnostic to string
std::string format_diagnostic_to_string(const diagnostic &diag) {
  std::stringstream ss;

  // Determine the level-specific formatting and colors
  std::string level_str;
  std::string level_label;
  fmt::color level_color;

  switch (diag.level) {
  case diagnostic_level::ERROR:
    level_str = "error";
    level_label = "ERROR";
    level_color = fmt::color::crimson;
    break;
  case diagnostic_level::WARNING:
    level_str = "warning";
    level_label = "WARN";
    level_color = fmt::color::gold;
    break;
  case diagnostic_level::NOTE:
    level_str = "note";
    level_label = "NOTE";
    level_color = fmt::color::cyan;
    break;
  case diagnostic_level::HELP:
    level_str = "help";
    level_label = "HELP";
    level_color = fmt::color::lime_green;
    break;
  }

  // Format the error header - first line with error code and message
  // Use color formatting
  ss << fmt::format(fg(level_color), "{}", level_label)
     << fmt::format(fg(fmt::color::white), "[")
     << fmt::format(fg(fmt::color::light_blue), "{}", diag.code)
     << fmt::format(fg(fmt::color::white), "]: ")
     << fmt::format(fg(fmt::color::white), "{}", diag.message) << "\n";

  // Format file location information if available - second line with path
  if (!diag.file_path.empty()) {
    ss << fmt::format(fg(fmt::color::light_blue), " --> ")
       << fmt::format(fg(fmt::color::white), "{}", diag.file_path);

    if (diag.line_number > 0) {
      ss << fmt::format(fg(fmt::color::white), ":")
         << fmt::format(fg(fmt::color::yellow), "{}", diag.line_number);
      if (diag.column_number > 0) {
        ss << fmt::format(fg(fmt::color::white), ":")
           << fmt::format(fg(fmt::color::yellow), "{}", diag.column_number);
      }
    }
    ss << "\n";
  }

  // Add visual spacing
  if (diag.line_number > 0) {
    ss << fmt::format(fg(fmt::color::light_blue), "  |\n");
  }

  // Format the code snippet if available with line number and content
  if (!diag.line_content.empty()) {
    // Print line number and code
    ss << fmt::format(fg(fmt::color::yellow), "{:3}", diag.line_number)
       << fmt::format(fg(fmt::color::light_blue), " | ")
       << fmt::format(fg(fmt::color::white), "{}", diag.line_content) << "\n";

    // Print error pointer with carets pointing to the relevant part
    ss << fmt::format(fg(fmt::color::light_blue), "    | ");

    // If we have a column number, show carets at that point
    if (diag.column_number > 0) {
      size_t token_start = std::max(0, diag.column_number - 1);
      size_t token_length =
          1; // Default to 1 character if we can't determine the length

      // Try to determine the token length if we have an identifier
      if (token_start < diag.line_content.length()) {
        if (std::isalnum(diag.line_content[token_start]) ||
            diag.line_content[token_start] == '_') {
          // Find beginning of the identifier
          while (token_start > 0 &&
                 (std::isalnum(diag.line_content[token_start - 1]) ||
                  diag.line_content[token_start - 1] == '_')) {
            token_start--;
          }

          // Find the end of the identifier
          size_t token_end = token_start;
          while (token_end < diag.line_content.length() &&
                 (std::isalnum(diag.line_content[token_end]) ||
                  diag.line_content[token_end] == '_')) {
            token_end++;
          }

          token_length = token_end - token_start;
        }
      }

      // Print spaces up to the token start
      for (size_t i = 0; i < token_start; i++) {
        ss << " ";
      }

      // Print carets under the entire token in the error color
      std::string carets(token_length, '^');
      ss << fmt::format(fg(level_color), "{}", carets) << "\n";
    } else {
      // If no column, just show a caret at the beginning of the line
      ss << fmt::format(fg(level_color), "^") << "\n";
    }
  }

  // Add another visual spacing
  ss << fmt::format(fg(fmt::color::light_blue), "  |\n");

  // Format help text if available to provide context and suggestions
  if (!diag.help_text.empty()) {
    ss << fmt::format(fg(fmt::color::lime_green), "help: ")
       << fmt::format(fg(fmt::color::white), "{}", diag.help_text) << "\n";
  } else {
    // Generate helpful suggestions based on error type if no explicit help text
    if (diag.level == diagnostic_level::ERROR) {
      // Common error types and helpful suggestions
      std::string help_text;
      if (diag.message.find("missing type specifier") != std::string::npos) {
        help_text = "every declaration needs a type - add an appropriate type "
                    "before the variable or function";
      } else if (diag.message.find("missing ';'") != std::string::npos) {
        help_text = "statement requires a semicolon at the end";
      } else if (diag.message.find("undeclared") != std::string::npos ||
                 diag.message.find("not declared") != std::string::npos) {
        help_text = "make sure this identifier is declared before use or check "
                    "for typos";
      } else if (diag.message.find("no matching") != std::string::npos) {
        help_text = "no function matches these argument types - check "
                    "parameters or add appropriate overload";
      } else if (diag.message.find("cannot convert") != std::string::npos) {
        help_text = "types are incompatible - add an explicit cast or use "
                    "compatible types";
      }

      if (!help_text.empty()) {
        ss << fmt::format(fg(fmt::color::lime_green), "help: ")
           << fmt::format(fg(fmt::color::white), "{}", help_text) << "\n";
      }
    } else if (diag.level == diagnostic_level::WARNING) {
      std::string help_text;
      if (diag.message.find("unused") != std::string::npos) {
        help_text =
            "consider using this variable or remove it to avoid warnings";
      } else if (diag.message.find("deprecated") != std::string::npos) {
        help_text = "this feature is deprecated - consider using a more modern "
                    "alternative";
      }

      if (!help_text.empty()) {
        ss << fmt::format(fg(fmt::color::lime_green), "help: ")
           << fmt::format(fg(fmt::color::white), "{}", help_text) << "\n";
      }
    }
  }

  // Add a newline for spacing between diagnostics
  ss << "\n";

  return ss.str();
}

void print_diagnostic(const diagnostic &diagnostic) {
  // Determine the level-specific formatting
  fmt::text_style level_style;
  std::string level_str;
  std::string level_label;

  switch (diagnostic.level) {
  case diagnostic_level::ERROR:
    level_style = ERROR_COLOR | fmt::emphasis::bold;
    level_str = "error";
    level_label = "ERROR";
    break;
  case diagnostic_level::WARNING:
    level_style = WARNING_COLOR | fmt::emphasis::bold;
    level_str = "warning";
    level_label = "WARN";
    break;
  case diagnostic_level::NOTE:
    level_style = NOTE_COLOR | fmt::emphasis::bold;
    level_str = "note";
    level_label = "NOTE";
    break;
  case diagnostic_level::HELP:
    level_style = HELP_COLOR | fmt::emphasis::bold;
    level_str = "help";
    level_label = "HELP";
    break;
  }

  // Print the error header
  fmt::print(level_style, "{}[{}]", level_label, diagnostic.code);
  fmt::print(": {}\n", diagnostic.message);

  // Print file location information if available
  if (!diagnostic.file_path.empty()) {
    fmt::print(LOCATION_COLOR | fmt::emphasis::bold, " --> ");
    fmt::print("{}", diagnostic.file_path);

    if (diagnostic.line_number > 0) {
      fmt::print(":{}", diagnostic.line_number);
      if (diagnostic.column_number > 0) {
        fmt::print(":{}", diagnostic.column_number);
      }
    }
    fmt::print("\n");
  }

  // Print the code snippet if available
  if (!diagnostic.line_content.empty()) {
    // Print line number
    fmt::print(LOCATION_COLOR, "{:>4} | ", diagnostic.line_number);

    if (diagnostic.column_number > 0) {
      // Find the start of the relevant token
      size_t token_start = std::max(0, diagnostic.column_number - 1);
      size_t token_length =
          1; // Default to 1 if we can't determine the actual length

      // Try to find the token boundary
      if (token_start < diagnostic.line_content.length()) {
        // If it's an identifier, find the whole word
        if (std::isalnum(diagnostic.line_content[token_start]) ||
            diagnostic.line_content[token_start] == '_') {
          // Find the beginning of the identifier
          while (token_start > 0 &&
                 (std::isalnum(diagnostic.line_content[token_start - 1]) ||
                  diagnostic.line_content[token_start - 1] == '_')) {
            token_start--;
          }

          // Find the end of the identifier
          size_t token_end = token_start;
          while (token_end < diagnostic.line_content.length() &&
                 (std::isalnum(diagnostic.line_content[token_end]) ||
                  diagnostic.line_content[token_end] == '_')) {
            token_end++;
          }

          token_length = token_end - token_start;
        }
      }

      // Print the line with the problematic token highlighted
      std::string before = diagnostic.line_content.substr(0, token_start);
      std::string token =
          diagnostic.line_content.substr(token_start, token_length);
      std::string after =
          diagnostic.line_content.substr(token_start + token_length);

      fmt::print("{}", before);
      fmt::print(HIGHLIGHT_COLOR | fmt::emphasis::bold, "{}", token);
      fmt::print("{}\n", after);

      // Print the error caret pointing to the column
      fmt::print(LOCATION_COLOR, "{:>4} | ", "");

      // Print spaces up to the token start
      for (size_t i = 0; i < token_start; i++) {
        fmt::print(" ");
      }

      // Print carets under the entire token
      for (size_t i = 0; i < token_length; i++) {
        fmt::print(CARET_COLOR | fmt::emphasis::bold, "^");
      }

      fmt::print("\n");
    } else {
      // Simple line printing without highlighting if we don't have column info
      fmt::print("{}\n", diagnostic.line_content);
    }
  }

  // Print help text if available
  if (!diagnostic.help_text.empty()) {
    fmt::print(HELP_COLOR | fmt::emphasis::bold, "help: {}\n",
               diagnostic.help_text);
  }

  // Add a newline for spacing
  fmt::print("\n");
}

std::vector<diagnostic> extract_diagnostics(const std::string &error_output) {
  std::vector<diagnostic> all_diagnostics;

  // Try parsing errors for different compilers/tools
  auto gcc_clang_diags = parse_gcc_clang_errors(error_output);
  auto msvc_diags = parse_msvc_errors(error_output);
  auto cmake_diags = parse_cmake_errors(error_output);
  auto ninja_diags = parse_ninja_errors(error_output);
  auto linker_diags = parse_linker_errors(error_output);
  auto cpack_diags = parse_cpack_errors(error_output); // Add CPack errors

  // Combine all diagnostics
  all_diagnostics.insert(all_diagnostics.end(), gcc_clang_diags.begin(),
                         gcc_clang_diags.end());
  all_diagnostics.insert(all_diagnostics.end(), msvc_diags.begin(),
                         msvc_diags.end());
  all_diagnostics.insert(all_diagnostics.end(), cmake_diags.begin(),
                         cmake_diags.end());
  all_diagnostics.insert(all_diagnostics.end(), ninja_diags.begin(),
                         ninja_diags.end());
  all_diagnostics.insert(all_diagnostics.end(), linker_diags.begin(),
                         linker_diags.end());
  all_diagnostics.insert(all_diagnostics.end(), cpack_diags.begin(),
                         cpack_diags.end()); // Add CPack diagnostics

  return all_diagnostics;
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

  // Regular expressions for linker errors
  // LLD-style linker error: "lld-link: error: undefined symbol: ..."
  std::regex lld_error_regex(R"(lld-link:\s*error:\s*(.*))");

  // ld-style linker error: "ld: undefined symbol: ..."
  std::regex ld_error_regex(R"(ld(\.\S+)?:\s*.*error:\s*(.*))");

  // MSVC linker error: "LINK : fatal error LNK1181: cannot open input file
  // '...'"
  std::regex msvc_link_error_regex(
      R"(LINK\s*:\s*(?:fatal\s*)?error\s*(LNK\d+):\s*(.*))");

  // Generic reference pattern: "referenced by file.obj"
  std::regex reference_regex(R"(>>>\s*referenced by\s*([^:\n]+))");

  // Try to extract specific error codes
  std::regex linker_code_regex(R"(error\s+(\w+\d+):)");

  std::string line;
  std::istringstream stream(error_output);
  std::string current_error;
  std::string current_file;

  while (std::getline(stream, line)) {
    std::smatch matches;

    // Try to match LLD linker errors
    if (std::regex_search(line, matches, lld_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      std::string message = matches[1].str();
      diag.message = message;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;

      // Try to extract error code from the message
      std::smatch code_match;
      if (std::regex_search(message, code_match, linker_code_regex)) {
        diag.code = code_match[1].str();
      } else {
        diag.code = "LLD";

        // Add specific context based on the message
        if (message.find("undefined symbol") != std::string::npos) {
          diag.code += "-UNDEFINED";
        } else if (message.find("duplicate symbol") != std::string::npos) {
          diag.code += "-DUPLICATE";
        } else if (message.find("cannot open") != std::string::npos) {
          diag.code += "-NOTFOUND";
        } else if (message.find("unresolved") != std::string::npos) {
          diag.code += "-UNRESOLVED";
        }
      }

      diagnostics.push_back(diag);
      current_error = message;
    }
    // Try to match ld linker errors
    else if (std::regex_search(line, matches, ld_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;
      std::string message = matches[2].str();
      diag.message = message;
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;

      // Try to extract error code from the message
      std::smatch code_match;
      if (std::regex_search(message, code_match, linker_code_regex)) {
        diag.code = code_match[1].str();
      } else {
        diag.code = "LD";

        // Add specific context based on the message
        if (message.find("undefined reference") != std::string::npos ||
            message.find("undefined symbol") != std::string::npos) {
          diag.code += "-UNDEFINED";
        } else if (message.find("duplicate symbol") != std::string::npos ||
                   message.find("multiple definition") != std::string::npos) {
          diag.code += "-DUPLICATE";
        } else if (message.find("cannot find") != std::string::npos ||
                   message.find("cannot open") != std::string::npos) {
          diag.code += "-NOTFOUND";
        } else if (message.find("unresolved") != std::string::npos) {
          diag.code += "-UNRESOLVED";
        }
      }

      diagnostics.push_back(diag);
      current_error = message;
    }
    // Try to match MSVC linker errors
    else if (std::regex_search(line, matches, msvc_link_error_regex)) {
      diagnostic diag;
      diag.level = diagnostic_level::ERROR;

      // MSVC has its own error codes like LNK2001, LNK2019, etc.
      // We'll use those directly for more accuracy
      diag.code = matches[1].str();
      diag.message = matches[2].str();
      diag.file_path = "";
      diag.line_number = 0;
      diag.column_number = 0;

      diagnostics.push_back(diag);
      current_error = matches[2].str();
    }
    // Try to match file references
    else if (std::regex_search(line, matches, reference_regex)) {
      if (!diagnostics.empty()) {
        std::string file_ref = matches[1].str();

        // If we have a current diagnostic, add the file reference to it
        auto &last_diag = diagnostics.back();
        if (last_diag.file_path.empty()) {
          last_diag.file_path = file_ref;
        } else {
          // Add as help text if we already have a file path
          last_diag.help_text += "Also referenced in: " + file_ref + "\n";
        }
      }
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

} // namespace cforge