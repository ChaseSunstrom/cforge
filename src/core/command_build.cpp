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

  // For multi-config generators (VS, Xcode, Ninja Multi-Config), keep a single build dir
  std::string generator = get_cmake_generator();
  if (is_multi_config_generator(generator)) {
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
                                       "Git Fetch for " + dep, verbose, 600);

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
                         "Git Checkout for " + dep, verbose, 600);

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
          execute_tool("git", clone_args, "", "Git Clone for " + dep, verbose, 600);

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
                         "Git Checkout for " + dep, verbose, 600);

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

  // Check if we're in a workspace and workspace CMakeLists exists
  auto [is_workspace, workspace_dir] = is_in_workspace(project_dir);
  bool use_workspace_build = false;
  if (is_workspace && project_dir == workspace_dir &&
      std::filesystem::exists(workspace_dir / "CMakeLists.txt")) {
    logger::print_status("Using workspace-level CMakeLists.txt for build");
    use_workspace_build = true;
  }
  // Determine build and source directories
  std::filesystem::path build_base_dir = use_workspace_build ?
      workspace_dir / DEFAULT_BUILD_DIR : project_dir / DEFAULT_BUILD_DIR;
  std::filesystem::path source_dir = use_workspace_build ? workspace_dir : project_dir;

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

  // Handle project-level dependencies and CMakeLists generation (skip in workspace build)
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
    if (!::generate_cmakelists_from_toml(project_dir, project_config, verbose)) {
      logger::print_error("Failed to generate CMakeLists.txt in project directory");
      return false;
    }

    // Update timestamp file
    std::ofstream timestamp(timestamp_file);
    if (timestamp) {
      timestamp << "Generated: " << std::time(nullptr) << std::endl;
      timestamp.close();
    }

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
      execute_tool("cmake", build_args, "", "CMake Build", verbose);

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
    // Generate workspace and project CMakeLists.txt before build
    logger::print_status("Generating workspace CMakeLists.txt");
    toml_reader ws_cfg;
    ws_cfg.load((workspace_dir / WORKSPACE_FILE).string());
    if (!generate_workspace_cmakelists(workspace_dir, ws_cfg, verbose)) {
      logger::print_error("Failed to generate workspace CMakeLists.txt");
      return 1;
    }
    workspace ws;
    if (ws.load(workspace_dir)) {
      for (const auto &proj : ws.get_projects()) {
        auto proj_toml = proj.path / CFORGE_FILE;
        if (std::filesystem::exists(proj_toml)) {
          toml_reader pcfg(toml::parse_file(proj_toml.string()));
          if (!generate_cmakelists_from_toml(proj.path, pcfg, verbose)) {
            logger::print_error("Failed to generate CMakeLists.txt for project: " + proj.name);
            return 1;
          }
        }
      }
    }
    logger::print_status("Building in workspace context: " + workspace_dir.string());
    // Determine workspace build directory
    std::filesystem::path base_build = workspace_dir / DEFAULT_BUILD_DIR;
    std::filesystem::path build_dir = get_build_dir_for_config(base_build.string(), config_name);
    // Ensure build directory exists
    if (!std::filesystem::exists(build_dir)) {
      try { std::filesystem::create_directories(build_dir); } catch(...) {}
    }
    // Configure workspace CMake
    std::vector<std::string> cmake_args = { "-S", workspace_dir.string(), "-B", build_dir.string(), "-DCMAKE_BUILD_TYPE=" + config_name };
    if (verbose) cmake_args.push_back("--debug-output");
    if (!run_cmake_configure(cmake_args, build_dir.string(), workspace_dir.string(), verbose)) {
      logger::print_error("Workspace CMake configuration failed");
      return 1;
    }
    // Build single target or entire workspace
    std::vector<std::string> build_args = {"--build", build_dir.string(), "--config", config_name};
    if (num_jobs > 0) {
      build_args.push_back("--parallel");
      build_args.push_back(std::to_string(num_jobs));
    }
    if (verbose) build_args.push_back("--verbose");
    if (!project_name.empty()) {
      // Build only the specified workspace target
      build_args.push_back("--target");
      build_args.push_back(project_name);
      logger::print_status("Building project: " + project_name + " in workspace");
    } else {
      logger::print_status("Building entire workspace");
    }
    bool result = execute_tool("cmake", build_args, "", "CMake Build", verbose);
    if (!result) {
      logger::print_error("Build failed");
      return 1;
    }
    logger::print_success((project_name.empty() ? "Workspace" : "Project '" + project_name + "'") + " built successfully");
    return 0;
  } else {
    // Single project build outside workspace
    // Ensure project CMakeLists.txt exists before building
    std::filesystem::path cmake_file = current_dir / "CMakeLists.txt";
    std::filesystem::path toml_file = current_dir / CFORGE_FILE;
    if (!std::filesystem::exists(cmake_file) && std::filesystem::exists(toml_file)) {
      logger::print_status("Generating project CMakeLists.txt for build");
      toml_reader proj_cfg(toml::parse_file(toml_file.string()));
      if (!generate_cmakelists_from_toml(current_dir, proj_cfg, verbose)) {
        logger::print_error("Failed to generate CMakeLists.txt for project build");
        return 1;
      }
    }
    // Build the standalone project
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
