/**
 * @file command_test.cpp
 * @brief Implementation of the 'test' command to run project tests
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
 * @brief Find the test executable in the build directory
 */
static std::filesystem::path
find_test_executable(const std::filesystem::path &build_dir,
                     const std::string &project_name, const std::string &config,
                     const std::string &test_executable_name = "") {
  // Determine base executable name
  std::string executable_base;
  if (!test_executable_name.empty()) {
    executable_base = test_executable_name;
  } else {
    // Format: project_name_config_tests (e.g., myproject_debug_tests)
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(),
                   config_lower.begin(), ::tolower);
    executable_base = project_name + "_" + config_lower + "_tests";
  }

  // Try the standard naming convention first
  std::string executable_name = executable_base;

// Add extension for Windows
#ifdef _WIN32
  executable_name += ".exe";
#endif

  // Common test executable locations
  std::vector<std::filesystem::path> search_paths = {
      build_dir / "bin" / executable_name,
      build_dir / "tests" / "bin" / executable_name,
      build_dir / "tests" / executable_name, build_dir / executable_name};

  // Check for the executable with the naming convention
  for (const auto &path : search_paths) {
    if (std::filesystem::exists(path)) {
      logger::print_status("Found test executable with expected name: " +
                           path.string());
      return path;
    }
  }

  // If not found, try alternative naming conventions
  std::vector<std::string> alt_names = {
      project_name + "_tests", // Standard name without config
      "test_" + project_name,  // Alternative prefix
      project_name + "_test"   // Alternative suffix
  };

  for (const auto &alt_base : alt_names) {
    std::string alt_name = alt_base;

// Add extension for Windows
#ifdef _WIN32
    alt_name += ".exe";
#endif

    for (const auto &base_path :
         {build_dir / "bin", build_dir / "tests" / "bin", build_dir / "tests",
          build_dir}) {
      std::filesystem::path alt_path = base_path / alt_name;
      if (std::filesystem::exists(alt_path)) {
        logger::print_status("Found alternative test executable: " +
                             alt_path.string());
        return alt_path;
      }
    }
  }

  // If still not found, do a recursive search for any executable that might be
  // a test
  logger::print_status("Recursively searching for test executable...");

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(build_dir)) {
    if (entry.is_regular_file()) {
      std::string filename = entry.path().filename().string();

      // Look for files that have "test" in their name
      std::string lower_filename = filename;
      std::transform(lower_filename.begin(), lower_filename.end(),
                     lower_filename.begin(), ::tolower);

      if (lower_filename.find("test") != std::string::npos) {
#ifdef _WIN32
        if (entry.path().extension() == ".exe") {
#else
        if ((entry.permissions(std::filesystem::status(entry.path())) &
             std::filesystem::perms::owner_exec)) {
#endif
          logger::print_status("Found test executable via search: " +
                               entry.path().string());
          return entry.path();
        }
      }
    }
  }

  return {};
}

/**
 * @brief Run CTest in the build directory
 */
static bool run_ctest(const std::filesystem::path &build_dir, bool verbose,
                      int jobs) {
  std::string command = "ctest";
  std::vector<std::string> args;

  if (verbose)
    args.push_back("-V");
  if (jobs > 0) {
    args.push_back("-j");
    args.push_back(std::to_string(jobs));
  }

  logger::print_status("Running tests with CTest...");
  auto result =
      execute_tool(command, args, build_dir.string(), "CTest", verbose);

  return result;
}

/**
 * @brief Run tests directly with the test executable
 */
static bool run_test_executable(const std::filesystem::path &test_executable,
                                const std::vector<std::string> &args,
                                bool verbose) {
  std::string command = test_executable.string();
  std::vector<std::string> test_args = args;

  auto working_dir = test_executable.parent_path();
  if (working_dir.empty()) {
    working_dir = std::filesystem::current_path();
  }

  logger::print_status("Running tests with " +
                       test_executable.filename().string());
  auto result =
      execute_tool(command, test_args, working_dir.string(), "Test", verbose);

  return result;
}

/**
 * @brief Handle the 'test' command
 */
cforge_int_t cforge_cmd_test(const cforge_context_t *ctx) {
  // Check if the project file exists
  if (!std::filesystem::exists(CFORGE_FILE)) {
    logger::print_error("No " + std::string(CFORGE_FILE) +
                        " file found in the current directory");
    return 1;
  }

  // Load project configuration
  toml_reader config;
  if (!config.load(CFORGE_FILE)) {
    logger::print_error("Failed to load " + std::string(CFORGE_FILE) + " file");
    return 1;
  }

  // Get project name
  std::string project_name;
  if (!config.has_key("project.name") || project_name.empty()) {
    project_name = config.get_string("project.name", "");
    if (project_name.empty()) {
      logger::print_error("Project name not found in " +
                          std::string(CFORGE_FILE));
      return 1;
    }
  }

  // Check if testing is enabled
  bool testing_enabled = config.get_bool("test.enabled", true);

  if (!testing_enabled) {
    logger::print_status("Testing is disabled in the project configuration");
    return 0;
  }

  // Get base build directory
  std::string base_build_dir = config.get_string("build.build_dir", "build");

  // Get build configuration
  std::string build_config = config.get_string("build.build_type", "Release");

  // Check for config in command line args
  if (ctx->args.args) {
    for (int i = 0; ctx->args.args[i]; ++i) {
      if (strcmp(ctx->args.args[i], "--config") == 0 ||
          strcmp(ctx->args.args[i], "-c") == 0) {
        if (ctx->args.args[i + 1]) {
          build_config = ctx->args.args[i + 1];
          break;
        }
      }
    }
  }

  // Get the config-specific build directory
  std::filesystem::path build_dir =
      get_build_dir_for_config(base_build_dir, build_config);

  // If build directory doesn't exist, build the project first
  if (!std::filesystem::exists(build_dir)) {
    logger::print_status(
        "Build directory not found, building project first...");
    if (cforge_cmd_build(ctx) != 0) {
      logger::print_error("Failed to build the project");
      return 1;
    }
  }

  // Get number of parallel jobs
  int jobs = static_cast<int>(config.get_int("test.jobs", 0));

  // Check for jobs in command line args
  if (ctx->args.args) {
    for (int i = 0; ctx->args.args[i]; ++i) {
      if (strcmp(ctx->args.args[i], "--jobs") == 0 ||
          strcmp(ctx->args.args[i], "-j") == 0) {
        if (ctx->args.args[i + 1]) {
          jobs = std::atoi(ctx->args.args[i + 1]);
          break;
        }
      }
    }
  }

  // Get verbose flag
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // Try running CTest first
  if (run_ctest(build_dir, verbose, jobs)) {
    logger::print_success("All tests passed");
    return 0;
  }

  // If CTest fails, try running the test executable directly
  std::string test_executable_name =
      config.get_string("test.test_executable", "");

  auto test_executable = find_test_executable(
      build_dir, project_name, build_config, test_executable_name);

  if (test_executable.empty()) {
    logger::print_error("Test executable not found");
    logger::print_status("Expected test executable format: " + project_name +
                         "_" + build_config + "_tests");
    return 1;
  }

  // Get test arguments and run tests with the executable
  std::vector<std::string> test_args = config.get_string_array("test.args");

  // Check for additional test args in command line
  if (ctx->args.args) {
    bool skip_next = false;
    for (int i = 0; ctx->args.args[i]; ++i) {
      if (skip_next) {
        skip_next = false;
        continue;
      }

      std::string arg = ctx->args.args[i];

      // Skip known flags and their values
      if (arg == "--config" || arg == "-c" || arg == "--jobs" || arg == "-j") {
        skip_next = true;
        continue;
      }

      // Skip cforge command name
      if (i == 0 && arg == "test") {
        continue;
      }

      // Add to test arguments if it's not a cforge flag
      if (arg.substr(0, 2) != "--" && arg.substr(0, 1) != "-") {
        test_args.push_back(arg);
      }
    }
  }

  if (run_test_executable(test_executable, test_args, verbose)) {
    logger::print_success("All tests passed");
    return 0;
  }

  return 1;
}