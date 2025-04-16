/**
 * @file command_init.cpp
 * @brief Implementation of the 'init' command to create new cforge projects
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/workspace.hpp"
#include "cforge/log.hpp"
#include "core/toml_reader.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>

using namespace cforge;

/**
 * @brief Split a comma-separated list of project names
 * 
 * @param project_list String containing comma-separated project names
 * @return Vector of individual project names
 */
/**
 * @brief Split a comma-separated list of project names
 * 
 * @param project_list String containing comma-separated project names
 * @return Vector of individual project names
 */
static std::vector<std::string> parse_project_list(const std::string& project_list) {
    std::vector<std::string> result;
    std::string::size_type start = 0;
    std::string::size_type end = 0;
    
    // First try comma-separated format
    if (project_list.find(',') != std::string::npos) {
        // Split by commas
        while ((end = project_list.find(',', start)) != std::string::npos) {
            // Extract the substring and trim whitespace
            std::string project = project_list.substr(start, end - start);
            // Trim leading and trailing whitespace
            project.erase(0, project.find_first_not_of(" \t"));
            project.erase(project.find_last_not_of(" \t") + 1);
            
            // Add to result if not empty
            if (!project.empty()) {
                result.push_back(project);
            }
            start = end + 1;
        }
        
        // Add the last part
        std::string last_project = project_list.substr(start);
        // Trim leading and trailing whitespace
        last_project.erase(0, last_project.find_first_not_of(" \t"));
        last_project.erase(last_project.find_last_not_of(" \t") + 1);
        
        if (!last_project.empty()) {
            result.push_back(last_project);
        }
    } else {
        // It's a single project - trim and add it
        std::string project = project_list;
        project.erase(0, project.find_first_not_of(" \t"));
        project.erase(project.find_last_not_of(" \t") + 1);
        
        if (!project.empty()) {
            result.push_back(project);
        }
    }
    
    return result;
}

/**
 * @brief Create default .gitignore file
 * 
 * @param project_path Path to project directory
 * @return bool Success flag
 */
static bool create_gitignore(const std::filesystem::path& project_path) {
    std::filesystem::path gitignore_path = project_path / ".gitignore";
    std::ofstream gitignore(gitignore_path);
    
    if (!gitignore.is_open()) {
        logger::print_error("Failed to create .gitignore file");
        return false;
    }
    
    gitignore << "# Build directory\n";
    gitignore << "build/\n";
    gitignore << "out/\n\n";
    
    gitignore << "# CMake build files\n";
    gitignore << "CMakeFiles/\n";
    gitignore << "cmake_install.cmake\n";
    gitignore << "CMakeCache.txt\n";
    gitignore << "*.cmake\n";
    gitignore << "!CMakeLists.txt\n\n";
    
    gitignore << "# IDEs\n";
    gitignore << ".vs/\n";
    gitignore << ".vscode/\n";
    gitignore << ".idea/\n";
    gitignore << "*.swp\n";
    gitignore << "*.swo\n\n";
    
    gitignore << "# Prerequisites\n";
    gitignore << "*.d\n\n";
    
    gitignore << "# Compiled Object files\n";
    gitignore << "*.slo\n";
    gitignore << "*.lo\n";
    gitignore << "*.o\n";
    gitignore << "*.obj\n\n";
    
    gitignore << "# Precompiled Headers\n";
    gitignore << "*.gch\n";
    gitignore << "*.pch\n\n";
    
    gitignore << "# Compiled Dynamic libraries\n";
    gitignore << "*.so\n";
    gitignore << "*.dylib\n";
    gitignore << "*.dll\n\n";
    
    gitignore << "# Fortran module files\n";
    gitignore << "*.mod\n";
    gitignore << "*.smod\n\n";
    
    gitignore << "# Compiled Static libraries\n";
    gitignore << "*.lai\n";
    gitignore << "*.la\n";
    gitignore << "*.a\n";
    gitignore << "*.lib\n\n";
    
    gitignore << "# Executables\n";
    gitignore << "*.exe\n";
    gitignore << "*.out\n";
    gitignore << "*.app\n";
    
    gitignore.close();
    
    logger::print_success("Created .gitignore file");
    return true;
}

/**
 * @brief Create default README.md file
 * 
 * @param project_path Path to project directory
 * @param project_name Project name (normalized with underscores for code usage)
 * @return bool Success flag
 */
static bool create_readme(const std::filesystem::path& project_path, const std::string& project_name) {
    // Get the directory name (which might have hyphens)
    std::string display_name = project_path.filename().string();
    
    std::filesystem::path readme_path = project_path / "README.md";
    std::ofstream readme(readme_path);
    
    if (!readme.is_open()) {
        logger::print_error("Failed to create README.md file");
        return false;
    }
    
    readme << "# " << display_name << "\n\n";
    readme << "A C++ project created with cforge.\n\n";
    
    readme << "## Building\n\n";
    readme << "```bash\n";
    readme << "# Configure\n";
    readme << "cmake -B build\n\n";
    readme << "# Build\n";
    readme << "cmake --build build\n\n";
    readme << "# Or using cforge\n";
    readme << "cforge build\n";
    readme << "```\n\n";
    
    readme << "## Running\n\n";
    readme << "```bash\n";
    readme << "# Run the executable\n";
    readme << "./build/bin/" << project_name << "\n\n";
    readme << "# Or using cforge\n";
    readme << "cforge run\n";
    readme << "```\n";
    
    readme.close();
    
    logger::print_success("Created README.md file");
    return true;
}

/**
 * @brief Create enhanced CMakeLists.txt file with proper workspace project linking support
 * 
 * @param project_path Path to project directory
 * @param project_name Project name
 * @param cpp_version C++ standard version (e.g., "17")
 * @param workspace_aware Enable workspace linking features
 * @return bool Success flag
 */
static bool create_cmakelists(
    const std::filesystem::path& project_path,
    const std::string& project_name,
    const std::string& cpp_version,
    bool workspace_aware = true
) {
    std::filesystem::path cmakelists_path = project_path / "CMakeLists.txt";
    
    // Check if the file already exists
    if (std::filesystem::exists(cmakelists_path)) {
        logger::print_warning("CMakeLists.txt already exists. Skipping.");
        return true;
    }
    
    std::ofstream cmakelists(cmakelists_path);
    if (!cmakelists) {
        logger::print_error("Failed to create CMakeLists.txt");
        return false;
    }
    
    cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
    cmakelists << "project(" << project_name << " VERSION 0.1.0 LANGUAGES CXX)\n\n";
    
    // C++ standard
    cmakelists << "# Set C++ standard\n";
    cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_version << ")\n";
    cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
    
    // Output directories
    cmakelists << "# Set output directories\n";
    cmakelists << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
    cmakelists << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
    cmakelists << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n\n";
    
    // Enhanced workspace project support
    if (workspace_aware) {
        cmakelists << "# Workspace integration support\n";
        cmakelists << "if(CMAKE_INCLUDE_PATH)\n";
        cmakelists << "    include_directories(${CMAKE_INCLUDE_PATH})\n";
        cmakelists << "endif()\n\n";
        
        cmakelists << "if(CMAKE_LIBRARY_PATH)\n";
        cmakelists << "    link_directories(${CMAKE_LIBRARY_PATH})\n";
        cmakelists << "endif()\n\n";
        
        // Support for specific workspace project dependencies
        cmakelists << "# Check for dependency-specific include/library paths\n";
        cmakelists << "# This allows proper linking between workspace projects\n";
        cmakelists << "function(check_workspace_dependency DEP_NAME)\n";
        cmakelists << "    if(DEFINED CFORGE_DEP_${DEP_NAME})\n";
        cmakelists << "        message(STATUS \"Using workspace dependency: ${DEP_NAME}\")\n";
        cmakelists << "        if(DEFINED CFORGE_${DEP_NAME}_INCLUDE)\n";
        cmakelists << "            include_directories(${CFORGE_${DEP_NAME}_INCLUDE})\n";
        cmakelists << "            message(STATUS \"  Include path: ${CFORGE_${DEP_NAME}_INCLUDE}\")\n";
        cmakelists << "        endif()\n";
        cmakelists << "        if(DEFINED CFORGE_${DEP_NAME}_LIB)\n";
        cmakelists << "            link_directories(${CFORGE_${DEP_NAME}_LIB})\n";
        cmakelists << "            message(STATUS \"  Library path: ${CFORGE_${DEP_NAME}_LIB}\")\n";
        cmakelists << "        endif()\n";
        cmakelists << "        set(CFORGE_HAS_${DEP_NAME} ON PARENT_SCOPE)\n";
        cmakelists << "    endif()\n";
        cmakelists << "endfunction()\n\n";
    }
    
    // vcpkg integration
    cmakelists << "# vcpkg integration\n";
    cmakelists << "if(DEFINED ENV{VCPKG_ROOT})\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE \"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "endif()\n\n";
    
    // Dependencies section
    cmakelists << "# Dependencies\n";
    cmakelists << "find_package(Threads REQUIRED)\n";
    cmakelists << "# Example of checking for a workspace dependency:\n";
    cmakelists << "# check_workspace_dependency(some_other_project)\n\n";
    
    // Source files
    cmakelists << "# Add source files\n";
    cmakelists << "file(GLOB_RECURSE SOURCES\n";
    cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp\n";
    cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c\n";
    cmakelists << ")\n\n";
    
    // Define the executable/library based on project type
    cmakelists << "# Define target name\n";
    cmakelists << "set(TARGET_NAME ${PROJECT_NAME})\n\n";
    
    // Project type determination based on sources
    cmakelists << "# Determine project type (executable vs. library)\n";
    cmakelists << "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp\" OR\n";
    cmakelists << "   EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/src/main.c\")\n";
    cmakelists << "    # This is an executable project\n";
    cmakelists << "    add_executable(${TARGET_NAME} ${SOURCES})\n";
    cmakelists << "    set(PROJECT_TYPE \"executable\")\n";
    cmakelists << "else()\n";
    cmakelists << "    # This is a library project\n";
    cmakelists << "    add_library(${TARGET_NAME} STATIC ${SOURCES})\n";
    cmakelists << "    set(PROJECT_TYPE \"library\")\n";
    cmakelists << "endif()\n\n";
    
    // Include directories
    cmakelists << "# Include directories\n";
    cmakelists << "target_include_directories(${TARGET_NAME} PUBLIC\n";
    cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/include\n";
    cmakelists << ")\n\n";
    
    // Link libraries
    cmakelists << "# Link libraries\n";
    cmakelists << "target_link_libraries(${TARGET_NAME} PRIVATE\n";
    cmakelists << "    Threads::Threads\n";
    cmakelists << "    # Add other libraries here\n";
    cmakelists << ")\n\n";
    
    // Compiler warnings
    cmakelists << "# Enable compiler warnings\n";
    cmakelists << "if(MSVC)\n";
    cmakelists << "    target_compile_options(${TARGET_NAME} PRIVATE /W4 /MP)\n";
    cmakelists << "else()\n";
    cmakelists << "    target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic)\n";
    cmakelists << "endif()\n\n";
    
    // Tests
    cmakelists << "# Testing\n";
    cmakelists << "option(BUILD_TESTING \"Build tests\" ON)\n";
    cmakelists << "if(BUILD_TESTING AND EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/tests\")\n";
    cmakelists << "    enable_testing()\n";
    cmakelists << "    add_subdirectory(tests)\n";
    cmakelists << "endif()\n\n";
    
    // Installation
    cmakelists << "# Installation\n";
    cmakelists << "include(GNUInstallDirs)\n";
    cmakelists << "if(PROJECT_TYPE STREQUAL \"executable\")\n";
    cmakelists << "    install(TARGETS ${TARGET_NAME}\n";
    cmakelists << "        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    cmakelists << "    )\n";
    cmakelists << "else()\n";
    cmakelists << "    install(TARGETS ${TARGET_NAME}\n";
    cmakelists << "        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << "        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << "        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
    cmakelists << "    )\n";
    cmakelists << "    # Install headers\n";
    cmakelists << "    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/\n";
    cmakelists << "        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
    cmakelists << "        FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
    cmakelists << "    )\n";
    cmakelists << "endif()\n\n";
    
    // CPack configuration
    cmakelists << "# Packaging with CPack\n";
    cmakelists << "include(CPack)\n";
    cmakelists << "set(CPACK_PACKAGE_NAME ${PROJECT_NAME})\n";
    cmakelists << "set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})\n";
    cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_NAME} - A C++ project created with cforge\")\n";
    cmakelists << "set(CPACK_PACKAGE_VENDOR \"Your Organization\")\n";
    
    // OS-specific packaging settings
    cmakelists << "# OS specific packaging settings\n";
    cmakelists << "if(WIN32)\n";
    cmakelists << "    set(CPACK_GENERATOR \"ZIP;NSIS\")\n";
    cmakelists << "elseif(APPLE)\n";
    cmakelists << "    set(CPACK_GENERATOR \"TGZ;DragNDrop\")\n";
    cmakelists << "else()\n";
    cmakelists << "    set(CPACK_GENERATOR \"TGZ;DEB\")\n";
    cmakelists << "endif()\n";
    
    cmakelists.close();
    logger::print_success("Created CMakeLists.txt file with workspace support");
    return true;
}

/**
 * @brief Create cforge.toml configuration file
 * 
 * @param project_path Path to project directory
 * @param project_name Project name (normalized with underscores)
 * @param cpp_version C++ standard version (e.g., "17")
 * @return bool Success flag
 */
static bool create_cforge_toml(
    const std::filesystem::path& project_path,
    const std::string& project_name,
    const std::string& cpp_version,
    bool with_tests
) {
    std::filesystem::path config_path = project_path / CFORGE_FILE;
    
    // Check if the file already exists
    if (std::filesystem::exists(config_path)) {
        logger::print_warning("cforge.toml already exists. Skipping.");
        return true;
    }
    
    std::ofstream config(config_path);
    if (!config) {
        logger::print_error("Failed to create cforge.toml file");
        return false;
    }
    
    config << "[project]\n";
    config << "name = \"" << project_name << "\"\n";
    config << "version = \"0.1.0\"\n";
    config << "description = \"A C++ project created with cforge\"\n";
    config << "cpp_standard = \"" << cpp_version << "\"\n";
    config << "authors = [\"Your Name <your.email@example.com>\"]\n";
    config << "homepage = \"https://github.com/yourusername/" << project_name << "\"\n";
    config << "repository = \"https://github.com/yourusername/" << project_name << ".git\"\n";
    config << "license = \"MIT\"\n\n";
    
    config << "[build]\n";
    config << "build_dir = \"build\"\n";
    config << "# For detailed configurations, you can specify build types\n";
    config << "build_type = \"Release\"  # Release, Debug, RelWithDebInfo, MinSizeRel\n";
    config << "# Set to true to clean the build directory before building\n";
    config << "# clean = false\n";
    config << "# Number of parallel jobs to use when building\n";
    config << "# jobs = 4\n\n";
    
    // Add custom build configurations
    config << "# Custom build configurations\n";
    config << "# You can define custom compiler flags for different build types\n";
    config << "[build.config.debug]\n";
    config << "# Custom compiler defines for Debug builds\n";
    config << "defines = [\"DEBUG=1\", \"ENABLE_LOGGING=1\"]\n";
    config << "# Custom compiler flags for Debug builds\n";
    config << "flags = [\"-g\", \"-O0\"]\n";
    config << "# Custom CMake arguments for Debug builds\n";
    config << "cmake_args = [\"-DENABLE_TESTS=ON\"]\n\n";
    
    config << "[build.config.release]\n";
    config << "# Custom compiler defines for Release builds\n";
    config << "defines = [\"NDEBUG=1\", \"RELEASE=1\"]\n";
    config << "# Custom compiler flags for Release builds\n";
    config << "flags = [\"-O3\"]\n";
    config << "# Custom CMake arguments for Release builds\n";
    config << "cmake_args = [\"-DENABLE_TESTS=OFF\"]\n\n";
    
    config << "[build.config.relwithdebinfo]\n";
    config << "# Custom compiler defines for RelWithDebInfo builds\n";
    config << "defines = [\"NDEBUG=1\"]\n";
    config << "# Custom compiler flags for RelWithDebInfo builds\n";
    config << "flags = [\"-O2\", \"-g\"]\n";
    config << "# Custom CMake arguments for RelWithDebInfo builds\n";
    config << "cmake_args = []\n\n";
    
    config << "[build.config.minsizerel]\n";
    config << "# Custom compiler defines for MinSizeRel builds\n";
    config << "defines = [\"NDEBUG=1\"]\n";
    config << "# Custom compiler flags for MinSizeRel builds\n";
    config << "flags = [\"-Os\"]\n";
    config << "# Custom CMake arguments for MinSizeRel builds\n";
    config << "cmake_args = []\n\n";
    
    config << "[test]\n";
    config << "# Enable or disable testing\n";
    config << "enabled = " << (with_tests ? "true" : "false") << "\n";
    config << "# Test executable name (default: ${project_name}_tests)\n";
    config << "# test_executable = \"${project_name}_tests\"\n";
    config << "# Additional arguments to pass to the test executable\n";
    config << "# args = [\"--gtest_filter=*\", \"--gtest_color=yes\"]\n";
    config << "# Number of parallel test jobs\n";
    config << "# jobs = 4\n\n";
    
    config << "[package]\n";
    config << "# Enable or disable packaging\n";
    config << "enabled = true\n";
    config << "# Package generators to use\n";
    config << "generators = []\n";
    config << "# Windows generators: ZIP, NSIS\n";
    config << "# Linux generators: TGZ, DEB, RPM\n";
    config << "# macOS generators: TGZ, DragNDrop\n";
    config << "vendor = \"Your Organization\"\n";
    config << "contact = \"Your Name <your.email@example.com>\"\n\n";
    
    config << "# [dependencies]\n";
    config << "# vcpkg = [\"fmt\", \"spdlog\"]\n";
    config << "# vcpkg_triplet = \"x64-windows\"  # Specify the vcpkg triplet if needed\n";
    config << "# vcpkg_path = \"/path/to/vcpkg\"  # Custom vcpkg path if needed\n";
    
    config.close();
    logger::print_success("Created cforge.toml file");
    return true;
}

/**
 * @brief Create a simple main.cpp file
 * 
 * @param project_path Path to project directory
 * @param project_name Project name
 * @return bool Success flag
 */
static bool create_main_cpp(const std::filesystem::path& project_path, const std::string& project_name) {
    // Create src directory if it doesn't exist
    std::filesystem::path src_dir = project_path / "src";
    try {
        std::filesystem::create_directories(src_dir);
    } catch (const std::exception& ex) {
        logger::print_error("Failed to create src directory: " + std::string(ex.what()));
        return false;
    }
    
    // Create main.cpp
    std::filesystem::path main_cpp_path = src_dir / "main.cpp";
    std::ofstream main_cpp(main_cpp_path);
    
    if (!main_cpp.is_open()) {
        logger::print_error("Failed to create main.cpp file");
        return false;
    }
    
    main_cpp << "/**\n";
    main_cpp << " * @file main.cpp\n";
    main_cpp << " * @brief Main entry point for " << project_name << "\n";
    main_cpp << " */\n\n";
    
    main_cpp << "#include <iostream>\n\n";
    
    main_cpp << "/**\n";
    main_cpp << " * @brief Main function\n";
    main_cpp << " * \n";
    main_cpp << " * @param argc Argument count\n";
    main_cpp << " * @param argv Argument values\n";
    main_cpp << " * @return int Exit code\n";
    main_cpp << " */\n";
    main_cpp << "int main(int argc, char* argv[]) {\n";
    main_cpp << "    std::cout << \"Hello from " << project_name << "!\" << std::endl;\n";
    main_cpp << "    return 0;\n";
    main_cpp << "}\n";
    
    main_cpp.close();
    
    logger::print_success("Created main.cpp file");
    return true;
}

/**
 * @brief Create include directory with example header
 * 
 * @param project_path Path to project directory
 * @param project_name Project name
 * @return bool Success flag
 */
static bool create_include_files(const std::filesystem::path& project_path, const std::string& project_name) {
    // Create include directory if it doesn't exist
    std::filesystem::path include_dir = project_path / "include";
    try {
        std::filesystem::create_directories(include_dir);
    } catch (const std::exception& ex) {
        logger::print_error("Failed to create include directory: " + std::string(ex.what()));
        return false;
    }
    
    // Create project include directory
    std::filesystem::path project_include_dir = include_dir / project_name;
    try {
        std::filesystem::create_directories(project_include_dir);
    } catch (const std::exception& ex) {
        logger::print_error("Failed to create project include directory: " + std::string(ex.what()));
        return false;
    }
    
    // Create example header file
    std::filesystem::path example_header_path = project_include_dir / "example.hpp";
    std::ofstream example_header(example_header_path);
    
    if (!example_header.is_open()) {
        logger::print_error("Failed to create example header file");
        return false;
    }
    
    example_header << "/**\n";
    example_header << " * @file example.hpp\n";
    example_header << " * @brief Example header file for " << project_name << "\n";
    example_header << " */\n\n";
    
    example_header << "#pragma once\n\n";
    
    example_header << "namespace " << project_name << " {\n\n";
    
    example_header << "/**\n";
    example_header << " * @brief Example function\n";
    example_header << " * \n";
    example_header << " * @return const char* A message\n";
    example_header << " */\n";
    example_header << "const char* get_example_message();\n\n";
    
    example_header << "} // namespace " << project_name << "\n";
    
    example_header.close();
    
    // Create example implementation file
    std::filesystem::path example_cpp_path = project_path / "src" / "example.cpp";
    std::ofstream example_cpp(example_cpp_path);
    
    if (!example_cpp.is_open()) {
        logger::print_error("Failed to create example implementation file");
        return false;
    }
    
    example_cpp << "/**\n";
    example_cpp << " * @file example.cpp\n";
    example_cpp << " * @brief Implementation of example functions for " << project_name << "\n";
    example_cpp << " */\n\n";
    
    example_cpp << "#include \"" << project_name << "/example.hpp\"\n\n";
    
    example_cpp << "namespace " << project_name << " {\n\n";
    
    example_cpp << "const char* get_example_message() {\n";
    example_cpp << "    return \"This is an example function from the " << project_name << " library.\";\n";
    example_cpp << "}\n\n";
    
    example_cpp << "} // namespace " << project_name << "\n";
    
    example_cpp.close();
    
    logger::print_success("Created example header and implementation files");
    return true;
}

/**
 * @brief Create test directory with basic test files and CMake configuration
 * 
 * @param project_path Path to project directory
 * @param project_name Project name
 * @return bool Success flag
 */
static bool create_test_files(const std::filesystem::path& project_path, const std::string& project_name) {
    // Create tests directory if it doesn't exist
    std::filesystem::path tests_dir = project_path / "tests";
    try {
        std::filesystem::create_directories(tests_dir);
    } catch (const std::exception& ex) {
        logger::print_error("Failed to create tests directory: " + std::string(ex.what()));
        return false;
    }
    
    // Create CMakeLists.txt for tests
    std::filesystem::path tests_cmake_path = tests_dir / "CMakeLists.txt";
    std::ofstream tests_cmake(tests_cmake_path);
    
    if (!tests_cmake.is_open()) {
        logger::print_error("Failed to create tests CMakeLists.txt file");
        return false;
    }
    
    tests_cmake << "# Tests CMakeLists.txt for " << project_name << "\n\n";
    
    // Add check for BUILD_TESTING flag
    tests_cmake << "# Only enable tests if BUILD_TESTING is enabled\n";
    tests_cmake << "if(NOT BUILD_TESTING)\n";
    tests_cmake << "    message(STATUS \"Tests disabled - skipping test setup\")\n";
    tests_cmake << "    return()\n";
    tests_cmake << "endif()\n\n";
    
    // GoogleTest integration
    tests_cmake << "# Include FetchContent for Google Test\n";
    tests_cmake << "include(FetchContent)\n";
    tests_cmake << "FetchContent_Declare(\n";
    tests_cmake << "  googletest\n";
    tests_cmake << "  GIT_REPOSITORY https://github.com/google/googletest.git\n";
    tests_cmake << "  GIT_TAG v1.14.0\n";
    tests_cmake << ")\n";
    tests_cmake << "set(gtest_force_shared_crt ON CACHE BOOL \"\" FORCE)\n";
    tests_cmake << "FetchContent_MakeAvailable(googletest)\n\n";
    
    tests_cmake << "# Disable install rules for GoogleTest\n";
    tests_cmake << "set_target_properties(gtest gtest_main gmock gmock_main\n";
    tests_cmake << "    PROPERTIES EXCLUDE_FROM_ALL 1\n";
    tests_cmake << "               EXCLUDE_FROM_DEFAULT_BUILD 1)\n\n";
    
    // Create test executables
    tests_cmake << "# Add test sources\n";
    tests_cmake << "set(TEST_SOURCES\n";
    tests_cmake << "    test_main.cpp\n";
    tests_cmake << "    test_example.cpp\n";
    tests_cmake << ")\n\n";
    
    // Define test executable name with config suffix
    tests_cmake << "# Define test executable name with configuration suffix\n";
    tests_cmake << "string(TOLOWER \"${CMAKE_BUILD_TYPE}\" build_type_lower)\n";
    tests_cmake << "set(TEST_EXECUTABLE_NAME ${PROJECT_NAME}_${build_type_lower}_tests)\n\n";
    
    tests_cmake << "# Add test executable\n";
    tests_cmake << "add_executable(${TEST_EXECUTABLE_NAME} ${TEST_SOURCES})\n\n";
    
    tests_cmake << "# Link with GoogleTest and project libraries\n";
    tests_cmake << "target_link_libraries(${TEST_EXECUTABLE_NAME} PRIVATE\n";
    tests_cmake << "    gtest\n";
    tests_cmake << "    gtest_main\n";
    tests_cmake << "    # Add any project libraries here\n";
    tests_cmake << ")\n\n";
    
    tests_cmake << "# Include directories\n";
    tests_cmake << "target_include_directories(${TEST_EXECUTABLE_NAME} PRIVATE\n";
    tests_cmake << "    ${CMAKE_SOURCE_DIR}/include\n";
    tests_cmake << "    ${CMAKE_SOURCE_DIR}/src\n";
    tests_cmake << ")\n\n";
    
    tests_cmake << "# Register tests with CTest\n";
    tests_cmake << "include(GoogleTest)\n";
    tests_cmake << "gtest_discover_tests(${TEST_EXECUTABLE_NAME})\n";
    
    tests_cmake.close();
    
    // Create test_main.cpp
    std::filesystem::path test_main_path = tests_dir / "test_main.cpp";
    std::ofstream test_main(test_main_path);
    
    if (!test_main.is_open()) {
        logger::print_error("Failed to create test_main.cpp file");
        return false;
    }
    
    test_main << "/**\n";
    test_main << " * @file test_main.cpp\n";
    test_main << " * @brief Main test runner for " << project_name << "\n";
    test_main << " */\n\n";
    
    test_main << "#include <gtest/gtest.h>\n\n";
    
    test_main << "/**\n";
    test_main << " * @brief Main function for running all tests\n";
    test_main << " */\n";
    test_main << "int main(int argc, char** argv) {\n";
    test_main << "    ::testing::InitGoogleTest(&argc, argv);\n";
    test_main << "    return RUN_ALL_TESTS();\n";
    test_main << "}\n";
    
    test_main.close();
    
    // Create test_example.cpp
    std::filesystem::path test_example_path = tests_dir / "test_example.cpp";
    std::ofstream test_example(test_example_path);
    
    if (!test_example.is_open()) {
        logger::print_error("Failed to create test_example.cpp file");
        return false;
    }
    
    test_example << "/**\n";
    test_example << " * @file test_example.cpp\n";
    test_example << " * @brief Example tests for " << project_name << "\n";
    test_example << " */\n\n";
    
    test_example << "#include <gtest/gtest.h>\n";
    test_example << "#include \"" << project_name << "/example.hpp\"\n\n";
    
    test_example << "/**\n";
    test_example << " * @brief Example test case\n";
    test_example << " */\n";
    test_example << "TEST(ExampleTest, BasicTest) {\n";
    test_example << "    // Call the example function from the library\n";
    test_example << "    const char* message = " << project_name << "::get_example_message();\n";
    test_example << "    \n";
    test_example << "    // Verify the result\n";
    test_example << "    ASSERT_NE(message, nullptr);\n";
    test_example << "    EXPECT_TRUE(strlen(message) > 0);\n";
    test_example << "}\n\n";
    
    test_example << "/**\n";
    test_example << " * @brief Another example test case\n";
    test_example << " */\n";
    test_example << "TEST(ExampleTest, TrivialTest) {\n";
    test_example << "    // Basic assertions\n";
    test_example << "    EXPECT_EQ(1, 1);\n";
    test_example << "    EXPECT_TRUE(true);\n";
    test_example << "    EXPECT_FALSE(false);\n";
    test_example << "}\n";
    
    test_example.close();
    
    logger::print_success("Created test files and configuration");
    return true;
}

/**
 * @brief Create default license file (MIT license by default)
 * 
 * @param project_path Path to project directory
 * @param project_name Project name
 * @return bool Success flag
 */
static bool create_license_file(const std::filesystem::path& project_path, const std::string& project_name) {
    std::filesystem::path license_path = project_path / "LICENSE";
    std::ofstream license(license_path);
    
    if (!license.is_open()) {
        logger::print_error("Failed to create LICENSE file");
        return false;
    }
    
    // Get current year for the license
    auto now = std::chrono::system_clock::now();
    std::time_t current_time = std::chrono::system_clock::to_time_t(now);
    std::tm* time_info = std::localtime(&current_time);
    int current_year = time_info->tm_year + 1900;
    
    license << "MIT License\n\n";
    license << "Copyright (c) " << current_year << " " << project_name << "\n\n";
    license << "Permission is hereby granted, free of charge, to any person obtaining a copy\n";
    license << "of this software and associated documentation files (the \"Software\"), to deal\n";
    license << "in the Software without restriction, including without limitation the rights\n";
    license << "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n";
    license << "copies of the Software, and to permit persons to whom the Software is\n";
    license << "furnished to do so, subject to the following conditions:\n\n";
    license << "The above copyright notice and this permission notice shall be included in all\n";
    license << "copies or substantial portions of the Software.\n\n";
    license << "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n";
    license << "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n";
    license << "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n";
    license << "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n";
    license << "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n";
    license << "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n";
    license << "SOFTWARE.\n";
    
    license.close();
    
    logger::print_success("Created LICENSE file (MIT license)");
    return true;
}

/**
 * @brief Check if git is available on the system
 * 
 * @return bool True if git is available
 */
static bool is_git_available() {
    return is_command_available("git");
}

/**
 * @brief Init a new git repository if requested and git is available
 * 
 * @param project_path Path to project directory
 * @param verbose Verbose output flag
 * @return bool Success flag (true if successful or git not available/not requested)
 */
static bool init_git_repository(const std::filesystem::path& project_path, bool verbose) {
    // If Git initialization wasn't explicitly requested, just return success
    if (!verbose) {
        logger::print_status("Git initialization skipped (use --git flag to enable)");
        return true;
    }
    
    // First check if git is available
    if (!is_git_available()) {
        logger::print_warning("Git not found in PATH. Skipping git initialization.");
        return true; // Not critical for project creation
    }
    
    logger::print_status("Initializing git repository...");
    
    std::vector<std::string> git_args = {"init"};
    
    // Use a shorter timeout for git init
    bool result = execute_tool("git", git_args, project_path.string(), "Git", verbose, 20);
    
    if (result) {
        logger::print_success("Initialized git repository");
        
        // Create an initial commit (optional)
        if (verbose) {
            logger::print_status("Creating initial commit...");
            
            // Add all files
            std::vector<std::string> git_add_args = {"add", "."};
            bool add_result = execute_tool("git", git_add_args, project_path.string(), "Git add", verbose, 10);
            
            if (add_result) {
                // Commit
                std::vector<std::string> git_commit_args = {"commit", "-m", "Initial commit"};
                bool commit_result = execute_tool("git", git_commit_args, project_path.string(), "Git commit", verbose, 10);
                
                if (commit_result) {
                    logger::print_success("Created initial commit");
                } else {
                    logger::print_warning("Failed to create initial commit. This is not critical.");
                }
            } else {
                logger::print_warning("Failed to add files to git. This is not critical.");
            }
        }
    } else {
        logger::print_warning("Failed to initialize git repository. This is not critical for project creation.");
    }
    
    // Return true regardless of git initialization result
    // as this is not critical for project creation
    return true;
}

/**
 * @brief Create all the project files and structure
 * 
 * @param project_path Path to project directory
 * @param project_name Project name
 * @param cpp_version C++ standard version
 * @param initialize_git Whether to initialize a git repository
 * @param verbose Verbose output flag
 * @param with_tests Whether to include test files
 * @return bool Success flag
 */
static bool create_project(
    const std::filesystem::path& project_path,
    const std::string& project_name,
    const std::string& cpp_version,
    bool initialize_git,
    bool verbose,
    bool with_tests = false
) {
    // Create project directory if it doesn't exist
    try {
        if (!std::filesystem::exists(project_path)) {
            bool created = std::filesystem::create_directories(project_path);
            if (!created) {
                logger::print_error("Failed to create project directory (returned false)");
                return false;
            }
            logger::print_status("Created project directory: " + project_path.string());
        } else {
            logger::print_status("Using existing project directory: " + project_path.string());
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception creating project directory: " + std::string(ex.what()));
        return false;
    }
    
    // Test write permissions
    try {
        std::string test_path = (project_path / "cforge_test_file").string();
        std::ofstream test_file(test_path);
        if (!test_file.is_open()) {
            logger::print_error("Project directory is not writable: " + project_path.string());
            return false;
        }
        test_file.close();
        std::filesystem::remove(test_path);
        logger::print_status("Project directory is writable");
    } catch (const std::exception& ex) {
        logger::print_error("Failed to write to project directory: " + std::string(ex.what()));
        return false;
    }
    
    // Create project files with better error handling
    bool success = true;
    
    // Create .gitignore
    try {
        if (!create_gitignore(project_path)) {
            logger::print_error("Failed to create .gitignore file");
            success = false;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception creating .gitignore: " + std::string(ex.what()));
        success = false;
    }
    
    // Create README.md
    try {
        if (!create_readme(project_path, project_name)) {
            logger::print_error("Failed to create README.md file");
            success = false;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception creating README.md: " + std::string(ex.what()));
        success = false;
    }
    
    // Create cforge.toml
    try {
        if (!create_cforge_toml(project_path, project_name, cpp_version, with_tests)) {
            logger::print_error("Failed to create cforge.toml file");
            success = false;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception creating cforge.toml: " + std::string(ex.what()));
        success = false;
    }
    
    // Create main.cpp
    try {
        if (!create_main_cpp(project_path, project_name)) {
            logger::print_error("Failed to create main.cpp file");
            success = false;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception creating main.cpp: " + std::string(ex.what()));
        success = false;
    }
    
    // Create include files
    try {
        if (!create_include_files(project_path, project_name)) {
            logger::print_error("Failed to create include files");
            success = false;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception creating include files: " + std::string(ex.what()));
        success = false;
    }
    
    // Create test files if tests are enabled
    if (with_tests) {
        try {
            if (!create_test_files(project_path, project_name)) {
                logger::print_error("Failed to create test files");
                success = false;
            }
        } catch (const std::exception& ex) {
            logger::print_error("Exception creating test files: " + std::string(ex.what()));
            success = false;
        }
    }
    
    // Create license file
    try {
        if (!create_license_file(project_path, project_name)) {
            logger::print_error("Failed to create LICENSE file");
            success = false;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception creating LICENSE file: " + std::string(ex.what()));
        success = false;
    }
    
    // Initialize git repository if requested
    if (initialize_git && success) {
        try {
            if (!init_git_repository(project_path, verbose)) {
                logger::print_warning("Failed to initialize git repository");
                // Continue anyway, not critical
            }
        } catch (const std::exception& ex) {
            logger::print_warning("Exception initializing git: " + std::string(ex.what()));
            // Continue anyway, not critical
        }
    }
    
    if (success) {
        logger::print_success("Project created successfully: " + project_name);
        logger::print_status("You can now build the project with: cforge build");
        logger::print_status("Or run the project with: cforge run");
        logger::print_status("CMakeLists.txt will be generated automatically when you run build");
    } else {
        logger::print_error("Failed to create some project files");
    }
    
    return success;
}

/**
 * @brief Handle the 'init' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_init(const cforge_context_t* ctx) {
    // Set flush after each log message to ensure output is visible
    std::cout.flush();
    std::cerr.flush();
    
    try {
        logger::print_status("Starting init command execution");
        

        // Debug the WORKSPACE_FILE constant
        logger::print_status("WORKSPACE_FILE defined as: " + std::string(WORKSPACE_FILE));
        
        // Check if a workspace configuration file exists in the current directory
        std::filesystem::path workspace_file_path = std::filesystem::path(ctx->working_dir) / WORKSPACE_FILE;
        bool workspace_file_exists = std::filesystem::exists(workspace_file_path);
        
        // Default project and workspace names
        std::string project_name = "cpp-project";
        bool is_workspace = false;
        bool from_file = false;
        std::string workspace_name;
        std::vector<std::string> project_names;
        std::string cpp_standard = "17";
        bool with_tests = false;
        bool with_git = false;
        std::string template_name = "app";
        bool has_projects_flag = false;

        // Process command line arguments - improved parsing
        if (ctx->args.args) {
            // First check for positional project name (before any flag)
            if (ctx->args.arg_count > 0 && ctx->args.args[0][0] != '-') {
                project_name = ctx->args.args[0];
                logger::print_status("Using positional project name: " + project_name);
            }
            
            // Parse flag arguments
            for (int i = 0; i < ctx->args.arg_count; ++i) {
                std::string arg = ctx->args.args[i];
                
                // Handle --from-file flag
                if (arg == "--from-file" || arg == "-f") {
                    from_file = true;
                    logger::print_status("Will use existing workspace.cforge.toml file");
                }
                // Handle --name or -n parameter
                else if (arg == "--name" || arg == "-n") {
                    if (i+1 < ctx->args.arg_count && ctx->args.args[i+1][0] != '-') {
                        project_name = ctx->args.args[i+1];
                        logger::print_status("Using project name from --name flag: " + project_name);
                        i++; // Skip the value in next iteration
                    }
                }
                // Handle --name=VALUE format
                else if (arg.compare(0, 7, "--name=") == 0) {
                    project_name = arg.substr(7);
                    logger::print_status("Using project name from --name= format: " + project_name);
                }
                // Handle --workspace or -w parameter
                else if (arg == "--workspace" || arg == "-w") {
                    is_workspace = true;
                    
                    // Check if next argument exists and is a value (not a flag)
                    if (i+1 < ctx->args.arg_count && ctx->args.args[i+1][0] != '-') {
                        workspace_name = ctx->args.args[i+1];
                        logger::print_status("Using workspace name from command line: " + workspace_name);
                        i++; // Skip the value in next iteration
                    } else {
                        // Use current directory name if no name provided
                        workspace_name = std::filesystem::path(ctx->working_dir).filename().string();
                        logger::print_status("No workspace name provided, using current directory: " + workspace_name);
                    }
                }
                // Handle --workspace=VALUE format
                else if (arg.compare(0, 12, "--workspace=") == 0) {
                    is_workspace = true;
                    workspace_name = arg.substr(12);
                    logger::print_status("Using workspace name from --workspace= format: " + workspace_name);
                }
                // Handle --projects or -p parameter
                else if (arg == "--projects" || arg == "-p") {
                    has_projects_flag = true;
                    
                    // If the next argument is a flag or end of args, use empty project list
                    if (i+1 >= ctx->args.arg_count || ctx->args.args[i+1][0] == '-') {
                        logger::print_warning("--projects flag provided but no projects specified");
                        continue;
                    }
                    
                    // Check if it's a comma-separated list
                    if (std::string(ctx->args.args[i+1]).find(',') != std::string::npos) {
                        // Parse comma-separated list
                        std::string projects_arg = ctx->args.args[i+1];
                        project_names = parse_project_list(projects_arg);
                        i++; // Skip the list in next iteration
                    } else {
                        // Collect all arguments until next flag
                        int j = i + 1;
                        while (j < ctx->args.arg_count && ctx->args.args[j][0] != '-') {
                            project_names.push_back(ctx->args.args[j]);
                            j++;
                        }
                        i = j - 1; // Update index to the last project name
                    }
                    
                    // Log found projects
                    for (const auto& proj : project_names) {
                        logger::print_status("Found project: " + proj);
                    }
                }
                // Handle --projects=VALUE format
                else if (arg.compare(0, 11, "--projects=") == 0) {
                    has_projects_flag = true;
                    std::string projects_list = arg.substr(11);
                    project_names = parse_project_list(projects_list);
                    
                    for (const auto& proj : project_names) {
                        logger::print_status("Found project from list: " + proj);
                    }
                }
                // Handle --cpp or -c parameter for C++ standard
                else if (arg == "--cpp" || arg == "-c") {
                    if (i+1 < ctx->args.arg_count) {
                        cpp_standard = ctx->args.args[i+1];
                        i++; // Skip the value in next iteration
                    }
                }
                // Handle --cpp=VALUE format
                else if (arg.compare(0, 6, "--cpp=") == 0) {
                    cpp_standard = arg.substr(6);
                }
                // Handle --with-tests or -t flag
                else if (arg == "--with-tests" || arg == "-t") {
                    with_tests = true;
                }
                // Handle --with-git or -g flag
                else if (arg == "--with-git" || arg == "-g") {
                    with_git = true;
                }
                // Handle --template parameter
                else if (arg == "--template") {
                    if (i+1 < ctx->args.arg_count) {
                        template_name = ctx->args.args[i+1];
                        i++; // Skip the value in next iteration
                    }
                }
                // Handle --template=VALUE format
                else if (arg.compare(0, 11, "--template=") == 0) {
                    template_name = arg.substr(11);
                }
            }
        }
        
        // If user didn't set workspace flag but specified projects, don't force workspace mode
        bool create_multiple_projects = has_projects_flag && !project_names.empty();
        
        // If this is a workspace but no projects specified, use the project name as default project
        if (is_workspace && project_names.empty()) {
            project_names.push_back(project_name);
            logger::print_status("No projects specified for workspace, using default project: '" + project_name + "'");
        }
        
        // Log configuration details
        logger::print_status("Configuration:");
        logger::print_status("- Is workspace: " + std::string(is_workspace ? "yes" : "no"));
        if (is_workspace) {
            logger::print_status("- Workspace name: " + workspace_name);
            logger::print_status("- Project count: " + std::to_string(project_names.size()));
            logger::print_status("- Projects: ");
            for (const auto& proj : project_names) {
                logger::print_status("  * " + proj);
            }
        } else if (create_multiple_projects) {
            logger::print_status("- Creating multiple projects: " + std::to_string(project_names.size()));
            logger::print_status("- Projects: ");
            for (const auto& proj : project_names) {
                logger::print_status("  * " + proj);
            }
        } else {
            logger::print_status("- Project name: " + project_name);
        }
        logger::print_status("- C++ standard: " + cpp_standard);
        logger::print_status("- With tests: " + std::string(with_tests ? "yes" : "no"));
        logger::print_status("- With git: " + std::string(with_git ? "yes" : "no"));
        logger::print_status("- Using template: " + template_name);
        
        // Handle loading from existing workspace file if requested
        if (from_file && workspace_file_exists) {
            logger::print_status("Initializing from existing workspace file: " + workspace_file_path.string());
            
            // Load the workspace configuration
            workspace_config config;
            if (!config.load(workspace_file_path.string())) {
                logger::print_error("Failed to load workspace configuration from " + workspace_file_path.string());
                return 1;
            }
            
            // Get the workspace name
            std::string ws_name = config.get_name();
            logger::print_status("Loaded workspace: " + ws_name);
            
            // Get the projects from the workspace
            std::vector<workspace_project> projects = config.get_projects();
            logger::print_status("Found " + std::to_string(projects.size()) + " projects in workspace");
            
            // Create each project
            for (const auto& project : projects) {
                logger::print_status("Creating project '" + project.name + "' from workspace configuration...");
                
                // Create the project directory
                std::filesystem::path project_dir = std::filesystem::path(ctx->working_dir) / project.path;
                if (!std::filesystem::exists(project_dir)) {
                    try {
                        std::filesystem::create_directories(project_dir);
                        logger::print_status("Created project directory: " + project_dir.string());
                    } catch (const std::exception& ex) {
                        logger::print_error("Failed to create project directory: " + project_dir.string() + " Error: " + ex.what());
                        continue;
                    }
                }
                
                // Create the project files
                if (!create_project(project_dir, project.name, cpp_standard, with_git, true, with_tests)) {
                    logger::print_error("Failed to create project '" + project.name + "'");
                    continue;
                }
                
                logger::print_success("Project '" + project.name + "' created successfully");
            }
            
            logger::print_success("All projects from workspace file initialized successfully");
            return 0;
        }
        // Handle workspace creation
        else if (is_workspace) {
            logger::print_status("Creating workspace '" + workspace_name + "' with " + 
                              std::to_string(project_names.size()) + " project(s)...");
            
            // Create workspace directory
            std::filesystem::path workspace_dir = ctx->working_dir;
            
            // Use the provided workspace name to create a subdirectory
            if (!workspace_name.empty()) {
                workspace_dir = workspace_dir / workspace_name;
                logger::print_status("Full workspace path: " + workspace_dir.string());
                
                // Create the workspace directory if it doesn't exist
                if (!std::filesystem::exists(workspace_dir)) {
                    try {
                        bool created = std::filesystem::create_directories(workspace_dir);
                        if (created) {
                            logger::print_status("Successfully created workspace directory: " + workspace_dir.string());
                        } else {
                            logger::print_error("Failed to create workspace directory (returned false)");
                            return 1;
                        }
                    } catch (const std::exception& ex) {
                        logger::print_error("Exception creating workspace directory: " + std::string(ex.what()));
                        return 1;
                    }
                } else {
                    logger::print_status("Using existing workspace directory: " + workspace_dir.string());
                }
            }
            
            // Test file creation to verify permissions
            try {
                std::string test_path = (workspace_dir / "cforge_test_file").string();
                std::ofstream test_file(test_path);
                if (!test_file.is_open()) {
                    logger::print_error("Workspace directory is not writable: " + workspace_dir.string());
                    logger::print_error("Please check permissions or try a different location");
                    return 1;
                }
                test_file.close();
                std::filesystem::remove(test_path);
                logger::print_status("Workspace directory is writable");
            } catch (const std::exception& ex) {
                logger::print_error("Failed to write to workspace directory: " + std::string(ex.what()));
                return 1;
            }

            logger::print_status("Creating workspace at: " + workspace_dir.string());
            
            // Debug: Print what config file we're trying to create
            std::filesystem::path config_path = workspace_dir / WORKSPACE_FILE;
            logger::print_status("Workspace config will be created at: " + config_path.string());
            
            // Build projects string for logging
            std::string projects_str = "";
            for (size_t i = 0; i < project_names.size(); ++i) {
                if (i > 0) projects_str += ", ";
                projects_str += project_names[i];
            }
            logger::print_status("Adding projects: " + projects_str);
            
            // Create workspace configuration file directly
            std::ofstream config_file(config_path);
            if (!config_file.is_open()) {
                logger::print_error("Failed to create workspace configuration file: " + config_path.string());
                return 1;
            }
            
            // Write a basic TOML configuration
            config_file << "[workspace]\n";
            config_file << "name = \"" << workspace_name << "\"\n";
            config_file << "description = \"A C++ workspace created with cforge\"\n\n";
            
            // Write projects list
            config_file << "# Projects in format: name:path:is_startup_project\n";
            config_file << "projects = [\n";
            
            for (size_t i = 0; i < project_names.size(); ++i) {
                const auto& proj_name = project_names[i];
                bool is_startup = (i == 0); // First project is startup by default
                
                config_file << "  \"" << proj_name << ":" 
                            << proj_name << ":" // Path is the same as name by default
                            << (is_startup ? "true" : "false") << "\"";
                
                if (i < project_names.size() - 1) {
                    config_file << ",";
                }
                config_file << "\n";
            }
            
            config_file << "]\n\n";
            config_file << "# Default startup project is the first project\n";
            if (!project_names.empty()) {
                config_file << "default_startup_project = \"" << project_names[0] << "\"\n";
            }
            
            config_file.close();
            logger::print_success("Created workspace configuration file");
            
            // Now create each project directly instead of using init_workspace
            bool all_projects_success = true;
            
            for (const auto& proj_name : project_names) {
                std::filesystem::path project_dir = workspace_dir / proj_name;
                logger::print_status("Creating project '" + proj_name + "' at " + project_dir.string());
                
                // Create the project with detailed logging
                if (!create_project(project_dir, proj_name, cpp_standard, with_git, true, with_tests)) {
                    logger::print_error("Failed to create project '" + proj_name + "'");
                    all_projects_success = false;
                    // Continue with other projects instead of stopping
                    continue;
                }
                
                logger::print_success("Project '" + proj_name + "' created successfully");
            }
            
            if (all_projects_success) {
                logger::print_success("Workspace '" + workspace_name + "' created successfully");
            } else {
                logger::print_warning("Workspace '" + workspace_name + "' created with some errors");
            }
            
            return all_projects_success ? 0 : 1;
        }
        // Handle multiple projects creation (without workspace)
        else if (create_multiple_projects) {
            logger::print_status("Creating " + std::to_string(project_names.size()) + " standalone projects...");
            
            bool all_projects_success = true;
            
            for (const auto& proj_name : project_names) {
                // Create the project in a new directory named after the project
                std::filesystem::path project_dir = std::filesystem::path(ctx->working_dir) / proj_name;
                logger::print_status("Creating project '" + proj_name + "' at " + project_dir.string());
                
                // Create the directory if it doesn't exist
                if (!std::filesystem::exists(project_dir)) {
                    try {
                        bool created = std::filesystem::create_directories(project_dir);
                        if (!created) {
                            logger::print_error("Failed to create project directory for '" + proj_name + "' (returned false)");
                            all_projects_success = false;
                            continue;
                        }
                    } catch (const std::exception& ex) {
                        logger::print_error("Exception creating project directory for '" + proj_name + "': " + std::string(ex.what()));
                        all_projects_success = false;
                        continue;
                    }
                }
                
                // Create the project files
                if (!create_project(project_dir, proj_name, cpp_standard, with_git, 
                                  logger::get_verbosity() >= log_verbosity::VERBOSITY_VERBOSE, 
                                  with_tests)) {
                    logger::print_error("Failed to create project '" + proj_name + "'");
                    all_projects_success = false;
                    continue;
                }
                
                logger::print_success("Project '" + proj_name + "' created successfully");
            }
            
            if (all_projects_success) {
                logger::print_success("All projects created successfully");
            } else {
                logger::print_warning("Some projects could not be created");
            }
            
            return all_projects_success ? 0 : 1;
        }
        // Handle single project creation
        else {
            logger::print_status("Creating project '" + project_name + "'...");
            
            // Decide whether to create in current directory or subdirectory
            std::filesystem::path project_dir;
            
            // If we're using a positional project name or explicit --name parameter,
            // create a subdirectory; otherwise use the current directory
            if (ctx->args.arg_count > 0 && ctx->args.args[0][0] != '-') {
                // Use the provided project name to create a subdirectory
                project_dir = std::filesystem::path(ctx->working_dir) / project_name;
                logger::print_status("Project will be created in new directory: " + project_dir.string());
                
                // Create the directory if it doesn't exist
                if (!std::filesystem::exists(project_dir)) {
                    try {
                        bool created = std::filesystem::create_directories(project_dir);
                        if (!created) {
                            logger::print_error("Failed to create project directory (returned false)");
                            return 1;
                        }
                    } catch (const std::exception& ex) {
                        logger::print_error("Exception creating project directory: " + std::string(ex.what()));
                        return 1;
                    }
                }
            } else {
                // Create in the current directory
                project_dir = std::filesystem::path(ctx->working_dir);
                logger::print_status("Project will be created in current directory: " + project_dir.string());
            }
            
            // Verify the directory is writable
            try {
                // Test file creation
                std::string test_path = (project_dir / "cforge_test_file").string();
                std::ofstream test_file(test_path);
                if (!test_file.is_open()) {
                    logger::print_error("Directory is not writable: " + project_dir.string());
                    logger::print_error("Please check permissions or try a different location");
                    return 1;
                }
                test_file.close();
                std::filesystem::remove(test_path);
            } catch (const std::exception& ex) {
                logger::print_error("Failed to write to directory: " + std::string(ex.what()));
                return 1;
            }
            
            // Create the project files
            logger::print_status("Creating project files...");
            
            if (!create_project(project_dir, project_name, cpp_standard, with_git, 
                              logger::get_verbosity() >= log_verbosity::VERBOSITY_VERBOSE, 
                              with_tests)) {
                logger::print_error("Failed to create project '" + project_name + "'");
                return 1;
            }
            
            logger::print_success("Project '" + project_name + "' created successfully");
        }
        
        logger::print_success("Command completed successfully");
        return 0;
    } catch (const std::exception& ex) {
        logger::print_error("Failed to initialize project: " + std::string(ex.what()));
        return 1;
    }
}