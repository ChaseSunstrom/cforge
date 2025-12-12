/**
 * @file command_clean.cpp
 * @brief Implementation of the 'clean' command to remove build artifacts
 */

#include "cforge/log.hpp"
#include "core/build_utils.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/script_runner.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace cforge;

/**
 * @brief Make all files in a directory writable (removes read-only attribute)
 *        This is needed on Windows to delete .git directories that have
 * read-only pack files
 *
 * @param dir Directory to process
 */
static void make_directory_writable(const std::filesystem::path &dir) {
  try {
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(dir)) {
      try {
        std::filesystem::permissions(entry.path(),
                                     std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::add);
      } catch (...) {
        // Ignore permission errors on individual files
      }
    }
  } catch (...) {
    // Ignore errors during iteration
  }
}

/**
 * @brief Forcefully remove a directory, handling .git directories on Windows
 *
 * @param dir Directory to remove
 * @return bool Success flag
 */
static bool force_remove_directory(const std::filesystem::path &dir) {
  if (!std::filesystem::exists(dir)) {
    return true;
  }

  // First attempt: standard remove_all
  try {
    std::filesystem::remove_all(dir);
    return true;
  } catch (...) {
    // Continue to fallback methods
  }

  // Second attempt: make all files writable, then remove
  make_directory_writable(dir);
  try {
    std::filesystem::remove_all(dir);
    return true;
  } catch (...) {
    // Continue to fallback methods
  }

#ifdef _WIN32
  // Third attempt on Windows: use system command
  std::string cmd = "cmd /c \"rmdir /s /q \"" + dir.string() + "\"\" 2>nul";
  int result = std::system(cmd.c_str());
  if (result == 0 || !std::filesystem::exists(dir)) {
    return true;
  }

  // Fourth attempt: use robocopy trick (create empty dir, mirror to target)
  // This can handle files that are locked or have special characters
  std::filesystem::path temp_empty =
      std::filesystem::temp_directory_path() / "cforge_empty_dir";
  try {
    std::filesystem::create_directories(temp_empty);
    cmd = "robocopy \"" + temp_empty.string() + "\" \"" + dir.string() +
          "\" /mir /r:1 /w:1 >nul 2>&1";
    std::system(cmd.c_str());
    std::filesystem::remove_all(temp_empty);
    std::filesystem::remove_all(dir);
    return !std::filesystem::exists(dir);
  } catch (...) {
    // Clean up temp directory
    try {
      std::filesystem::remove_all(temp_empty);
    } catch (...) {
    }
  }
#endif

  return !std::filesystem::exists(dir);
}

// Note: get_build_dir_for_config() is now in build_utils.hpp

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
                                  bool /*verbose*/) {
  if (!std::filesystem::exists(build_dir)) {
    logger::print_status("Build directory does not exist, nothing to clean: " +
                         build_dir.string());
    return true;
  }

  logger::removing(build_dir.string());

  if (force_remove_directory(build_dir)) {
    logger::print_action("Removed", build_dir.string());
    return true;
  } else {
    logger::print_error("Failed to remove build directory: " +
                        build_dir.string());
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
  logger::cleaning("CMake temporary files");

  std::vector<std::string> cmake_files = {
      "CMakeCache.txt",        "CMakeFiles",
      "cmake_install.cmake",   "CMakeScripts",
      "compile_commands.json", "CTestTestfile.cmake",
      "CMakeLists.txt.user",   "CMakeLists.txt",
      "cforge.hash"};

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
    logger::print_action("Cleaned",
                         std::to_string(count) + " CMake files/directories");

    // Check if CMakeLists.txt was removed and cforge.toml exists
    if (std::filesystem::exists(std::filesystem::current_path() /
                                CFORGE_FILE)) {
      logger::print_status("CMakeLists.txt has been deleted. It will be "
                           "regenerated from cforge.toml when you run build");
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
                                   [[maybe_unused]] const std::string &config,
                                   bool verbose) {
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
  logger::print_status("Running CMake configure");

  bool result =
      execute_tool(cmake_cmd, cmake_args, build_dir.string(), "CMake", verbose);

  if (!result) {
    logger::print_error("Failed to regenerate CMake files");
    return false;
  }

  logger::print_action("Regenerated", "CMake files");
  return true;
}

/**
 * @brief Handle the 'clean' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_clean(const cforge_context_t *ctx) {
  // Check for workspace clean
  std::filesystem::path current_dir(ctx->working_dir);
  if (std::filesystem::exists(current_dir / WORKSPACE_FILE)) {
    // Workspace cleaning: only clean root workspace build outputs
    logger::cleaning("workspace build outputs");
    // Parse clean arguments
    bool clean_all = false;
    bool clean_cmake = true;
    bool regenerate = false;
    // bool deep = false; // Reserved for future deep clean functionality
    std::string config_name;
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    for (int i = 0; i < ctx->args.arg_count; ++i) {
      std::string arg = ctx->args.args[i];
      if (arg == "--all")
        clean_all = true;
      else if (arg == "--no-cmake")
        clean_cmake = false;
      else if (arg == "--regenerate")
        regenerate = true;
      // --deep flag reserved for future use
      else if ((arg == "--config" || arg == "-c") &&
               i + 1 < ctx->args.arg_count) {
        config_name = ctx->args.args[++i];
      }
    }
    // Determine workspace build directory(s)
    std::filesystem::path base_build = current_dir / DEFAULT_BUILD_DIR;
    std::vector<std::filesystem::path> build_dirs;
    if (clean_all) {
      // Clean all config-specific build dirs as before
      for (const auto &dir : find_all_build_dirs(base_build.string())) {
        build_dirs.push_back(current_dir / dir);
      }
    } else {
      // Always clean the base build directory in workspace
      logger::cleaning(base_build.string());
      build_dirs.push_back(base_build);
    }
    // Clean CMake files in workspace root
    if (clean_cmake) {
      auto old_cwd = std::filesystem::current_path();
      std::filesystem::current_path(current_dir);
      clean_cmake_files(verbose);
      std::filesystem::current_path(old_cwd);
      // Also remove workspace CMakeLists.txt if present
      std::filesystem::path ws_cmake = current_dir / "CMakeLists.txt";
      if (std::filesystem::exists(ws_cmake)) {
        logger::removing(ws_cmake.string());
        try {
          std::filesystem::remove(ws_cmake);
          logger::print_action("Removed", "workspace CMakeLists.txt");
        } catch (const std::exception &e) {
          logger::print_error("Failed to remove workspace CMakeLists.txt: " +
                              std::string(e.what()));
        }
      }

      // Also clean CMake files in each project directory
      workspace ws;
      if (ws.load(current_dir)) {
        auto projects = ws.get_projects();
        for (const auto &proj : projects) {
          std::filesystem::path proj_path = proj.path;
          logger::cleaning("CMake files in " + proj_path.string());
          auto p_old = std::filesystem::current_path();
          std::filesystem::current_path(proj_path);
          clean_cmake_files(verbose);
          std::filesystem::current_path(p_old);
        }
      }
    }
    // Remove build directories
    for (auto &bd : build_dirs) {
      clean_build_directory(bd, verbose);
      if (regenerate) {
        regenerate_cmake_files(current_dir, bd, config_name, verbose);
      }
    }
    logger::print_action("Finished", "workspace clean");
    return 0;
  }
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
  bool deep = false;       // Deep clean: remove dependencies directory

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
    } else if ((arg == "--config" || arg == "-c") &&
               (i + 1) < ctx->args.arg_count) {
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
    logger::cleaning("all build configurations");
    build_dirs = find_all_build_dirs(base_build_dir);
  } else {
    logger::cleaning("build configuration: " +
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
      logger::removing(deps_path.string());
      try {
        std::filesystem::remove_all(deps_path);
        logger::print_action("Removed", deps_path.string());
      } catch (const std::exception &e) {
        logger::print_error("Failed to remove dependencies directory: " +
                            std::string(e.what()));
        all_cleaned = false;
      }
    } else {
      logger::print_status(
          "Dependencies directory does not exist, nothing to clean: " +
          deps_path.string());
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
    logger::print_action("Finished", "clean");
    return 0;
  } else {
    logger::print_error("Some directories could not be cleaned");
    return 1;
  }
}