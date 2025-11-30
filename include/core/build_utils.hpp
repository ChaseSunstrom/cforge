/**
 * @file build_utils.hpp
 * @brief Shared build utilities to eliminate code duplication across commands
 */

#pragma once

#include "cforge/log.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"

#include <filesystem>
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
inline bool is_generator_valid(const std::string &gen) {
  process_result pr =
      execute_process("cmake", {"--help"}, "", nullptr, nullptr, 10);
  if (!pr.success)
    return true; // Assume valid if we can't check
  return pr.stdout_output.find(gen) != std::string::npos;
}

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
inline std::string get_cmake_generator() {
#ifdef _WIN32
  // Prefer Ninja Multi-Config if available and supported
  if (is_command_available("ninja", 15) &&
      is_generator_valid("Ninja Multi-Config")) {
    logger::print_verbose("Using Ninja Multi-Config generator");
    return "Ninja Multi-Config";
  }

  // Try Visual Studio 17 2022
  if (is_generator_valid("Visual Studio 17 2022")) {
    logger::print_verbose("Using Visual Studio 17 2022 generator");
    return "Visual Studio 17 2022";
  }

  // Fallback to Visual Studio 16 2019 if available
  if (is_generator_valid("Visual Studio 16 2019")) {
    logger::print_verbose("Using Visual Studio 16 2019 generator");
    return "Visual Studio 16 2019";
  }

  // Last resort: Ninja Multi-Config
  logger::print_verbose("Falling back to Ninja Multi-Config generator");
  return "Ninja Multi-Config";
#else
  return "Unix Makefiles";
#endif
}

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
inline std::filesystem::path
get_build_dir_for_config(const std::string &base_dir, const std::string &config,
                         bool create_if_missing = true) {
  std::filesystem::path build_path;

  // For multi-config generators, use single build directory
  std::string generator = get_cmake_generator();
  if (is_multi_config_generator(generator) || config.empty()) {
    build_path = base_dir;
  } else {
    // Transform config name to lowercase for directory naming
    std::string config_lower = string_to_lower(config);
    build_path = base_dir + "-" + config_lower;
  }

  // Create directory if requested and doesn't exist
  if (create_if_missing && !std::filesystem::exists(build_path)) {
    try {
      std::filesystem::create_directories(build_path);
    } catch (const std::exception &e) {
      logger::print_warning("Failed to create build directory: " +
                            std::string(e.what()));
    }
  }

  return build_path;
}

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
inline std::string get_build_config(const char *explicit_config, int arg_count,
                                    char *const *args,
                                    const toml_reader *project_config) {
  // Priority 1: Direct configuration argument
  if (explicit_config != nullptr && strlen(explicit_config) > 0) {
    logger::print_verbose("Using build configuration from direct argument: " +
                          std::string(explicit_config));
    return std::string(explicit_config);
  }

  // Priority 2: Command line argument
  if (arg_count > 0 && args != nullptr) {
    for (int i = 0; i < arg_count; ++i) {
      if (args[i] == nullptr)
        continue;
      std::string arg = args[i];
      if (arg == "--config" || arg == "-c") {
        if (i + 1 < arg_count && args[i + 1] != nullptr) {
          std::string config = args[i + 1];
          logger::print_verbose(
              "Using build configuration from command line: " + config);
          return config;
        }
      } else if (arg.length() > 9 && arg.substr(0, 9) == "--config=") {
        std::string config = arg.substr(9);
        logger::print_verbose("Using build configuration from command line: " +
                              config);
        return config;
      }
    }
  }

  // Priority 3: Configuration from cforge.toml
  if (project_config != nullptr) {
    std::string config = project_config->get_string("build.build_type", "");
    if (!config.empty()) {
      logger::print_verbose("Using build configuration from cforge.toml: " +
                            config);
      return config;
    }
  }

  // Priority 4: Default to Release
  logger::print_verbose(
      "No build configuration specified, defaulting to Release");
  return "Release";
}

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
inline std::filesystem::path
find_project_binary(const std::filesystem::path &build_dir,
                    const std::string &project_name, const std::string &config,
                    const std::string &binary_type = "executable") {
  (void)binary_type; // Reserved for future use
  std::vector<std::filesystem::path> search_paths;

  // Common output locations
  search_paths.push_back(build_dir / config / project_name);
  search_paths.push_back(build_dir / config /
                         (project_name + ".exe")); // Windows
  search_paths.push_back(build_dir / "bin" / config / project_name);
  search_paths.push_back(build_dir / "bin" / config /
                         (project_name + ".exe")); // Windows
  search_paths.push_back(build_dir / project_name);
  search_paths.push_back(build_dir / (project_name + ".exe")); // Windows

  for (const auto &path : search_paths) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

  return {};
}

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
inline bool
ensure_cmake_configured(const std::filesystem::path &project_dir,
                        const std::filesystem::path &build_dir,
                        const std::string &config, bool verbose,
                        const std::vector<std::string> &extra_args = {}) {

  std::vector<std::string> cmake_args = {"-B", build_dir.string(),
                                         "-S", project_dir.string(),
                                         "-G", get_cmake_generator()};

  // Add config for multi-config generators
  std::string generator = get_cmake_generator();
  if (!is_multi_config_generator(generator)) {
    cmake_args.push_back("-DCMAKE_BUILD_TYPE=" + config);
  }

  // Add any extra arguments
  for (const auto &arg : extra_args) {
    cmake_args.push_back(arg);
  }

  return execute_tool("cmake", cmake_args, project_dir.string(), "CMake",
                      verbose, 120);
}

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
inline bool run_cmake_build(const std::filesystem::path &build_dir,
                            const std::string &config,
                            const std::string &target = "", int num_jobs = 0,
                            bool verbose = false) {

  std::vector<std::string> build_args = {"--build", build_dir.string(),
                                         "--config", config};

  if (!target.empty()) {
    build_args.push_back("--target");
    build_args.push_back(target);
  }

  if (num_jobs > 0) {
    build_args.push_back("-j");
    build_args.push_back(std::to_string(num_jobs));
  }

  return execute_tool("cmake", build_args, "", "CMake Build", verbose, 600);
}

} // namespace cforge
