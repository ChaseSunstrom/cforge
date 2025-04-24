/**
 * @file command_build.cpp
 * @brief Implementation of the 'build' command
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/error_format.hpp"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace cforge;

// Default build configuration if not specified
#define DEFAULT_BUILD_CONFIG "Release"

/**
 * @brief Get the generator string for CMake based on platform
 *
 * @return std::string CMake generator string
 */
static std::string get_cmake_generator() {
#ifdef _WIN32
  // Check if Ninja is available - prefer it if it is
  if (is_command_available("ninja", 15)) {
    logger::print_verbose(
        "Ninja is available, using Ninja Multi-Config generator");
    return "Ninja Multi-Config";
  }

  logger::print_verbose(
      "Ninja not found, falling back to Visual Studio generator");
  return "Visual Studio 17 2022";
#else
  return "Unix Makefiles";
#endif
}

/**
 * @brief Parse build configuration from command line arguments
 *
 * @param ctx Command context
 * @param project_config TOML reader for project config
 * @return std::string Build configuration
 */
static std::string get_build_config(const cforge_context_t *ctx,
                                    const toml_reader *project_config) {
  // Priority 1: Direct configuration argument
  if (ctx->args.config != nullptr && strlen(ctx->args.config) > 0) {
    logger::print_verbose("Using build configuration from direct argument: " +
                          std::string(ctx->args.config));
    return std::string(ctx->args.config);
  }

  // Priority 2: Command line argument
  if (ctx->args.arg_count > 0) {
    for (int i = 0; i < ctx->args.arg_count; ++i) {
      std::string arg = ctx->args.args[i];
      if (arg == "--config" || arg == "-c") {
        if (i + 1 < ctx->args.arg_count) {
          std::string config = ctx->args.args[i + 1];
          logger::print_verbose(
              "Using build configuration from command line: " + config);
          return config;
        }
      } else if (arg.substr(0, 9) == "--config=") {
        std::string config = arg.substr(9);
        logger::print_verbose("Using build configuration from command line: " +
                              config);
        return config;
      }
    }
  }

  // Priority 3: Configuration from cforge.toml
  std::string config = project_config->get_string("build.build_type", "");
  if (!config.empty()) {
    logger::print_verbose("Using build configuration from cforge.toml: " +
                          config);
    return config;
  }

  // Priority 4: Default to Release
  logger::print_verbose(
      "No build configuration specified, defaulting to Release");
  return "Release";
}

/**
 * @brief Check if the current directory is within a workspace
 *
 * @param path Directory to check
 * @return std::pair<bool, std::filesystem::path> Pair of (is_workspace,
 * workspace_directory)
 */
std::pair<bool, std::filesystem::path>
is_in_workspace(const std::filesystem::path &path) {
  // Check if the current directory has a workspace.toml file
  std::filesystem::path workspace_file = path / WORKSPACE_FILE;
  if (std::filesystem::exists(workspace_file)) {
    return {true, path};
  }

  // Check parent directories
  std::filesystem::path current = path;
  while (current.has_parent_path() && current != current.parent_path()) {
    current = current.parent_path();
    workspace_file = current / WORKSPACE_FILE;
    if (std::filesystem::exists(workspace_file)) {
      return {true, current};
    }
  }

  // Not in a workspace
  return {false, {}};
}

/**
 * @brief Get build directory path based on base directory and configuration
 *
 * @param base_dir Base build directory from configuration
 * @param config Build configuration (Release, Debug, etc.)
 * @return std::filesystem::path The configured build directory
 */
static std::filesystem::path
get_build_dir_for_config(const std::string &base_dir,
                         const std::string &config) {
  // If config is empty, use the base directory
  if (config.empty()) {
    return base_dir;
  }

  // Transform config name to lowercase for directory naming
  std::string config_lower = string_to_lower(config);

  // For Ninja Multi-Config, we don't append the config to the build directory
  std::string generator = get_cmake_generator();
  if (generator.find("Ninja Multi-Config") != std::string::npos) {
    // Create the build directory if it doesn't exist
    std::filesystem::path build_path(base_dir);
    if (!std::filesystem::exists(build_path)) {
      std::filesystem::create_directories(build_path);
    }
    return base_dir;
  }

  // Format build directory based on configuration
  std::string build_dir = base_dir + "-" + config_lower;

  // Create the build directory if it doesn't exist
  std::filesystem::path build_path(build_dir);
  if (!std::filesystem::exists(build_path)) {
    std::filesystem::create_directories(build_path);
  }

  return build_dir;
}

/**
 * @brief Check if Visual Studio is available
 *
 * @return bool True if Visual Studio is available
 */
static bool is_visual_studio_available() {
  // Check common Visual Studio installation paths
  std::vector<std::string> vs_paths = {
      "C:\\Program Files\\Microsoft Visual "
      "Studio\\2022\\Community\\Common7\\IDE\\devenv.exe",
      "C:\\Program Files\\Microsoft Visual "
      "Studio\\2022\\Professional\\Common7\\IDE\\devenv.exe",
      "C:\\Program Files\\Microsoft Visual "
      "Studio\\2022\\Enterprise\\Common7\\IDE\\devenv.exe",
      "C:\\Program Files (x86)\\Microsoft Visual "
      "Studio\\2019\\Community\\Common7\\IDE\\devenv.exe",
      "C:\\Program Files (x86)\\Microsoft Visual "
      "Studio\\2019\\Professional\\Common7\\IDE\\devenv.exe",
      "C:\\Program Files (x86)\\Microsoft Visual "
      "Studio\\2019\\Enterprise\\Common7\\IDE\\devenv.exe"};

  for (const auto &path : vs_paths) {
    if (std::filesystem::exists(path)) {
      logger::print_verbose("Found Visual Studio at: " + path);
      return true;
    }
  }

  return false;
}

/**
 * @brief Check if CMake is available on the system
 *
 * @return bool True if CMake is available
 */
static bool is_cmake_available() {
  bool available = is_command_available("cmake");
  if (!available) {
    logger::print_warning("CMake not found in PATH using detection check.");
    logger::print_status(
        "Please install CMake from https://cmake.org/download/ and make sure "
        "it's in your PATH.");
    logger::print_status("We'll still attempt to run the cmake command in case "
                         "this is a false negative.");

    // Suggest alternative build methods
    if (is_visual_studio_available()) {
      logger::print_status("Visual Studio is available. You can open the "
                           "project in Visual Studio and build it there.");
      logger::print_status("1. Open Visual Studio");
      logger::print_status("2. Select 'Open a local folder'");
      logger::print_status("3. Navigate to your project folder and select it");
      logger::print_status(
          "4. Visual Studio will automatically configure the CMake project");
    }
  }
  return true; // Always return true to allow the build to proceed
}

/**
 * @brief Clone and update Git dependencies for a project
 *
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
bool clone_git_dependencies(const std::filesystem::path &project_dir,
                            const toml_reader &project_config, bool verbose) {
  // Check if we have Git dependencies
  if (!project_config.has_key("dependencies.git")) {
    logger::print_verbose("No Git dependencies to setup");
    return true;
  }

  // Get dependencies directory from configuration
  std::string deps_dir =
      project_config.get_string("dependencies.directory", "deps");
  std::filesystem::path deps_path = project_dir / deps_dir;

  // Create dependencies directory if it doesn't exist
  if (!std::filesystem::exists(deps_path)) {
    logger::print_verbose("Creating dependencies directory: " +
                          deps_path.string());
    std::filesystem::create_directories(deps_path);
  }

  // Check if git is available
  if (!is_command_available("git", 20)) {
    logger::print_error("Git is not available. Please install Git and ensure "
                        "it's in your PATH.");
    return false;
  }

  // Get all Git dependencies
  auto git_deps = project_config.get_table_keys("dependencies.git");
  logger::print_status("Setting up " + std::to_string(git_deps.size()) +
                       " Git dependencies...");

  for (const auto &dep : git_deps) {
    // Get dependency configuration
    std::string url =
        project_config.get_string("dependencies.git." + dep + ".url", "");
    if (url.empty()) {
      logger::print_warning("Git dependency '" + dep +
                            "' is missing a URL, skipping");
      continue;
    }

    // Get reference (tag, branch, or commit)
    std::string tag =
        project_config.get_string("dependencies.git." + dep + ".tag", "");
    std::string branch =
        project_config.get_string("dependencies.git." + dep + ".branch", "");
    std::string commit =
        project_config.get_string("dependencies.git." + dep + ".commit", "");
    std::string ref = tag;
    if (ref.empty())
      ref = branch;
    if (ref.empty())
      ref = commit;

    // Path to dependency directory
    std::filesystem::path dep_path = deps_path / dep;

    if (std::filesystem::exists(dep_path)) {
      logger::print_status(
          "Dependency '" + dep +
          "' directory already exists at: " + dep_path.string());

      // Update the repository if it exists
      logger::print_status("Updating dependency '" + dep + "' from remote...");

      // Run git fetch to update
      std::vector<std::string> fetch_args = {"fetch", "--quiet"};
      if (verbose) {
        fetch_args.pop_back(); // Remove --quiet for verbose output
      }

      bool fetch_result = execute_tool("git", fetch_args, dep_path.string(),
                                       "Git Fetch for " + dep, verbose);

      if (!fetch_result) {
        logger::print_warning("Failed to fetch updates for '" + dep +
                              "', continuing with existing version");
      }

      // Checkout specific ref if provided
      if (!ref.empty()) {
        logger::print_status("Checking out " + ref + " for dependency '" + dep +
                             "'");

        std::vector<std::string> checkout_args = {"checkout", ref};
        if (!verbose) {
          checkout_args.push_back("--quiet");
        }

        bool checkout_result =
            execute_tool("git", checkout_args, dep_path.string(),
                         "Git Checkout for " + dep, verbose);

        if (!checkout_result) {
          logger::print_warning("Failed to checkout " + ref + " for '" + dep +
                                "', continuing with current version");
        }
      }
    } else {
      // Clone the repository
      logger::print_status("Cloning dependency '" + dep + "' from " + url +
                           "...");

      std::vector<std::string> clone_args = {"clone", url, dep_path.string()};
      if (!verbose) {
        clone_args.push_back("--quiet");
      }

      bool clone_result =
          execute_tool("git", clone_args, "", "Git Clone for " + dep, verbose);

      if (!clone_result) {
        logger::print_error("Failed to clone dependency '" + dep + "' from " +
                            url);
        return false;
      }

      // Checkout specific ref if provided
      if (!ref.empty()) {
        logger::print_status("Checking out " + ref + " for dependency '" + dep +
                             "'");

        std::vector<std::string> checkout_args = {"checkout", ref};
        if (!verbose) {
          checkout_args.push_back("--quiet");
        }

        bool checkout_result =
            execute_tool("git", checkout_args, dep_path.string(),
                         "Git Checkout for " + dep, verbose);

        if (!checkout_result) {
          logger::print_error("Failed to checkout " + ref +
                              " for dependency '" + dep + "'");
          return false;
        }
      }

      logger::print_success("Successfully cloned dependency '" + dep + "'");
    }
  }

  logger::print_success("All Git dependencies are set up");
  return true;
}

/**
 * @brief Add Git dependencies configuration to CMakeLists.txt
 *
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param deps_dir Dependencies directory
 * @param cmakelists CMakeLists.txt output stream
 */
static void
configure_git_dependencies_in_cmake(const toml_reader &project_config,
                                    const std::string &deps_dir,
                                    std::ofstream &cmakelists) {
  // Check if we have Git dependencies
  if (!project_config.has_key("dependencies.git")) {
    return;
  }

  cmakelists << "# Git dependencies\n";
  cmakelists << "include(FetchContent)\n";

  // Make sure the dependencies directory exists
  cmakelists << "# Ensure dependencies directory exists\n";
  cmakelists << "set(DEPS_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/" << deps_dir
             << "\")\n";
  cmakelists << "file(MAKE_DIRECTORY ${DEPS_DIR})\n\n";

  // Loop through all git dependencies
  auto git_deps = project_config.get_table_keys("dependencies.git");
  for (const auto &dep : git_deps) {
    std::string url =
        project_config.get_string("dependencies.git." + dep + ".url", "");
    if (url.empty()) {
      continue;
    }

    // Get reference (tag, branch, or commit)
    std::string tag =
        project_config.get_string("dependencies.git." + dep + ".tag", "");
    std::string branch =
        project_config.get_string("dependencies.git." + dep + ".branch", "");
    std::string commit =
        project_config.get_string("dependencies.git." + dep + ".commit", "");

    // Get dependency options
    bool make_available = project_config.get_bool(
        "dependencies.git." + dep + ".make_available", true);
    bool include =
        project_config.get_bool("dependencies.git." + dep + ".include", true);
    bool link =
        project_config.get_bool("dependencies.git." + dep + ".link", true);
    std::string target_name = project_config.get_string(
        "dependencies.git." + dep + ".target_name", "");

    cmakelists << "# " << dep << " dependency\n";
    cmakelists << "message(STATUS \"Setting up " << dep << " dependency from "
               << url << "\")\n";

    // FetchContent declaration
    cmakelists << "FetchContent_Declare(" << dep << "\n";
    cmakelists << "    SOURCE_DIR ${DEPS_DIR}/" << dep << "\n";
    cmakelists << ")\n";

    // Process include directories
    if (include) {
      cmakelists << "# Include directories for " << dep << "\n";

      std::vector<std::string> include_dirs;
      std::string include_dirs_key =
          "dependencies.git." + dep + ".include_dirs";

      if (project_config.has_key(include_dirs_key)) {
        include_dirs = project_config.get_string_array(include_dirs_key);
      } else {
        // Default include directories
        include_dirs.push_back("include");
        include_dirs.push_back(".");
      }

      for (const auto &inc_dir : include_dirs) {
        cmakelists << "include_directories(${DEPS_DIR}/" << dep << "/"
                   << inc_dir << ")\n";
      }
      cmakelists << "\n";
    }

    // Special handling for common libraries
    if (dep == "json" || dep == "nlohmann_json") {
      // For nlohmann/json, we typically don't need to build anything
      cmakelists << "# For nlohmann/json, we just need the include directory\n";
      cmakelists << "FetchContent_GetProperties(" << dep << ")\n";
      cmakelists << "if(NOT " << dep << "_POPULATED)\n";
      cmakelists
          << "    message(STATUS \"Making nlohmann/json available...\")\n";
      cmakelists << "    FetchContent_Populate(" << dep << ")\n";
      cmakelists << "endif()\n\n";
    } else if (dep == "fmt") {
      // For fmt, we need to build the library
      cmakelists << "# For fmt, we need to build the library\n";
      cmakelists << "set(FMT_TEST OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(FMT_DOC OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(FMT_SYSTEM_HEADERS ON CACHE BOOL \"\" FORCE)\n";
      cmakelists << "FetchContent_MakeAvailable(" << dep << ")\n\n";
    } else if (dep == "spdlog") {
      // For spdlog, similar configuration
      cmakelists << "# For spdlog, configure options\n";
      cmakelists << "set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(SPDLOG_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      if (make_available) {
        cmakelists << "FetchContent_MakeAvailable(" << dep << ")\n\n";
      } else {
        cmakelists << "FetchContent_GetProperties(" << dep << ")\n";
        cmakelists << "if(NOT " << dep << "_POPULATED)\n";
        cmakelists << "    FetchContent_Populate(" << dep << ")\n";
        cmakelists << "endif()\n\n";
      }
    } else {
      // General case for other dependencies
      if (make_available) {
        cmakelists << "FetchContent_MakeAvailable(" << dep << ")\n\n";
      } else {
        cmakelists << "FetchContent_GetProperties(" << dep << ")\n";
        cmakelists << "if(NOT " << dep << "_POPULATED)\n";
        cmakelists << "    FetchContent_Populate(" << dep << ")\n";
        cmakelists << "endif()\n\n";
      }
    }
  }
}

/**
 * @brief Generate a CMakeLists.txt file from cforge.toml configuration
 *
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool
generate_cmakelists_from_toml(const std::filesystem::path &project_dir,
                              const toml_reader &project_config, bool verbose) {
  // Path to CMakeLists.txt in project directory
  std::filesystem::path cmakelists_path = project_dir / "CMakeLists.txt";
  
  // Skip generation if CMakeLists.txt already exists in project folder
  bool file_exists = std::filesystem::exists(cmakelists_path);
  
  if (file_exists) {
    logger::print_verbose("CMakeLists.txt already exists in project directory, using existing file");
    return true;
  }
  
  logger::print_status("Generating CMakeLists.txt from cforge.toml...");
  
  // Check if we're in a workspace
  auto [is_workspace, workspace_dir] = is_in_workspace(project_dir);
  
  // Create CMakeLists.txt in the project directory
  std::ofstream cmakelists(cmakelists_path);
  if (!cmakelists.is_open()) {
    logger::print_error("Failed to create CMakeLists.txt in project directory");
    return false;
  }

  // Get the right build directory for the configuration
  std::string build_config =
      project_config.get_string("build.build_type", "Debug");
  std::filesystem::path build_base_dir = project_dir / "build";
  std::filesystem::path build_dir =
      get_build_dir_for_config(build_base_dir.string(), build_config);

  // Create build directory if it doesn't exist
  if (!std::filesystem::exists(build_dir)) {
    logger::print_verbose("Creating build directory: " + build_dir.string());
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::filesystem::filesystem_error &e) {
      logger::print_error("Failed to create build directory: " +
                          std::string(e.what()));
      return false;
    }
  }

  // Get project metadata
  std::string project_name =
      project_config.get_string("project.name", "cpp-project");
  std::string project_version =
      project_config.get_string("project.version", "0.1.0");
  std::string project_description = project_config.get_string(
      "project.description", "A C++ project created with cforge");

  // Write initial CMake configuration
  cmakelists << "# CMakeLists.txt for " << project_name << " v"
             << project_version << "\n";
  cmakelists << "# Generated by cforge - C++ project management tool\n\n";

  cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
  cmakelists << "# Project configuration\n";
  cmakelists << "project(" << project_name << " VERSION " << project_version
             << " LANGUAGES CXX)\n\n";

  // Include the original project source directory
  cmakelists << "# Set source directory\n";
  {
    // Normalize path to use forward slashes for CMake
    std::string src_dir = project_dir.string();
    std::replace(src_dir.begin(), src_dir.end(), '\\', '/');
    cmakelists << "set(SOURCE_DIR \"" << src_dir << "\")\n\n";
  }

  // Get author information
  std::vector<std::string> authors =
      project_config.get_string_array("project.authors");
  std::string author_string;
  if (!authors.empty()) {
    for (size_t i = 0; i < authors.size(); ++i) {
      if (i > 0)
        author_string += ", ";
      author_string += authors[i];
    }
  } else {
    author_string = "CForge User";
  }

  // Project description
  cmakelists << "# Project description\n";
  cmakelists << "set(PROJECT_DESCRIPTION \"" << project_description << "\")\n";
  cmakelists << "set(PROJECT_AUTHOR \"" << author_string << "\")\n\n";

  // Get C++ standard
  std::string cpp_standard =
      project_config.get_string("project.cpp_standard", "17");

  // Set C++ standard
  cmakelists << "# Set C++ standard\n";
  cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_standard << ")\n";
  cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
  cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

  // Get binary type (executable, shared_lib, static_lib, or header_only)
  std::string binary_type =
      project_config.get_string("project.binary_type", "executable");

  // Get build settings
  std::string build_type =
      project_config.get_string("build.build_type", "Debug");

  // Set up build configurations
  cmakelists << "# Build configurations\n";
  cmakelists << "if(NOT CMAKE_BUILD_TYPE)\n";
  cmakelists << "    set(CMAKE_BUILD_TYPE \"" << build_type << "\")\n";
  cmakelists << "endif()\n\n";

  cmakelists << "message(STATUS \"Building with ${CMAKE_BUILD_TYPE} "
                "configuration\")\n\n";

  // Set up output directories
  cmakelists << "# Set output directories\n";
  cmakelists << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "
                "\"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
  cmakelists << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "
                "\"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
  cmakelists << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "
                "\"${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}\")\n\n";

  // Get dependencies directory (default: deps)
  std::string deps_dir =
      project_config.get_string("dependencies.directory", "deps");

  // Handle Git dependencies
  configure_git_dependencies_in_cmake(project_config, deps_dir, cmakelists);

  // Check for vcpkg dependencies and add toolchain file if needed
  if (project_config.has_key("dependencies.vcpkg")) {
    cmakelists << "# vcpkg integration\n";
    cmakelists << "if(DEFINED ENV{VCPKG_ROOT})\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "elseif(EXISTS "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\")\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "endif()\n\n";
  }

  // Add vcpkg dependencies
  if (project_config.has_key("dependencies.vcpkg")) {
    cmakelists << "# Dependencies\n";

    // Iterate through all vcpkg dependencies
    auto vcpkg_deps = project_config.get_table_keys("dependencies.vcpkg");
    for (const auto &dep : vcpkg_deps) {
      std::string version;
      std::vector<std::string> components;

      // Check if dependency is specified with version and components
      if (project_config.has_key("dependencies.vcpkg." + dep + ".version")) {
        version = project_config.get_string(
            "dependencies.vcpkg." + dep + ".version", "");

        // Check for components
        if (project_config.has_key("dependencies.vcpkg." + dep +
                                   ".components")) {
          components = project_config.get_string_array("dependencies.vcpkg." +
                                                       dep + ".components");
        }
      } else {
        // Simple dependency specification
        version = project_config.get_string("dependencies.vcpkg." + dep, "");
      }

      cmakelists << "# Find " << dep << "\n";
      if (!version.empty()) {
        cmakelists << "find_package(" << dep << " " << version;
      } else {
        cmakelists << "find_package(" << dep;
      }

      // Add components if any
      if (!components.empty()) {
        cmakelists << " COMPONENTS";
        for (const auto &comp : components) {
          cmakelists << " " << comp;
        }
      }

      cmakelists << " REQUIRED)\n";
    }
    cmakelists << "\n";
  }

  // Source files - use the original project source directory
  cmakelists << "# Add source files\n";
  cmakelists << "file(GLOB_RECURSE SOURCES\n";
  cmakelists << "    \"${SOURCE_DIR}/src/*.cpp\"\n";
  cmakelists << "    \"${SOURCE_DIR}/src/*.c\"\n";
  cmakelists << ")\n\n";

  // Check for additional sources
  if (project_config.has_key("project.additional_sources")) {
    auto additional_sources =
        project_config.get_string_array("project.additional_sources");
    if (!additional_sources.empty()) {
      cmakelists << "# Add additional source files\n";
      for (const auto &source : additional_sources) {
        cmakelists << "file(GLOB_RECURSE ADDITIONAL_SOURCES_" << source
                   << " \"${SOURCE_DIR}/" << source << "\")\n";
        cmakelists << "list(APPEND SOURCES ${ADDITIONAL_SOURCES_" << source
                   << "})\n";
      }
      cmakelists << "\n";
    }
  }

  // Define target
  cmakelists << "# Add target\n";
  if (binary_type == "executable") {
    cmakelists << "add_executable(${PROJECT_NAME} ${SOURCES})\n\n";
  } else if (binary_type == "shared_lib") {
    cmakelists << "add_library(${PROJECT_NAME} SHARED ${SOURCES})\n\n";
  } else if (binary_type == "static_lib") {
    cmakelists << "add_library(${PROJECT_NAME} STATIC ${SOURCES})\n\n";
  } else if (binary_type == "header_only") {
    cmakelists << "add_library(${PROJECT_NAME} INTERFACE)\n\n";
  } else {
    // Default to executable
    cmakelists << "add_executable(${PROJECT_NAME} ${SOURCES})\n\n";
  }

  // Add include directories - use the original project include directory
  if (binary_type == "header_only") {
    cmakelists << "# Include directories\n";
    cmakelists << "target_include_directories(${PROJECT_NAME} INTERFACE\n";
    cmakelists << "    \"${SOURCE_DIR}/include\"\n";
    cmakelists << ")\n\n";
  } else {
    cmakelists << "# Include directories\n";
    cmakelists << "target_include_directories(${PROJECT_NAME} PUBLIC\n";
    cmakelists << "    \"${SOURCE_DIR}/include\"\n";
    cmakelists << ")\n\n";
  }

  // Add additional include directories
  if (project_config.has_key("project.additional_includes")) {
    auto additional_includes =
        project_config.get_string_array("project.additional_includes");
    if (!additional_includes.empty()) {
      cmakelists << "# Add additional include directories\n";
      for (const auto &include : additional_includes) {
        if (binary_type == "header_only") {
          cmakelists << "target_include_directories(${PROJECT_NAME} INTERFACE "
                        "\"${SOURCE_DIR}/"
                     << include << "\")\n";
        } else {
          cmakelists << "target_include_directories(${PROJECT_NAME} PUBLIC "
                        "\"${SOURCE_DIR}/"
                     << include << "\")\n";
        }
      }
      cmakelists << "\n";
    }
  }

  // Add compiler definitions
  if (project_config.has_key("project.definitions")) {
    auto definitions = project_config.get_string_array("project.definitions");
    if (!definitions.empty()) {
      cmakelists << "# Add compiler definitions\n";
      for (const auto &def : definitions) {
        if (binary_type == "header_only") {
          cmakelists << "target_compile_definitions(${PROJECT_NAME} INTERFACE "
                     << def << ")\n";
        } else {
          cmakelists << "target_compile_definitions(${PROJECT_NAME} PUBLIC "
                     << def << ")\n";
        }
      }
      cmakelists << "\n";
    }
  }

  // Link libraries
  cmakelists << "# Link libraries\n";
  if (binary_type == "header_only") {
    cmakelists << "target_link_libraries(${PROJECT_NAME} INTERFACE\n";
  } else {
    cmakelists << "target_link_libraries(${PROJECT_NAME} PUBLIC\n";
  }

  // Standard libraries
  cmakelists << "    ${CMAKE_THREAD_LIBS_INIT}\n";

  // Link vcpkg dependencies
  if (project_config.has_key("dependencies.vcpkg")) {
    auto vcpkg_deps = project_config.get_table_keys("dependencies.vcpkg");
    for (const auto &dep : vcpkg_deps) {
      cmakelists << "    " << dep << "::" << dep << "\n";
    }
  }

  // Link Git dependencies
  if (project_config.has_key("dependencies.git")) {
    auto git_deps = project_config.get_table_keys("dependencies.git");
    for (const auto &dep : git_deps) {
      // Check if this dependency should be linked
      bool link =
          project_config.get_bool("dependencies.git." + dep + ".link", true);

      if (link) {
        cmakelists << "    " << dep << "::" << dep << "\n";
      }
    }
  }

  // Add additional libraries
  if (project_config.has_key("build.libraries")) {
    auto libraries = project_config.get_string_array("build.libraries");
    for (const auto &lib : libraries) {
      cmakelists << "    " << lib << "\n";
    }
  }

  cmakelists << ")\n\n";

  // Add compiler options
  cmakelists << "# Compiler options\n";
  if (binary_type == "header_only") {
    // Header-only libraries don't have compile options
    cmakelists << "# No compile options for header-only libraries\n\n";
  } else {
    cmakelists << "if(MSVC)\n";
    cmakelists << "    target_compile_options(${PROJECT_NAME} PRIVATE /W4)\n";
    cmakelists << "else()\n";
    cmakelists << "    target_compile_options(${PROJECT_NAME} PRIVATE -Wall "
                  "-Wextra -Wpedantic)\n";
    cmakelists << "endif()\n\n";
  }

  // Add tests if available
  cmakelists << "# Tests\n";
  std::filesystem::path tests_dir = project_dir / "tests";
  if (std::filesystem::exists(tests_dir) &&
      std::filesystem::is_directory(tests_dir)) {
    cmakelists << "if(BUILD_TESTING)\n";
    cmakelists << "    enable_testing()\n";
    cmakelists << "    add_subdirectory(\"${SOURCE_DIR}/tests\" "
                  "${CMAKE_BINARY_DIR}/tests)\n";
    cmakelists << "endif()\n\n";
  } else {
    cmakelists << "# No tests directory found\n\n";
  }

  // Installation
  cmakelists << "# Installation\n";
  cmakelists << "include(GNUInstallDirs)\n";

  if (binary_type == "executable") {
    // For executables
    cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
    cmakelists << "    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    cmakelists << "    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << "    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << ")\n\n";

    // Also copy PDB files for Windows Debug builds
    cmakelists << "if(MSVC AND CMAKE_BUILD_TYPE STREQUAL \"Debug\")\n";
    cmakelists << "    install(FILES "
                  "\"${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}/"
                  "$<TARGET_FILE_NAME:${PROJECT_NAME}>.pdb\"\n";
    cmakelists << "            DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    cmakelists << "            CONFIGURATIONS Debug)\n";
    cmakelists << "endif()\n\n";
  } else if (binary_type == "shared_lib" || binary_type == "static_lib") {
    // For libraries
    cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
    cmakelists << "    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    cmakelists << "    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << "    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << ")\n\n";

    // Install headers
    cmakelists << "install(DIRECTORY \"${SOURCE_DIR}/include/\"\n";
    cmakelists << "    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
    cmakelists << "    FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
    cmakelists << ")\n\n";
  } else if (binary_type == "header_only") {
    // For header-only libraries
    cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
    cmakelists << "    EXPORT ${PROJECT_NAME}Targets\n";
    cmakelists << ")\n\n";

    // Install headers
    cmakelists << "install(DIRECTORY \"${SOURCE_DIR}/include/\"\n";
    cmakelists << "    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
    cmakelists << "    FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
    cmakelists << ")\n\n";
  }

  // CPack configuration
  cmakelists << "# CPack configuration\n";
  cmakelists << "set(CPACK_PACKAGE_NAME ${PROJECT_NAME})\n";
  cmakelists << "set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})\n";
  cmakelists << "set(CPACK_PACKAGE_VENDOR \"${PROJECT_AUTHOR}\")\n";
  cmakelists
      << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_DESCRIPTION}\")\n";
  cmakelists << "set(CPACK_RESOURCE_FILE_LICENSE \"${SOURCE_DIR}/LICENSE\")\n";
  cmakelists
      << "set(CPACK_RESOURCE_FILE_README \"${SOURCE_DIR}/README.md\")\n\n";

  // OS-specific settings
  cmakelists << "# OS-specific settings\n";
  cmakelists << "if(WIN32)\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;NSIS\")\n";
  cmakelists << "    set(CPACK_NSIS_MODIFY_PATH ON)\n";
  cmakelists << "elseif(APPLE)\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;TGZ;DragNDrop\")\n";
  cmakelists << "else()\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;TGZ;DEB\")\n";
  cmakelists << "endif()\n\n";

  // Binary packages with config
  cmakelists << "# Binary packages with config\n";
  cmakelists << "set(CPACK_PACKAGE_FILE_NAME "
                "\"${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_"
                "SYSTEM_NAME}-${CMAKE_BUILD_TYPE}\")\n\n";

  // Components
  cmakelists << "# Component-based installation\n";
  cmakelists << "set(CPACK_COMPONENTS_ALL Runtime Development)\n";
  cmakelists << "set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME \"Runtime Files\")\n";
  cmakelists << "set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME \"Development "
                "Files\")\n";
  cmakelists << "set(CPACK_COMPONENT_RUNTIME_DESCRIPTION \"Runtime libraries "
                "and executables\")\n";
  cmakelists << "set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION \"Development "
                "headers and libraries\")\n\n";

  // Include CPack
  cmakelists << "# Include CPack\n";
  cmakelists << "include(CPack)\n";

  cmakelists.close();
  logger::print_verbose("Generated CMakeLists.txt in project directory: " +
                        cmakelists_path.string());
  return true;
}

/**
 * @brief Check if the generator is a multi-configuration generator
 *
 * @param generator Generator name
 * @return bool True if multi-config
 */
static bool is_multi_config_generator(const std::string &generator) {
  // Common multi-config generators
  return generator.find("Visual Studio") != std::string::npos ||
         generator.find("Xcode") != std::string::npos ||
         generator.find("Ninja Multi-Config") != std::string::npos;
}

/**
 * @brief Run CMake configure step
 *
 * @param cmake_args CMake arguments
 * @param build_dir Build directory
 * @param verbose Verbose output
 * @return bool Success flag
 */
static bool run_cmake_configure(const std::vector<std::string> &cmake_args,
                                const std::string &build_dir,
                                const std::string &project_dir, bool verbose) {
  // Set a longer timeout for Windows
#ifdef _WIN32
  int timeout = 180; // 3 minutes for Windows
#else
  int timeout = 120; // 2 minutes for other platforms
#endif

  logger::print_status("Running CMake Configure...");

  // Check if the -DCMAKE_BUILD_TYPE argument is present
  bool has_build_type = false;
  for (const auto &arg : cmake_args) {
    if (arg.find("-DCMAKE_BUILD_TYPE=") != std::string::npos) {
      has_build_type = true;
      logger::print_verbose("Using build type: " + arg);
      break;
    }
  }

  // Ensure build type is being passed - just in case
  if (!has_build_type) {
    logger::print_warning(
        "No build type specified in CMake arguments - this should not happen");
  }

  // Log the full command in verbose mode
  if (verbose) {
    std::string cmd = "cmake";
    for (const auto &arg : cmake_args) {
      cmd += " " + arg;
    }
    logger::print_verbose("Full CMake command: " + cmd);
  }

  // Execute CMake and capture output
  process_result pr = execute_process("cmake", cmake_args, project_dir, nullptr,
                                      nullptr, timeout);
  bool result = pr.success;

  if (result) {
    logger::print_success("CMake Configure completed successfully");
  }

  // Verify that the configuration was successful by checking for CMakeCache.txt
  std::filesystem::path build_path(build_dir);
  bool cmake_success =
      result && std::filesystem::exists(build_path / "CMakeCache.txt");

  if (!cmake_success) {
    // Try formatting errors first from stderr, then stdout
    std::string formatted_errors = format_build_errors(pr.stderr_output);
    if (formatted_errors.empty()) {
      formatted_errors = format_build_errors(pr.stdout_output);
    }
    if (!formatted_errors.empty()) {
      logger::print_error("Error details:");
      std::istringstream iss(formatted_errors);
      std::string line;
      while (std::getline(iss, line)) {
        if (!line.empty()) {
          logger::print_error(line);
        }
      }
    } else {
      // Fallback: print raw outputs
      if (!pr.stderr_output.empty()) {
        logger::print_error("Raw stderr output:");
        std::istringstream ess(pr.stderr_output);
        std::string line;
        while (std::getline(ess, line)) {
          if (!line.empty())
            logger::print_error(line);
        }
      }
      if (!pr.stdout_output.empty()) {
        logger::print_error("Raw stdout output:");
        std::istringstream oss(pr.stdout_output);
        std::string line;
        while (std::getline(oss, line)) {
          if (!line.empty())
            logger::print_error(line);
        }
      }
    }
    return false;
  }

  return true;
}

/**
 * @brief Build the project with CMake
 *
 * @param project_dir Project directory
 * @param build_config Build configuration
 * @param num_jobs Number of parallel jobs (0 for default)
 * @param verbose Verbose output
 * @param target Optional target to build
 * @param built_projects Set of already built projects to avoid rebuilding
 * @return bool Success flag
 */
static bool build_project(const std::filesystem::path &project_dir,
                          const std::string &build_config, int num_jobs,
                          bool verbose, const std::string &target = "",
                          std::set<std::string> *built_projects = nullptr) {
  // Get project name from directory
  std::string project_name = project_dir.filename().string();

  // If we're tracking built projects, check if this one is already done
  if (built_projects &&
      built_projects->find(project_name) != built_projects->end()) {
    logger::print_verbose("Project '" + project_name +
                          "' already built, skipping");
    return true;
  }

  logger::print_status("Building project: " + project_name + " [" +
                       build_config + "]");

  // Load project configuration
  toml::table config_table;
  std::filesystem::path config_path = project_dir / "cforge.toml";

  bool has_project_config = false;
  if (std::filesystem::exists(config_path)) {
    try {
      config_table = toml::parse_file(config_path.string());
      has_project_config = true;
    } catch (const toml::parse_error &e) {
      logger::print_error("Failed to parse cforge.toml: " +
                          std::string(e.what()));
      // Continue with default values
    }
  }

  // Create a toml_reader wrapper for consistent API
  toml_reader project_config(config_table);

  // Check if we're in a workspace
  auto [is_workspace, workspace_dir] = is_in_workspace(project_dir);
  bool use_workspace_build = false;

  if (is_workspace) {
    // Only print this if we're in a workspace but not already at the workspace
    // root
    if (project_dir != workspace_dir) {
      logger::print_status("Detected workspace at: " + workspace_dir.string());
    }

    // Check if workspace has a CMakeLists.txt
    if (std::filesystem::exists(workspace_dir / "CMakeLists.txt")) {
      logger::print_status("Using workspace-level CMakeLists.txt for build");
      use_workspace_build = true;
    }

    // Check for project dependencies if we're building a specific project
    if (has_project_config && !use_workspace_build && built_projects) {
      // Check for project dependencies
      if (project_config.has_key("dependencies.project")) {
        auto project_deps =
            project_config.get_table_keys("dependencies.project");

        if (!project_deps.empty()) {
          logger::print_status("Checking project dependencies...");

          // Build dependencies first
          for (const auto &dep : project_deps) {
            // Skip if already built
            if (built_projects->find(dep) != built_projects->end()) {
              logger::print_verbose("Dependency '" + dep +
                                    "' already built, skipping");
              continue;
            }

            // Find the project directory
            std::filesystem::path dep_path = workspace_dir / dep;
            if (!std::filesystem::exists(dep_path) ||
                !std::filesystem::exists(dep_path / "cforge.toml")) {
              logger::print_warning("Dependency project '" + dep +
                                    "' not found in workspace");
              continue;
            }

            logger::print_status("Building dependency: " + dep);

            // Build the dependency
            if (!build_project(dep_path, build_config, num_jobs, verbose, "",
                               built_projects)) {
              logger::print_error("Failed to build dependency '" + dep +
                                  "', cannot continue with '" + project_name +
                                  "'");
              return false;
            }

            // Mark as built
            built_projects->insert(dep);
          }
        }
      }
    }
  }

  // Use the right directory for building
  std::filesystem::path build_base_dir;
  std::filesystem::path source_dir;

  if (use_workspace_build) {
    build_base_dir = workspace_dir / "build";
    source_dir = workspace_dir;
  } else {
    build_base_dir = project_dir / "build";
    source_dir = project_dir;
  }

  // Get the config-specific build directory
  std::filesystem::path build_dir =
      get_build_dir_for_config(build_base_dir.string(), build_config);
  logger::print_verbose("Using build directory: " + build_dir.string());

  // Make sure the build directory exists
  if (!std::filesystem::exists(build_dir)) {
    logger::print_status("Creating build directory: " + build_dir.string());
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::filesystem::filesystem_error &e) {
      logger::print_error("Failed to create build directory: " +
                          std::string(e.what()));
      return false;
    }
  }

  // For workspace projects, we don't need to handle dependencies or generate
  // CMakeLists.txt
  if (!use_workspace_build && has_project_config) {
    // Clone Git dependencies before generating CMakeLists.txt
    if (project_config.has_key("dependencies.git")) {
      logger::print_status("Setting up Git dependencies...");
      try {
        // Make sure we're in the project directory for relative paths to work
        std::filesystem::current_path(project_dir);

        if (!clone_git_dependencies(project_dir, project_config, verbose)) {
          logger::print_error("Failed to clone Git dependencies");
          return false;
        }

        logger::print_success("Git dependencies successfully set up");
      } catch (const std::exception &ex) {
        logger::print_error("Exception while setting up Git dependencies: " +
                            std::string(ex.what()));
        return false;
      }
    }

    // Generate CMakeLists.txt in the build directory
    std::filesystem::path timestamp_file =
        build_dir / ".cforge_cmakefile_timestamp";

    // Generate new CMakeLists.txt in the build directory
    if (!generate_cmakelists_from_toml(project_dir, project_config, verbose)) {
      logger::print_error("Failed to generate CMakeLists.txt in project directory");
      return false;
    }

    // Update timestamp file
    std::ofstream timestamp(timestamp_file);
    if (timestamp) {
      timestamp << "Generated: " << std::time(nullptr) << std::endl;
      timestamp.close();
    }

    logger::print_success("Generated CMakeLists.txt file in project directory");
  }

  // Prepare CMake arguments
  std::vector<std::string> cmake_args = {
      "-S", source_dir.string(),
      "-B", build_dir.string(),
      "-DCMAKE_BUILD_TYPE=" + build_config
  };

  // For multi-config generators, we need special handling
  std::string generator = get_cmake_generator();
  if (is_multi_config_generator(generator)) {
    logger::print_verbose("Using multi-config generator: " + generator);
  }

  // Add any custom CMake arguments
  if (has_project_config) {
    std::string config_key =
        "build.config." + string_to_lower(build_config) + ".cmake_args";
    if (project_config.has_key(config_key)) {
      auto custom_args = project_config.get_string_array(config_key);
      for (const auto &arg : custom_args) {
        cmake_args.push_back(arg);
      }
    }
  }

  // Check for vcpkg integration
  if (has_project_config && project_config.has_key("dependencies.vcpkg")) {
    // First try environment variable
    const char *vcpkg_root = std::getenv("VCPKG_ROOT");
    if (vcpkg_root) {
      std::string toolchain_path =
          std::string(vcpkg_root) + "/scripts/buildsystems/vcpkg.cmake";

      // Replace backslashes with forward slashes if on Windows
      std::replace(toolchain_path.begin(), toolchain_path.end(), '\\', '/');

      cmake_args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + toolchain_path);
      logger::print_status("Using vcpkg toolchain from VCPKG_ROOT: " +
                           toolchain_path);
    } else {
      // Try local vcpkg installation
      std::string toolchain_path =
          (source_dir / "vcpkg/scripts/buildsystems/vcpkg.cmake").string();

      // Replace backslashes with forward slashes if on Windows
      std::replace(toolchain_path.begin(), toolchain_path.end(), '\\', '/');

      if (std::filesystem::exists(toolchain_path)) {
        cmake_args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + toolchain_path);
        logger::print_status("Using local vcpkg toolchain: " + toolchain_path);
      } else {
        logger::print_warning("vcpkg dependencies specified but couldn't find "
                              "vcpkg toolchain file");
        logger::print_warning("Please set VCPKG_ROOT environment variable or "
                              "install vcpkg in the project directory");
      }
    }
  }

  // Choose generator
  // Check if config specifies a generator
  if (has_project_config && project_config.has_key("cmake.generator")) {
    generator = project_config.get_string("cmake.generator", "");
    cmake_args.push_back("-G");
    cmake_args.push_back(generator);
    logger::print_status("Using CMake generator from config: " + generator);
  }

  // Add extra verbose flag
  if (verbose) {
    cmake_args.push_back("--debug-output");
  }

  // Store the original directory to restore later
  auto original_dir = std::filesystem::current_path();

  // Change to build directory
  try {
    // First ensure the parent directory exists
    std::filesystem::create_directories(build_dir);

    // Then change to the build directory
    std::filesystem::current_path(build_dir);
    logger::print_verbose("Changed working directory to: " +
                          build_dir.string());
  } catch (const std::filesystem::filesystem_error &e) {
    logger::print_error("Failed to change directory: " + std::string(e.what()));
    return false;
  }

  // Run CMake configuration
  logger::print_status("Configuring with CMake...");
  bool configure_result = run_cmake_configure(cmake_args, build_dir.string(),
                                              project_dir.string(), verbose);

  if (!configure_result) {
    logger::print_error("CMake configuration failed for project: " +
                        project_name);
    std::filesystem::current_path(original_dir);
    return false;
  }

  // Run CMake build
  logger::print_status("Building with CMake...");

  // Set up build arguments
  std::vector<std::string> build_args = {"--build", "."};

  // Add config - always specify it for both single-config and multi-config
  // generators
  build_args.push_back("--config");
  build_args.push_back(build_config);
  logger::print_verbose("Using build configuration: " + build_config);

  // Add parallel build flag with appropriate jobs
  if (num_jobs > 0) {
    build_args.push_back("--parallel");
    build_args.push_back(std::to_string(num_jobs));
    logger::print_verbose("Using parallel build with " +
                          std::to_string(num_jobs) + " jobs");
  } else {
    // Default to number of logical cores
    build_args.push_back("--parallel");
    logger::print_verbose("Using parallel build with default number of jobs");
  }

  // Add target if specified
  if (!target.empty()) {
    build_args.push_back("--target");
    build_args.push_back(target);
    logger::print_status("Building target: " + target);
  }

  // Add verbose flag
  if (verbose) {
    build_args.push_back("--verbose");
  }

  // Run the build
  bool build_result =
      execute_tool("cmake", build_args, "", "CMake Build", verbose, 0);

  // Restore original directory
  try {
    std::filesystem::current_path(original_dir);
    logger::print_verbose("Restored working directory to: " +
                          original_dir.string());
  } catch (const std::filesystem::filesystem_error &e) {
    logger::print_warning("Failed to restore directory: " +
                          std::string(e.what()));
    // Continue anyway
  }

  if (build_result) {
    logger::print_success("Built project: " + project_name + " [" +
                          build_config + "]");

    // If we're tracking built projects, add this one
    if (built_projects) {
      built_projects->insert(project_name);
    }

    return true;
  } else {
    logger::print_error("Failed to build project: " + project_name + " [" +
                        build_config + "]");

    // Check for common build errors and provide more helpful messages
    std::filesystem::path cmake_error_log =
        build_dir / "CMakeFiles" / "CMakeError.log";
    if (std::filesystem::exists(cmake_error_log)) {
      logger::print_status(
          "Checking CMake error log for additional information...");
      try {
        std::ifstream error_log(cmake_error_log);
        if (error_log.is_open()) {
          std::string error_content((std::istreambuf_iterator<char>(error_log)),
                                    std::istreambuf_iterator<char>());

          // Only show a short preview of the error log
          if (!error_content.empty()) {
            if (error_content.length() > 500) {
              error_content =
                  error_content.substr(0, 500) + "...\n(error log truncated)";
            }
            logger::print_error("CMake Error Log:\n" + error_content);
            logger::print_status("Full error log available at: " +
                                 cmake_error_log.string());
          }
        }
      } catch (const std::exception &ex) {
        logger::print_warning("Could not read CMake error log: " +
                              std::string(ex.what()));
      }
    }

    logger::print_status("For more detailed build information, try running "
                         "with -v/--verbose flag");
    return false;
  }
}

/**
 * @brief Build a workspace project
 *
 * @param workspace_dir Workspace directory
 * @param project Project to build
 * @param build_config Build configuration
 * @param num_jobs Number of parallel jobs
 * @param verbose Verbose output
 * @param target Optional target to build
 * @return bool Success flag
 */
static bool build_workspace_project(const std::filesystem::path &workspace_dir,
                                    const workspace_project &project,
                                    const std::string &build_config,
                                    int num_jobs, bool verbose,
                                    const std::string &target) {
  // Change to project directory
  std::filesystem::current_path(project.path);

  // Load project configuration
  toml::table config_table;
  std::filesystem::path config_path = project.path / CFORGE_FILE;

  try {
    config_table = toml::parse_file(config_path.string());
  } catch (const toml::parse_error &e) {
    logger::print_error("Failed to load project configuration for '" +
                        project.name + "': " + std::string(e.what()));
    return false;
  }

  // Create a toml_reader wrapper
  toml_reader config_data(config_table);

  // Determine build directory
  std::string base_build_dir =
      config_data.get_string("build.build_dir", "build");

  // Get the config-specific build directory
  std::filesystem::path build_dir =
      get_build_dir_for_config(base_build_dir, build_config);

  // Build the project
  bool success =
      build_project(project.path, build_config, num_jobs, verbose, target);

  if (!success) {
    logger::print_error("Failed to build project '" + project.name + "'");
    return false;
  }

  return true;
}

/**
 * @brief Generate a workspace-level CMakeLists.txt file
 *
 * @param workspace_dir Workspace directory
 * @param workspace_config Workspace configuration
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool
generate_workspace_cmakelists(const std::filesystem::path &workspace_dir,
                              const toml_reader &workspace_config,
                              bool verbose) {
  std::filesystem::path cmakelists_path = workspace_dir / "CMakeLists.txt";

  logger::print_status(
      "Generating workspace CMakeLists.txt from workspace.toml...");

  // Create CMakeLists.txt
  std::ofstream cmakelists(cmakelists_path);
  if (!cmakelists.is_open()) {
    logger::print_error("Failed to create workspace CMakeLists.txt");
    return false;
  }

  // Get workspace metadata
  std::string workspace_name =
      workspace_config.get_string("workspace.name", "cpp-workspace");
  std::string workspace_description = workspace_config.get_string(
      "workspace.description", "A C++ workspace created with cforge");

  // Write initial CMake configuration
  cmakelists << "# Workspace CMakeLists.txt for " << workspace_name << "\n";
  cmakelists << "# Generated by cforge - C++ project management tool\n\n";

  cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
  cmakelists << "# Workspace configuration\n";
  cmakelists << "project(" << workspace_name << " LANGUAGES CXX)\n\n";

  // Workspace description
  cmakelists << "# Workspace description\n";
  cmakelists << "set(WORKSPACE_DESCRIPTION \"" << workspace_description
             << "\")\n\n";

  // Common build settings
  cmakelists << "# Common build settings\n";
  cmakelists << "set(CMAKE_CXX_STANDARD "
             << workspace_config.get_string("workspace.cpp_standard", "17")
             << ")\n";
  cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
  cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

  // Set up build configurations
  std::string build_type =
      workspace_config.get_string("workspace.build_type", "Debug");
  cmakelists << "# Build configuration\n";
  cmakelists << "if(NOT CMAKE_BUILD_TYPE)\n";
  cmakelists << "    set(CMAKE_BUILD_TYPE \"" << build_type << "\")\n";
  cmakelists << "endif()\n\n";

  cmakelists << "message(STATUS \"Building workspace with ${CMAKE_BUILD_TYPE} "
                "configuration\")\n\n";

  // Set up output directories
  cmakelists << "# Set output directories\n";
  cmakelists << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "
                "\"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
  cmakelists << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "
                "\"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
  cmakelists << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "
                "\"${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}\")\n\n";

  // Check for workspace-wide dependencies
  if (workspace_config.has_key("dependencies.git")) {
    std::string deps_dir =
        workspace_config.get_string("dependencies.directory", "deps");
    cmakelists << "# Workspace-level Git dependencies\n";
    configure_git_dependencies_in_cmake(workspace_config, deps_dir, cmakelists);
  }

  // Check for vcpkg dependencies
  if (workspace_config.has_key("dependencies.vcpkg")) {
    cmakelists << "# vcpkg integration\n";
    cmakelists << "if(DEFINED ENV{VCPKG_ROOT})\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "elseif(EXISTS "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\")\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "endif()\n\n";

    // Add vcpkg dependencies
    cmakelists << "# Dependencies\n";
    auto vcpkg_deps = workspace_config.get_table_keys("dependencies.vcpkg");
    for (const auto &dep : vcpkg_deps) {
      std::string version = workspace_config.get_string(
          "dependencies.vcpkg." + dep + ".version", "");
      std::vector<std::string> components = workspace_config.get_string_array(
          "dependencies.vcpkg." + dep + ".components");

      // Add find_package command
      cmakelists << "find_package(" << dep;

      // Add version if specified
      if (!version.empty()) {
        cmakelists << " " << version;
      }

      // Add components if specified
      if (!components.empty()) {
        cmakelists << " COMPONENTS";
        for (const auto &comp : components) {
          cmakelists << " " << comp;
        }
      }

      cmakelists << " REQUIRED)\n";
    }
    cmakelists << "\n";
  }

  // Add Thread package
  cmakelists << "find_package(Threads REQUIRED)\n\n";

  // Add all projects
  cmakelists << "# Add all projects in the workspace\n";

  // Discover projects in the workspace
  std::vector<std::string> projects;
  for (const auto &entry : std::filesystem::directory_iterator(workspace_dir)) {
    if (entry.is_directory() &&
        std::filesystem::exists(entry.path() / "cforge.toml") &&
        entry.path().filename() != "build") {
      projects.push_back(entry.path().filename().string());
    }
  }

  if (workspace_config.has_key("workspace.projects")) {
    // Use projects defined in the workspace configuration
    auto config_projects =
        workspace_config.get_string_array("workspace.projects");
    if (!config_projects.empty()) {
      projects = config_projects;
    }
  }

  if (projects.empty()) {
    logger::print_warning("No projects found in workspace");
  } else {
    logger::print_status("Found " + std::to_string(projects.size()) +
                         " projects in workspace");

    // First, analyze project dependencies to determine the correct order
    std::map<std::string, std::vector<std::string>> project_dependencies;

    // Collect all project dependencies
    for (const auto &project : projects) {
      std::filesystem::path project_config_path =
          workspace_dir / project / "cforge.toml";
      if (std::filesystem::exists(project_config_path)) {
        try {
          toml::table project_table =
              toml::parse_file(project_config_path.string());
          toml_reader project_config(project_table);

          // Check for project dependencies
          if (project_config.has_key("dependencies.project")) {
            auto project_deps =
                project_config.get_table_keys("dependencies.project");
            project_dependencies[project] = project_deps;
            logger::print_verbose("Project '" + project + "' depends on " +
                                  std::to_string(project_deps.size()) +
                                  " other workspace projects");
          } else {
            // No dependencies
            project_dependencies[project] = std::vector<std::string>();
          }
        } catch (const toml::parse_error &e) {
          logger::print_warning("Failed to parse cforge.toml for project '" +
                                project + "': " + std::string(e.what()));
          // Assume no dependencies if we can't parse the file
          project_dependencies[project] = std::vector<std::string>();
        }
      } else {
        // No config file, assume no dependencies
        project_dependencies[project] = std::vector<std::string>();
      }
    }

    // Function to check for circular dependencies
    std::function<bool(const std::string &, std::set<std::string> &,
                       std::set<std::string> &)>
        detect_cycle = [&](const std::string &project,
                           std::set<std::string> &visited,
                           std::set<std::string> &in_path) {
          if (in_path.find(project) != in_path.end())
            return true; // Circular dependency found
          if (visited.find(project) != visited.end())
            return false; // Already processed

          visited.insert(project);
          in_path.insert(project);

          for (const auto &dep : project_dependencies[project]) {
            if (detect_cycle(dep, visited, in_path))
              return true;
          }

          in_path.erase(project);
          return false;
        };

    // Check for circular dependencies
    std::set<std::string> visited, in_path;
    bool has_circular = false;
    for (const auto &project : projects) {
      if (visited.find(project) == visited.end()) {
        if (detect_cycle(project, visited, in_path)) {
          logger::print_error(
              "Circular dependency detected involving project '" + project +
              "'");
          has_circular = true;
          break;
        }
      }
    }

    if (has_circular) {
      logger::print_warning("Circular dependencies found, projects will be "
                            "added in original order");
    } else {
      // Sort projects based on dependencies (topological sort)
      std::vector<std::string> sorted_projects;
      visited.clear();

      std::function<void(const std::string &)> visit =
          [&](const std::string &project) {
            if (visited.find(project) != visited.end())
              return;
            visited.insert(project);

            for (const auto &dep : project_dependencies[project]) {
              visit(dep);
            }

            sorted_projects.push_back(project);
          };

      for (const auto &project : projects) {
        visit(project);
      }

      // Update the projects list to respect dependency order
      if (!sorted_projects.empty()) {
        projects = sorted_projects;
        logger::print_verbose("Projects sorted by dependency order");
      }
    }

    // Add each project
    for (const auto &project : projects) {
      cmakelists << "# Project: " << project << "\n";
      cmakelists << "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/" << project
                 << "/CMakeLists.txt\")\n";
      cmakelists << "    add_subdirectory(" << project << ")\n";
      cmakelists << "else()\n";
      cmakelists << "    message(WARNING \"Project " << project
                 << " has no CMakeLists.txt file\")\n";
      cmakelists << "endif()\n\n";
    }
  }

  // Workspace-level targets
  if (workspace_config.has_key("workspace.targets")) {
    cmakelists << "# Workspace-level targets\n";
    auto targets = workspace_config.get_table_keys("workspace.targets");

    for (const auto &target : targets) {
      std::string target_type = workspace_config.get_string(
          "workspace.targets." + target + ".type", "custom");
      cmakelists << "# Target: " << target << " (Type: " << target_type
                 << ")\n";

      if (target_type == "executable") {
        // Handle executable targets
        std::vector<std::string> sources = workspace_config.get_string_array(
            "workspace.targets." + target + ".sources");

        if (!sources.empty()) {
          cmakelists << "add_executable(" << target << "\n";
          for (const auto &source : sources) {
            cmakelists << "    " << source << "\n";
          }
          cmakelists << ")\n";

          // Add dependencies
          auto dependencies = workspace_config.get_string_array(
              "workspace.targets." + target + ".depends");
          if (!dependencies.empty()) {
            cmakelists << "add_dependencies(" << target << "\n";
            for (const auto &dep : dependencies) {
              cmakelists << "    " << dep << "\n";
            }
            cmakelists << ")\n";
          }

          // Link libraries
          auto libraries = workspace_config.get_string_array(
              "workspace.targets." + target + ".links");
          if (!libraries.empty()) {
            cmakelists << "target_link_libraries(" << target << " PRIVATE\n";
            for (const auto &lib : libraries) {
              cmakelists << "    " << lib << "\n";
            }
            cmakelists << ")\n";
          }

          cmakelists << "\n";
        }
      } else if (target_type == "custom") {
        // Handle custom targets
        std::string command = workspace_config.get_string(
            "workspace.targets." + target + ".command", "");
        if (!command.empty()) {
          cmakelists << "add_custom_target(" << target << "\n";
          cmakelists << "    COMMAND " << command << "\n";
          cmakelists << "    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}\n";
          cmakelists << "    COMMENT \"Running custom target " << target
                     << "\"\n";
          cmakelists << ")\n\n";
        }
      }
    }
  }

  cmakelists.close();
  logger::print_success("Generated workspace CMakeLists.txt file");
  return true;
}

/**
 * @brief Handle the 'build' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_build(const cforge_context_t *ctx) {
  // Check if we're in a workspace
  std::filesystem::path current_dir = std::filesystem::path(ctx->working_dir);
  auto [is_workspace, workspace_dir] = is_in_workspace(current_dir);

  // Parse command line arguments
  std::string config_name;
  int num_jobs = 0;
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
  std::string target;
  std::string project_name;
  bool generate_workspace_cmake = false;
  bool force_regenerate = false;

  // Extract command line arguments
  for (int i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];

    if (arg == "-c" || arg == "--config") {
      if (i + 1 < ctx->args.arg_count) {
        config_name = ctx->args.args[i + 1];
        logger::print_verbose("Using build configuration from command line: " +
                              config_name);
        i++; // Skip the next argument
      }
    } else if (arg.substr(0, 9) == "--config=") {
      config_name = arg.substr(9);
      logger::print_verbose("Using build configuration from command line: " +
                            config_name);
    } else if (arg == "-j" || arg == "--jobs") {
      if (i + 1 < ctx->args.arg_count) {
        try {
          num_jobs = std::stoi(ctx->args.args[i + 1]);
        } catch (...) {
          logger::print_warning("Invalid jobs value, using default");
        }
        i++; // Skip the next argument
      }
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-t" || arg == "--target") {
      if (i + 1 < ctx->args.arg_count) {
        target = ctx->args.args[i + 1];
        i++; // Skip the next argument
      }
    } else if (arg == "-p" || arg == "--project") {
      if (i + 1 < ctx->args.arg_count) {
        project_name = ctx->args.args[i + 1];
        i++; // Skip the next argument
      }
    } else if (arg == "--gen-workspace-cmake") {
      generate_workspace_cmake = true;
    } else if (arg == "--force-regenerate") {
      force_regenerate = true;
    }
  }

  // Check ctx.args.config if config_name is still empty
  if (config_name.empty() && ctx->args.config != nullptr &&
      strlen(ctx->args.config) > 0) {
    config_name = ctx->args.config;
    logger::print_verbose("Using build configuration from context: " +
                          config_name);
  }

  // If still no specific configuration is provided, use the default
  if (config_name.empty()) {
    config_name = "Debug";
    logger::print_verbose("No configuration specified, using default: " +
                          config_name);
  } else {
    // Convert to lowercase for case-insensitive comparison
    std::string config_lower = string_to_lower(config_name);

    // Capitalize first letter for standard configs
    if (config_lower == "debug" || config_lower == "release" ||
        config_lower == "relwithdebinfo" || config_lower == "minsizerel") {
      config_name = config_lower;
      config_name[0] = std::toupper(config_name[0]);
    }
  }

  logger::print_status("Using build configuration: " + config_name);

  int result = 0;

  if (is_workspace) {
    logger::print_status("Building in workspace context: " +
                         current_dir.string());

    if (project_name.empty()) {
      logger::print_status("Building all projects in workspace");

      // Check if we have a workspace CMakeLists.txt
      if (std::filesystem::exists(workspace_dir / "CMakeLists.txt")) {

        // Clean the workspace build directory if force_regenerate is set
        if (force_regenerate) {
          try {
            std::filesystem::path workspace_build_config_dir =
                get_build_dir_for_config(workspace_dir.string(), config_name);

            if (std::filesystem::exists(workspace_build_config_dir)) {
              // Remove only CMake files, not actual build outputs
              if (std::filesystem::exists(workspace_build_config_dir /
                                          "CMakeCache.txt")) {
                std::filesystem::remove(workspace_build_config_dir /
                                        "CMakeCache.txt");
              }

              // Remove CMakeFiles directory
              if (std::filesystem::exists(workspace_build_config_dir /
                                          "CMakeFiles")) {
                std::filesystem::remove_all(workspace_build_config_dir /
                                            "CMakeFiles");
              }
            }

            logger::print_status("Cleaned old CMake cache for workspace build");
          } catch (const std::filesystem::filesystem_error &e) {
            logger::print_warning(
                "Failed to clean workspace build directory: " +
                std::string(e.what()));
            // Continue anyway
          }
        }

        // First try to build the entire workspace at once
        if (build_project(workspace_dir, config_name, num_jobs, verbose,
                          target)) {
          logger::print_success("Workspace built successfully");
          return 0;
        }

        // If workspace build fails, try building each project individually
        logger::print_warning(
            "Workspace build failed, trying to build projects individually");
      } else {
        logger::print_warning("No workspace-level CMakeLists.txt found");
        logger::print_status("You may want to use --gen-workspace-cmake to "
                             "create one for improved build efficiency");
      }

      // Build projects individually
      bool at_least_one_success = false;
      std::set<std::string> built_projects;

      for (const auto &entry :
           std::filesystem::directory_iterator(workspace_dir)) {
        if (entry.is_directory() &&
            std::filesystem::exists(entry.path() / "cforge.toml") &&
            entry.path().filename() != "build") {
          std::string project = entry.path().filename().string();

          // Skip if already built through dependencies
          if (built_projects.find(project) != built_projects.end()) {
            logger::print_verbose("Project '" + project +
                                  "' already built as a dependency, skipping");
            continue;
          }

          bool project_success =
              build_project(entry.path(), config_name, num_jobs, verbose,
                            target, &built_projects);

          if (project_success) {
            at_least_one_success = true;
            // Just print a simple success message without repeating "built
            // successfully"
            logger::print_success("Project '" + project + "' completed");
          } else {
            logger::print_error("Failed to build project: " + project);
            result = 1;
          }
        }
      }

      if (at_least_one_success) {
        logger::print_status("Some projects built successfully");
      }
      if (!at_least_one_success) {
        logger::print_error("All projects failed to build");
        return 1;
      }
    }
  } else {
    if (!build_project(current_dir, config_name, num_jobs, verbose, target)) {
      return 1;
    }
  }

  return result;
}

/**
 * @brief Configure project dependencies in CMakeLists.txt
 *
 * @param workspace_dir Workspace directory
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param cmakelists CMakeLists.txt output stream
 */
static void configure_project_dependencies_in_cmake(
    const std::filesystem::path &workspace_dir,
    const std::filesystem::path &project_dir, const toml_reader &project_config,
    std::ofstream &cmakelists) {
  // Check if we have project dependencies
  if (!project_config.has_key("dependencies.project")) {
    return;
  }

  cmakelists << "# Workspace project dependencies\n";

  // Loop through all project dependencies
  auto project_deps = project_config.get_table_keys("dependencies.project");
  for (const auto &dep : project_deps) {
    // Check if the project exists in the workspace
    std::filesystem::path dep_path = workspace_dir / dep;
    if (!std::filesystem::exists(dep_path) ||
        !std::filesystem::exists(dep_path / "cforge.toml")) {
      cmakelists << "# WARNING: Dependency project '" << dep
                 << "' not found in workspace\n";
      continue;
    }

    // Get dependency options
    bool include = project_config.get_bool(
        "dependencies.project." + dep + ".include", true);
    bool link =
        project_config.get_bool("dependencies.project." + dep + ".link", true);
    std::string target_name = project_config.get_string(
        "dependencies.project." + dep + ".target_name", "");

    cmakelists << "# Project dependency: " << dep << "\n";

    // If target name not specified, use the project name
    if (target_name.empty()) {
      target_name = dep;
    }

    // Process include directories if needed
    if (include) {
      cmakelists << "# Include directories for project dependency '" << dep
                 << "'\n";

      std::vector<std::string> include_dirs;
      std::string include_dirs_key =
          "dependencies.project." + dep + ".include_dirs";

      if (project_config.has_key(include_dirs_key)) {
        include_dirs = project_config.get_string_array(include_dirs_key);
      } else {
        // Default include directories
        include_dirs.push_back("include");
        include_dirs.push_back(".");
      }

      for (const auto &inc_dir : include_dirs) {
        cmakelists << "include_directories(\"${CMAKE_CURRENT_SOURCE_DIR}/../"
                   << dep << "/" << inc_dir << "\")\n";
      }
      cmakelists << "\n";
    }
  }
}
