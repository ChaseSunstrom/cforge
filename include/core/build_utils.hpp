/**
 * @file build_utils.hpp
 * @brief Shared build utilities to eliminate code duplication across commands
 */

#pragma once

#include "cforge/log.hpp"
#include "core/constants.h"
#include "core/platform.hpp"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Check if a CMake generator is a multi-configuration generator
 *
 * Multi-config generators (Visual Studio, Xcode, Ninja Multi-Config) handle
 * Debug/Release configurations within a single build directory.
 *
 * @param generator Generator name
 * @return true if multi-config generator
 */
inline bool is_multi_config_generator(const std::string &generator) {
  return generator.find("Visual Studio") != std::string::npos ||
         generator.find("Xcode") != std::string::npos ||
         generator.find("Ninja Multi-Config") != std::string::npos;
}

/**
 * @brief Check if a CMake generator is valid/available
 *
 * @param gen Generator name to check
 * @return true if generator is supported by CMake
 */
bool is_generator_valid(const std::string &gen);

/**
 * @brief Get the appropriate CMake generator for the current platform
 *
 * Priority order on Windows:
 * 1. Ninja Multi-Config (if ninja available)
 * 2. Visual Studio 17 2022
 * 3. Visual Studio 16 2019
 * 4. Fallback to Ninja Multi-Config
 *
 * On Unix: Unix Makefiles
 *
 * @return CMake generator string
 */
std::string get_cmake_generator();

/**
 * @brief Get the build directory path for a given configuration
 *
 * For multi-config generators, uses a single build directory.
 * For single-config generators, appends config name to directory.
 *
 * @param base_dir Base build directory from configuration
 * @param config Build configuration (Release, Debug, etc.)
 * @param create_if_missing Create directory if it doesn't exist
 * @return Build directory path
 */
std::filesystem::path
get_build_dir_for_config(const std::string &base_dir, const std::string &config,
                         bool create_if_missing = true);

/**
 * @brief Get build configuration from various sources
 *
 * Priority order:
 * 1. Explicit config argument (if provided)
 * 2. Command line --config argument
 * 3. cforge.toml build.build_type setting
 * 4. Default to "Release"
 *
 * @param explicit_config Direct config value (can be nullptr)
 * @param arg_count Number of command line arguments
 * @param args Command line arguments array
 * @param project_config TOML reader for project config
 * @return Build configuration string
 */
std::string get_build_config(const char *explicit_config, cforge_int_t arg_count,
                             char *const *args,
                             const toml_reader *project_config);

/**
 * @brief Find the project output binary path
 *
 * @param build_dir Build directory
 * @param project_name Project name
 * @param config Build configuration
 * @param binary_type Type of binary (executable, shared_library,
 * static_library)
 * @return Path to the binary, or empty if not found
 */
std::filesystem::path
find_project_binary(const std::filesystem::path &build_dir,
                    const std::string &project_name, const std::string &config,
                    const std::string &binary_type = "executable");

/**
 * @brief Ensure CMake is configured for a project
 *
 * @param project_dir Project directory
 * @param build_dir Build directory
 * @param config Build configuration
 * @param verbose Verbose output
 * @param extra_args Additional CMake arguments
 * @return true if configuration succeeded
 */
bool ensure_cmake_configured(const std::filesystem::path &project_dir,
                             const std::filesystem::path &build_dir,
                             const std::string &config, bool verbose,
                             const std::vector<std::string> &extra_args = {});

/**
 * @brief Run CMake build for a project
 *
 * @param build_dir Build directory
 * @param config Build configuration
 * @param target Specific target to build (empty for all)
 * @param num_jobs Number of parallel jobs (0 for default)
 * @param verbose Verbose output
 * @return true if build succeeded
 */
bool run_cmake_build(const std::filesystem::path &build_dir,
                     const std::string &config,
                     const std::string &target = "", cforge_int_t num_jobs = 0,
                     bool verbose = false);

// =============================================================================
// Smart Rebuild Utilities
// =============================================================================

/**
 * @brief Check if a file is newer than another file
 *
 * @param source Source file to check
 * @param target Target file to compare against
 * @return true if source is newer than target, or if target doesn't exist
 */
bool is_file_newer(const std::filesystem::path &source,
                   const std::filesystem::path &target);

/**
 * @brief Check if CMakeLists.txt needs regeneration from cforge.toml
 *
 * @param project_dir Project directory containing cforge.toml
 * @return true if CMakeLists.txt needs to be regenerated
 */
bool needs_cmakelists_regeneration(const std::filesystem::path &project_dir);

/**
 * @brief Check if CMake reconfiguration is needed
 *
 * @param project_dir Project directory
 * @param build_dir Build directory
 * @return true if CMake needs to be reconfigured
 */
bool needs_cmake_reconfigure(const std::filesystem::path &project_dir,
                             const std::filesystem::path &build_dir);

/**
 * @brief Result of prepare_project_for_build
 */
struct build_preparation_result {
  bool success = false;           ///< True if preparation succeeded
  bool cmakelists_regenerated = false;  ///< True if CMakeLists.txt was regenerated
  bool cmake_reconfigured = false;      ///< True if CMake was reconfigured
  std::string error_message;      ///< Error message if failed
};

// Forward declaration - implemented in workspace.cpp
bool generate_cmakelists_from_toml(const std::filesystem::path &project_dir,
                                   const toml_reader &project_config,
                                   bool verbose);

/**
 * @brief Prepare a project for building with smart rebuild detection
 *
 * This function implements the smart rebuild pipeline:
 * 1. Check if CMakeLists.txt needs regeneration from cforge.toml
 * 2. Regenerate if needed (using generate_cmakelists_from_toml)
 * 3. Check if CMake needs reconfiguration
 * 4. Reconfigure if needed
 *
 * @param project_dir Project directory
 * @param build_dir Build directory
 * @param config Build configuration (Debug, Release, etc.)
 * @param verbose Verbose output
 * @param force_regenerate Force CMakeLists.txt regeneration
 * @param force_reconfigure Force CMake reconfiguration
 * @return build_preparation_result with status information
 */
build_preparation_result
prepare_project_for_build(const std::filesystem::path &project_dir,
                          const std::filesystem::path &build_dir,
                          const std::string &config,
                          bool verbose,
                          bool force_regenerate = false,
                          bool force_reconfigure = false);

} // namespace cforge
