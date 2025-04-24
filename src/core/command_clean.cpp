/**
 * @file command_clean.cpp
 * @brief Implementation of the 'clean' command to remove build artifacts
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace cforge;

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
  // If config is empty or the default "Release", use the base dir as is
  if (config.empty() || config == "Release") {
    return base_dir;
  }

  // Otherwise, append the lowercase config name to the build directory
  std::string config_lower = config;
  std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(),
                 ::tolower);

  // Format: build-config (e.g., build-debug)
  return base_dir + "-" + config_lower;
}

/**
 * @brief Find all configuration-specific build directories
 *
 * @param base_dir Base build directory
 * @return std::vector<std::filesystem::path> List of build directories
 */
static std::vector<std::filesystem::path>
find_all_build_dirs(const std::string &base_dir) {
  std::vector<std::filesystem::path> build_dirs;

  // Add the base directory
  build_dirs.push_back(base_dir);

  // Check for config-specific directories
  std::vector<std::string> configs = {"debug", "relwithdebinfo", "minsizerel"};
  for (const auto &config : configs) {
    std::filesystem::path config_dir =
        get_build_dir_for_config(base_dir, config);
    if (std::filesystem::exists(config_dir)) {
      build_dirs.push_back(config_dir);
    }
  }

  return build_dirs;
}

/**
 * @brief Clean the build directory
 *
 * @param build_dir Path to the build directory
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool clean_build_directory(const std::filesystem::path &build_dir,
                                  bool verbose) {
  if (!std::filesystem::exists(build_dir)) {
    logger::print_status("Build directory does not exist, nothing to clean: " +
                         build_dir.string());
    return true;
  }

  logger::print_status("Removing build directory: " + build_dir.string());

  try {
    std::filesystem::remove_all(build_dir);
    logger::print_success("Removed build directory: " + build_dir.string());
    return true;
  } catch (const std::exception &e) {
    logger::print_error("Failed to remove build directory: " +
                        std::string(e.what()));
    return false;
  }
}

/**
 * @brief Clean CMake cache files
 *
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool clean_cmake_files(bool verbose) {
  logger::print_status("Cleaning CMake temporary files...");

  std::vector<std::string> cmake_files = {
      "CMakeCache.txt",        "CMakeFiles",
      "cmake_install.cmake",   "CMakeScripts",
      "compile_commands.json", "CTestTestfile.cmake",
      "CMakeLists.txt.user",   "CMakeLists.txt"};

  bool success = true;
  int count = 0;

  // Remove CMake files from current directory
  for (const auto &file : cmake_files) {
    std::filesystem::path filepath = std::filesystem::current_path() / file;

    if (std::filesystem::exists(filepath)) {
      try {
        if (file == "CMakeLists.txt") {
          // Only remove CMakeLists.txt if cforge.toml exists - it will be
          // regenerated during build
          if (std::filesystem::exists(std::filesystem::current_path() /
                                      CFORGE_FILE)) {
            std::filesystem::remove(filepath);
            count++;
            if (verbose) {
              logger::print_verbose("Removed: " + filepath.string() +
                                    " (will be regenerated from cforge.toml)");
            }
          } else {
            // Skip CMakeLists.txt if no cforge.toml exists
            if (verbose) {
              logger::print_verbose("Preserving: " + filepath.string() +
                                    " (no cforge.toml found)");
            }
          }
        } else {
          // Remove other CMake files
          if (std::filesystem::is_directory(filepath)) {
            count += std::filesystem::remove_all(filepath);
          } else {
            std::filesystem::remove(filepath);
            count++;
          }

          if (verbose) {
            logger::print_verbose("Removed: " + filepath.string());
          }
        }
      } catch (const std::exception &e) {
        logger::print_error("Failed to remove " + filepath.string() + ": " +
                            std::string(e.what()));
        success = false;
      }
    }
  }

  if (count > 0) {
    logger::print_success("Cleaned " + std::to_string(count) +
                          " CMake files/directories");

    // Check if CMakeLists.txt was removed and cforge.toml exists
    if (std::filesystem::exists(std::filesystem::current_path() /
                                CFORGE_FILE)) {
      logger::print_status("CMakeLists.txt has been deleted. It will be "
                           "regenerated from cforge.toml when you run build.");
    }
  } else {
    logger::print_status("No CMake files found to clean");
  }

  return success;
}

/**
 * @brief Regenerate the CMake files after cleaning
 *
 * @param project_dir Project directory
 * @param build_dir Build directory to regenerate
 * @param config Build configuration
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool regenerate_cmake_files(const std::filesystem::path &project_dir,
                                   const std::filesystem::path &build_dir,
                                   const std::string &config, bool verbose) {
  // Check if build directory exists, create if not
  if (!std::filesystem::exists(build_dir)) {
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::exception &e) {
      logger::print_error("Failed to create build directory: " +
                          std::string(e.what()));
      return false;
    }
  }

  // Determine cmake command and arguments
  std::string cmake_cmd;

#ifdef _WIN32
  cmake_cmd = "cmake";
#else
  cmake_cmd = "cmake";
#endif

  // Prepare arguments
  std::vector<std::string> cmake_args;

  // Add source directory (relative to build directory)
  std::filesystem::path rel_path =
      std::filesystem::relative(project_dir, build_dir);
  cmake_args.push_back(rel_path.string());

  // Add generator
#ifdef _WIN32
  cmake_args.push_back("-G");
  cmake_args.push_back("Visual Studio 17 2022");
#else
  cmake_args.push_back("-G");
  cmake_args.push_back("Unix Makefiles");
#endif

  // Add build type for single-config generators
#ifndef _WIN32
  if (!config.empty()) {
    cmake_args.push_back("-DCMAKE_BUILD_TYPE=" + config);
  }
#endif

  // Execute cmake command
  logger::print_status("Running CMake configure...");

  bool result =
      execute_tool(cmake_cmd, cmake_args, build_dir.string(), "CMake", verbose);

  if (!result) {
    logger::print_error("Failed to regenerate CMake files");
    return false;
  }

  logger::print_success("CMake files regenerated successfully");
  return true;
}

/**
 * @brief Handle the 'clean' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_clean(const cforge_context_t *ctx) {
  // Check if cforge.toml exists
  if (!std::filesystem::exists(CFORGE_FILE)) {
    logger::print_error("Not a valid cforge project (missing " +
                        std::string(CFORGE_FILE) + ")");
    return 1;
  }

  // Load project configuration
  toml_reader config;
  if (!config.load(CFORGE_FILE)) {
    logger::print_error("Failed to parse " + std::string(CFORGE_FILE));
    return 1;
  }

  // Get project directory
  std::filesystem::path project_dir = ctx->working_dir;

  // Get base build directory from config
  std::string base_build_dir = config.get_string("build.build_dir", "build");

  // Check arguments
  std::string config_name; // Specific configuration to clean
  bool clean_all = false;  // Clean all configurations
  bool clean_cmake =
      true; // Clean CMake files by default since we regenerate CMakeLists.txt
  bool regenerate = false; // Regenerate CMake files after cleaning
  bool deep = false; // Deep clean: remove dependencies directory

  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // Parse arguments
  for (int i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];

    if (arg == "--all") {
      clean_all = true;
    } else if (arg == "--no-cmake") {
      clean_cmake = false; // Only way to disable cleaning CMake files
    } else if (arg == "--regenerate") {
      regenerate = true;
    } else if (arg == "--deep") {
      deep = true;
    } else if ((arg == "--config" || arg == "-c") && (i + 1) < ctx->args.arg_count) {
      config_name = ctx->args.args[++i];
    }
  }

  // If no specific configuration and not cleaning all, use default config
  if (!clean_all && config_name.empty()) {
    config_name = config.get_string("build.default_config", "Release");
  }

  // Determine which build directories to clean
  std::vector<std::filesystem::path> build_dirs;

  if (clean_all) {
    logger::print_status("Cleaning all build configurations");
    build_dirs = find_all_build_dirs(base_build_dir);
  } else {
    logger::print_status("Cleaning build configuration: " +
                         (config_name.empty() ? "Default" : config_name));
    build_dirs.push_back(get_build_dir_for_config(base_build_dir, config_name));
  }

  // Clean CMake files if requested (which is now the default)
  if (clean_cmake) {
    clean_cmake_files(verbose);
  }

  // Clean each build directory
  bool all_cleaned = true;
  for (const auto &build_dir : build_dirs) {
    if (!clean_build_directory(build_dir, verbose)) {
      all_cleaned = false;
    }
  }

  // Remove dependencies directory if deep clean specified
  if (deep) {
    std::string deps_dir = config.get_string("dependencies.directory", "deps");
    std::filesystem::path deps_path = project_dir / deps_dir;
    if (std::filesystem::exists(deps_path)) {
      logger::print_status("Removing dependencies directory: " + deps_path.string());
      try {
        std::filesystem::remove_all(deps_path);
        logger::print_success("Removed dependencies directory: " + deps_path.string());
      } catch (const std::exception &e) {
        logger::print_error("Failed to remove dependencies directory: " + std::string(e.what()));
        all_cleaned = false;
      }
    } else {
      logger::print_status("Dependencies directory does not exist, nothing to clean: " + deps_path.string());
    }
  }

  // Regenerate CMake files if requested
  if (regenerate) {
    std::filesystem::path build_dir =
        get_build_dir_for_config(base_build_dir, config_name);
    if (!regenerate_cmake_files(project_dir, build_dir, config_name, verbose)) {
      logger::print_error("Failed to regenerate CMake files");
      return 1;
    }
  }

  if (all_cleaned) {
    logger::print_success("Clean completed successfully");
    return 0;
  } else {
    logger::print_error("Some directories could not be cleaned");
    return 1;
  }
}