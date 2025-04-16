/**
 * @file error_format.cpp
 * @brief Implementation of error formatting utilities
 */

#include "core/error_format.hpp"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <fstream>
#include <regex>
#include <map>

// Add header-only mode for fmt library
#define FMT_HEADER_ONLY
// Use these headers in this specific order
#include "fmt/core.h"
#include "fmt/format.h"
#include "fmt/color.h"

namespace cforge {

// Use simple foreground colors instead of custom text_style
const auto ERROR_COLOR = fmt::fg(fmt::color::crimson);
const auto WARNING_COLOR = fmt::fg(fmt::color::gold);
const auto NOTE_COLOR = fmt::fg(fmt::color::steel_blue);
const auto HELP_COLOR = fmt::fg(fmt::color::medium_sea_green);
const auto CODE_COLOR = fmt::fg(fmt::color::slate_gray);
const auto LOCATION_COLOR = fmt::fg(fmt::color::light_blue);
const auto HIGHLIGHT_COLOR = fmt::fg(fmt::color::red);
const auto CARET_COLOR = fmt::fg(fmt::color::orange_red);

std::string format_build_errors(const std::string& error_output) {
    auto diagnostics = extract_diagnostics(error_output);
    
    // If no diagnostics were parsed, just return the raw output
    if (diagnostics.empty()) {
        return error_output;
    }
    
    // Print all diagnostics
    for (const auto& diag : diagnostics) {
        print_diagnostic(diag);
    }
    
    // Return empty string since we've already printed the diagnostics
    return "";
}

void print_diagnostic(const Diagnostic& diagnostic) {
    // Determine the level-specific formatting
    fmt::text_style level_style;
    std::string level_str;
    std::string level_label;
    
    switch (diagnostic.level) {
        case DiagnosticLevel::ERROR:
            level_style = ERROR_COLOR | fmt::emphasis::bold;
            level_str = "error";
            level_label = "ERROR";
            break;
        case DiagnosticLevel::WARNING:
            level_style = WARNING_COLOR | fmt::emphasis::bold;
            level_str = "warning";
            level_label = "WARN";
            break;
        case DiagnosticLevel::NOTE:
            level_style = NOTE_COLOR | fmt::emphasis::bold;
            level_str = "note";
            level_label = "NOTE";
            break;
        case DiagnosticLevel::HELP:
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
        fmt::print("{:>4} | ", diagnostic.line_number);
        
        // Print the line content
        fmt::print("{}\n", diagnostic.line_content);
        
        // Print the error caret pointing to the column
        if (diagnostic.column_number > 0) {
            fmt::print("{:>4} | ", "");
            // Print spaces up to the column
            for (int i = 1; i < diagnostic.column_number; i++) {
                fmt::print(" ");
            }
            // Print the caret
            fmt::print(CARET_COLOR | fmt::emphasis::bold, "^\n");
        }
    }
    
    // Print help text if available
    if (!diagnostic.help_text.empty()) {
        fmt::print(HELP_COLOR | fmt::emphasis::bold, "help: {}\n", diagnostic.help_text);
    }
    
    // Add a newline for spacing
    fmt::print("\n");
}

std::vector<Diagnostic> extract_diagnostics(const std::string& error_output) {
    std::vector<Diagnostic> all_diagnostics;
    
    // Try parsing errors for different compilers/tools
    auto gcc_clang_diags = parse_gcc_clang_errors(error_output);
    auto msvc_diags = parse_msvc_errors(error_output);
    auto cmake_diags = parse_cmake_errors(error_output);
    auto ninja_diags = parse_ninja_errors(error_output);
    auto linker_diags = parse_linker_errors(error_output);
    
    // Combine all diagnostics
    all_diagnostics.insert(all_diagnostics.end(), gcc_clang_diags.begin(), gcc_clang_diags.end());
    all_diagnostics.insert(all_diagnostics.end(), msvc_diags.begin(), msvc_diags.end());
    all_diagnostics.insert(all_diagnostics.end(), cmake_diags.begin(), cmake_diags.end());
    all_diagnostics.insert(all_diagnostics.end(), ninja_diags.begin(), ninja_diags.end());
    all_diagnostics.insert(all_diagnostics.end(), linker_diags.begin(), linker_diags.end());
    
    return all_diagnostics;
}

std::vector<Diagnostic> parse_gcc_clang_errors(const std::string& error_output) {
    std::vector<Diagnostic> diagnostics;
    
    // Regular expression to match GCC/Clang error format
    // Example: file.cpp:10:15: error: 'foo' was not declared in this scope
    std::regex error_regex(R"(([^:]+):(\d+):(\d+):\s+(error|warning|note):\s+([^:]+)(?:\[([^\]]+)\])?)"
                         R"(|([^:]+):(\d+):\s+(error|warning|note):\s+([^:]+)(?:\[([^\]]+)\])?)");
    
    std::string line;
    std::istringstream stream(error_output);
    std::map<std::string, std::string> file_contents;
    
    while (std::getline(stream, line)) {
        std::smatch matches;
        if (std::regex_search(line, matches, error_regex)) {
            Diagnostic diag;
            
            // Determine which pattern matched and extract fields accordingly
            if (matches[4].matched) {  // First pattern with column number
                diag.file_path = matches[1].str();
                diag.line_number = std::stoi(matches[2].str());
                diag.column_number = std::stoi(matches[3].str());
                
                std::string level_str = matches[4].str();
                diag.message = matches[5].str();
                
                // Use a more specific error code based on message content
                if (matches[6].matched) {
                    diag.code = matches[6].str();
                } else {
                    // Generate a meaningful error code based on the message
                    if (diag.message.find("expected") != std::string::npos) {
                        diag.code = "E1001"; // syntax error - expected something
                    } else if (diag.message.find("undeclared") != std::string::npos || 
                               diag.message.find("not declared") != std::string::npos) {
                        diag.code = "E1002"; // undeclared identifier
                    } else if (diag.message.find("undefined") != std::string::npos) {
                        diag.code = "E1003"; // undefined reference/symbol
                    } else if (diag.message.find("cannot convert") != std::string::npos || 
                               diag.message.find("invalid conversion") != std::string::npos) {
                        diag.code = "E1004"; // type conversion error
                    } else if (diag.message.find("no matching") != std::string::npos) {
                        diag.code = "E1005"; // no matching function/method
                    } else if (diag.message.find("redefinition") != std::string::npos || 
                               diag.message.find("already defined") != std::string::npos) {
                        diag.code = "E1006"; // redefinition error
                    } else {
                        diag.code = "E1000"; // generic error
                    }
                }
                
                if (level_str == "error") {
                    diag.level = DiagnosticLevel::ERROR;
                } else if (level_str == "warning") {
                    diag.level = DiagnosticLevel::WARNING;
                    
                    // If it's a warning and no specific code was provided, use W prefix
                    if (!matches[6].matched) {
                        if (diag.message.find("unused") != std::string::npos) {
                            diag.code = "W2001"; // unused variable/function
                        } else if (diag.message.find("implicit") != std::string::npos) {
                            diag.code = "W2002"; // implicit conversion
                        } else if (diag.message.find("deprecated") != std::string::npos) {
                            diag.code = "W2003"; // deprecated feature
                        } else {
                            diag.code = "W2000"; // generic warning
                        }
                    }
                } else if (level_str == "note") {
                    diag.level = DiagnosticLevel::NOTE;
                    // Use N prefix for notes
                    if (!matches[6].matched) {
                        diag.code = "N3000";
                    }
                }
            } else if (matches[9].matched) {  // Second pattern without column number
                diag.file_path = matches[7].str();
                diag.line_number = std::stoi(matches[8].str());
                diag.column_number = 0;
                
                std::string level_str = matches[9].str();
                diag.message = matches[10].str();
                
                // Use a more specific error code based on message content
                if (matches[11].matched) {
                    diag.code = matches[11].str();
                } else {
                    // Generate a meaningful error code based on the message
                    if (diag.message.find("expected") != std::string::npos) {
                        diag.code = "E1001"; // syntax error - expected something
                    } else if (diag.message.find("undeclared") != std::string::npos || 
                               diag.message.find("not declared") != std::string::npos) {
                        diag.code = "E1002"; // undeclared identifier
                    } else if (diag.message.find("undefined") != std::string::npos) {
                        diag.code = "E1003"; // undefined reference/symbol
                    } else if (diag.message.find("cannot convert") != std::string::npos || 
                               diag.message.find("invalid conversion") != std::string::npos) {
                        diag.code = "E1004"; // type conversion error
                    } else if (diag.message.find("no matching") != std::string::npos) {
                        diag.code = "E1005"; // no matching function/method
                    } else if (diag.message.find("redefinition") != std::string::npos || 
                               diag.message.find("already defined") != std::string::npos) {
                        diag.code = "E1006"; // redefinition error
                    } else {
                        diag.code = "E1000"; // generic error
                    }
                }
                
                if (level_str == "error") {
                    diag.level = DiagnosticLevel::ERROR;
                } else if (level_str == "warning") {
                    diag.level = DiagnosticLevel::WARNING;
                    
                    // If it's a warning and no specific code was provided, use W prefix
                    if (!matches[11].matched) {
                        if (diag.message.find("unused") != std::string::npos) {
                            diag.code = "W2001"; // unused variable/function
                        } else if (diag.message.find("implicit") != std::string::npos) {
                            diag.code = "W2002"; // implicit conversion
                        } else if (diag.message.find("deprecated") != std::string::npos) {
                            diag.code = "W2003"; // deprecated feature
                        } else {
                            diag.code = "W2000"; // generic warning
                        }
                    }
                } else if (level_str == "note") {
                    diag.level = DiagnosticLevel::NOTE;
                    // Use N prefix for notes
                    if (!matches[11].matched) {
                        diag.code = "N3000";
                    }
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
                    
                    while (std::getline(file_stream, file_line) && current_line < diag.line_number) {
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
                diag.help_text = "Check for typos or make sure to include the appropriate header";
            } else if (diag.message.find("expected") != std::string::npos) {
                diag.help_text = "Check for missing syntax elements";
            }
            
            diagnostics.push_back(diag);
        }
    }
    
    return diagnostics;
}

std::vector<Diagnostic> parse_msvc_errors(const std::string& error_output) {
    std::vector<Diagnostic> diagnostics;
    
    // Regular expression for MSVC error format
    // Example: C:\path\to\file.cpp(10,15): error C2065: 'foo': undeclared identifier
    std::regex error_regex(R"(([^(]+)\((\d+)(?:,(\d+))?\):\s+(error|warning|note)\s+([A-Z]\d+):\s+(.+))");
    
    std::string line;
    std::istringstream stream(error_output);
    std::map<std::string, std::string> file_contents;
    
    while (std::getline(stream, line)) {
        std::smatch matches;
        if (std::regex_search(line, matches, error_regex)) {
            Diagnostic diag;
            
            diag.file_path = matches[1].str();
            diag.line_number = std::stoi(matches[2].str());
            diag.column_number = matches[3].matched ? std::stoi(matches[3].str()) : 0;
            
            std::string level_str = matches[4].str();
            diag.code = matches[5].str();
            diag.message = matches[6].str();
            
            if (level_str == "error") {
                diag.level = DiagnosticLevel::ERROR;
            } else if (level_str == "warning") {
                diag.level = DiagnosticLevel::WARNING;
            } else if (level_str == "note") {
                diag.level = DiagnosticLevel::NOTE;
            }
            
            // Try to extract line content if possible
            if (!diag.file_path.empty() && diag.line_number > 0) {
                // Same file content extraction as in parse_gcc_clang_errors
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
                    
                    while (std::getline(file_stream, file_line) && current_line < diag.line_number) {
                        current_line++;
                        if (current_line == diag.line_number) {
                            diag.line_content = file_line;
                            break;
                        }
                    }
                }
            }
            
            // Add MSVC-specific help text
            if (diag.code == "C2065") {
                diag.help_text = "The identifier is not declared in this scope";
            } else if (diag.code == "C2143") {
                diag.help_text = "Check for missing syntax elements";
            } else if (diag.code == "C3861") {
                diag.help_text = "Function not found, check for typos or missing includes";
            }
            
            diagnostics.push_back(diag);
        }
    }
    
    return diagnostics;
}

std::vector<Diagnostic> parse_cmake_errors(const std::string& error_output) {
    std::vector<Diagnostic> diagnostics;
    
    // Regular expression for CMake error format
    // Example: CMake Error at CMakeLists.txt:10 (add_executable): Target "my_target" already exists.
    std::regex error_regex(R"(CMake\s+(Error|Warning)(?:\s+at\s+([^:]+):(\d+)\s+\(([^)]+)\))?:\s+(.+))");
    
    std::string line;
    std::istringstream stream(error_output);
    
    while (std::getline(stream, line)) {
        std::smatch matches;
        if (std::regex_search(line, matches, error_regex)) {
            Diagnostic diag;
            
            std::string level_str = matches[1].str();
            if (level_str == "Error") {
                diag.level = DiagnosticLevel::ERROR;
                diag.code = "CMake0001";
            } else if (level_str == "Warning") {
                diag.level = DiagnosticLevel::WARNING;
                diag.code = "CMake0002";
            }
            
            if (matches[2].matched) {
                diag.file_path = matches[2].str();
                diag.line_number = std::stoi(matches[3].str());
                std::string command = matches[4].str();
                std::string message = matches[5].str();
                diag.message = message + " (in " + command + ")";
                
                // Assign more specific error codes based on command
                if (level_str == "Error") {
                    if (command == "find_package") {
                        diag.code = "CM1001"; // package not found
                    } else if (command == "add_executable" || command == "add_library") {
                        diag.code = "CM1002"; // target definition error
                    } else if (command == "target_link_libraries") {
                        diag.code = "CM1003"; // linking error
                    } else if (command == "include") {
                        diag.code = "CM1004"; // include error
                    } else {
                        diag.code = "CM1000"; // generic CMake error
                    }
                } else if (level_str == "Warning") {
                    if (command == "find_package") {
                        diag.code = "CM2001"; // package warning
                    } else if (command.find("deprecated") != std::string::npos) {
                        diag.code = "CM2002"; // deprecation warning
                    } else {
                        diag.code = "CM2000"; // generic CMake warning
                    }
                }
            } else {
                // For errors not associated with a specific file
                std::string message = matches[5].str();
                diag.message = message;
                
                // Assign more specific error codes based on message content
                if (level_str == "Error") {
                    if (message.find("Could not find") != std::string::npos || 
                        message.find("not found") != std::string::npos) {
                        diag.code = "CM1001"; // not found error
                    } else if (message.find("already exists") != std::string::npos) {
                        diag.code = "CM1005"; // duplicate definition
                    } else if (message.find("syntax error") != std::string::npos) {
                        diag.code = "CM1006"; // syntax error
                    } else {
                        diag.code = "CM1000"; // generic CMake error
                    }
                } else if (level_str == "Warning") {
                    if (message.find("deprecated") != std::string::npos) {
                        diag.code = "CM2002"; // deprecation warning
                    } else if (message.find("unused") != std::string::npos) {
                        diag.code = "CM2003"; // unused variable/target
                    } else {
                        diag.code = "CM2000"; // generic CMake warning
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

std::vector<Diagnostic> parse_ninja_errors(const std::string& error_output) {
    std::vector<Diagnostic> diagnostics;
    
    // Regular expression for Ninja error format
    // Example: ninja: error: build.ninja:10: syntax error
    std::regex error_regex(R"(ninja:\s+(error|warning):\s+(?:([^:]+):(\d+):\s+)?(.+))");
    
    std::string line;
    std::istringstream stream(error_output);
    
    while (std::getline(stream, line)) {
        std::smatch matches;
        if (std::regex_search(line, matches, error_regex)) {
            Diagnostic diag;
            
            std::string level_str = matches[1].str();
            if (level_str == "error") {
                diag.level = DiagnosticLevel::ERROR;
                diag.code = "NINJA0001";
            } else if (level_str == "warning") {
                diag.level = DiagnosticLevel::WARNING;
                diag.code = "NINJA0002";
            }
            
            if (matches[2].matched) {
                diag.file_path = matches[2].str();
                diag.line_number = std::stoi(matches[3].str());
            }
            
            diag.message = matches[4].str();
            std::string message = diag.message;
            
            // More specific error codes for ninja
            if (level_str == "error") {
                if (message.find("syntax error") != std::string::npos) {
                    diag.code = "NJ1001"; // syntax error
                } else if (message.find("multiple rules") != std::string::npos) {
                    diag.code = "NJ1002"; // multiple rules for one target
                } else if (message.find("missing") != std::string::npos) {
                    diag.code = "NJ1003"; // missing input
                } else if (message.find("stopping") != std::string::npos || 
                           message.find("failed") != std::string::npos) {
                    diag.code = "NJ1004"; // build stopped
                } else if (message.find("unknown") != std::string::npos) {
                    diag.code = "NJ1005"; // unknown target/variable
                } else {
                    diag.code = "NJ1000"; // generic ninja error
                }
            } else if (level_str == "warning") {
                if (message.find("duplicate") != std::string::npos) {
                    diag.code = "NJ2001"; // duplicate definition
                } else if (message.find("deprecated") != std::string::npos) {
                    diag.code = "NJ2002"; // deprecated feature
                } else {
                    diag.code = "NJ2000"; // generic ninja warning
                }
            }
            
            // Add help text
            diag.help_text = "Check your build configuration";
            
            diagnostics.push_back(diag);
        }
    }
    
    return diagnostics;
}

std::vector<Diagnostic> parse_linker_errors(const std::string& error_output) {
    std::vector<Diagnostic> diagnostics;
    
    // Regular expressions for linker errors
    // LLD-style linker error: "lld-link: error: undefined symbol: ..."
    std::regex lld_error_regex(R"(lld-link:\s*error:\s*(.*))");
    
    // ld-style linker error: "ld: undefined symbol: ..."
    std::regex ld_error_regex(R"(ld(\.\S+)?:\s*.*error:\s*(.*))");
    
    // MSVC linker error: "LINK : fatal error LNK1181: cannot open input file '...'"
    std::regex msvc_link_error_regex(R"(LINK\s*:\s*(?:fatal\s*)?error\s*(LNK\d+):\s*(.*))");
    
    // Generic reference pattern: "referenced by file.obj"
    std::regex reference_regex(R"(>>>\s*referenced by\s*([^:\n]+))");
    
    std::string line;
    std::istringstream stream(error_output);
    std::string current_error;
    std::string current_file;
    
    while (std::getline(stream, line)) {
        std::smatch matches;
        
        // Try to match LLD linker errors
        if (std::regex_search(line, matches, lld_error_regex)) {
            Diagnostic diag;
            diag.level = DiagnosticLevel::ERROR;
            std::string message = matches[1].str();
            diag.message = message;
            diag.file_path = "";
            diag.line_number = 0;
            diag.column_number = 0;
            
            // Generate a more specific error code based on the message
            if (message.find("undefined symbol") != std::string::npos) {
                diag.code = "L4001"; // undefined symbol
            } else if (message.find("duplicate symbol") != std::string::npos) {
                diag.code = "L4002"; // duplicate symbol
            } else if (message.find("cannot open") != std::string::npos) {
                diag.code = "L4003"; // cannot open file
            } else if (message.find("unresolved") != std::string::npos) {
                diag.code = "L4004"; // unresolved external
            } else {
                diag.code = "L4000"; // generic linker error
            }
            
            diagnostics.push_back(diag);
            current_error = message;
        }
        // Try to match ld linker errors
        else if (std::regex_search(line, matches, ld_error_regex)) {
            Diagnostic diag;
            diag.level = DiagnosticLevel::ERROR;
            std::string message = matches[2].str();
            diag.message = message;
            diag.file_path = "";
            diag.line_number = 0;
            diag.column_number = 0;
            
            // Generate a more specific error code based on the message
            if (message.find("undefined reference") != std::string::npos || 
                message.find("undefined symbol") != std::string::npos) {
                diag.code = "L4001"; // undefined symbol
            } else if (message.find("duplicate symbol") != std::string::npos || 
                       message.find("multiple definition") != std::string::npos) {
                diag.code = "L4002"; // duplicate symbol
            } else if (message.find("cannot find") != std::string::npos || 
                       message.find("cannot open") != std::string::npos) {
                diag.code = "L4003"; // cannot open file
            } else if (message.find("unresolved") != std::string::npos) {
                diag.code = "L4004"; // unresolved external
            } else {
                diag.code = "L4000"; // generic linker error
            }
            
            diagnostics.push_back(diag);
            current_error = message;
        }
        // Try to match MSVC linker errors
        else if (std::regex_search(line, matches, msvc_link_error_regex)) {
            Diagnostic diag;
            diag.level = DiagnosticLevel::ERROR;
            
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
                auto& last_diag = diagnostics.back();
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

} // namespace cforge 