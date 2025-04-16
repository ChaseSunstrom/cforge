/**
 * @file error_format.hpp
 * @brief Utilities for formatting error messages in a Rust-like style
 */

#pragma once

#include <string>
#include <vector>
#include <regex>
#include "cforge/log.hpp"

#ifdef ERROR
#undef ERROR
#endif

namespace cforge {

/**
 * @brief Error level for diagnostic messages
 */
enum class DiagnosticLevel {
    ERROR,
    WARNING,
    NOTE,
    HELP
};

/**
 * @brief Structure representing a code diagnostic
 */
struct Diagnostic {
    DiagnosticLevel level;
    std::string code;
    std::string message;
    std::string file_path;
    int line_number;
    int column_number;
    std::string line_content;
    std::string help_text;
};

/**
 * @brief Format build error output in a Rust-like style
 * 
 * @param error_output Raw error output from the compiler or linker
 * @return Formatted error messages
 */
std::string format_build_errors(const std::string& error_output);

/**
 * @brief Print a diagnostic in a Rust-like style
 * 
 * @param diagnostic The diagnostic to print
 */
void print_diagnostic(const Diagnostic& diagnostic);

/**
 * @brief Extract diagnostics from compiler error output
 * 
 * @param error_output Raw error output from compiler or linker
 * @return Vector of extracted diagnostics
 */
std::vector<Diagnostic> extract_diagnostics(const std::string& error_output);

/**
 * @brief Parse GCC/Clang style errors and warnings
 * 
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<Diagnostic> parse_gcc_clang_errors(const std::string& error_output);

/**
 * @brief Parse MSVC style errors and warnings
 * 
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<Diagnostic> parse_msvc_errors(const std::string& error_output);

/**
 * @brief Parse CMake errors
 * 
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<Diagnostic> parse_cmake_errors(const std::string& error_output);

/**
 * @brief Parse Ninja errors
 * 
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<Diagnostic> parse_ninja_errors(const std::string& error_output);

/**
 * @brief Parse linker errors
 * 
 * @param error_output Raw error output
 * @return Vector of extracted diagnostics
 */
std::vector<Diagnostic> parse_linker_errors(const std::string& error_output);

} // namespace cforge 