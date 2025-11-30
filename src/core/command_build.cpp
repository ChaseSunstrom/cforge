/**
 * @file command_build.cpp
 * @brief Implementation of the 'build' command
 */

#include "cforge/log.hpp"
#include "core/build_utils.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/dependency_hash.hpp"
#include "core/error_format.hpp"
#include "core/file_system.h"
#include "core/git_utils.hpp"
#include "core/process_utils.hpp"
#include "core/script_runner.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
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
[[maybe_unused]] static bool is_cmake_available() {
  bool available = is_command_available("cmake");
  if (!available) {
    logger::print_warning("CMake not found in PATH using detection check");
    logger::print_verbose(
        "Please install CMake from https://cmake.org/download/ and make sure "
        "it's in your PATH");
    logger::print_verbose(
        "We'll still attempt to run the cmake command in case "
        "this is a false negative");

    // Suggest alternative build methods
    if (is_visual_studio_available()) {
      logger::print_verbose("Visual Studio is available. You can open the "
                            "project in Visual Studio and build it there");
      logger::print_verbose("1. Open Visual Studio");
      logger::print_verbose("2. Select 'Open a local folder'");
      logger::print_verbose("3. Navigate to your project folder and select it");
      logger::print_verbose(
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
 * @param skip_deps Skip dependencies flag
 * @return bool Success flag
 */
bool clone_git_dependencies(const std::filesystem::path &project_dir,
                            const toml_reader &project_config, bool verbose,
                            bool skip_deps) {
  // Check if we should skip dependency updates
  if (skip_deps) {
    logger::print_verbose("Skipping Git dependency updates (--skip-deps flag)");
    return true;
  }

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

  // Load dependency hashes
  dependency_hash dep_hashes;
  dep_hashes.load(project_dir);

  // Calculate current cforge.toml hash from file content
  std::filesystem::path toml_file = project_dir / "cforge.toml";
  std::string toml_hash;
  {
    std::ifstream toml_stream(toml_file);
    if (toml_stream) {
      std::ostringstream ss;
      ss << toml_stream.rdbuf();
      toml_hash = dep_hashes.calculate_file_content_hash(ss.str());
    } else {
      toml_hash.clear();
    }
  }
  std::string stored_toml_hash = dep_hashes.get_hash("cforge.toml");

  // Get all Git dependencies
  auto git_deps = project_config.get_table_keys("dependencies.git");
  logger::print_action("Fetching",
                       std::to_string(git_deps.size()) + " Git dependencies");

  bool all_success = true;

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

    // Get custom directory if specified
    std::string custom_dir =
        project_config.get_string("dependencies.git." + dep + ".directory", "");
    std::filesystem::path dep_path =
        custom_dir.empty() ? deps_path / dep : project_dir / custom_dir / dep;

    // Check if version has changed
    std::string stored_version = dep_hashes.get_version(dep);
    bool version_changed = !ref.empty() && ref != stored_version;

    if (std::filesystem::exists(dep_path)) {
      // If version changed, remove the directory and reclone
      if (version_changed) {
        logger::print_action("Updating", "version changed for '" + dep +
                                             "', removing existing directory");
        try {
          std::filesystem::remove_all(dep_path);
        } catch (const std::exception &e) {
          logger::print_error("Failed to remove directory for '" + dep +
                              "': " + e.what());
          all_success = false;
          continue;
        }
      } else {
        // Check if update is needed based on directory hash
        std::string current_hash =
            dependency_hash::calculate_directory_hash(dep_path);
        std::string stored_hash = dep_hashes.get_hash(dep);

        bool needs_update =
            current_hash != stored_hash || stored_toml_hash != toml_hash;

        if (!needs_update) {
          // Inform that dependency is up to date and no update is needed
          logger::print_verbose("Dependency '" + dep +
                                "' is up to date, skipping update");
          continue;
        }

        logger::print_verbose(
            "Dependency '" + dep +
            "' directory exists but needs update at: " + dep_path.string());

        // Update the repository
        logger::print_action("Updating",
                             "dependency '" + dep + "' from remote");

        // Run git fetch to update
        std::vector<std::string> fetch_args = {"fetch", "--quiet", "--depth=1"};
        if (verbose) {
          fetch_args.pop_back(); // Remove --quiet for verbose output
        }

        // Set a shorter timeout for fetch operations
        bool fetch_result = execute_tool("git", fetch_args, dep_path.string(),
                                         "Git Fetch for " + dep, verbose, 30);

        if (!fetch_result) {
          logger::print_warning("Failed to fetch updates for '" + dep +
                                "', continuing with existing version");
          all_success = false;
          continue;
        }

        // Checkout specific ref if provided
        if (!ref.empty()) {
          logger::print_action("Checking out",
                               ref + " for dependency '" + dep + "'");

          std::vector<std::string> checkout_args = {"checkout", ref, "--quiet"};
          if (verbose) {
            checkout_args.pop_back(); // Remove --quiet for verbose output
          }

          bool checkout_result =
              execute_tool("git", checkout_args, dep_path.string(),
                           "Git Checkout for " + dep, verbose, 30);

          if (!checkout_result) {
            logger::print_warning("Failed to checkout " + ref + " for '" + dep +
                                  "', continuing with current version");
            all_success = false;
            continue;
          }
        }

        // Update hash after successful update
        current_hash = dependency_hash::calculate_directory_hash(dep_path);
        dep_hashes.set_hash(dep, current_hash);
        if (!ref.empty()) {
          dep_hashes.set_version(dep, ref);
        }
        continue;
      }
    }

    // Create parent directory if it doesn't exist
    std::filesystem::create_directories(dep_path.parent_path());

    // Clone the repository
    logger::fetching(dep + " from " + url);

    std::vector<std::string> clone_args = {"clone", "--depth=1", url,
                                           dep_path.string()};

    // Add specific ref if provided
    if (!ref.empty()) {
      clone_args.push_back("--branch");
      clone_args.push_back(ref);
    }

    if (!verbose) {
      clone_args.push_back("--quiet");
    }

    bool clone_result = execute_tool("git", clone_args, "",
                                     "Git Clone for " + dep, verbose, 600);

    if (!clone_result) {
      logger::print_error("Failed to clone dependency '" + dep + "' from " +
                          url);
      all_success = false;
      continue;
    }

    // Checkout specific commit if provided (since --branch doesn't work with
    // commit hashes)
    if (!commit.empty()) {
      logger::print_action("Checking out", "commit " + commit +
                                               " for dependency '" + dep + "'");

      std::vector<std::string> checkout_args = {"checkout", commit, "--quiet"};
      if (verbose) {
        checkout_args.pop_back(); // Remove --quiet for verbose output
      }

      bool checkout_result =
          execute_tool("git", checkout_args, dep_path.string(),
                       "Git Checkout for " + dep, verbose, 30);

      if (!checkout_result) {
        logger::print_error("Failed to checkout commit " + commit +
                            " for dependency '" + dep + "'");
        all_success = false;
        continue;
      }
    }

    // Store hash and version for newly cloned dependency
    std::string current_hash =
        dependency_hash::calculate_directory_hash(dep_path);
    dep_hashes.set_hash(dep, current_hash);
    if (!ref.empty()) {
      dep_hashes.set_version(dep, ref);
    }

    logger::print_action("Downloaded", dep);
  }

  // Store cforge.toml hash
  dep_hashes.set_hash("cforge.toml", toml_hash);

  // Save updated hashes
  dep_hashes.save(project_dir);

  if (all_success) {
    logger::print_action("Finished", "all Git dependencies are set up");
  } else {
    logger::print_warning("some Git dependencies had issues during setup");
  }
  return all_success;
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

  logger::configuring("CMake");

  if (verbose) {
    std::string cmd = "cmake";
    for (const auto &arg : cmake_args) {
      // Quote arguments that contain spaces
      if (arg.find(' ') != std::string::npos) {
        cmd += " \"" + arg + "\"";
      } else {
        cmd += " " + arg;
      }
    }
    logger::print_verbose("Command: " + cmd);
  }
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
    logger::print_action("Finished", "CMake configuration");
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
 * @param skip_deps Skip dependencies flag
 * @return bool Success flag
 */
static bool build_project(const std::filesystem::path &project_dir,
                          const std::string &build_config, int num_jobs,
                          bool verbose, const std::string &target = "",
                          std::set<std::string> *built_projects = nullptr,
                          bool skip_deps = false) {
  // Start project build timer
  auto project_build_start = std::chrono::steady_clock::now();

  // Get project name from directory
  std::string project_name = project_dir.filename().string();

  // If we're tracking built projects, check if this one is already done
  if (built_projects &&
      built_projects->find(project_name) != built_projects->end()) {
    logger::print_verbose("Project '" + project_name +
                          "' already built, skipping");
    return true;
  }

  logger::building(project_name + " [" + build_config + "]");

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
    logger::print_verbose("Using workspace-level CMakeLists.txt for build");
    use_workspace_build = true;
  }
  // Determine build and source directories
  std::filesystem::path build_base_dir = use_workspace_build
                                             ? workspace_dir / DEFAULT_BUILD_DIR
                                             : project_dir / DEFAULT_BUILD_DIR;
  std::filesystem::path source_dir =
      use_workspace_build ? workspace_dir : project_dir;

  // Get the config-specific build directory
  std::filesystem::path build_dir =
      get_build_dir_for_config(build_base_dir.string(), build_config);
  logger::print_verbose("Using build directory: " + build_dir.string());

  // Make sure the build directory exists
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

  // Handle project-level dependencies and CMakeLists generation (skip in
  // workspace build)
  if (!use_workspace_build && has_project_config) {
    // Clone Git dependencies before generating CMakeLists.txt
    if (project_config.has_key("dependencies.git")) {
      logger::print_action("Setting up", "Git dependencies");
      try {
        // Make sure we're in the project directory for relative paths to work
        std::filesystem::current_path(project_dir);

        if (!clone_git_dependencies(project_dir, project_config, verbose,
                                    skip_deps)) {
          logger::print_error("Failed to clone Git dependencies");
          return false;
        }

        logger::print_action("Finished",
                             "Git dependencies successfully set up");
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
    if (!::generate_cmakelists_from_toml(project_dir, project_config,
                                         verbose)) {
      logger::print_error(
          "Failed to generate CMakeLists.txt in project directory");
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
  std::vector<std::string> cmake_args = {"-S", source_dir.string(), "-B",
                                         build_dir.string(),
                                         "-DCMAKE_BUILD_TYPE=" + build_config};

  // Inject top-level build.defines into CMake args
  if (has_project_config && project_config.has_key("build.defines")) {
    auto global_defs = project_config.get_string_array("build.defines");
    for (const auto &d : global_defs) {
      std::string def = d;
      // Append '=ON' if no value provided
      if (def.find('=') == std::string::npos) {
        def += "=ON";
      }
      cmake_args.push_back(std::string("-D") + def);
    }
  }
  // Inject config-specific defines: build.config.<config>.defines
  {
    std::string defs_key =
        "build.config." + string_to_lower(build_config) + ".defines";
    if (has_project_config && project_config.has_key(defs_key)) {
      auto cfg_defs = project_config.get_string_array(defs_key);
      for (const auto &d : cfg_defs) {
        std::string def = d;
        // Append '=ON' if no value provided
        if (def.find('=') == std::string::npos) {
          def += "=ON";
        }
        cmake_args.push_back(std::string("-D") + def);
      }
    }
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

  // Cross-compilation settings: inject toolchain file, system name, and
  // processors
  if (has_project_config &&
      project_config.has_key("build.cross.toolchain_file")) {
    std::string tc_file =
        project_config.get_string("build.cross.toolchain_file", "");
    if (!tc_file.empty()) {
      cmake_args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + tc_file);
      logger::print_verbose("Using CMake toolchain file: " + tc_file);
    }
  }
  if (has_project_config && project_config.has_key("build.cross.system_name")) {
    std::string sys_name =
        project_config.get_string("build.cross.system_name", "");
    if (!sys_name.empty()) {
      cmake_args.push_back("-DCMAKE_SYSTEM_NAME=" + sys_name);
      logger::print_verbose("Using CMake system name: " + sys_name);
    }
  }
  if (has_project_config &&
      project_config.has_key("build.cross.system_processor")) {
    std::string sys_proc =
        project_config.get_string("build.cross.system_processor", "");
    if (!sys_proc.empty()) {
      cmake_args.push_back("-DCMAKE_SYSTEM_PROCESSOR=" + sys_proc);
      logger::print_verbose("Using CMake system processor: " + sys_proc);
    }
  }
  if (has_project_config && project_config.has_key("build.cross.c_compiler")) {
    std::string cc = project_config.get_string("build.cross.c_compiler", "");
    if (!cc.empty()) {
      cmake_args.push_back("-DCMAKE_C_COMPILER=" + cc);
      logger::print_verbose("Using CMake C compiler: " + cc);
    }
  }
  if (has_project_config &&
      project_config.has_key("build.cross.cxx_compiler")) {
    std::string cxx = project_config.get_string("build.cross.cxx_compiler", "");
    if (!cxx.empty()) {
      cmake_args.push_back("-DCMAKE_CXX_COMPILER=" + cxx);
      logger::print_verbose("Using CMake CXX compiler: " + cxx);
    }
  }

  // Custom compiler specification: separate C and C++ compilers
  if (has_project_config && project_config.has_key("cmake.c_compiler")) {
    std::string cc = project_config.get_string("cmake.c_compiler", "");
    if (!cc.empty()) {
      cmake_args.push_back("-DCMAKE_C_COMPILER=" + cc);
      logger::print_verbose("Using C compiler: " + cc);
    }
  }
  if (has_project_config && project_config.has_key("cmake.cxx_compiler")) {
    std::string cxx = project_config.get_string("cmake.cxx_compiler", "");
    if (!cxx.empty()) {
      cmake_args.push_back("-DCMAKE_CXX_COMPILER=" + cxx);
      logger::print_verbose("Using C++ compiler: " + cxx);
    }
  }

  // Project-level C and C++ standard overrides
  if (has_project_config) {
    std::string cstd = project_config.get_string("project.c_standard", "");
    if (!cstd.empty()) {
      cmake_args.push_back("-DCMAKE_C_STANDARD=" + cstd);
      logger::print_verbose("Using C standard: " + cstd);
    }
    std::string cppstd = project_config.get_string("project.cpp_standard", "");
    if (!cppstd.empty()) {
      cmake_args.push_back("-DCMAKE_CXX_STANDARD=" + cppstd);
      logger::print_verbose("Using C++ standard: " + cppstd);
    }
  }

  // Determine CMake generator: use override in cforge.toml if present,
  // otherwise pick default
  std::string generator;
  if (has_project_config && project_config.has_key("cmake.generator")) {
    generator = project_config.get_string("cmake.generator", "");
    if (!generator.empty()) {
      logger::print_verbose("Using CMake generator from config: " + generator);
    } else {
      generator = get_cmake_generator();
      logger::print_verbose("No CMake generator in config, using default: " +
                            generator);
    }
  } else {
    generator = get_cmake_generator();
    logger::print_verbose("Using default CMake generator: " + generator);
  }

  // vcpkg integration: support path and triplet
  if (has_project_config && project_config.has_key("dependencies.vcpkg")) {
    // Determine vcpkg root directory
    std::string vcpkg_root;
    if (project_config.has_key("dependencies.vcpkg.path")) {
      vcpkg_root = project_config.get_string("dependencies.vcpkg.path", "");
    } else if (const char *env = std::getenv("VCPKG_ROOT")) {
      vcpkg_root = env;
    } else {
      vcpkg_root = (source_dir / "vcpkg").string();
    }
    // Compute toolchain file path
    std::string toolchain_path =
        vcpkg_root + "/scripts/buildsystems/vcpkg.cmake";
    std::replace(toolchain_path.begin(), toolchain_path.end(), '\\', '/');
    if (std::filesystem::exists(toolchain_path)) {
      cmake_args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + toolchain_path);
      logger::print_verbose("Using vcpkg toolchain: " + toolchain_path);
    } else {
      logger::print_warning("vcpkg toolchain file not found: " +
                            toolchain_path);
    }
    // Add triplet if specified
    if (project_config.has_key("dependencies.vcpkg.triplet")) {
      std::string triplet =
          project_config.get_string("dependencies.vcpkg.triplet", "");
      if (!triplet.empty()) {
        cmake_args.push_back("-DVCPKG_TARGET_TRIPLET=" + triplet);
        logger::print_verbose("Using vcpkg triplet: " + triplet);
      }
    }
  }

  // If using Ninja and a toolset is specified, force C/C++ compilers
  if (generator.find("Ninja") != std::string::npos && has_project_config &&
      project_config.has_key("cmake.toolset")) {
    std::string toolset = project_config.get_string("cmake.toolset", "");
    if (!toolset.empty()) {
      cmake_args.push_back("-DCMAKE_C_COMPILER=" + toolset);
      cmake_args.push_back("-DCMAKE_CXX_COMPILER=" + toolset);
      logger::print_verbose("Using C/C++ compiler for Ninja: " + toolset);
    }
  }

  // Validate generator: if invalid, fallback and warn
  if (!is_generator_valid(generator)) {
    logger::print_warning("CMake does not support generator: " + generator +
                          ", falling back to default generator");
    generator = get_cmake_generator();
    logger::print_verbose("Using fallback CMake generator: " + generator);
  }
  // Inject generator flag
  cmake_args.push_back("-G");
  cmake_args.push_back(generator);

  // If Visual Studio generator, specify platform and optional toolset
  if (generator.rfind("Visual Studio", 0) == 0) {
    // Read platform from config or default to x64
    std::string platform = "x64";
    if (has_project_config && project_config.has_key("cmake.platform")) {
      platform = project_config.get_string("cmake.platform", platform);
    }
    cmake_args.push_back("-A");
    cmake_args.push_back(platform);
    logger::print_verbose("Using CMake platform: " + platform);
    // Optional toolset
    if (has_project_config && project_config.has_key("cmake.toolset")) {
      std::string toolset = project_config.get_string("cmake.toolset", "");
      if (!toolset.empty()) {
        cmake_args.push_back("-T");
        cmake_args.push_back(toolset);
        logger::print_verbose("Using CMake toolset: " + toolset);
      }
    }
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
  logger::configuring("project with CMake");
  bool configure_result = run_cmake_configure(cmake_args, build_dir.string(),
                                              project_dir.string(), verbose);

  if (!configure_result) {
    logger::print_error("CMake configuration failed for project: " +
                        project_name);
    std::filesystem::current_path(original_dir);
    return false;
  }

  // Run CMake build
  logger::compiling(project_name);

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
    logger::print_verbose("Building target: " + target);
  }

  // Add verbose flag
  if (verbose) {
    build_args.push_back("--verbose");
  }

  // If Visual Studio generator, override MSBuild OutDir to bin/<config>
  if (generator.rfind("Visual Studio", 0) == 0) {
    // Separator for generator args
    build_args.push_back("--");
    // Compute absolute outdir path
    std::filesystem::path outdir = build_dir / "bin" / build_config;
    // Normalize separator for MSBuild
    std::string outdir_str = outdir.string();
    build_args.push_back(std::string("/p:OutDir=") + outdir_str + "\\");
    logger::print_verbose(std::string("Overriding MSBuild OutDir to: ") +
                          outdir_str);
  }

  // Run the build
  bool build_result =
      execute_tool("cmake", build_args, "", "CMake Build", verbose);

  // Clean up empty config directories under the build root
  for (const auto &cfg : {"Debug", "Release", "RelWithDebInfo"}) {
    std::filesystem::path cfg_dir = build_dir / cfg;
    if (std::filesystem::exists(cfg_dir) &&
        std::filesystem::is_directory(cfg_dir) &&
        std::filesystem::is_empty(cfg_dir)) {
      std::filesystem::remove(cfg_dir);
      logger::print_verbose("Removed empty config directory: " +
                            cfg_dir.string());
    }
  }

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
    // Calculate build duration
    auto project_build_end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           project_build_end - project_build_start)
                           .count();
    std::string duration_str = fmt::format("{:.2f}s", duration_ms / 1000.0);
    logger::finished(build_config, duration_str);

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
      logger::print_verbose(
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
            logger::print_verbose("Full error log available at: " +
                                  cmake_error_log.string());
          }
        }
      } catch (const std::exception &ex) {
        logger::print_warning("Could not read CMake error log: " +
                              std::string(ex.what()));
      }
    }

    logger::print_verbose("For more detailed build information, try running "
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
 * @param skip_deps Skip dependencies flag
 * @return bool Success flag
 */
[[maybe_unused]] static bool build_workspace_project(const std::filesystem::path & /*workspace_dir*/,
                                    const workspace_project &project,
                                    const std::string &build_config,
                                    int num_jobs, bool verbose,
                                    const std::string &target,
                                    bool skip_deps = false) {
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
  bool success = build_project(project.path, build_config, num_jobs, verbose,
                               target, nullptr, skip_deps);

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
  // Start build timer
  auto build_start_time = std::chrono::steady_clock::now();

  // Check if we're in a workspace
  std::filesystem::path current_dir = std::filesystem::path(ctx->working_dir);
  auto [is_workspace, workspace_dir] = is_in_workspace(current_dir);

  // Parse command line arguments
  std::string config_name;
  int num_jobs = 0;
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
  std::string target;
  std::string project_name;
  [[maybe_unused]] bool generate_workspace_cmake = false;
  [[maybe_unused]] bool force_regenerate = false;
  bool skip_deps = false;

  // Extract command line arguments
  for (int i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];

    if (arg == "--skip-deps" || arg == "--no-deps") {
      skip_deps = true;
    } else if (arg == "--no-warnings") {
      g_suppress_warnings = true;
      logger::print_verbose("Suppressing build warnings (--no-warnings flag)");
    } else if (arg == "-c" || arg == "--config") {
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

  // If skip_deps is set, add it to the project config
  if (skip_deps) {
    logger::print_verbose("Skipping Git dependency updates (--skip-deps flag)");
    toml::table config_table;
    std::filesystem::path config_path = current_dir / CFORGE_FILE;
    if (std::filesystem::exists(config_path)) {
      try {
        config_table = toml::parse_file(config_path.string());
        if (!config_table.contains("build")) {
          config_table.insert("build", toml::table{});
        }
        auto &build_table = *config_table.get_as<toml::table>("build");
        build_table.insert_or_assign("skip_deps", true);
      } catch (...) {
        // Ignore errors modifying the config
      }
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

  logger::print_verbose("Using build configuration: " + config_name);

  // Pre-build script support using shared script_runner
  if (!run_pre_build_scripts(is_workspace ? workspace_dir : current_dir,
                             is_workspace, verbose)) {
    return 1;
  }

  int result = 0;

  if (is_workspace) {
    // Generate workspace and project CMakeLists.txt before build
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
            logger::print_error(
                "Failed to generate CMakeLists.txt for project: " + proj.name);
            return 1;
          }
        }
      }
    }
    logger::print_verbose("Building in workspace context: " +
                          workspace_dir.string());
    // Save current directory and switch to workspace root
    auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(workspace_dir);
    // Determine workspace build directory
    std::filesystem::path build_dir = workspace_dir / DEFAULT_BUILD_DIR;
    // Ensure build directory exists
    if (!std::filesystem::exists(build_dir)) {
      try {
        std::filesystem::create_directories(build_dir);
      } catch (...) {
      }
    }
    // Configure workspace CMake
    std::vector<std::string> cmake_args = {"-S", workspace_dir.string(), "-B",
                                           build_dir.string(),
                                           "-DCMAKE_BUILD_TYPE=" + config_name};
    if (verbose)
      cmake_args.push_back("--debug-output");
    if (!run_cmake_configure(cmake_args, build_dir.string(),
                             workspace_dir.string(), verbose)) {
      logger::print_error("Workspace CMake configuration failed");
      // Restore original directory before exit
      std::filesystem::current_path(original_cwd);
      return 1;
    }
    // Build single target or entire workspace
    std::vector<std::string> build_args = {"--build", build_dir.string(),
                                           "--config", config_name};
    if (num_jobs > 0) {
      build_args.push_back("--parallel");
      build_args.push_back(std::to_string(num_jobs));
    }
    if (verbose)
      build_args.push_back("--verbose");
    if (!project_name.empty()) {
      // Build only the specified workspace target
      build_args.push_back("--target");
      build_args.push_back(project_name);
      logger::building(project_name + " in workspace");
    } else {
      logger::building("entire workspace");
    }

    // Handle Git dependencies for workspace projects if not skipped
    if (!skip_deps) {
      for (const auto &proj : ws.get_projects()) {
        auto proj_toml = proj.path / CFORGE_FILE;
        if (std::filesystem::exists(proj_toml)) {
          toml_reader pcfg(toml::parse_file(proj_toml.string()));
          if (pcfg.has_key("dependencies.git")) {
            logger::print_action("Setting up",
                                 "Git dependencies for project: " + proj.name);
            try {
              std::filesystem::current_path(proj.path);
              if (!clone_git_dependencies(proj.path, pcfg, verbose,
                                          skip_deps)) {
                logger::print_error(
                    "Failed to clone Git dependencies for project: " +
                    proj.name);
                std::filesystem::current_path(original_cwd);
                return 1;
              }
            } catch (const std::exception &ex) {
              logger::print_error(
                  "Exception while setting up Git dependencies for project " +
                  proj.name + ": " + std::string(ex.what()));
              std::filesystem::current_path(original_cwd);
              return 1;
            }
          }
        }
      }
      std::filesystem::current_path(workspace_dir);
    }

    bool result = execute_tool("cmake", build_args, "", "CMake Build", verbose);
    // Restore original directory
    std::filesystem::current_path(original_cwd);
    if (!result) {
      logger::print_error("Build failed");
      return 1;
    }
    // Calculate workspace build duration
    auto build_end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           build_end_time - build_start_time)
                           .count();
    std::string duration_str = fmt::format("{:.2f}s", duration_ms / 1000.0);
    logger::finished(config_name, duration_str);
    // Clean up empty config directories under workspace build root
    {
      std::filesystem::path build_root = workspace_dir / DEFAULT_BUILD_DIR;
      for (const auto &cfg : {"Debug", "Release", "RelWithDebInfo"}) {
        std::filesystem::path cfg_dir = build_root / cfg;
        if (std::filesystem::exists(cfg_dir) &&
            std::filesystem::is_directory(cfg_dir) &&
            std::filesystem::is_empty(cfg_dir)) {
          std::filesystem::remove(cfg_dir);
          logger::print_verbose("Removed empty workspace config directory: " +
                                cfg_dir.string());
        }
      }
    }

    // Post-build script support (workspace)
    if (!run_post_build_scripts(workspace_dir, true, verbose)) {
      return 1;
    }

    return 0;
  } else {
    // Single project build outside workspace
    // Ensure project CMakeLists.txt exists before building
    std::filesystem::path cmake_file = current_dir / "CMakeLists.txt";
    std::filesystem::path toml_file = current_dir / CFORGE_FILE;
    if (!std::filesystem::exists(cmake_file) &&
        std::filesystem::exists(toml_file)) {
      logger::print_verbose("Generating project CMakeLists.txt for build");
      toml_reader proj_cfg(toml::parse_file(toml_file.string()));
      if (!generate_cmakelists_from_toml(current_dir, proj_cfg, verbose)) {
        logger::print_error(
            "Failed to generate CMakeLists.txt for project build");
        return 1;
      }
    }
    // Build the standalone project
    if (!build_project(current_dir, config_name, num_jobs, verbose, target,
                       nullptr, skip_deps)) {
      return 1;
    }

    // Post-build script support (single project)
    if (!run_post_build_scripts(current_dir, false, verbose)) {
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
[[maybe_unused]] static void configure_project_dependencies_in_cmake(
    const std::filesystem::path &workspace_dir,
    const std::filesystem::path & /*project_dir*/, const toml_reader &project_config,
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
    [[maybe_unused]] bool link =
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
