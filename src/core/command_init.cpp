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

using namespace cforge;

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
    
    while ((end = project_list.find(',', start)) != std::string::npos) {
        // Extract the substring and trim whitespace
        std::string project = project_list.substr(start, end - start);
        // Add to result if not empty
        if (!project.empty()) {
            result.push_back(project);
        }
        start = end + 1;
    }
    
    // Add the last part
    std::string last_project = project_list.substr(start);
    if (!last_project.empty()) {
        result.push_back(last_project);
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
 * @brief Create default CMakeLists.txt file with proper packaging and testing support
 * 
 * @param project_path Path to project directory
 * @param project_name Project name
 * @param cpp_version C++ standard version (e.g., "17")
 * @return bool Success flag
 */
static bool create_cmakelists(
    const std::filesystem::path& project_path,
    const std::string& project_name,
    const std::string& cpp_version
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
    
    // vcpkg integration
    cmakelists << "# vcpkg integration - uncomment the line below if using vcpkg\n";
    cmakelists << "# set(CMAKE_TOOLCHAIN_FILE \"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\" CACHE STRING \"\")\n\n";
    
    // Dependencies
    cmakelists << "# Find dependencies (if using vcpkg)\n";
    cmakelists << "# find_package(SomePackage REQUIRED)\n\n";
    
    // Source files
    cmakelists << "# Add source files\n";
    cmakelists << "file(GLOB_RECURSE SOURCES src/*.cpp)\n\n";
    
    // Define the executable name
    cmakelists << "# Define the executable name\n";
    cmakelists << "set(EXECUTABLE_NAME ${PROJECT_NAME})\n\n";
    
    // Support configuration-specific executable names if desired
    cmakelists << "# Configuration-specific executable name (comment out if not needed)\n";
    cmakelists << "# For debug builds, append '_d' to executable name\n";
    cmakelists << "if(CMAKE_BUILD_TYPE STREQUAL \"Debug\")\n";
    cmakelists << "    set(EXECUTABLE_NAME ${EXECUTABLE_NAME}_d)\n";
    cmakelists << "endif()\n\n";
    
    // Add executable
    cmakelists << "# Add executable\n";
    cmakelists << "add_executable(${EXECUTABLE_NAME} ${SOURCES})\n\n";
    
    // Include directories
    cmakelists << "# Include directories\n";
    cmakelists << "target_include_directories(${EXECUTABLE_NAME} PRIVATE\n";
    cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/include\n";
    cmakelists << ")\n\n";
    
    // Link libraries
    cmakelists << "# Link libraries\n";
    cmakelists << "# target_link_libraries(${EXECUTABLE_NAME} PRIVATE SomePackage)\n\n";
    
    // Compiler warnings
    cmakelists << "# Enable compiler warnings\n";
    cmakelists << "if(MSVC)\n";
    cmakelists << "    target_compile_options(${EXECUTABLE_NAME} PRIVATE /W4)\n";
    cmakelists << "else()\n";
    cmakelists << "    target_compile_options(${EXECUTABLE_NAME} PRIVATE -Wall -Wextra -Wpedantic)\n";
    cmakelists << "endif()\n\n";
    
    // Testing
    cmakelists << "# Testing configuration\n";
    cmakelists << "enable_testing()\n";
    cmakelists << "option(BUILD_TESTING \"Build the testing tree.\" ON)\n\n";
    
    cmakelists << "if(BUILD_TESTING)\n";
    cmakelists << "    # Add test directory\n";
    cmakelists << "    add_subdirectory(tests)\n";
    cmakelists << "endif()\n\n";
    
    // Installation
    cmakelists << "# Installation configuration\n";
    cmakelists << "install(TARGETS ${EXECUTABLE_NAME}\n";
    cmakelists << "    RUNTIME DESTINATION bin\n";
    cmakelists << "    LIBRARY DESTINATION lib\n";
    cmakelists << "    ARCHIVE DESTINATION lib\n";
    cmakelists << ")\n\n";
    
    cmakelists << "# Install headers\n";
    cmakelists << "install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/\n";
    cmakelists << "    DESTINATION include\n";
    cmakelists << "    FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
    cmakelists << ")\n\n";
    
    // CPack
    cmakelists << "# Packaging configuration with CPack\n";
    cmakelists << "include(CPack)\n";
    cmakelists << "set(CPACK_PACKAGE_NAME ${PROJECT_NAME})\n";
    cmakelists << "set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})\n";
    cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_NAME} - A C++ project created with cforge\")\n";
    cmakelists << "set(CPACK_PACKAGE_VENDOR \"Your Organization\")\n";
    cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_FILE \"${CMAKE_CURRENT_SOURCE_DIR}/README.md\")\n";
    cmakelists << "set(CPACK_RESOURCE_FILE_LICENSE \"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE\")\n\n";
    
    // OS specific packaging settings
    cmakelists << "# OS specific packaging settings\n";
    cmakelists << "if(WIN32)\n";
    cmakelists << "    set(CPACK_GENERATOR \"ZIP;NSIS\")\n";
    cmakelists << "    set(CPACK_NSIS_PACKAGE_NAME \"${PROJECT_NAME}\")\n";
    cmakelists << "    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)\n";
    cmakelists << "elseif(APPLE)\n";
    cmakelists << "    set(CPACK_GENERATOR \"TGZ;DragNDrop\")\n";
    cmakelists << "else()\n";
    cmakelists << "    set(CPACK_GENERATOR \"TGZ;DEB\")\n";
    cmakelists << "    set(CPACK_DEBIAN_PACKAGE_MAINTAINER \"Your Name\")\n";
    cmakelists << "    set(CPACK_DEBIAN_PACKAGE_SECTION \"devel\")\n";
    cmakelists << "endif()\n\n";
    
    // Custom targets for different configurations
    cmakelists << "# Add a custom target to build all configurations\n";
    cmakelists << "add_custom_target(build-all)\n\n";
    
    cmakelists << "# Helper macro to add configuration-specific build targets\n";
    cmakelists << "macro(add_build_configuration CONFIG)\n";
    cmakelists << "    string(TOLOWER \"${CONFIG}\" CONFIG_LOWER)\n";
    cmakelists << "    add_custom_target(build-${CONFIG_LOWER}\n";
    cmakelists << "        COMMAND ${CMAKE_COMMAND} --build . --config ${CONFIG}\n";
    cmakelists << "        COMMENT \"Building ${CONFIG} configuration\"\n";
    cmakelists << "    )\n";
    cmakelists << "    add_dependencies(build-all build-${CONFIG_LOWER})\n";
    cmakelists << "endmacro()\n\n";
    
    cmakelists << "# Add standard configurations\n";
    cmakelists << "add_build_configuration(Debug)\n";
    cmakelists << "add_build_configuration(Release)\n";
    cmakelists << "add_build_configuration(RelWithDebInfo)\n";
    cmakelists << "add_build_configuration(MinSizeRel)\n";
    
    cmakelists.close();
    logger::print_success("Created CMakeLists.txt file");
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
    const std::string& cpp_version
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
    config << "enabled = true\n";
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
 * @return bool Success flag
 */
static bool create_project(
    const std::filesystem::path& project_path,
    const std::string& project_name,
    const std::string& cpp_version,
    bool initialize_git,
    bool verbose
) {
    // Create project directory if it doesn't exist
    try {
        std::filesystem::create_directories(project_path);
    } catch (const std::exception& ex) {
        logger::print_error("Failed to create project directory: " + std::string(ex.what()));
        return false;
    }
    
    // Create project files
    bool success = true;
    success &= create_gitignore(project_path);
    success &= create_readme(project_path, project_name);
    // Don't create CMakeLists.txt - it will be generated by the build command
    // success &= create_cmakelists(project_path, project_name, cpp_version);
    success &= create_cforge_toml(project_path, project_name, cpp_version);
    success &= create_main_cpp(project_path, project_name);
    success &= create_include_files(project_path, project_name);
    success &= create_test_files(project_path, project_name);
    success &= create_license_file(project_path, project_name);
    
    // Initialize git repository if requested
    if (initialize_git && success) {
        success &= init_git_repository(project_path, verbose);
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
 * @brief Initialize a workspace with multiple projects
 * 
 * @param workspace_name Name of the workspace
 * @param workspace_path Path where to create the workspace
 * @param project_names Names of projects to create in the workspace
 * @param cpp_version C++ standard version
 * @param with_tests Whether to include test files
 * @param with_git Whether to initialize git repository
 * @param verbose Verbose output
 * @return true if successful
 */
static bool init_workspace(
    const std::string& workspace_name,
    const std::filesystem::path& workspace_path,
    const std::vector<std::string>& project_names,
    const std::string& cpp_version,
    bool with_tests,
    bool with_git,
    bool verbose
) {
    // Create workspace directory if needed
    bool create_new_dir = !workspace_name.empty() && workspace_name != workspace_path.filename().string();
    std::filesystem::path workspace_dir = workspace_path;
    
    if (create_new_dir) {
        workspace_dir = workspace_path / workspace_name;
        if (!std::filesystem::exists(workspace_dir)) {
            try {
                std::filesystem::create_directories(workspace_dir);
            } catch (const std::exception& ex) {
                logger::print_error("Failed to create workspace directory: " + workspace_dir.string() + " Error: " + ex.what());
                return false;
            }
        }
    }
    
    // Create workspace configuration
    workspace_config config;
    config.set_name(workspace_name.empty() ? workspace_dir.filename().string() : workspace_name);
    config.set_description("A C++ workspace");
    
    // Create specified projects
    std::string ws_name = config.get_name();
    logger::print_status("Creating " + std::to_string(project_names.size()) + " projects in workspace '" + ws_name + "'");
    
    for (const auto& project_name : project_names) {
        // Create project directory inside workspace
        std::filesystem::path project_dir = workspace_dir / project_name;
        if (!std::filesystem::exists(project_dir)) {
            try {
                std::filesystem::create_directories(project_dir);
            } catch (const std::exception& ex) {
                logger::print_error("Failed to create project directory: " + project_dir.string() + " Error: " + ex.what());
                return false;
            }
        }
        
        // Create project files
        if (!create_project(project_dir, project_name, cpp_version, with_git, verbose)) {
            logger::print_error("Failed to create project '" + project_name + "'");
            return false;
        }
        
        // Add project to workspace config (using relative path)
        workspace_project project;
        project.name = project_name;
        project.path = std::filesystem::path(project_name); // Store as a relative path
        project.is_startup_project = (project_name == project_names.front()); // First project is startup by default
        config.get_projects().push_back(project);
    }
    
    // Save workspace configuration
    std::filesystem::path config_path = workspace_dir / WORKSPACE_FILE;
    if (!config.save(config_path.string())) {
        logger::print_error("Failed to save workspace configuration");
        return false;
    }
    
    logger::print_success("Workspace '" + config.get_name() + "' created successfully");
    return true;
}

/**
 * @brief Handle the 'init' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_init(const cforge_context_t* ctx) {
    try {
        logger::print_status("Starting init command execution");
        
        // Get project/workspace name from arguments or use default
        std::string name = "cpp-project";
        if (ctx->args.args && ctx->args.args[0] && ctx->args.args[0][0] != '-') {
            name = ctx->args.args[0];
        }

        // Determine if this is a workspace creation request
        bool is_workspace = false;
        std::string workspace_name;
        std::vector<std::string> project_names;

        logger::print_status("Checking for workspace flag and projects...");
        
        // Process command line arguments
        if (ctx->args.args) {
            logger::print_status("Processing command line arguments...");
            
            try {
                // Log all raw arguments
                int arg_count = 0;
                for (int i = 0; ctx->args.args[i] && i < 100; ++i) { // Cap at 100 for safety
                    logger::print_status("Raw argument [" + std::to_string(i) + "]: '" + ctx->args.args[i] + "'");
                    arg_count++;
                }
                logger::print_status("Total arguments: " + std::to_string(arg_count));
                
                for (int i = 0; ctx->args.args[i] && i < 100; ++i) { // Cap at 100 for safety
                    std::string arg = ctx->args.args[i];
                    logger::print_status("Processing argument " + std::to_string(i) + ": '" + arg + "'");
                    
                    // Parse --workspace[=name] or --workspace name 
                    if (arg == "--workspace") {
                        is_workspace = true;
                        // Check if next argument exists and is a valid name (not a flag or the end of arguments)
                        if (ctx->args.args[i+1] && ctx->args.args[i+1][0] != '-') {
                            workspace_name = ctx->args.args[i+1];
                            logger::print_status("Found workspace name: '" + workspace_name + "'");
                            i++; // Skip the workspace name in the next iteration
                        } else {
                            // If no name provided, use the current directory as the workspace
                            workspace_name = std::filesystem::path(ctx->working_dir).filename().string();
                            logger::print_status("No workspace name provided after --workspace flag, using current directory name: '" + workspace_name + "'");
                        }
                    } 
                    // Check for --workspace=NAME format
                    else if (arg.compare(0, 12, "--workspace=") == 0) {
                        is_workspace = true;
                        workspace_name = arg.substr(12);
                        logger::print_status("Found workspace name from --workspace= format: '" + workspace_name + "'");
                    }
                    // Handle --projects flag
                    else if (arg == "--projects") {
                        // Collect all project names until we hit another flag or end of arguments
                        i++; // Move to the next argument
                        while (ctx->args.args[i] && ctx->args.args[i][0] != '-') {
                            project_names.push_back(ctx->args.args[i]);
                            logger::print_status(std::string("Added project: '") + ctx->args.args[i] + "'");
                            i++;
                        }
                        i--; // Adjust for the loop increment
                    }
                }
                logger::print_status("Finished argument processing loop");
            } catch (const std::exception& ex) {
                logger::print_error("Exception during argument processing: " + std::string(ex.what()));
            }
            
            logger::print_status("Finished processing command line arguments");
        }

        logger::print_status("After argument parsing, is_workspace=" + 
                          std::string(is_workspace ? "true" : "false") + 
                          ", workspace_name='" + workspace_name + "'" +
                          ", project_count=" + std::to_string(project_names.size()));

        // Get C++ standard from arguments or use default
        std::string cpp_standard = "17";
        if (ctx->args.args) {
            for (int i = 0; ctx->args.args[i]; ++i) {
                if (strcmp(ctx->args.args[i], "--std") == 0 && ctx->args.args[i+1]) {
                    cpp_standard = ctx->args.args[i+1];
                    break;
                }
            }
        }
        logger::print_status("Using C++ standard: " + cpp_standard);
        
        // Check for test flag
        bool with_tests = false;
        if (ctx->args.args) {
            for (int i = 0; ctx->args.args[i]; ++i) {
                if (strcmp(ctx->args.args[i], "--with-tests") == 0) {
                    with_tests = true;
                    break;
                }
            }
        }
        logger::print_status("With tests: " + std::string(with_tests ? "yes" : "no"));
        
        // Check for git flag
        bool with_git = false;
        if (ctx->args.args) {
            for (int i = 0; ctx->args.args[i]; ++i) {
                if (strcmp(ctx->args.args[i], "--git") == 0) {
                    with_git = true;
                    break;
                }
            }
        }
        logger::print_status("With git: " + std::string(with_git ? "yes" : "no"));
        
        // Get template type
        std::string template_type = "app";
        if (ctx->args.args) {
            for (int i = 0; ctx->args.args[i]; ++i) {
                if (strcmp(ctx->args.args[i], "--template") == 0 && ctx->args.args[i+1]) {
                    template_type = ctx->args.args[i+1];
                    i++; // Skip the template value
                    break;
                }
            }
        }
        logger::print_status("Using template: " + template_type);
        
        // Determine verbosity
        bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
        
        // Log a status about what we're going to do
        logger::print_status(is_workspace ? 
                          "Proceeding with workspace creation..." : 
                          "Proceeding with project creation...");
        
        if (is_workspace) {
            // Use current directory path as the workspace base
            std::filesystem::path workspace_base = ctx->working_dir;
            
            // If no projects were specified, create a default one
            if (project_names.empty()) {
                project_names.push_back("main-project");
                logger::print_status("No projects specified, using default project: main-project");
            }
            
            logger::print_status("Creating workspace '" + workspace_name + "' with " + 
                               std::to_string(project_names.size()) + " project(s)...");
            
            if (!init_workspace(workspace_name, workspace_base, project_names, cpp_standard, with_tests, with_git, verbose)) {
                logger::print_error("Failed to initialize workspace");
                return 1;
            }
            
            logger::print_success("Workspace '" + workspace_name + "' created successfully");
        } else {
            logger::print_status("Creating project '" + name + "'...");
            
            if (!create_project(ctx->working_dir, name, cpp_standard, with_git, verbose)) {
                logger::print_error("Failed to initialize project");
                return 1;
            }
            
            logger::print_success("Project '" + name + "' created successfully");
        }
        
        return 0;
    } catch (const std::exception& ex) {
        logger::print_error("Exception during command execution: " + std::string(ex.what()));
        return 1;
    }
} 