/**
 * @file error_format.hpp
 * @brief Utilities for formatting error messages in a Rust-like style
 */

#pragma once

#include "cforge/log.hpp"
#include <regex>
#include <string>
#include <vector>

#ifdef ERROR
#undef ERROR
#endif

namespace cforge {

/**
 * @brief Error level for diagnostic messages
 */
enum class diagnostic_level { ERROR, WARNING, NOTE, HELP };

/**
 * @brief Structure representing a suggested code fix
 */
struct fix_suggestion {
  std::string description;   // Human-readable description of the fix
  std::string replacement;   // The suggested replacement text
  int start_line = 0;        // Line where fix starts (0 = same as error)
  int start_column = 0;      // Column where fix starts
  int end_line = 0;          // Line where fix ends
  int end_column = 0;        // Column where fix ends
  bool is_insertion = false; // True if this is an insertion, not a replacement
};

/**
 * @brief Structure representing a code diagnostic
 */
struct diagnostic {
  diagnostic_level level;
  std::string code;
  std::string message;
  std::string file_path;
  int line_number;
  int column_number;
  std::string line_content;
  std::string help_text;          // Legacy help text field
  std::vector<std::string> notes; // Additional notes/context
  std::string help;               // New help message field
  int occurrence_count =
      1; // For deduplication - how many times this error occurred
  std::vector<fix_suggestion> fixes; // Suggested fixes for this error
};

/**
 * @brief Structure for error/warning summary statistics
 */
struct error_summary {
  int total_errors = 0;
  int total_warnings = 0;
  int total_notes = 0;
  int compiler_errors = 0;
  int linker_errors = 0;
  int cmake_errors = 0;
  int template_errors = 0;
  std::vector<std::pair<std::string, int>>
      error_categories; // (category, count)
};

/**
 * @brief Format build error output in a Rust-like style
 *
 * @param error_output Raw error output from the compiler or linker
 * @return Formatted error messages
 */
std::string format_build_errors(const std::string &error_output);

/**
 * @brief Print a diagnostic in a Rust-like style
 *
 * @param diagnostic The diagnostic to print
 */
void print_diagnostic(const diagnostic &diagnostic);

/**
 * @brief Format a diagnostic to a string in Rust-like style
 *
 * @param diagnostic The diagnostic to format
 * @return std::string Formatted diagnostic string
 */
std::string format_diagnostic_to_string(const diagnostic &diagnostic);

/**
 * @brief Extract diagnostics from compiler error output
 *
 * @param error_output Raw error output from compiler or linker
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> extract_diagnostics(const std::string &error_output);

/**
 * @brief Parse GCC/Clang style errors and warnings
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_gcc_clang_errors(const std::string &error_output);

/**
 * @brief Parse MSVC style errors and warnings
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_msvc_errors(const std::string &error_output);

/**
 * @brief Parse CMake errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_cmake_errors(const std::string &error_output);

/**
 * @brief Parse Ninja errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_ninja_errors(const std::string &error_output);

/**
 * @brief Parse linker errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_linker_errors(const std::string &error_output);

/**
 * @brief Parse CPack errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_cpack_errors(const std::string &error_output);

/**
 * @brief Parse compiler errors and warnings
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_compiler_errors(const std::string &error_output);

/**
 * @brief Parse C++ template instantiation errors
 *
 * Template errors are notoriously verbose - this parser extracts
 * the key information and presents it in a readable format.
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_template_errors(const std::string &error_output);

/**
 * @brief Parse preprocessor errors (#error, macro expansion, etc.)
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_preprocessor_errors(const std::string &error_output);

/**
 * @brief Parse AddressSanitizer/UndefinedBehaviorSanitizer output
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_sanitizer_errors(const std::string &error_output);

/**
 * @brief Parse assertion failures (assert, static_assert)
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_assertion_errors(const std::string &error_output);

/**
 * @brief Parse C++20 module errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_module_errors(const std::string &error_output);

/**
 * @brief Parse runtime errors (segfault, exceptions, etc.)
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_runtime_errors(const std::string &error_output);

/**
 * @brief Parse test framework output (Google Test, Catch2, Boost.Test)
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_test_framework_errors(const std::string &error_output);

/**
 * @brief Parse static analysis tool output (clang-tidy, cppcheck)
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_static_analysis_errors(const std::string &error_output);

/**
 * @brief Parse C++20 concept constraint errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_concept_errors(const std::string &error_output);

/**
 * @brief Parse constexpr evaluation errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_constexpr_errors(const std::string &error_output);

/**
 * @brief Parse C++20 coroutine errors (co_await, co_yield, co_return)
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_coroutine_errors(const std::string &error_output);

/**
 * @brief Parse C++20 ranges library errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_ranges_errors(const std::string &error_output);

/**
 * @brief Parse CUDA/HIP GPU compiler errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_cuda_hip_errors(const std::string &error_output);

/**
 * @brief Parse Intel ICC/ICX compiler errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_intel_compiler_errors(const std::string &error_output);

/**
 * @brief Parse precompiled header (PCH) errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_pch_errors(const std::string &error_output);

/**
 * @brief Parse cross-compilation and ABI mismatch errors
 *
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<diagnostic> parse_abi_errors(const std::string &error_output);

/**
 * @brief Deduplicate diagnostics by grouping similar errors
 *
 * Groups errors with the same code and similar messages, incrementing
 * the occurrence_count field. Useful for linker errors where the same
 * undefined symbol may appear many times.
 *
 * @param diagnostics Vector of diagnostics to deduplicate
 * @return Deduplicated vector with occurrence counts
 */
std::vector<diagnostic>
deduplicate_diagnostics(std::vector<diagnostic> diagnostics);

/**
 * @brief Calculate summary statistics from diagnostics
 *
 * @param diagnostics Vector of diagnostics to summarize
 * @return Summary statistics
 */
error_summary
calculate_error_summary(const std::vector<diagnostic> &diagnostics);

/**
 * @brief Format error summary as a string (Cargo-style)
 *
 * Example output:
 *   error: build failed
 *      3 compiler errors
 *      2 linker errors
 *      5 warnings
 *
 * @param summary The error summary to format
 * @return Formatted summary string
 */
std::string format_error_summary(const error_summary &summary);

/**
 * @brief Suggest libraries for common undefined symbols
 *
 * Maps common Windows API functions to their required .lib files
 * and common Unix symbols to their libraries.
 *
 * @param symbol The undefined symbol name
 * @return Suggested library name, or empty string if unknown
 */
std::string suggest_library_for_symbol(const std::string &symbol);

/**
 * @brief Generate fix suggestions for a diagnostic
 *
 * Analyzes the error message and code context to suggest potential fixes.
 * Supports common errors like:
 * - Missing semicolons
 * - Missing closing braces/parentheses
 * - Missing includes for standard types
 * - Typos in identifiers
 * - Unused variable fixes
 * - Missing return statements
 *
 * @param diag The diagnostic to analyze
 * @return Vector of suggested fixes
 */
std::vector<fix_suggestion> generate_fix_suggestions(const diagnostic &diag);

/**
 * @brief Suggest include for a missing type
 *
 * Maps common C++ standard library types to their headers.
 *
 * @param type_name The missing type name
 * @return The header to include, or empty string if unknown
 */
std::string suggest_include_for_type(const std::string &type_name);

/**
 * @brief Find similar identifiers for typo correction
 *
 * Uses edit distance to find identifiers similar to the unknown one.
 *
 * @param unknown_identifier The identifier that wasn't found
 * @param available_identifiers List of known identifiers to compare against
 * @param max_distance Maximum edit distance to consider (default 2)
 * @return Vector of similar identifiers, sorted by similarity
 */
std::vector<std::string>
find_similar_identifiers(const std::string &unknown_identifier,
                         const std::vector<std::string> &available_identifiers,
                         int max_distance = 2);

} // namespace cforge