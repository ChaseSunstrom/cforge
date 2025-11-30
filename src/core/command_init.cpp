/**
 * @file command_init.cpp
 * @brief Implementation of the 'init' command to create new cforge projects
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

using namespace cforge;

// Global init template name (executable, static-lib, shared-library,
// header-only)
static std::string g_template_name = "executable";
// Add a flag to force overwrite existing files
static bool g_force_overwrite = false;

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
static std::vector<std::string>
parse_project_list(const std::string &project_list) {
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
[[maybe_unused]] static bool create_gitignore(const std::filesystem::path &project_path) {
  std::filesystem::path gitignore_path = project_path / ".gitignore";

  if (std::filesystem::exists(gitignore_path) && !g_force_overwrite) {
    logger::print_warning(".gitignore already exists, skipping");
    return true;
  } else if (std::filesystem::exists(gitignore_path) && g_force_overwrite) {
    logger::print_action("Overwriting", ".gitignore");
  }

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

  logger::created(".gitignore");
  return true;
}

/**
 * @brief Create default README.md file
 *
 * @param project_path Path to project directory
 * @param project_name Project name (normalized with underscores for code usage)
 * @return bool Success flag
 */
static bool create_readme(const std::filesystem::path &project_path,
                          const std::string &project_name) {
  std::filesystem::path readme_path = project_path / "README.md";

  if (std::filesystem::exists(readme_path) && !g_force_overwrite) {
    logger::print_warning("README.md already exists, skipping");
    return true;
  } else if (std::filesystem::exists(readme_path) && g_force_overwrite) {
    logger::print_action("Overwriting", "README.md");
  }

  std::ofstream readme(readme_path);

  if (!readme.is_open()) {
    logger::print_error("Failed to create README.md file");
    return false;
  }

  readme << "# " << project_path.filename().string() << "\n\n";
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

  logger::created("README.md");
  return true;
}

/**
 * @brief Create enhanced CMakeLists.txt file with proper workspace project
 * linking support
 *
 * @param project_path Path to project directory
 * @param project_name Project name
 * @param cpp_version C++ standard version (e.g., "17")
 * @param workspace_aware Enable workspace linking features
 * @return bool Success flag
 */
static bool create_cmakelists(const std::filesystem::path &project_path,
                              const std::string &project_name,
                              const std::string &cpp_version,
                              bool workspace_aware = true) {
  std::filesystem::path cmakelists_path = project_path / "CMakeLists.txt";

  if (std::filesystem::exists(cmakelists_path) && !g_force_overwrite) {
    logger::print_warning("CMakeLists.txt already exists, skipping");
    return true;
  } else if (std::filesystem::exists(cmakelists_path) && g_force_overwrite) {
    logger::print_action("Overwriting", "CMakeLists.txt");
  }

  std::ofstream cmakelists(cmakelists_path);
  if (!cmakelists) {
    logger::print_error("Failed to create CMakeLists.txt");
    return false;
  }

  cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
  cmakelists << "project(" << project_name
             << " VERSION 0.1.0 LANGUAGES CXX)\n\n";

  // C++ standard
  cmakelists << "# Set C++ standard\n";
  cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_version << ")\n";
  cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
  cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

  // Output directories
  cmakelists << "# Set output directories\n";
  cmakelists
      << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib\")\n";
  cmakelists
      << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib\")\n";
  cmakelists
      << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin\")\n\n";

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
    cmakelists << "        message(STATUS \"Using workspace dependency: "
                  "${DEP_NAME}\")\n";
    cmakelists << "        if(DEFINED CFORGE_${DEP_NAME}_INCLUDE)\n";
    cmakelists
        << "            include_directories(${CFORGE_${DEP_NAME}_INCLUDE})\n";
    cmakelists << "            message(STATUS \"  Include path: "
                  "${CFORGE_${DEP_NAME}_INCLUDE}\")\n";
    cmakelists << "        endif()\n";
    cmakelists << "        if(DEFINED CFORGE_${DEP_NAME}_LIB)\n";
    cmakelists << "            link_directories(${CFORGE_${DEP_NAME}_LIB})\n";
    cmakelists << "            message(STATUS \"  Library path: "
                  "${CFORGE_${DEP_NAME}_LIB}\")\n";
    cmakelists << "        endif()\n";
    cmakelists << "        set(CFORGE_HAS_${DEP_NAME} ON PARENT_SCOPE)\n";
    cmakelists << "    endif()\n";
    cmakelists << "endfunction()\n\n";
  }

  // vcpkg integration
  cmakelists << "# vcpkg integration\n";
  cmakelists << "if(DEFINED ENV{VCPKG_ROOT})\n";
  cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                "\"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"\n";
  cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
  cmakelists << "endif()\n\n";

  // Dependencies section
  cmakelists << "# Dependencies\n";
  cmakelists << "find_package(Threads REQUIRED)\n";
  // Example of checking for a workspace dependency:
  // check_workspace_dependency(some_other_project)

  // Source files
  cmakelists << "# Add source files\n";
  cmakelists << "file(GLOB_RECURSE SOURCES\n";
  cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp\n";
  cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c\n";
  cmakelists << ")\n\n";

  // Define the executable/library based on project type
  cmakelists << "# Define target name\n";
  cmakelists << "set(TARGET_NAME ${PROJECT_NAME})\n\n";

  // Project type based on init template
  if (g_template_name == "executable" || g_template_name == "app" ||
      g_template_name == "application") {
    cmakelists << "# This is an executable project\n";
    cmakelists << "add_executable(${TARGET_NAME} ${SOURCES})\n";
    cmakelists << "set(PROJECT_TYPE \"executable\")\n\n";
  } else if (g_template_name == "shared-library" ||
             g_template_name == "shared_library") {
    cmakelists << "# This is a shared library project\n";
    cmakelists << "add_library(${TARGET_NAME} SHARED ${SOURCES})\n";
    cmakelists << "set(PROJECT_TYPE \"shared_library\")\n\n";
  } else if (g_template_name == "header-only" ||
             g_template_name == "header_only") {
    cmakelists << "# This is a header-only interface library project\n";
    cmakelists << "add_library(${TARGET_NAME} INTERFACE)\n";
    cmakelists << "set(PROJECT_TYPE \"interface\")\n\n";
  } else {
    // static library template
    cmakelists << "# This is a static library project\n";
    cmakelists << "add_library(${TARGET_NAME} STATIC ${SOURCES})\n";
    cmakelists << "set(PROJECT_TYPE \"static_library\")\n\n";
  }

  // Include directories
  cmakelists << "# Include directories\n";
  cmakelists << "target_include_directories(${TARGET_NAME} PUBLIC\n";
  cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/include\n";
  cmakelists << ")\n\n";

  // Link libraries
  cmakelists << "# Link libraries\n";
  cmakelists << "target_link_libraries(${TARGET_NAME} PRIVATE\n";
  cmakelists << "    Threads::Threads\n";
  // Add other libraries here
  cmakelists << ")\n\n";

  // Compiler warnings
  cmakelists << "# Enable compiler warnings\n";
  cmakelists << "if(MSVC)\n";
  cmakelists << "    target_compile_options(${TARGET_NAME} PRIVATE /W4 /MP)\n";
  cmakelists << "else()\n";
  cmakelists << "    target_compile_options(${TARGET_NAME} PRIVATE -Wall "
                "-Wextra -Wpedantic)\n";
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
  cmakelists
      << "        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
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
  cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_NAME} - A "
                "C++ project created with cforge\")\n";
  cmakelists << "set(CPACK_PACKAGE_VENDOR \"Your Organization\")\n";

  // OS-specific packaging settings
  cmakelists << "# OS specific packaging settings\n";
  cmakelists << "if(WIN32)\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;NSIS\")\n";
  cmakelists << "elseif(APPLE)\n";
  cmakelists << "    set(CPACK_GENERATOR \"TGZ;\")\n";
  cmakelists << "else()\n";
  cmakelists << "    set(CPACK_GENERATOR \"TGZ;DEB\")\n";
  cmakelists << "endif()\n";

  cmakelists.close();
  logger::created("CMakeLists.txt");
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
static bool create_cforge_toml(const std::filesystem::path &project_path,
                               const std::string &project_name,
                               const std::string &cpp_version,
                               bool with_tests) {
  std::filesystem::path config_path = project_path / CFORGE_FILE;

  if (std::filesystem::exists(config_path) && !g_force_overwrite) {
    logger::print_warning("cforge.toml already exists, skipping");
    return true;
  } else if (std::filesystem::exists(config_path) && g_force_overwrite) {
    logger::print_action("Overwriting", "cforge.toml");
  }

  std::ofstream config(config_path);
  if (!config) {
    logger::print_error("Failed to create cforge.toml file");
    return false;
  }

  config << "# Project configuration for " << project_name << "\n\n";

  config << "[project]\n";
  config << "name = \"" << project_name << "\"\n";
  config << "version = \"0.1.0\"\n";
  config << "description = \"A C++ project created with cforge\"\n";
  config << "cpp_standard = \"" << cpp_version << "\"\n";
  config << "c_standard = \"11\"\n";

  if (g_template_name == "executable" || g_template_name == "app" ||
      g_template_name == "application") {
    config << "binary_type = \"executable\"  # executable, shared_lib, "
              "static_lib, or header_only\n";
  } else if (g_template_name == "shared-library" ||
             g_template_name == "shared_library") {
    config << "binary_type = \"shared_lib\"  # executable, shared_lib, "
              "static_lib, or header_only\n";
  } else if (g_template_name == "header-only" ||
             g_template_name == "header_only") {
    config << "binary_type = \"header_only\"  # executable, shared_lib, "
              "static_lib, or header_only\n";
  } else {
    config << "binary_type = \"static_lib\"  # executable, shared_lib, "
              "static_lib, or header_only\n";
  }

  config << "authors = [\"Your Name <your.email@example.com>\"]\n";
  config << "homepage = \"https://github.com/yourusername/" << project_name
         << "\"\n";
  config << "repository = \"https://github.com/yourusername/" << project_name
         << ".git\"\n";
  config << "license = \"MIT\"\n\n";

  config << "[build]\n";
  config << "build_type = \"Debug\"  # Debug, Release, RelWithDebInfo, "
            "MinSizeRel\n";
  config << "directory = \"build\"\n";
  config << "source_dirs = [\"src\"]\n";
  config << "include_dirs = [\"include\"]\n";
  config << "# Uncomment to specify custom source patterns\n";
  config << "# source_patterns = [\"src/*.cpp\", \"src/**/*.cpp\"]\n";
  config << "# Uncomment to specify individual source files\n";
  config << "# source_files = [\"src/main.cpp\", \"src/example.cpp\"]\n\n";

  // Add build configuration for different build types
  config << "[build.config.debug]\n";
  config << "defines = [\"DEBUG=1\", \"ENABLE_LOGGING=1\"]\n";
  config << "flags = [\"-g\", \"-O0\"]\n";
  config << "cmake_args = [\"-DENABLE_TESTS=ON\"]\n\n";

  config << "[build.config.release]\n";
  config << "defines = [\"NDEBUG=1\"]\n";
  config << "flags = [\"-O3\"]\n";
  config << "cmake_args = [\"-DENABLE_TESTS=OFF\"]\n\n";

  config << "[build.config.relwithdebinfo]\n";
  config << "defines = [\"NDEBUG=1\"]\n";
  config << "flags = [\"-O2\", \"-g\"]\n";
  config << "cmake_args = []\n\n";

  config << "[build.config.minsizerel]\n";
  config << "defines = [\"NDEBUG=1\"]\n";
  config << "flags = [\"-Os\"]\n";
  config << "cmake_args = []\n\n";

  config << "[test]\n";
  config << "enabled = " << (with_tests ? "true" : "false") << "\n";

  config << "[package]\n";
  config << "enabled = true\n";
  config << "generators = []  # Package generators\n";
  config << "# Windows generators: ZIP, NSIS\n";
  config << "# Linux generators: TGZ, DEB, RPM\n";
  config << "# macOS generators: TGZ\n";
  config << "vendor = \"Your Organization\"\n";
  config << "contact = \"Your Name <your.email@example.com>\"\n\n";

  // Dependencies section
  config << "# Dependencies section\n";
  config << "# [dependencies]\n\n";

  config << "# vcpkg dependencies\n";
  config << "# [dependencies.vcpkg]\n";
  config << "# fmt = \"9.1.0\"  # Package name = version\n";
  config << "# curl = { version = \"7.80.0\", components = [\"ssl\"] }  # With "
            "components\n\n";

  config << "# git dependencies\n";
  config << "# [dependencies.git]\n";
  config << "# json = { url = \"https://github.com/nlohmann/json.git\", tag = "
            "\"v3.11.2\" }\n";
  config << "# spdlog = { url = \"https://github.com/gabime/spdlog.git\", "
            "branch = \"v1.x\" }\n\n";

  config << "# system dependencies\n";
  config << "# [dependencies.system]\n";
  config << "# OpenGL = true  # System-provided dependency\n";

  config.close();
  logger::created("cforge.toml");
  return true;
}

/**
 * @brief Create a simple main.cpp file
 *
 * @param project_path Path to project directory
 * @param project_name Project name
 * @return bool Success flag
 */
static bool create_main_cpp(const std::filesystem::path &project_path,
                            const std::string &project_name) {
  std::filesystem::path src_dir = project_path / "src";

  if (!std::filesystem::exists(src_dir)) {
    std::filesystem::create_directories(src_dir);
  }

  // Create main.cpp only for executable (app) projects
  if (g_template_name == "executable") {
    std::filesystem::path main_cpp_path = src_dir / "main.cpp";

    if (std::filesystem::exists(main_cpp_path) && !g_force_overwrite) {
      logger::print_warning("main.cpp already exists, skipping");
      return true;
    } else if (std::filesystem::exists(main_cpp_path) && g_force_overwrite) {
      logger::print_action("Overwriting", "main.cpp");
    }

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
    main_cpp << "    std::cout << \"Hello from " << project_name
             << "!\" << std::endl;\n";
    main_cpp << "    return 0;\n";
    main_cpp << "}\n";

    main_cpp.close();
    logger::created("src/main.cpp");
    return true;
  }

  return true;
}

/**
 * @brief Create include files
 *
 * @param project_path Project path
 * @param project_name Project name (normalized with underscores for code usage)
 * @return bool Success flag
 */
static bool create_include_files(const std::filesystem::path &project_path,
                                 const std::string &project_name) {
  std::filesystem::path include_dir = project_path / "include";

  if (!std::filesystem::exists(include_dir)) {
    std::filesystem::create_directories(include_dir);
  }

  // Create project-specific include directory
  std::filesystem::path project_include_dir = include_dir / project_name;

  if (!std::filesystem::exists(project_include_dir)) {
    std::filesystem::create_directories(project_include_dir);
  }

  // Create example.hpp
  std::filesystem::path example_header_path =
      project_include_dir / "example.hpp";

  if (std::filesystem::exists(example_header_path) && !g_force_overwrite) {
    logger::print_warning("example.hpp already exists, skipping");
    return true;
  } else if (std::filesystem::exists(example_header_path) &&
             g_force_overwrite) {
    logger::print_action("Overwriting", "example.hpp");
  }

  std::ofstream example_header(example_header_path);

  if (!example_header.is_open()) {
    return false;
  }

  example_header << "/**\n";
  example_header << " * @file example.hpp\n";
  example_header << " * @brief Example header file for " << project_name
                 << "\n";
  example_header << " */\n\n";
  example_header << "#pragma once\n\n";
  example_header << "namespace " << project_name << " {\n\n";
  example_header << "/**\n";
  example_header << " * @brief Get an example message\n";
  example_header << " * @return const char* The message\n";
  example_header << " */\n";
  example_header << "const char* get_example_message();\n\n";
  example_header << "} // namespace " << project_name << "\n";

  return true;
}

/**
 * @brief Create example implementation file
 *
 * @param project_path Project path
 * @param project_name Project name (normalized with underscores for code usage)
 * @return bool Success flag
 */
static bool
create_example_implementation(const std::filesystem::path &project_path,
                              const std::string &project_name) {
  std::filesystem::path src_dir = project_path / "src";

  if (!std::filesystem::exists(src_dir)) {
    std::filesystem::create_directories(src_dir);
  }

  // Create example.cpp
  std::filesystem::path example_cpp_path = src_dir / "example.cpp";

  if (std::filesystem::exists(example_cpp_path) && !g_force_overwrite) {
    logger::print_warning("example.cpp already exists, skipping");
    return true;
  } else if (std::filesystem::exists(example_cpp_path) && g_force_overwrite) {
    logger::print_action("Overwriting", "example.cpp");
  }

  std::ofstream example_cpp(example_cpp_path);

  if (!example_cpp.is_open()) {
    return false;
  }

  example_cpp << "/**\n";
  example_cpp << " * @file example.cpp\n";
  example_cpp << " * @brief Implementation of example functions for "
              << project_name << "\n";
  example_cpp << " */\n\n";
  example_cpp << "#include \"" << project_name << "/example.hpp\"\n\n";
  example_cpp << "namespace " << project_name << " {\n\n";
  example_cpp << "const char* get_example_message() {\n";
  example_cpp << "    return \"This is an example function from the "
              << project_name << " library.\";\n";
  example_cpp << "}\n\n";
  example_cpp << "} // namespace " << project_name << "\n";

  return true;
}

/**
 * @brief Create test files
 *
 * @param project_path Project path
 * @param project_name Project name (normalized with underscores for code usage)
 * @return bool Success flag
 */
static bool create_test_files(const std::filesystem::path &project_path,
                              const std::string &project_name) {
  std::filesystem::path tests_dir = project_path / "tests";

  if (!std::filesystem::exists(tests_dir)) {
    std::filesystem::create_directories(tests_dir);
  }

  // Create CMakeLists.txt for tests
  std::filesystem::path tests_cmake_path = tests_dir / "CMakeLists.txt";

  if (std::filesystem::exists(tests_cmake_path) && !g_force_overwrite) {
    logger::print_warning("tests/CMakeLists.txt already exists, skipping");
  } else {
    if (std::filesystem::exists(tests_cmake_path) && g_force_overwrite) {
      logger::print_action("Overwriting", "tests/CMakeLists.txt");
    }
    std::ofstream tests_cmake(tests_cmake_path);
    if (!tests_cmake.is_open()) {
      return false;
    }
    tests_cmake << "# Tests CMakeLists.txt for " << project_name << "\n\n";
    tests_cmake << "# Find GoogleTest\n";
    tests_cmake << "include(FetchContent)\n";
    tests_cmake << "FetchContent_Declare(\n";
    tests_cmake << "  googletest\n";
    tests_cmake
        << "  GIT_REPOSITORY https://github.com/google/googletest.git\n";
    tests_cmake << "  GIT_TAG release-1.12.1\n";
    tests_cmake << ")\n\n";
    tests_cmake << "# For Windows: Prevent overriding the parent project's "
                   "compiler/linker settings\n";
    tests_cmake << "set(gtest_force_shared_crt ON CACHE BOOL \"\" FORCE)\n";
    tests_cmake << "FetchContent_MakeAvailable(googletest)\n\n";
    tests_cmake << "# Enable testing\n";
    tests_cmake << "enable_testing()\n\n";
    tests_cmake << "# Include GoogleTest\n";
    tests_cmake << "include(GoogleTest)\n\n";
    tests_cmake << "# Create test executable\n";
    tests_cmake << "# Convert build type to lowercase for naming\n";
    tests_cmake
        << "string(TOLOWER \"${CMAKE_BUILD_TYPE}\" build_type_lower)\n\n";
    tests_cmake << "set(TEST_EXECUTABLE_NAME "
                   "${PROJECT_NAME}_${build_type_lower}_tests)\n\n";
    tests_cmake << "add_executable(${TEST_EXECUTABLE_NAME}\n";
    tests_cmake << "  test_main.cpp\n";
    tests_cmake << "  test_example.cpp\n";
    tests_cmake << ")\n\n";
    tests_cmake
        << "target_include_directories(${TEST_EXECUTABLE_NAME} PRIVATE\n";
    tests_cmake << "  ${CMAKE_SOURCE_DIR}/include\n";
    tests_cmake << ")\n\n";
    tests_cmake << "target_link_libraries(${TEST_EXECUTABLE_NAME} PRIVATE\n";
    tests_cmake << "  ${PROJECT_NAME}\n";
    tests_cmake << "  gtest_main\n";
    tests_cmake << "  gmock_main\n";
    tests_cmake << ")\n\n";
    tests_cmake << "gtest_discover_tests(${TEST_EXECUTABLE_NAME})\n";
    tests_cmake.close();
  }

  // Create test_main.cpp
  std::filesystem::path test_main_path = tests_dir / "test_main.cpp";

  if (std::filesystem::exists(test_main_path) && !g_force_overwrite) {
    logger::print_warning("test_main.cpp already exists, skipping");
  } else {
    if (std::filesystem::exists(test_main_path) && g_force_overwrite) {
      logger::print_action("Overwriting", "test_main.cpp");
    }
    std::ofstream test_main(test_main_path);
    if (!test_main.is_open()) {
      return false;
    }
    test_main << "/**\n";
    test_main << " * @file test_main.cpp\n";
    test_main << " * @brief Main test runner for " << project_name << "\n";
    test_main << " */\n\n";
    test_main << "#include <gtest/gtest.h>\n\n";
    test_main << "// Let Google Test handle main\n";
    test_main << "// This is not strictly necessary with gtest_main linkage\n";
    test_main << "int main(int argc, char **argv) {\n";
    test_main << "    ::testing::InitGoogleTest(&argc, argv);\n";
    test_main << "    return RUN_ALL_TESTS();\n";
    test_main << "}\n";
    test_main.close();
  }

  // Create test_example.cpp
  std::filesystem::path test_example_path = tests_dir / "test_example.cpp";

  if (std::filesystem::exists(test_example_path) && !g_force_overwrite) {
    logger::print_warning("test_example.cpp already exists, skipping");
  } else {
    if (std::filesystem::exists(test_example_path) && g_force_overwrite) {
      logger::print_action("Overwriting", "test_example.cpp");
    }
    std::ofstream test_example(test_example_path);
    if (!test_example.is_open()) {
      return false;
    }
    test_example << "/**\n";
    test_example << " * @file test_example.cpp\n";
    test_example << " * @brief Example tests for " << project_name << "\n";
    test_example << " */\n\n";
    test_example << "#include <gtest/gtest.h>\n";
    test_example << "#include \"" << project_name << "/example.hpp\"\n\n";
    test_example << "// Example test case\n";
    test_example << "TEST(ExampleTest, GetMessage) {\n";
    test_example << "    // Arrange\n";
    test_example << "    const char* message = " << project_name
                 << "::get_example_message();\n";
    test_example << "    \n";
    test_example << "    // Act & Assert\n";
    test_example << "    EXPECT_NE(message, nullptr);\n";
    test_example << "    EXPECT_STRNE(message, \"\");\n";
    test_example << "}\n";
    test_example.close();
  }

  return true;
}

/**
 * @brief Create default license file (MIT license by default)
 *
 * @param project_path Path to project directory
 * @param project_name Project name
 * @return bool Success flag
 */
static bool create_license_file(const std::filesystem::path &project_path,
                                const std::string &project_name) {
  std::filesystem::path license_path = project_path / "LICENSE";

  if (std::filesystem::exists(license_path) && !g_force_overwrite) {
    logger::print_warning("LICENSE already exists, skipping");
    return true;
  } else if (std::filesystem::exists(license_path) && g_force_overwrite) {
    logger::print_action("Overwriting", "LICENSE");
  }

  std::ofstream license(license_path);

  if (!license.is_open()) {
    logger::print_error("Failed to create LICENSE file");
    return false;
  }

  // Get current year for the license
  auto now = std::chrono::system_clock::now();
  std::time_t current_time = std::chrono::system_clock::to_time_t(now);
  std::tm *time_info = std::localtime(&current_time);
  int current_year = time_info->tm_year + 1900;

  license << "MIT License\n\n";
  license << "Copyright (c) " << current_year << " " << project_name << "\n\n";
  license << "Permission is hereby granted, free of charge, to any person "
             "obtaining a copy\n";
  license << "of this software and associated documentation files (the "
             "\"Software\"), to deal\n";
  license << "in the Software without restriction, including without "
             "limitation the rights\n";
  license << "to use, copy, modify, merge, publish, distribute, sublicense, "
             "and/or sell\n";
  license << "copies of the Software, and to permit persons to whom the "
             "Software is\n";
  license << "furnished to do so, subject to the following conditions:\n\n";
  license << "The above copyright notice and this permission notice shall be "
             "included in all\n";
  license << "copies or substantial portions of the Software.\n\n";
  license << "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY "
             "KIND, EXPRESS OR\n";
  license << "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF "
             "MERCHANTABILITY,\n";
  license << "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO "
             "EVENT SHALL THE\n";
  license << "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR "
             "OTHER\n";
  license << "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, "
             "ARISING FROM,\n";
  license << "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER "
             "DEALINGS IN THE\n";
  license << "SOFTWARE.\n";

  license.close();

  logger::created("LICENSE (MIT)");
  return true;
}

/**
 * @brief Check if git is available on the system
 *
 * @return bool True if git is available
 */
static bool is_git_available() { return is_command_available("git"); }

/**
 * @brief Init a new git repository if requested and git is available
 *
 * @param project_path Path to project directory
 * @param verbose Verbose output flag
 * @return bool Success flag (true if successful or git not available/not
 * requested)
 */
[[maybe_unused]] static bool init_git_repository(const std::filesystem::path &project_path,
                                bool verbose) {
  // If Git initialization wasn't explicitly requested, just return success
  if (!verbose) {
    logger::print_action("Skipped",
                         "git initialization (use --git flag to enable)");
    return true;
  }

  // First check if git is available
  if (!is_git_available()) {
    logger::print_warning("Git not found in PATH, skipping git initialization");
    return true; // Not critical for project creation
  }

  logger::print_action("Initializing", "git repository");

  std::vector<std::string> git_args = {"init"};

  // Use a shorter timeout for git init
  bool result =
      execute_tool("git", git_args, project_path.string(), "Git", verbose, 20);

  if (result) {
    logger::created("git repository");

    // Create an initial commit (optional)
    if (verbose) {
      logger::print_action("Creating", "initial commit");

      // Add all files
      std::vector<std::string> git_add_args = {"add", "."};
      bool add_result = execute_tool("git", git_add_args, project_path.string(),
                                     "Git add", verbose, 10);

      if (add_result) {
        // Commit
        std::vector<std::string> git_commit_args = {"commit", "-m",
                                                    "Initial commit"};
        bool commit_result =
            execute_tool("git", git_commit_args, project_path.string(),
                         "Git commit", verbose, 10);

        if (commit_result) {
          logger::created("initial commit");
        } else {
          logger::print_warning(
              "Failed to create initial commit. This is not critical");
        }
      } else {
        logger::print_warning(
            "Failed to add files to git. This is not critical");
      }
    }
  } else {
    logger::print_warning("Failed to initialize git repository. This is not "
                          "critical for project creation");
  }

  // Return true regardless of git initialization result
  // as this is not critical for project creation
  return true;
}

/**
 * @brief Normalize a project name by replacing special characters with
 * underscores
 *
 * @param name The original project name
 * @return std::string Normalized project name safe for C++ identifiers
 */
static std::string normalize_project_name(const std::string &name) {
  std::string normalized = name;

  // Replace special characters with underscores
  // This includes hyphens, @, #, $, %, etc.
  for (char &c : normalized) {
    // Allow alphanumeric characters and underscores
    if (!std::isalnum(c) && c != '_') {
      c = '_';
    }
  }

  // Ensure it starts with a letter or underscore (valid C++ identifier)
  if (!normalized.empty() && std::isdigit(normalized[0])) {
    normalized = "_" + normalized;
  }

  return normalized;
}

/**
 * @brief Generate a workspace-level CMakeLists.txt file
 *
 * @param workspace_dir Workspace directory
 * @param workspace_name Workspace name
 * @param project_names List of project names in the workspace
 * @param cpp_standard C++ standard
 * @return bool Success flag
 */
static bool
generate_workspace_cmakelists(const std::filesystem::path &workspace_dir,
                              const std::string &workspace_name,
                              const std::vector<std::string> &project_names,
                              const std::string &cpp_standard) {
  std::filesystem::path cmakelists_path = workspace_dir / "CMakeLists.txt";

  if (std::filesystem::exists(cmakelists_path) && !g_force_overwrite) {
    logger::print_warning(
        "Workspace-level CMakeLists.txt already exists, skipping");
    return true;
  } else if (std::filesystem::exists(cmakelists_path) && g_force_overwrite) {
    logger::print_action("Overwriting", "workspace-level CMakeLists.txt");
  }

  std::ofstream cmakelists(cmakelists_path);
  if (!cmakelists.is_open()) {
    logger::print_error("Failed to create workspace CMakeLists.txt at: " +
                        cmakelists_path.string());
    return false;
  }

  // Write the workspace CMakeLists.txt
  cmakelists << "# Workspace CMakeLists.txt for " << workspace_name << "\n";
  cmakelists << "# Generated by cforge - C++ project management tool\n\n";

  cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";

  cmakelists << "# Workspace configuration\n";
  cmakelists << "project(" << workspace_name << " LANGUAGES CXX)\n\n";

  cmakelists << "# Set C++ standard for the entire workspace\n";
  cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_standard << ")\n";
  cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
  cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

  cmakelists << "# Set output directories for the workspace\n";
  cmakelists << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
  cmakelists << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
  cmakelists
      << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n\n";

  cmakelists << "# Enable testing for the workspace\n";
  cmakelists << "enable_testing()\n\n";

  cmakelists << "# Add all projects in the workspace\n";
  for (const auto &project : project_names) {
    cmakelists << "add_subdirectory(" << project << ")\n";
  }
  cmakelists << "\n";

  cmakelists << "# Print workspace configuration details\n";
  cmakelists << "message(STATUS \"Configured workspace: " << workspace_name
             << "\")\n";
  cmakelists << "message(STATUS \"  - C++ Standard: ${CMAKE_CXX_STANDARD}\")\n";
  cmakelists << "message(STATUS \"  - Build Type: ${CMAKE_BUILD_TYPE}\")\n";
  cmakelists << "message(STATUS \"  - Projects: ";
  for (size_t i = 0; i < project_names.size(); ++i) {
    if (i > 0)
      cmakelists << ", ";
    cmakelists << project_names[i];
  }
  cmakelists << "\")\n";

  cmakelists.close();
  logger::created("workspace CMakeLists.txt");
  return true;
}

/**
 * @brief Create project files
 *
 * @param project_path Project directory path
 * @param project_name Project name
 * @param cpp_version C++ version
 * @param with_git Initialize git repository
 * @param with_tests Include test files
 * @param cmake_preset CMake preset to use (optional)
 * @param build_type Build type (Debug/Release) (optional)
 * @return bool Success flag
 */
static bool create_project(const std::filesystem::path &project_path,
                           const std::string &project_name,
                           const std::string &cpp_version, bool /*with_git*/,
                           bool with_tests,
                           const std::string & /*cmake_preset*/ = "",
                           const std::string & /*build_type*/ = "Debug") {
  try {
    // Create the project directory if it doesn't exist
    if (!std::filesystem::exists(project_path)) {
      std::filesystem::create_directories(project_path);
    }

    // Normalize the project name for code usage
    std::string normalized_name = normalize_project_name(project_name);

    // Create project skeleton
    std::filesystem::create_directories(project_path / "src");
    std::filesystem::create_directories(project_path / "include");

    // Create README file
    if (!create_readme(project_path, project_name)) {
      logger::print_error("Failed to create README.md");
      return false;
    }

    // Create CMakeLists.txt
    if (!create_cmakelists(project_path, project_name, cpp_version,
                           with_tests)) {
      logger::print_error("Failed to create CMakeLists.txt");
      return false;
    }

    // Create cforge.toml configuration
    if (!create_cforge_toml(project_path, project_name, cpp_version,
                            with_tests)) {
      logger::print_error("Failed to create cforge.toml");
      return false;
    }

    // Create src/main.cpp
    if (!create_main_cpp(project_path, project_name)) {
      logger::print_error("Failed to create main.cpp");
      return false;
    }

    // Create include files
    if (!create_include_files(project_path, normalized_name)) {
      logger::print_error("Failed to create include files");
      return false;
    }

    // Create example implementation file
    if (!create_example_implementation(project_path, normalized_name)) {
      logger::print_error("Failed to create implementation files");
      return false;
    }

    // Create test files if requested
    if (with_tests) {
      if (!create_test_files(project_path, normalized_name)) {
        logger::print_error("Failed to create test files");
        return false;
      }
    }

    // Create license file
    if (!create_license_file(project_path, project_name)) {
      logger::print_error("Failed to create LICENSE file");
      return false;
    }

    return true;
  } catch (const std::exception &ex) {
    logger::print_error("Failed to create project: " + std::string(ex.what()));
    return false;
  }
}

/**
 * @brief Handle the 'init' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_init(const cforge_context_t *ctx) {
  // Set flush after each log message to ensure output is visible
  std::cout.flush();
  std::cerr.flush();

  try {
    // Check if a workspace configuration file exists in the current directory
    std::filesystem::path workspace_file_path =
        std::filesystem::path(ctx->working_dir) / WORKSPACE_FILE;
    bool workspace_file_exists = std::filesystem::exists(workspace_file_path);

    // Default project and workspace names - use current directory name as
    // default project name
    std::string project_name =
        std::filesystem::path(ctx->working_dir).filename().string();
    bool is_workspace = false;
    bool from_file = false;
    std::string workspace_name;
    std::vector<std::string> project_names;
    std::string cpp_standard = "17";
    bool with_tests = false;
    bool with_git = false;
    std::string template_name = "executable";
    bool has_projects_flag = false;

    // Process command line arguments - improved parsing
    if (ctx->args.args) {
      // First check for positional project name (before any flag)
      if (ctx->args.arg_count > 0 && ctx->args.args[0][0] != '-') {
        project_name = ctx->args.args[0];
      }

      // Parse flag arguments
      for (int i = 0; i < ctx->args.arg_count; ++i) {
        std::string arg = ctx->args.args[i];

        // Handle overwrite flag
        if (arg == "--overwrite") {
          g_force_overwrite = true;
        }
        // Handle --from-file flag
        else if (arg == "--from-file" || arg == "-f") {
          from_file = true;
        }
        // Handle --name or -n parameter
        else if (arg == "--name" || arg == "-n") {
          if (i + 1 < ctx->args.arg_count && ctx->args.args[i + 1][0] != '-') {
            project_name = ctx->args.args[i + 1];
            i++; // Skip the value in next iteration
          }
        }
        // Handle --name=VALUE format
        else if (arg.compare(0, 7, "--name=") == 0) {
          project_name = arg.substr(7);
        }
        // Handle --workspace or -w parameter
        else if (arg == "--workspace" || arg == "-w") {
          is_workspace = true;

          // Check if next argument exists and is a value (not a flag)
          if (i + 1 < ctx->args.arg_count && ctx->args.args[i + 1][0] != '-') {
            workspace_name = ctx->args.args[i + 1];
            i++; // Skip the value in next iteration
          } else {
            // Use current directory name if no name provided
            workspace_name =
                std::filesystem::path(ctx->working_dir).filename().string();
          }
        }
        // Handle --workspace=VALUE format
        else if (arg.compare(0, 12, "--workspace=") == 0) {
          is_workspace = true;
          workspace_name = arg.substr(12);
        }
        // Handle --projects or -p parameter
        else if (arg == "--projects" || arg == "-p") {
          has_projects_flag = true;

          // If the next argument is a flag or end of args, use empty project
          // list
          if (i + 1 >= ctx->args.arg_count || ctx->args.args[i + 1][0] == '-') {
            logger::print_warning(
                "--projects flag provided but no projects specified");
            continue;
          }

          // Check if it's a comma-separated list
          if (std::string(ctx->args.args[i + 1]).find(',') !=
              std::string::npos) {
            // Parse comma-separated list
            std::string projects_arg = ctx->args.args[i + 1];
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
        }
        // Handle --projects=VALUE format
        else if (arg.compare(0, 11, "--projects=") == 0) {
          has_projects_flag = true;
          std::string projects_list = arg.substr(11);
          project_names = parse_project_list(projects_list);
        }
        // Handle --cpp or -c parameter for C++ standard
        else if (arg == "--cpp" || arg == "-c") {
          if (i + 1 < ctx->args.arg_count) {
            cpp_standard = ctx->args.args[i + 1];
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
          if (i + 1 < ctx->args.arg_count) {
            template_name = ctx->args.args[i + 1];
            i++; // Skip the value in next iteration
          }
        }
        // Handle --template=VALUE format
        else if (arg.compare(0, 11, "--template=") == 0) {
          template_name = arg.substr(11);
        }
      }
    }

    // Apply selected template
    g_template_name = template_name;

    // If user didn't set workspace flag but specified projects, don't force
    // workspace mode
    bool create_multiple_projects = has_projects_flag && !project_names.empty();

    // If this is a workspace but no projects specified, use the project name as
    // default project
    if (is_workspace && project_names.empty()) {
      project_names.push_back(project_name);
    }

    // Handle loading from existing workspace file if requested
    if (from_file && workspace_file_exists) {
      logger::print_action("Loading", workspace_file_path.string());

      // Load the workspace configuration
      workspace_config config;
      if (!config.load(workspace_file_path.string())) {
        logger::print_error("Failed to load workspace configuration from " +
                            workspace_file_path.string());
        return 1;
      }

      // Get the workspace name
      std::string ws_name = config.get_name();

      // Get the projects from the workspace
      std::vector<workspace_project> projects = config.get_projects();

      // Create each project
      for (const auto &project : projects) {
        logger::creating(project.name);

        // Create the project directory
        std::filesystem::path project_dir =
            std::filesystem::path(ctx->working_dir) / project.path;
        if (!std::filesystem::exists(project_dir)) {
          try {
            std::filesystem::create_directories(project_dir);
          } catch (const std::exception &ex) {
            logger::print_error("Failed to create project directory: " +
                                project_dir.string() + " Error: " + ex.what());
            continue;
          }
        }

        // Create the project files
        if (!create_project(project_dir, project.name, cpp_standard, with_git,
                            with_tests)) {
          logger::print_error("Failed to create project '" + project.name +
                              "'");
          continue;
        }

        logger::created(project.name);
      }

      logger::finished(ws_name);
      return 0;
    }
    // Handle workspace creation
    else if (is_workspace) {
      logger::creating(workspace_name);

      // Create workspace directory
      std::filesystem::path workspace_dir = ctx->working_dir;

      // Use the provided workspace name to create a subdirectory
      if (!workspace_name.empty()) {
        workspace_dir = workspace_dir / workspace_name;

        // Create the workspace directory if it doesn't exist
        if (!std::filesystem::exists(workspace_dir)) {
          try {
            bool created = std::filesystem::create_directories(workspace_dir);
            if (!created) {
              logger::print_error(
                  "Failed to create workspace directory (returned false)");
              return 1;
            }
          } catch (const std::exception &ex) {
            logger::print_error("Exception creating workspace directory: " +
                                std::string(ex.what()));
            return 1;
          }
        }
      }

      // Test file creation to verify permissions
      try {
        std::string test_path = (workspace_dir / "cforge_test_file").string();
        std::ofstream test_file(test_path);
        if (!test_file.is_open()) {
          logger::print_error("Workspace directory is not writable: " +
                              workspace_dir.string());
          logger::print_error(
              "Please check permissions or try a different location");
          return 1;
        }
        test_file.close();
        std::filesystem::remove(test_path);
      } catch (const std::exception &ex) {
        logger::print_error("Failed to write to workspace directory: " +
                            std::string(ex.what()));
        return 1;
      }

      std::filesystem::path config_path = workspace_dir / WORKSPACE_FILE;

      // Create workspace configuration file directly
      if (std::filesystem::exists(config_path) && !g_force_overwrite) {
        logger::print_warning("Workspace configuration file '" +
                              config_path.string() +
                              "' already exists. Skipping creation");
      } else {
        if (std::filesystem::exists(config_path) && g_force_overwrite) {
          logger::print_action("Overwriting", "workspace configuration");
        }
        std::ofstream config_file(config_path);
        if (!config_file.is_open()) {
          logger::print_error(
              "Failed to create workspace configuration file: " +
              config_path.string());
          return 1;
        }
        // Write a basic TOML configuration
        config_file << "[workspace]\n";
        config_file << "name = \"" << workspace_name << "\"\n";
        config_file
            << "description = \"A C++ workspace created with cforge\"\n\n";

        // Write projects as array-of-tables
        for (size_t i = 0; i < project_names.size(); ++i) {
          const auto &proj_name = project_names[i];
          bool is_startup = (i == 0); // first project marked startup
          config_file << "[[workspace.project]]\n";
          config_file << "name    = \"" << proj_name << "\"\n";
          config_file << "path    = \"" << proj_name << "\"\n";
          config_file << "startup = " << (is_startup ? "true" : "false")
                      << "\n\n";
        }
        // Optionally record main_project fallback
        if (!project_names.empty()) {
          config_file << "# main_project = \"" << project_names[0] << "\"\n";
        }

        config_file.close();
        logger::created("workspace configuration");
      }

      // Generate workspace-level CMakeLists.txt
      if (!generate_workspace_cmakelists(workspace_dir, workspace_name,
                                         project_names, cpp_standard)) {
        logger::print_warning(
            "Failed to generate workspace-level CMakeLists.txt");
        // Continue anyway, not critical
      }

      // Now create each project directly instead of using init_workspace
      bool all_projects_success = true;

      for (const auto &proj_name : project_names) {
        std::filesystem::path project_dir = workspace_dir / proj_name;
        logger::creating(proj_name);

        // Create the project with detailed logging
        if (!create_project(project_dir, proj_name, cpp_standard, with_git,
                            with_tests)) {
          logger::print_error("Failed to create project '" + proj_name + "'");
          all_projects_success = false;
          // Continue with other projects instead of stopping
          continue;
        }

        logger::created(proj_name);
      }

      if (all_projects_success) {
        logger::finished(workspace_name);
      } else {
        logger::print_warning("Workspace '" + workspace_name +
                              "' created with some errors");
      }

      return all_projects_success ? 0 : 1;
    }
    // Handle multiple projects creation (without workspace)
    else if (create_multiple_projects) {
      bool all_projects_success = true;

      for (const auto &proj_name : project_names) {
        logger::creating(proj_name);

        // Create the project in a new directory named after the project
        std::filesystem::path project_dir =
            std::filesystem::path(ctx->working_dir) / proj_name;

        // Create the directory if it doesn't exist
        if (!std::filesystem::exists(project_dir)) {
          try {
            bool created = std::filesystem::create_directories(project_dir);
            if (!created) {
              logger::print_error("Failed to create project directory for '" +
                                  proj_name + "' (returned false)");
              all_projects_success = false;
              continue;
            }
          } catch (const std::exception &ex) {
            logger::print_error("Exception creating project directory for '" +
                                proj_name + "': " + std::string(ex.what()));
            all_projects_success = false;
            continue;
          }
        }

        // Create the project files
        if (!create_project(project_dir, proj_name, cpp_standard, with_git,
                            with_tests, "", "Debug")) {
          logger::print_error("Failed to create project '" + proj_name + "'");
          all_projects_success = false;
          continue;
        }

        logger::created(proj_name);
      }

      if (!all_projects_success) {
        logger::print_warning("Some projects could not be created");
      }

      return all_projects_success ? 0 : 1;
    }
    // Handle single project creation
    else {
      logger::creating(project_name);

      // Decide whether to create in current directory or subdirectory
      std::filesystem::path project_dir;

      // If we're using a positional project name or explicit --name parameter,
      // create a subdirectory; otherwise use the current directory
      if (ctx->args.arg_count > 0 && ctx->args.args[0][0] != '-') {
        // Use the provided project name to create a subdirectory
        project_dir = std::filesystem::path(ctx->working_dir) / project_name;

        // Create the directory if it doesn't exist
        if (!std::filesystem::exists(project_dir)) {
          try {
            bool created = std::filesystem::create_directories(project_dir);
            if (!created) {
              logger::print_error(
                  "Failed to create project directory (returned false)");
              return 1;
            }
          } catch (const std::exception &ex) {
            logger::print_error("Exception creating project directory: " +
                                std::string(ex.what()));
            return 1;
          }
        }
      } else {
        // Create in the current directory
        project_dir = std::filesystem::path(ctx->working_dir);
      }

      // Verify the directory is writable
      try {
        // Test file creation
        std::string test_path = (project_dir / "cforge_test_file").string();
        std::ofstream test_file(test_path);
        if (!test_file.is_open()) {
          logger::print_error("Directory is not writable: " +
                              project_dir.string());
          logger::print_error(
              "Please check permissions or try a different location");
          return 1;
        }
        test_file.close();
        std::filesystem::remove(test_path);
      } catch (const std::exception &ex) {
        logger::print_error("Failed to write to directory: " +
                            std::string(ex.what()));
        return 1;
      }

      // Default build type and CMake preset
      std::string build_type = "Debug";
      std::string cmake_preset = "";

      // Create the project with detailed logging
      if (!create_project(project_dir, project_name, cpp_standard, with_git,
                          with_tests, cmake_preset, build_type)) {
        logger::print_error("Failed to create project '" + project_name + "'");
        return 1;
      }

      logger::finished(project_name);
    }

    return 0;
  } catch (const std::exception &ex) {
    logger::print_error("Failed to initialize project: " +
                        std::string(ex.what()));
    return 1;
  }
}