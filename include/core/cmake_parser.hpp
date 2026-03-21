/**
 * @file cmake_parser.hpp
 * @brief Regex-based CMakeLists.txt parser for cforge migrate
 */

#pragma once

#include <string>
#include <vector>

namespace cforge {

/// A single dependency extracted from CMakeLists.txt.
struct cmake_dependency {
    std::string name;           // Package/library name
    std::string version;        // Version string, empty if not found
    std::string git_url;        // GIT_REPOSITORY if from FetchContent
    std::string git_tag;        // GIT_TAG if from FetchContent, empty if not found
    bool is_fetch_content;      // True if from FetchContent_Declare
    bool is_find_package;       // True if from find_package
    bool is_subdirectory;       // True if from add_subdirectory
};

/// Aggregated result of parsing one CMakeLists.txt file.
struct cmake_parse_result {
    // [project]
    std::string project_name;       // from project()
    std::string version;            // from project(... VERSION x.y.z ...)
    std::string cpp_standard;       // from set(CMAKE_CXX_STANDARD xx)
    std::string c_standard;         // from set(CMAKE_C_STANDARD xx)
    std::string binary_type;        // "executable", "static", "shared", "interface"
    std::string target_name;        // first target name from add_executable/add_library

    // [build]
    std::vector<std::string> source_dirs;   // deduced from file(GLOB...) or target_sources
    std::vector<std::string> include_dirs;  // from target_include_directories

    // [dependencies]
    std::vector<cmake_dependency> dependencies;

    // Compiler settings
    std::vector<std::string> compile_definitions;  // from target_compile_definitions
    std::vector<std::string> compile_options;       // from target_compile_options
    std::vector<std::string> link_libraries;        // from target_link_libraries (non-dep names)

    // Parse diagnostics — warnings to display to the user
    std::vector<std::string> warnings;
};

/// Parse a CMakeLists.txt file using regex-based line-by-line extraction.
/// Each extractor is independent; a failure in one does not affect others.
/// Returns a cmake_parse_result populated with everything that could be extracted.
cmake_parse_result parse_cmake_file(const std::string &cmake_path);

} // namespace cforge
