/**
 * @file build_utils.cpp
 * @brief Implementation of shared build utilities
 */

#include "core/build_utils.hpp"
#include "core/constants.h"
#include "core/types.h"

namespace cforge {

bool is_generator_valid(const std::string &gen) {
  process_result pr =
      execute_process("cmake", {"--help"}, "", nullptr, nullptr, 10);
  if (!pr.success)
    return true; // Assume valid if we can't check
  return pr.stdout_output.find(gen) != std::string::npos;
}

std::string get_cmake_generator() {
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

std::filesystem::path
get_build_dir_for_config(const std::string &base_dir, const std::string &config,
                         bool create_if_missing) {
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

std::string get_build_config(const char *explicit_config, cforge_int_t arg_count,
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

std::filesystem::path
find_project_binary(const std::filesystem::path &build_dir,
                    const std::string &project_name, const std::string &config,
                    const std::string &binary_type) {
  (void)binary_type; // Reserved for future use
  std::vector<std::filesystem::path> search_paths;

  // Common output locations
  search_paths.emplace_back(build_dir / config / project_name);
  search_paths.emplace_back(build_dir / config /
                         (project_name + ".exe")); // Windows
  search_paths.emplace_back(build_dir / "bin" / config / project_name);
  search_paths.emplace_back(build_dir / "bin" / config /
                         (project_name + ".exe")); // Windows
  search_paths.emplace_back(build_dir / project_name);
  search_paths.emplace_back(build_dir / (project_name + ".exe")); // Windows

  for (const auto &path : search_paths) {
    if (std::filesystem::exists(path)) {
      return path;
    }
  }

  return {};
}

bool ensure_cmake_configured(const std::filesystem::path &project_dir,
                             const std::filesystem::path &build_dir,
                             const std::string &config, bool verbose,
                             const std::vector<std::string> &extra_args) {

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

bool run_cmake_build(const std::filesystem::path &build_dir,
                     const std::string &config, const std::string &target,
                     cforge_int_t num_jobs, bool verbose) {

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

bool is_file_newer(const std::filesystem::path &source,
                   const std::filesystem::path &target) {
  if (!std::filesystem::exists(target)) {
    return true;  // Target doesn't exist, so source is "newer"
  }
  if (!std::filesystem::exists(source)) {
    return false;  // Source doesn't exist
  }

  try {
    auto source_time = std::filesystem::last_write_time(source);
    auto target_time = std::filesystem::last_write_time(target);
    return source_time > target_time;
  } catch (const std::exception &e) {
    logger::print_verbose("Error comparing file times: " + std::string(e.what()));
    return true;  // Assume rebuild needed on error
  }
}

bool needs_cmakelists_regeneration(const std::filesystem::path &project_dir) {
  std::filesystem::path toml_path = project_dir / CFORGE_FILE;
  std::filesystem::path cmake_path = project_dir / "CMakeLists.txt";

  // No cforge.toml = nothing to regenerate from
  if (!std::filesystem::exists(toml_path)) {
    return false;
  }

  // CMakeLists.txt doesn't exist = needs generation
  if (!std::filesystem::exists(cmake_path)) {
    logger::print_verbose("CMakeLists.txt doesn't exist, regeneration needed");
    return true;
  }

  // Check if cforge.toml is newer than CMakeLists.txt
  if (is_file_newer(toml_path, cmake_path)) {
    logger::print_verbose("cforge.toml is newer than CMakeLists.txt, regeneration needed");
    return true;
  }

  return false;
}

bool needs_cmake_reconfigure(const std::filesystem::path &project_dir,
                             const std::filesystem::path &build_dir) {
  std::filesystem::path cmake_cache = build_dir / "CMakeCache.txt";
  std::filesystem::path cmake_path = project_dir / "CMakeLists.txt";

  // No CMakeCache.txt = needs configuration
  if (!std::filesystem::exists(cmake_cache)) {
    logger::print_verbose("CMakeCache.txt doesn't exist, reconfiguration needed");
    return true;
  }

  // Check if CMakeLists.txt is newer than CMakeCache.txt
  if (is_file_newer(cmake_path, cmake_cache)) {
    logger::print_verbose("CMakeLists.txt is newer than CMakeCache.txt, reconfiguration needed");
    return true;
  }

  // Also check cforge.toml
  std::filesystem::path toml_path = project_dir / CFORGE_FILE;
  if (std::filesystem::exists(toml_path) && is_file_newer(toml_path, cmake_cache)) {
    logger::print_verbose("cforge.toml is newer than CMakeCache.txt, reconfiguration needed");
    return true;
  }

  return false;
}

build_preparation_result
prepare_project_for_build(const std::filesystem::path &project_dir,
                          const std::filesystem::path &build_dir,
                          const std::string &config,
                          bool verbose,
                          bool force_regenerate,
                          bool force_reconfigure) {
  build_preparation_result result;
  result.success = true;

  std::filesystem::path toml_path = project_dir / CFORGE_FILE;

  // Step 1: Check if CMakeLists.txt needs regeneration
  bool need_regen = force_regenerate || needs_cmakelists_regeneration(project_dir);

  if (need_regen && std::filesystem::exists(toml_path)) {
    logger::print_action("Regenerating", "CMakeLists.txt from cforge.toml");

    // Load the project config
    try {
      toml::table config_table = toml::parse_file(toml_path.string());
      toml_reader project_config(config_table);

      // Generate CMakeLists.txt
      if (!generate_cmakelists_from_toml(project_dir, project_config, verbose)) {
        result.success = false;
        result.error_message = "Failed to generate CMakeLists.txt";
        return result;
      }
      result.cmakelists_regenerated = true;
    } catch (const std::exception &e) {
      result.success = false;
      result.error_message = "Failed to parse cforge.toml: " + std::string(e.what());
      return result;
    }
  }

  // Step 2: Check if CMake needs reconfiguration
  // Note: If we regenerated CMakeLists.txt, we likely need to reconfigure
  bool need_reconfig = force_reconfigure ||
                       result.cmakelists_regenerated ||
                       needs_cmake_reconfigure(project_dir, build_dir);

  if (need_reconfig) {
    logger::print_action("Configuring", "project with CMake");

    // Create build directory if it doesn't exist
    if (!std::filesystem::exists(build_dir)) {
      try {
        std::filesystem::create_directories(build_dir);
      } catch (const std::exception &e) {
        result.success = false;
        result.error_message = "Failed to create build directory: " + std::string(e.what());
        return result;
      }
    }

    // Run CMake configuration
    if (!ensure_cmake_configured(project_dir, build_dir, config, verbose)) {
      result.success = false;
      result.error_message = "CMake configuration failed";
      return result;
    }
    result.cmake_reconfigured = true;
  }

  return result;
}

} // namespace cforge
