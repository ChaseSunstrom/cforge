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
#include "core/include_analyzer.hpp"
#include "core/lockfile.hpp"
#include "core/process_utils.hpp"
#include "core/registry.hpp"
#include "core/script_runner.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
#include "core/workspace.hpp"

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
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
      cforge::logger::print_verbose("Found Visual Studio at: " + path);
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
  bool available = cforge::is_command_available("cmake");
  if (!available) {
    cforge::logger::print_warning("CMake not found in PATH using detection check");
    cforge::logger::print_verbose(
        "Please install CMake from https://cmake.org/download/ and make sure "
        "it's in your PATH");
    cforge::logger::print_verbose(
        "We'll still attempt to run the cmake command in case "
        "this is a false negative");

    // Suggest alternative build methods
    if (is_visual_studio_available()) {
      cforge::logger::print_verbose("Visual Studio is available. You can open the "
                            "project in Visual Studio and build it there");
      cforge::logger::print_verbose("1. Open Visual Studio");
      cforge::logger::print_verbose("2. Select 'Open a local folder'");
      cforge::logger::print_verbose("3. Navigate to your project folder and select it");
      cforge::logger::print_verbose(
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
                            const cforge::toml_reader&project_config, bool verbose,
                            bool skip_deps) {
  // Check if we should skip dependency updates
  if (skip_deps) {
    cforge::logger::print_verbose("Skipping Git dependency updates (--skip-deps flag)");
    return true;
  }

  // Check if we have Git dependencies
  if (!project_config.has_key("dependencies.git")) {
    cforge::logger::print_verbose("No Git dependencies to setup");
    return true;
  }

  // Get dependencies directory from configuration
  std::string deps_dir =
      project_config.get_string("dependencies.directory", "deps");
  std::filesystem::path deps_path = project_dir / deps_dir;

  // Create dependencies directory if it doesn't exist
  if (!std::filesystem::exists(deps_path)) {
    cforge::logger::print_verbose("Creating dependencies directory: " +
                          deps_path.string());
    std::filesystem::create_directories(deps_path);
  }

  // Check if git is available
  if (!cforge::is_command_available("git", 20)) {
    cforge::logger::print_error("Git is not available. Please install Git and ensure "
                        "it's in your PATH.");
    return false;
  }

  // Load dependency hashes
  cforge::dependency_hash dep_hashes;
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
  cforge::logger::print_action("Fetching",
                       std::to_string(git_deps.size()) + " Git dependencies");

  bool all_success = true;

  for (const auto &dep : git_deps) {
    // Get dependency configuration
    std::string url =
        project_config.get_string("dependencies.git." + dep + ".url", "");
    if (url.empty()) {
      cforge::logger::print_warning("Git dependency '" + dep +
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
        cforge::logger::print_action("Updating", "version changed for '" + dep +
                                             "', removing existing directory");
        try {
          std::filesystem::remove_all(dep_path);
        } catch (const std::exception &e) {
          cforge::logger::print_error("Failed to remove directory for '" + dep +
                              "': " + e.what());
          all_success = false;
          continue;
        }
      } else {
        // Check if update is needed based on directory hash
        std::string current_hash =
            cforge::dependency_hash::calculate_directory_hash(dep_path);
        std::string stored_hash = dep_hashes.get_hash(dep);

        bool needs_update =
            current_hash != stored_hash || stored_toml_hash != toml_hash;

        if (!needs_update) {
          // Inform that dependency is up to date and no update is needed
          cforge::logger::print_verbose("Dependency '" + dep +
                                "' is up to date, skipping update");
          continue;
        }

        cforge::logger::print_verbose(
            "Dependency '" + dep +
            "' directory exists but needs update at: " + dep_path.string());

        // Update the repository
        cforge::logger::print_action("Updating",
                             "dependency '" + dep + "' from remote");

        // Run git fetch to update
        std::vector<std::string> fetch_args = {"fetch", "--quiet", "--depth=1"};
        if (verbose) {
          fetch_args.pop_back(); // Remove --quiet for verbose output
        }

        // Set a shorter timeout for fetch operations
        bool fetch_result = cforge::execute_tool("git", fetch_args, dep_path.string(),
                                         "Git Fetch for " + dep, verbose, 30);

        if (!fetch_result) {
          cforge::logger::print_warning("Failed to fetch updates for '" + dep +
                                "', continuing with existing version");
          all_success = false;
          continue;
        }

        // Checkout specific ref if provided
        if (!ref.empty()) {
          cforge::logger::print_action("Checking out",
                               ref + " for dependency '" + dep + "'");

          std::vector<std::string> checkout_args = {"checkout", ref, "--quiet"};
          if (verbose) {
            checkout_args.pop_back(); // Remove --quiet for verbose output
          }

          bool checkout_result =
              cforge::execute_tool("git", checkout_args, dep_path.string(),
                           "Git Checkout for " + dep, verbose, 30);

          if (!checkout_result) {
            cforge::logger::print_warning("Failed to checkout " + ref + " for '" + dep +
                                  "', continuing with current version");
            all_success = false;
            continue;
          }
        }

        // Update hash after successful update
        current_hash = cforge::dependency_hash::calculate_directory_hash(dep_path);
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
    cforge::logger::fetching(dep + " from " + url);

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

    bool clone_result = cforge::execute_tool("git", clone_args, "",
                                     "Git Clone for " + dep, verbose, 600);

    if (!clone_result) {
      cforge::logger::print_error("Failed to clone dependency '" + dep + "' from " +
                          url);
      all_success = false;
      continue;
    }

    // Checkout specific commit if provided (since --branch doesn't work with
    // commit hashes)
    if (!commit.empty()) {
      cforge::logger::print_action("Checking out", "commit " + commit +
                                               " for dependency '" + dep + "'");

      std::vector<std::string> checkout_args = {"checkout", commit, "--quiet"};
      if (verbose) {
        checkout_args.pop_back(); // Remove --quiet for verbose output
      }

      bool checkout_result =
          cforge::execute_tool("git", checkout_args, dep_path.string(),
                       "Git Checkout for " + dep, verbose, 30);

      if (!checkout_result) {
        cforge::logger::print_error("Failed to checkout commit " + commit +
                            " for dependency '" + dep + "'");
        all_success = false;
        continue;
      }
    }

    // Store hash and version for newly cloned dependency
    std::string current_hash =
        cforge::dependency_hash::calculate_directory_hash(dep_path);
    dep_hashes.set_hash(dep, current_hash);
    if (!ref.empty()) {
      dep_hashes.set_version(dep, ref);
    }

    cforge::logger::print_action("Downloaded", dep);
  }

  // Save updated dependency hashes (but NOT cforge.toml hash - that's handled by generate_cmakelists_from_toml)
  dep_hashes.save(project_dir);

  if (all_success) {
    cforge::logger::print_action("Finished", "all Git dependencies are set up");
  } else {
    cforge::logger::print_warning("some Git dependencies had issues during setup");
  }
  return all_success;
}

/**
 * @brief Expand placeholders in a setup command
 *
 * Placeholders:
 * - {package_dir}: Path to the package directory
 * - {version}: Package version
 * - {option:name}: Value of option 'name' from setup_options
 *
 * @param command Command template with placeholders
 * @param package_dir Package directory path
 * @param version Package version
 * @param options Setup options map
 * @return Expanded command string
 */
static std::string expand_setup_command(
    const std::string &command,
    const std::filesystem::path &package_dir,
    const std::string &version,
    const std::map<std::string, std::string> &options) {

  std::string result = command;

  // Replace {package_dir}
  cforge_size_t pos;
  while ((pos = result.find("{package_dir}")) != std::string::npos) {
    result.replace(pos, 13, package_dir.string());
  }

  // Replace {version}
  while ((pos = result.find("{version}")) != std::string::npos) {
    result.replace(pos, 9, version);
  }

  // Replace {option:name} placeholders
  cforge_size_t start = 0;
  while ((start = result.find("{option:", start)) != std::string::npos) {
    cforge_size_t end = result.find("}", start);
    if (end == std::string::npos) break;

    std::string option_name = result.substr(start + 8, end - start - 8);
    std::string value;

    auto it = options.find(option_name);
    if (it != options.end()) {
      value = it->second;
    } else {
      // Option not found, leave empty
      cforge::logger::print_warning("Setup option '" + option_name + "' not found, using empty value");
    }

    result.replace(start, end - start + 1, value);
    start += value.length();
  }

  return result;
}

/**
 * @brief Run setup commands for a package
 *
 * @param pkg Package info from registry
 * @param package_dir Package directory
 * @param version Package version
 * @param setup_options Merged options (defaults + user overrides)
 * @param verbose Verbose output
 * @return true if setup succeeded or was skipped
 */
static bool run_package_setup(
    const cforge::package_info&pkg,
    const std::filesystem::path &package_dir,
    const std::string &version,
    const std::map<std::string, std::string> &setup_options,
    bool verbose) {

  if (!pkg.setup.has_setup()) {
    return true;  // No setup needed
  }

  // Determine platform-specific commands
  std::vector<std::string> commands = pkg.setup.commands;
  std::vector<std::string> required_tools = pkg.setup.required_tools;

#ifdef _WIN32
  if (!pkg.setup.windows.commands.empty()) {
    commands = pkg.setup.windows.commands;
  }
  if (!pkg.setup.windows.required_tools.empty()) {
    required_tools = pkg.setup.windows.required_tools;
  }
#elif defined(__APPLE__)
  if (!pkg.setup.macos.commands.empty()) {
    commands = pkg.setup.macos.commands;
  }
  if (!pkg.setup.macos.required_tools.empty()) {
    required_tools = pkg.setup.macos.required_tools;
  }
#else
  if (!pkg.setup.linux.commands.empty()) {
    commands = pkg.setup.linux.commands;
  }
  if (!pkg.setup.linux.required_tools.empty()) {
    required_tools = pkg.setup.linux.required_tools;
  }
#endif

  if (commands.empty()) {
    return true;  // No commands to run
  }

  // Check if outputs already exist (skip setup if all exist)
  if (!pkg.setup.outputs.empty()) {
    bool all_exist = true;
    for (const auto &output : pkg.setup.outputs) {
      std::filesystem::path output_path = package_dir / output;
      if (!std::filesystem::exists(output_path)) {
        all_exist = false;
        break;
      }
    }
    if (all_exist) {
      cforge::logger::print_verbose("Setup outputs already exist for '" + pkg.name + "', skipping");
      return true;
    }
  }

  // Check required tools
  for (const auto &tool : required_tools) {
    if (!cforge::is_command_available(tool, 5)) {
      cforge::logger::print_error("Required tool '" + tool + "' not found for package '" + pkg.name + "' setup");
      cforge::logger::print_error("Please install '" + tool + "' and ensure it's in your PATH");
      return false;
    }
  }

  cforge::logger::print_action("Setting up", pkg.name);

  // Determine working directory
  std::filesystem::path workdir = package_dir;
  if (!pkg.setup.workdir.empty() && pkg.setup.workdir != ".") {
    workdir = package_dir / pkg.setup.workdir;
  }

  // Run each command
  for (const auto &cmd_template : commands) {
    std::string cmd = expand_setup_command(cmd_template, package_dir, version, setup_options);

    cforge::logger::print_verbose("Running setup command: " + cmd);

    // Parse command into program and arguments
    // Simple parsing: first token is program, rest are arguments
    std::istringstream iss(cmd);
    std::string program;
    iss >> program;

    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) {
      args.push_back(arg);
    }

    bool result = cforge::execute_tool(program, args, workdir.string(),
                               "Setup for " + pkg.name, verbose, 300);

    if (!result) {
      cforge::logger::print_error("Setup command failed for package '" + pkg.name + "': " + cmd);
      return false;
    }
  }

  cforge::logger::print_action("Finished", "setup for " + pkg.name);
  return true;
}

/**
 * @brief Resolve and clone index (registry) dependencies for a project
 *
 * This function reads the [dependencies] section, identifies packages that
 * should come from the cforge-index registry, resolves them to git URLs/tags,
 * and clones them.
 *
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param verbose Verbose output flag
 * @param skip_deps Skip dependencies flag
 * @return bool Success flag
 */
bool resolve_index_dependencies(const std::filesystem::path &project_dir,
                                 const cforge::toml_reader&project_config,
                                 bool verbose, bool skip_deps) {
  if (skip_deps) {
    cforge::logger::print_verbose("Skipping index dependency resolution (--skip-deps flag)");
    return true;
  }

  // Check if we have a [dependencies] section
  if (!project_config.has_key("dependencies")) {
    cforge::logger::print_verbose("No dependencies section found");
    return true;
  }

  // Get dependencies directory from configuration
  std::string deps_dir =
      project_config.get_string("dependencies.directory", "deps");
  std::filesystem::path deps_path = project_dir / deps_dir;

  // Create dependencies directory if it doesn't exist
  if (!std::filesystem::exists(deps_path)) {
    std::filesystem::create_directories(deps_path);
  }

  // Structure to hold index dependency info with user options
  struct index_dep_entry {
    std::string name;
    std::string version;
    std::map<std::string, std::string> options;  // User-specified setup options
  };

  // Get all dependency keys - look for entries that are index dependencies
  // An index dependency is specified as: name = "version" or name = { version = "..." }
  // without git/vcpkg/system/project flags
  auto dep_keys = project_config.get_table_keys("dependencies");
  cforge::logger::print_verbose("resolve_index_dependencies: Found " + std::to_string(dep_keys.size()) + " keys in [dependencies]");

  std::vector<index_dep_entry> index_deps;

  for (const auto &dep : dep_keys) {
    cforge::logger::print_verbose("  Checking key: " + dep);

    // Skip known non-package keys
    if (dep == "directory" || dep == "git" || dep == "vcpkg" ||
        dep == "system" || dep == "project" || dep == "subdirectory" ||
        dep == "fetch_content") {
      cforge::logger::print_verbose("    Skipping (special key)");
      continue;
    }

    std::string dep_key = "dependencies." + dep;

    // Check if this is a simple string version or a table
    std::string version = project_config.get_string(dep_key, "");
    if (!version.empty()) {
      // Simple format: dep = "version"
      cforge::logger::print_verbose("    Found index dep: " + dep + " = " + version);
      index_deps.push_back({dep, version, {}});
      continue;
    }

    // Check if it's a table with version key
    std::string version_key = dep_key + ".version";
    version = project_config.get_string(version_key, "");

    // Skip if it has explicit source indicators
    bool has_git = !project_config.get_string(dep_key + ".git", "").empty();
    bool has_vcpkg = project_config.get_bool(dep_key + ".vcpkg", false);
    bool has_system = project_config.get_bool(dep_key + ".system", false);
    bool has_project = project_config.get_bool(dep_key + ".project", false);

    if (!has_git && !has_vcpkg && !has_system && !has_project && !version.empty()) {
      // This is an index dependency
      cforge::logger::print_verbose("    Found index dep (table): " + dep + " = " + version);

      // Get user-specified options (e.g., glad = { version = "*", options = { api = "gl:core=3.3" } })
      std::map<std::string, std::string> user_options;
      std::string options_key = dep_key + ".options";
      if (project_config.has_key(options_key)) {
        user_options = project_config.get_string_map(options_key);
        cforge::logger::print_verbose("    User options: " + std::to_string(user_options.size()) + " found");
      }

      index_deps.push_back({dep, version, user_options});
    } else {
      cforge::logger::print_verbose("    Skipping (no version or has source indicator)");
    }
  }

  if (index_deps.empty()) {
    cforge::logger::print_verbose("No index dependencies found to resolve");
    return true;
  }

  cforge::logger::print_verbose("Total index deps to resolve: " + std::to_string(index_deps.size()));

  cforge::logger::print_action("Resolving", std::to_string(index_deps.size()) + " package(s) from registry");

  // Initialize registry
  cforge::registry reg;

  // Check if registry needs update
  if (reg.needs_update()) {
    cforge::logger::print_action("Updating", "package index");
    if (!reg.update()) {
      cforge::logger::print_warning("Failed to update package index, using cached version");
    }
  }

  // Check if git is available
  if (!cforge::is_command_available("git", 20)) {
    cforge::logger::print_error("Git is not available. Please install Git.");
    return false;
  }

  // Load dependency hashes
  cforge::dependency_hash dep_hashes;
  dep_hashes.load(project_dir);

  bool all_success = true;
  bool deps_changed = false;  // Track if any deps were cloned/updated

  for (const auto &dep_entry : index_deps) {
    const std::string &name = dep_entry.name;
    const std::string &version_spec = dep_entry.version;
    const auto &user_options = dep_entry.options;

    // Get package info from registry
    auto pkg_opt = reg.get_package(name);
    if (!pkg_opt) {
      cforge::logger::print_error("Package '" + name + "' not found in registry");
      all_success = false;
      continue;
    }

    const auto &pkg = *pkg_opt;

    // Resolve version
    std::string resolved_version = reg.resolve_version(name, version_spec);
    if (resolved_version.empty()) {
      cforge::logger::print_error("Could not resolve version '" + version_spec + "' for package '" + name + "'");
      all_success = false;
      continue;
    }

    // Find the version entry to get the tag
    std::string git_tag;
    for (const auto &ver : pkg.versions) {
      if (ver.version == resolved_version) {
        git_tag = ver.tag;
        break;
      }
    }

    if (git_tag.empty()) {
      cforge::logger::print_error("Could not find git tag for version " + resolved_version + " of " + name);
      all_success = false;
      continue;
    }

    std::filesystem::path dep_path = deps_path / name;

    // Check if already exists and up to date
    std::string stored_version = dep_hashes.get_version(name);
    bool version_changed = resolved_version != stored_version;

    if (std::filesystem::exists(dep_path)) {
      if (version_changed) {
        cforge::logger::print_action("Updating", name + " from " + stored_version + " to " + resolved_version);
        try {
          std::filesystem::remove_all(dep_path);
          deps_changed = true;  // Version update requires regeneration
        } catch (const std::exception &e) {
          cforge::logger::print_error("Failed to remove old version of '" + name + "': " + e.what());
          all_success = false;
          continue;
        }
      } else {
        cforge::logger::print_verbose("Package '" + name + "' already at version " + resolved_version);
        continue;
      }
    }

    // Clone the package
    cforge::logger::fetching(name + "@" + resolved_version);

    std::vector<std::string> clone_args = {"clone", "--depth=1", pkg.repository,
                                           dep_path.string(), "--branch", git_tag};
    if (!verbose) {
      clone_args.push_back("--quiet");
    }

    bool clone_result = cforge::execute_tool("git", clone_args, "",
                                     "Git Clone for " + name, verbose, 600);

    if (!clone_result) {
      cforge::logger::print_error("Failed to clone package '" + name + "' from " + pkg.repository);
      all_success = false;
      continue;
    }

    cforge::logger::print_action("Downloaded", name + "@" + resolved_version);

    // Run setup commands if the package has any
    if (pkg.setup.has_setup()) {
      // Merge default options with user-specified options
      std::map<std::string, std::string> merged_options = pkg.setup.defaults;
      for (const auto &[key, value] : user_options) {
        merged_options[key] = value;
      }

      if (!run_package_setup(pkg, dep_path, resolved_version, merged_options, verbose)) {
        cforge::logger::print_error("Setup failed for package '" + name + "'");
        all_success = false;
        // Don't continue - the package is cloned but setup failed
        // Store hash anyway so we can retry setup on next build
      }
    }

    // Store hash and version
    std::string current_hash = cforge::dependency_hash::calculate_directory_hash(dep_path);
    dep_hashes.set_hash(name, current_hash);
    dep_hashes.set_version(name, resolved_version);
    deps_changed = true;  // Mark that deps changed
  }

  // If any deps were cloned/updated, clear the cforge.toml hash to force CMakeLists.txt regeneration
  if (deps_changed) {
    cforge::logger::print_verbose("Dependencies changed - will force CMakeLists.txt regeneration");
    dep_hashes.set_hash("cforge.toml", "");  // Clear the hash to force regeneration
  }

  // Save updated dependency hashes
  dep_hashes.save(project_dir);

  if (all_success) {
    cforge::logger::print_action("Finished", "all packages resolved");
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
  cforge_int_t timeout = 180; // 3 minutes for Windows
#else
  cforge_int_t timeout = 120; // 2 minutes for other platforms
#endif

  cforge::logger::configuring("CMake");

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
    cforge::logger::print_verbose("Command: " + cmd);
  }
  // Check if the -DCMAKE_BUILD_TYPE argument is present
  bool has_build_type = false;
  for (const auto &arg : cmake_args) {
    if (arg.find("-DCMAKE_BUILD_TYPE=") != std::string::npos) {
      has_build_type = true;
      cforge::logger::print_verbose("Using build type: " + arg);
      break;
    }
  }

  // Ensure build type is being passed - just in case
  if (!has_build_type) {
    cforge::logger::print_warning(
        "No build type specified in CMake arguments - this should not happen");
  }

  // Log the full command in verbose mode
  if (verbose) {
    std::string cmd = "cmake";
    for (const auto &arg : cmake_args) {
      cmd += " " + arg;
    }
    cforge::logger::print_verbose("Full CMake command: " + cmd);
  }

  // Execute CMake and capture output
  cforge::process_result pr = cforge::execute_process("cmake", cmake_args, project_dir, nullptr,
                                      nullptr, timeout);
  bool result = pr.success;

  if (result) {
    cforge::logger::print_action("Finished", "CMake configuration");
  }

  // Verify that the configuration was successful by checking for CMakeCache.txt
  std::filesystem::path build_path(build_dir);
  bool cmake_success =
      result && std::filesystem::exists(build_path / "CMakeCache.txt");

  if (!cmake_success) {
    // Try formatting errors first from stderr, then stdout
    std::string formatted_errors = cforge::format_build_errors(pr.stderr_output);
    if (formatted_errors.empty()) {
      formatted_errors = cforge::format_build_errors(pr.stdout_output);
    }
    if (!formatted_errors.empty()) {
      // Print formatted errors directly (they already contain "error" prefix from Rust-style formatting)
      fmt::print("{}", formatted_errors);
    } else {
      // Fallback: print raw outputs
      if (!pr.stderr_output.empty()) {
        cforge::logger::print_error("Raw stderr output:");
        std::istringstream ess(pr.stderr_output);
        std::string line;
        while (std::getline(ess, line)) {
          if (!line.empty())
            cforge::logger::print_error(line);
        }
      }
      if (!pr.stdout_output.empty()) {
        cforge::logger::print_error("Raw stdout output:");
        std::istringstream oss(pr.stdout_output);
        std::string line;
        while (std::getline(oss, line)) {
          if (!line.empty())
            cforge::logger::print_error(line);
        }
      }
    }
    return false;
  }

  return true;
}

/**
 * @brief Check for circular include dependencies and warn/fail if found
 *
 * @param project_dir Project directory
 * @param project_config Project configuration
 * @param verbose Verbose output
 * @return bool True if build should continue, false if it should fail
 */
static bool check_circular_dependencies(const std::filesystem::path &project_dir,
                                         const cforge::toml_reader&project_config,
                                         bool verbose) {
  // Check if circular dependency check is enabled (default: true)
  bool warn_circular = project_config.get_bool("build.warn_circular", true);
  bool fail_on_circular = project_config.get_bool("build.fail_on_circular", false);

  if (!warn_circular && !fail_on_circular) {
    return true; // Checks disabled
  }

  if (verbose) {
    cforge::logger::print_verbose("Checking for circular include dependencies...");
  }

  cforge::include_analyzer analyzer(project_dir);
  cforge::include_analysis_result result = analyzer.analyze(false); // Don't include deps

  if (!result.has_cycles) {
    if (verbose) {
      cforge::logger::print_verbose("No circular dependencies found");
    }
    return true;
  }

  // Circular dependencies found - emit warnings
  for (const auto &chain : result.chains) {
    // Build the chain string
    std::string chain_str;
    for (cforge_size_t i = 0; i < chain.files.size(); ++i) {
      chain_str += chain.files[i];
      if (i < chain.files.size() - 1) {
        chain_str += " -> ";
      }
    }

    // Format warning in Rust style
    fmt::print(fg(fmt::color::yellow) | fmt::emphasis::bold, "warning");
    fmt::print(": circular include detected\n");
    fmt::print(fg(fmt::color::blue) | fmt::emphasis::bold, "  --> ");
    fmt::print("{}\n", chain.root);
    fmt::print(fg(fmt::color::blue) | fmt::emphasis::bold, "   |\n");
    fmt::print(fg(fmt::color::blue) | fmt::emphasis::bold, "   = ");
    fmt::print("{}\n", chain_str);
    fmt::print(fg(fmt::color::blue) | fmt::emphasis::bold, "   = ");
    fmt::print(fg(fmt::color::green), "help");
    fmt::print(": consider forward declarations or restructuring\n\n");
  }

  // Summary
  fmt::print(fg(fmt::color::yellow) | fmt::emphasis::bold, "warning");
  fmt::print(": {} circular dependency chain{} detected\n",
             result.chains.size(),
             result.chains.size() == 1 ? "" : "s");

  if (fail_on_circular) {
    cforge::logger::print_error("Build failed due to circular dependencies (build.fail_on_circular = true)");
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
                          const std::string &build_config, cforge_int_t num_jobs,
                          bool verbose, const std::string &target = "",
                          std::set<std::string> *built_projects = nullptr,
                          bool skip_deps = false,
                          const std::string &cross_profile = "") {
  // Start project build timer
  auto project_build_start = std::chrono::steady_clock::now();

  // Load project configuration first to get the correct project name
  toml::table config_table;
  std::filesystem::path config_path = project_dir / "cforge.toml";

  bool has_project_config = false;
  if (std::filesystem::exists(config_path)) {
    try {
      config_table = toml::parse_file(config_path.string());
      has_project_config = true;
    } catch (const toml::parse_error &e) {
      cforge::logger::print_error("Failed to parse cforge.toml: " +
                          std::string(e.what()));
      // Continue with default values
    }
  }

  // Create a toml_reader wrapper for consistent API
  cforge::toml_reader project_config(config_table);

  // Get project name from cforge.toml, fallback to directory name
  std::string project_name = project_config.get_string(
      "project.name", project_dir.filename().string());

  // If we're tracking built projects, check if this one is already done
  if (built_projects &&
      built_projects->find(project_name) != built_projects->end()) {
    cforge::logger::print_verbose("Project '" + project_name +
                          "' already built, skipping");
    return true;
  }

  cforge::logger::building(project_name + " [" + build_config + "]");

  // Pre-build check for circular include dependencies
  if (has_project_config && !check_circular_dependencies(project_dir, project_config, verbose)) {
    return false;
  }

  // Check if we're in a workspace and workspaceCMakeLists exists
  auto [is_workspace, workspace_dir] = cforge::is_in_workspace(project_dir);
  bool use_workspace_build = false;
  if (is_workspace&& project_dir == workspace_dir &&
      std::filesystem::exists(workspace_dir / "CMakeLists.txt")) {
    cforge::logger::print_verbose("Using workspace-level CMakeLists.txt for build");
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
      cforge::get_build_dir_for_config(build_base_dir.string(), build_config);
  cforge::logger::print_verbose("Using build directory: " + build_dir.string());

  // Make sure the build directory exists
  if (!std::filesystem::exists(build_dir)) {
    cforge::logger::print_verbose("Creating build directory: " + build_dir.string());
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::filesystem::filesystem_error &e) {
      cforge::logger::print_error("Failed to create build directory: " +
                          std::string(e.what()));
      return false;
    }
  }

  // Handle project-level dependencies and CMakeLists generation (skip in
  // workspace build)
  if (!use_workspace_build && has_project_config) {
    // Resolve index/registrydependencies first (they get cloned to deps/)
    // Skip if using FetchContent mode (CMake will handle downloading)
    bool use_fetch_content = project_config.get_bool("dependencies.fetch_content", true);
    if (!use_fetch_content) {
      try {
        std::filesystem::current_path(project_dir);
        if (!resolve_index_dependencies(project_dir, project_config, verbose, skip_deps)) {
          cforge::logger::print_warning("Some index dependencies could not be resolved");
        }
      } catch (const std::exception &ex) {
        cforge::logger::print_warning("Exception while resolving index dependencies: " +
                              std::string(ex.what()));
      }
    } else {
      cforge::logger::print_verbose("Using FetchContent mode - CMake will download index dependencies");
    }

    // Clone Git dependencies before generating CMakeLists.txt
    if (project_config.has_key("dependencies.git")) {
      cforge::logger::print_action("Setting up", "Git dependencies");
      try {
        // Make sure we're in the project directory for relative paths to work
        std::filesystem::current_path(project_dir);

        if (!clone_git_dependencies(project_dir, project_config, verbose,
                                    skip_deps)) {
          cforge::logger::print_error("Failed to clone Git dependencies");
          return false;
        }

        cforge::logger::print_action("Finished",
                             "Git dependencies successfully set up");
      } catch (const std::exception &ex) {
        cforge::logger::print_error("Exception while setting up Git dependencies: " +
                            std::string(ex.what()));
        return false;
      }
    }

    // Generate/update lock file after dependencies are resolved
    {
      bool use_fetch_content = project_config.get_bool("dependencies.fetch_content", true);
      std::string deps_dir_str =
          project_config.get_string("dependencies.directory", "deps");
      std::filesystem::path deps_path = project_dir / deps_dir_str;

      if (use_fetch_content) {
        // FetchContent mode: generate lock file from cforge.toml + registry
        cforge::generate_lockfile_from_config(project_dir, project_config, verbose);
      } else if (std::filesystem::exists(deps_path)) {
        // Clone mode: scan deps directory
        cforge::update_lockfile(project_dir, deps_path, verbose);
      }
    }

    // Generate CMakeLists.txt in the build directory
    std::filesystem::path timestamp_file =
        build_dir / ".cforge_cmakefile_timestamp";

    // Generate new CMakeLists.txt in the build directory
    if (!::cforge::generate_cmakelists_from_toml(project_dir, project_config,
                                         verbose)) {
      cforge::logger::print_error(
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

  // ccache/sccache integration
  // Configuration: build.compiler_cache = "auto" (default), "ccache", "sccache", or "none"
  if (has_project_config) {
    std::string cache_mode = project_config.get_string("build.compiler_cache", "auto");
    std::string cache_program;

    if (cache_mode == "none") {
      cforge::logger::print_verbose("Compiler cache disabled by configuration");
    } else if (cache_mode == "ccache") {
      if (cforge::is_command_available("ccache", 5)) {
        cache_program = "ccache";
      } else {
        cforge::logger::print_warning("ccache requested but not found in PATH");
      }
    } else if (cache_mode == "sccache") {
      if (cforge::is_command_available("sccache", 5)) {
        cache_program = "sccache";
      } else {
        cforge::logger::print_warning("sccache requested but not found in PATH");
      }
    } else if (cache_mode == "auto") {
      // Auto-detect: prefer ccache, fallback to sccache
      if (cforge::is_command_available("ccache", 5)) {
        cache_program = "ccache";
      } else if (cforge::is_command_available("sccache", 5)) {
        cache_program = "sccache";
      }
    }

    if (!cache_program.empty()) {
      cmake_args.push_back("-DCMAKE_C_COMPILER_LAUNCHER=" + cache_program);
      cmake_args.push_back("-DCMAKE_CXX_COMPILER_LAUNCHER=" + cache_program);
      cforge::logger::print_action("Using", cache_program + " for compilation caching");
    }
  }

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
        "build.config." + cforge::string_to_lower(build_config) + ".defines";
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
        "build.config." + cforge::string_to_lower(build_config) + ".cmake_args";
    if (project_config.has_key(config_key)) {
      auto custom_args = project_config.get_string_array(config_key);
      for (const auto &arg : custom_args) {
        cmake_args.push_back(arg);
      }
    }
  }

  // Cross-compilation settings
  // Supports both [cross] section and [cross.profile.<name>] profiles
  bool cross_enabled = false;
  std::string cross_system;
  std::string cross_processor;
  std::string cross_toolchain;
  std::string cross_c_compiler;
  std::string cross_cxx_compiler;
  std::string cross_sysroot;
  std::string cross_find_root;
  std::map<std::string, std::string> cross_variables;

  if (has_project_config) {
    // Check if a profile is specified via command line
    if (!cross_profile.empty()) {
      std::string profile_key = "cross.profile." + cross_profile;
      if (project_config.has_key(profile_key + ".system") ||
          project_config.has_key(profile_key + ".toolchain")) {
        cross_enabled = true;
        cforge::logger::print_action("Cross-compiling", "using profile '" + cross_profile + "'");

        // Read profile settings
        cross_system = project_config.get_string(profile_key + ".system", "");
        cross_processor = project_config.get_string(profile_key + ".processor", "");
        cross_toolchain = project_config.get_string(profile_key + ".toolchain", "");
        cross_sysroot = project_config.get_string(profile_key + ".sysroot", "");

        // Compilers can be specified as inline table or separate keys
        cross_c_compiler = project_config.get_string(profile_key + ".compilers.c", "");
        cross_cxx_compiler = project_config.get_string(profile_key + ".compilers.cxx", "");
        if (cross_c_compiler.empty()) {
          cross_c_compiler = project_config.get_string(profile_key + ".c", "");
        }
        if (cross_cxx_compiler.empty()) {
          cross_cxx_compiler = project_config.get_string(profile_key + ".cxx", "");
        }

        // Read variables as inline table
        cross_variables = project_config.get_string_map(profile_key + ".variables");
      } else {
        cforge::logger::print_error("Cross-compilation profile '" + cross_profile + "' not found");
        return false;
      }
    }
    // Check default [cross] section if no profile specified
    else if (project_config.get_bool("cross.enabled", false)) {
      cross_enabled = true;
      cforge::logger::print_action("Cross-compiling", "using default cross configuration");

      // Read [cross.target] settings
      cross_system = project_config.get_string("cross.target.system", "");
      cross_processor = project_config.get_string("cross.target.processor", "");
      cross_toolchain = project_config.get_string("cross.target.toolchain", "");

      // Read [cross.compilers] settings
      cross_c_compiler = project_config.get_string("cross.compilers.c", "");
      cross_cxx_compiler = project_config.get_string("cross.compilers.cxx", "");

      // Read [cross.paths] settings
      cross_sysroot = project_config.get_string("cross.paths.sysroot", "");
      cross_find_root = project_config.get_string("cross.paths.find_root", "");

      // Read [cross.variables] as inline table
      cross_variables = project_config.get_string_map("cross.variables");
    }

    // Apply cross-compilation settings to CMake args
    if (cross_enabled) {
      if (!cross_toolchain.empty()) {
        // Expand environment variables in toolchain path
        std::string expanded_toolchain = cross_toolchain;
        cforge_size_t pos = 0;
        while ((pos = expanded_toolchain.find("${", pos)) != std::string::npos) {
          cforge_size_t end = expanded_toolchain.find("}", pos);
          if (end != std::string::npos) {
            std::string var_name = expanded_toolchain.substr(pos + 2, end - pos - 2);
            const char* var_value = std::getenv(var_name.c_str());
            if (var_value) {
              expanded_toolchain.replace(pos, end - pos + 1, var_value);
            } else {
              pos = end + 1;
            }
          } else {
            break;
          }
        }
        cmake_args.push_back("-DCMAKE_TOOLCHAIN_FILE=" + expanded_toolchain);
        cforge::logger::print_verbose("Using toolchain file: " + expanded_toolchain);
      }
      if (!cross_system.empty()) {
        cmake_args.push_back("-DCMAKE_SYSTEM_NAME=" + cross_system);
        cforge::logger::print_verbose("Target system: " + cross_system);
      }
      if (!cross_processor.empty()) {
        cmake_args.push_back("-DCMAKE_SYSTEM_PROCESSOR=" + cross_processor);
        cforge::logger::print_verbose("Target processor: " + cross_processor);
      }
      if (!cross_c_compiler.empty()) {
        cmake_args.push_back("-DCMAKE_C_COMPILER=" + cross_c_compiler);
        cforge::logger::print_verbose("C compiler: " + cross_c_compiler);
      }
      if (!cross_cxx_compiler.empty()) {
        cmake_args.push_back("-DCMAKE_CXX_COMPILER=" + cross_cxx_compiler);
        cforge::logger::print_verbose("C++ compiler: " + cross_cxx_compiler);
      }
      if (!cross_sysroot.empty()) {
        cmake_args.push_back("-DCMAKE_SYSROOT=" + cross_sysroot);
        cforge::logger::print_verbose("Sysroot: " + cross_sysroot);
      }
      if (!cross_find_root.empty()) {
        cmake_args.push_back("-DCMAKE_FIND_ROOT_PATH=" + cross_find_root);
        cforge::logger::print_verbose("Find root path: " + cross_find_root);
      }
      // Apply custom variables
      for (const auto& [var_name, var_value] : cross_variables) {
        cmake_args.push_back("-D" + var_name + "=" + var_value);
        cforge::logger::print_verbose("Variable: " + var_name + "=" + var_value);
      }
    }
  }

  // Custom compiler specification: separate C and C++ compilers
  if (has_project_config && project_config.has_key("cmake.c_compiler")) {
    std::string cc = project_config.get_string("cmake.c_compiler", "");
    if (!cc.empty()) {
      cmake_args.push_back("-DCMAKE_C_COMPILER=" + cc);
      cforge::logger::print_verbose("Using C compiler: " + cc);
    }
  }
  if (has_project_config && project_config.has_key("cmake.cxx_compiler")) {
    std::string cxx = project_config.get_string("cmake.cxx_compiler", "");
    if (!cxx.empty()) {
      cmake_args.push_back("-DCMAKE_CXX_COMPILER=" + cxx);
      cforge::logger::print_verbose("Using C++ compiler: " + cxx);
    }
  }

  // Project-level C and C++ standard overrides
  if (has_project_config) {
    std::string cstd = project_config.get_string("project.c_standard", "");
    if (!cstd.empty()) {
      cmake_args.push_back("-DCMAKE_C_STANDARD=" + cstd);
      cforge::logger::print_verbose("Using C standard: " + cstd);
    }
    std::string cppstd = project_config.get_string("project.cpp_standard", "");
    if (!cppstd.empty()) {
      cmake_args.push_back("-DCMAKE_CXX_STANDARD=" + cppstd);
      cforge::logger::print_verbose("Using C++ standard: " + cppstd);
    }
  }

  // Determine CMake generator: use override in cforge.toml if present,
  // otherwise pick default
  std::string generator;
  if (has_project_config && project_config.has_key("cmake.generator")) {
    generator = project_config.get_string("cmake.generator", "");
    if (!generator.empty()) {
      cforge::logger::print_verbose("Using CMake generator from config: " + generator);
    } else {
      generator = cforge::get_cmake_generator();
      cforge::logger::print_verbose("No CMake generator in config, using default: " +
                            generator);
    }
  } else {
    generator = cforge::get_cmake_generator();
    cforge::logger::print_verbose("Using default CMake generator: " + generator);
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
      cforge::logger::print_verbose("Using vcpkg toolchain: " + toolchain_path);
    } else {
      cforge::logger::print_warning("vcpkg toolchain file not found: " +
                            toolchain_path);
    }
    // Add triplet if specified
    if (project_config.has_key("dependencies.vcpkg.triplet")) {
      std::string triplet =
          project_config.get_string("dependencies.vcpkg.triplet", "");
      if (!triplet.empty()) {
        cmake_args.push_back("-DVCPKG_TARGET_TRIPLET=" + triplet);
        cforge::logger::print_verbose("Using vcpkg triplet: " + triplet);
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
      cforge::logger::print_verbose("Using C/C++ compiler for Ninja: " + toolset);
    }
  }

  // Validate generator: if invalid, fallback and warn
  if (!cforge::is_generator_valid(generator)) {
    cforge::logger::print_warning("CMake does not support generator: " + generator +
                          ", falling back to default generator");
    generator = cforge::get_cmake_generator();
    cforge::logger::print_verbose("Using fallback CMake generator: " + generator);
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
    cforge::logger::print_verbose("Using CMake platform: " + platform);
    // Optional toolset
    if (has_project_config && project_config.has_key("cmake.toolset")) {
      std::string toolset = project_config.get_string("cmake.toolset", "");
      if (!toolset.empty()) {
        cmake_args.push_back("-T");
        cmake_args.push_back(toolset);
        cforge::logger::print_verbose("Using CMake toolset: " + toolset);
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
    cforge::logger::print_verbose("Changed working directory to: " +
                          build_dir.string());
  } catch (const std::filesystem::filesystem_error &e) {
    cforge::logger::print_error("Failed to change directory: " + std::string(e.what()));
    return false;
  }

  // Run CMake configuration
  cforge::logger::configuring("project with CMake");
  bool configure_result = run_cmake_configure(cmake_args, build_dir.string(),
                                              project_dir.string(), verbose);

  if (!configure_result) {
    cforge::logger::print_error("CMake configuration failed for project: " +
                        project_name);
    std::filesystem::current_path(original_dir);
    return false;
  }

  // Run CMake build
  cforge::logger::compiling(project_name);

  // Set up build arguments
  std::vector<std::string> build_args = {"--build", "."};

  // Add config - always specify it for both single-config and multi-config
  // generators
  build_args.push_back("--config");
  build_args.push_back(build_config);
  cforge::logger::print_verbose("Using build configuration: " + build_config);

  // Add parallel build flag with appropriate jobs
  if (num_jobs > 0) {
    build_args.push_back("--parallel");
    build_args.push_back(std::to_string(num_jobs));
    cforge::logger::print_verbose("Using parallel build with " +
                          std::to_string(num_jobs) + " jobs");
  } else {
    // Default to number of logical cores
    build_args.push_back("--parallel");
    cforge::logger::print_verbose("Using parallel build with default number of jobs");
  }

  // Add target if specified
  if (!target.empty()) {
    build_args.push_back("--target");
    build_args.push_back(target);
    cforge::logger::print_verbose("Building target: " + target);
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
    cforge::logger::print_verbose(std::string("Overriding MSBuild OutDir to: ") +
                          outdir_str);
  }

  // Run the build with longer timeout for CI environments
  // Release builds especially on Windows CI can take several minutes
  cforge_int_t build_timeout = 600; // 10 minutes
  bool build_result =
      cforge::execute_tool("cmake", build_args, "", "CMake Build", verbose, build_timeout);

  // Clean up empty config directories under the build root
  for (const auto &cfg : {"Debug", "Release", "RelWithDebInfo"}) {
    std::filesystem::path cfg_dir = build_dir / cfg;
    if (std::filesystem::exists(cfg_dir) &&
        std::filesystem::is_directory(cfg_dir) &&
        std::filesystem::is_empty(cfg_dir)) {
      std::filesystem::remove(cfg_dir);
      cforge::logger::print_verbose("Removed empty config directory: " +
                            cfg_dir.string());
    }
  }

  // Restore original directory
  try {
    std::filesystem::current_path(original_dir);
    cforge::logger::print_verbose("Restored working directory to: " +
                          original_dir.string());
  } catch (const std::filesystem::filesystem_error &e) {
    cforge::logger::print_warning("Failed to restore directory: " +
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
    cforge::logger::finished(build_config, duration_str);

    // If we're tracking built projects, add this one
    if (built_projects) {
      built_projects->insert(project_name);
    }

    return true;
  } else {
    cforge::logger::print_error("Failed to build project: " + project_name + " [" +
                        build_config + "]");

    // Check for common build errors and provide more helpful messages
    std::filesystem::path cmake_error_log =
        build_dir / "CMakeFiles" / "CMakeError.log";
    if (std::filesystem::exists(cmake_error_log)) {
      cforge::logger::print_verbose(
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
            cforge::logger::print_error("CMake Error Log:\n" + error_content);
            cforge::logger::print_verbose("Full error log available at: " +
                                  cmake_error_log.string());
          }
        }
      } catch (const std::exception &ex) {
        cforge::logger::print_warning("Could not read CMake error log: " +
                              std::string(ex.what()));
      }
    }

    cforge::logger::print_verbose("For more detailed build information, try running "
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
                                    const cforge::workspace_project&project,
                                    const std::string &build_config,
                                    cforge_int_t num_jobs, bool verbose,
                                    const std::string &target,
                                    bool skip_deps = false,
                                    const std::string &cross_profile = "") {
  // Change to project directory
  std::filesystem::current_path(project.path);

  // Load project configuration
  toml::table config_table;
  std::filesystem::path config_path = project.path / CFORGE_FILE;

  try {
    config_table = toml::parse_file(config_path.string());
  } catch (const toml::parse_error &e) {
    cforge::logger::print_error("Failed to load project configuration for '" +
                        project.name + "': " + std::string(e.what()));
    return false;
  }

  // Create a toml_reader wrapper
  cforge::toml_reader config_data(config_table);

  // Determine build directory
  std::string base_build_dir =
      config_data.get_string("build.build_dir", "build");

  // Get the config-specific build directory
  std::filesystem::path build_dir =
      cforge::get_build_dir_for_config(base_build_dir, build_config);

  // Build the project
  bool success = build_project(project.path, build_config, num_jobs, verbose,
                               target, nullptr, skip_deps, cross_profile);

  if (!success) {
    cforge::logger::print_error("Failed to build project '" + project.name + "'");
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
  auto [is_workspace, workspace_dir] = cforge::is_in_workspace(current_dir);

  // Parse command line arguments
  std::string config_name;
  cforge_int_t num_jobs = 0;
  bool verbose = cforge::logger::get_verbosity() == cforge::log_verbosity::VERBOSITY_VERBOSE;
  std::string target;
  std::string project_name;
  std::string cross_profile;  // Cross-compilation profile name
  [[maybe_unused]] bool generate_workspace_cmake = false;
  [[maybe_unused]] bool force_regenerate = false;
  bool skip_deps = false;

  // Extract command line arguments
  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];

    if (arg == "--skip-deps" || arg == "--no-deps") {
      skip_deps = true;
    } else if (arg == "--no-warnings") {
      cforge::g_suppress_warnings = true;
      cforge::logger::print_verbose("Suppressing build warnings (--no-warnings flag)");
    } else if (arg == "-c" || arg == "--config") {
      if (i + 1 < ctx->args.arg_count) {
        config_name = ctx->args.args[i + 1];
        cforge::logger::print_verbose("Using build configuration from command line: " +
                              config_name);
        i++; // Skip the next argument
      }
    } else if (arg.substr(0, 9) == "--config=") {
      config_name = arg.substr(9);
      cforge::logger::print_verbose("Using build configuration from command line: " +
                            config_name);
    } else if (arg == "-j" || arg == "--jobs") {
      if (i + 1 < ctx->args.arg_count) {
        try {
          num_jobs = std::stoi(ctx->args.args[i + 1]);
        } catch (...) {
          cforge::logger::print_warning("Invalid jobs value, using default");
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
    } else if (arg == "--profile" || arg == "-P") {
      if (i + 1 < ctx->args.arg_count) {
        cross_profile = ctx->args.args[i + 1];
        cforge::logger::print_verbose("Using cross-compilation profile: " + cross_profile);
        i++; // Skip the next argument
      }
    } else if (arg.substr(0, 10) == "--profile=") {
      cross_profile = arg.substr(10);
      cforge::logger::print_verbose("Using cross-compilation profile: " + cross_profile);
    }
  }

  // If skip_deps is set, add it to the project config
  if (skip_deps) {
    cforge::logger::print_verbose("Skipping Git dependency updates (--skip-deps flag)");
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
    cforge::logger::print_verbose("Using build configuration from context: " +
                          config_name);
  }

  // If still no specific configuration is provided, use the default
  if (config_name.empty()) {
    config_name = "Debug";
    cforge::logger::print_verbose("No configuration specified, using default: " +
                          config_name);
  } else {
    // Convert to lowercase for case-insensitive comparison
    std::string config_lower = cforge::string_to_lower(config_name);

    // Capitalize first letter for standard configs
    if (config_lower == "debug" || config_lower == "release" ||
        config_lower == "relwithdebinfo" || config_lower == "minsizerel") {
      config_name = config_lower;
      config_name[0] = std::toupper(config_name[0]);
    }
  }

  cforge::logger::print_verbose("Using build configuration: " + config_name);

  // Pre-build script support using shared script_runner
  if (!cforge::run_pre_build_scripts(is_workspace? workspace_dir : current_dir,
                             is_workspace, verbose)) {
    return 1;
  }

  cforge_int_t result = 0;

  if (is_workspace) {
    cforge::logger::print_verbose("Building in workspace context: " +
                          workspace_dir.string());
    // Save current directory and switch to workspace root
    auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(workspace_dir);

    // Load workspace configuration
    cforge::toml_reader ws_cfg;
    ws_cfg.load((workspace_dir / WORKSPACE_FILE).string());

    cforge::workspace ws;
    if (!ws.load(workspace_dir)) {
      cforge::logger::print_error("Failed to load workspace");
      std::filesystem::current_path(original_cwd);
      return 1;
    }

    // STEP 1: Resolve all dependencies FIRST (before CMakeLists generation)
    // This ensures dependencies are available when CMakeLists.txt references them
    if (!skip_deps) {
      cforge::logger::print_action("Resolving", "workspace dependencies");
      for (const auto &proj : ws.get_projects()) {
        auto proj_toml = proj.path / CFORGE_FILE;
        if (std::filesystem::exists(proj_toml)) {
          cforge::toml_reader pcfg(toml::parse_file(proj_toml.string()));

          // Resolve index/registrydependencies first (skip if using FetchContent)
          bool proj_use_fetch_content = pcfg.get_bool("dependencies.fetch_content", true);
          if (!proj_use_fetch_content) {
            try {
              std::filesystem::current_path(proj.path);
              if (!resolve_index_dependencies(proj.path, pcfg, verbose, skip_deps)) {
                cforge::logger::print_warning("Some index dependencies could not be resolved for project: " + proj.name);
              }
            } catch (const std::exception &ex) {
              cforge::logger::print_warning("Exception while resolving index dependencies for project " +
                                    proj.name + ": " + std::string(ex.what()));
            }
          }

          // Then handle Git dependencies
          if (pcfg.has_key("dependencies.git")) {
            cforge::logger::print_action("Setting up",
                                 "Git dependencies for project: " + proj.name);
            try {
              std::filesystem::current_path(proj.path);
              if (!clone_git_dependencies(proj.path, pcfg, verbose,
                                          skip_deps)) {
                cforge::logger::print_error(
                    "Failed to clone Git dependencies for project: " +
                    proj.name);
                std::filesystem::current_path(original_cwd);
                return 1;
              }
            } catch (const std::exception &ex) {
              cforge::logger::print_error(
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

    // STEP 2: Generate cforge::workspaceand project CMakeLists.txt AFTER dependencies are resolved
    if (!cforge::generate_workspace_cmakelists(workspace_dir, ws_cfg, verbose)) {
      cforge::logger::print_error("Failed to generate cforge::workspaceCMakeLists.txt");
      std::filesystem::current_path(original_cwd);
      return 1;
    }
    for (const auto &proj : ws.get_projects()) {
      auto proj_toml = proj.path / CFORGE_FILE;
      if (std::filesystem::exists(proj_toml)) {
        cforge::toml_reader pcfg(toml::parse_file(proj_toml.string()));
        if (!cforge::generate_cmakelists_from_toml(proj.path, pcfg, verbose)) {
          cforge::logger::print_error(
              "Failed to generate CMakeLists.txt for project: " + proj.name);
          std::filesystem::current_path(original_cwd);
          return 1;
        }
      }
    }

    // STEP 3: Determine workspace build directory and configure CMake
    std::filesystem::path build_dir = workspace_dir / DEFAULT_BUILD_DIR;
    // Ensure build directory exists
    if (!std::filesystem::exists(build_dir)) {
      try {
        std::filesystem::create_directories(build_dir);
      } catch (...) {
      }
    }
    // Configure cforge::workspaceCMake
    std::vector<std::string> cmake_args = {"-S", workspace_dir.string(), "-B",
                                           build_dir.string(),
                                           "-DCMAKE_BUILD_TYPE=" + config_name};
    if (verbose)
      cmake_args.push_back("--debug-output");
    if (!run_cmake_configure(cmake_args, build_dir.string(),
                             workspace_dir.string(), verbose)) {
      cforge::logger::print_error("Workspace CMake configuration failed");
      // Restore original directory before exit
      std::filesystem::current_path(original_cwd);
      return 1;
    }

    // STEP 4: Build single target or entire workspace
    std::vector<std::string> build_args = {"--build", build_dir.string(),
                                           "--config", config_name};
    if (num_jobs > 0) {
      build_args.push_back("--parallel");
      build_args.push_back(std::to_string(num_jobs));
    }
    if (verbose)
      build_args.push_back("--verbose");
    if (!project_name.empty()) {
      // Build only the specified cforge::workspacetarget
      build_args.push_back("--target");
      build_args.push_back(project_name);
      cforge::logger::building(project_name + " in workspace");
    } else {
      cforge::logger::building("entire workspace");
    }

    // Use longer timeout for workspace builds in CI environments
    cforge_int_t build_timeout = 600; // 10 minutes
    bool result = cforge::execute_tool("cmake", build_args, "", "CMake Build", verbose, build_timeout);
    // Restore original directory
    std::filesystem::current_path(original_cwd);
    if (!result) {
      cforge::logger::print_error("Build failed");
      return 1;
    }
    // Calculate workspace build duration
    auto build_end_time = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           build_end_time - build_start_time)
                           .count();
    std::string duration_str = fmt::format("{:.2f}s", duration_ms / 1000.0);
    cforge::logger::finished(config_name, duration_str);
    // Clean up empty config directories under workspace build root
    {
      std::filesystem::path build_root = workspace_dir / DEFAULT_BUILD_DIR;
      for (const auto &cfg : {"Debug", "Release", "RelWithDebInfo"}) {
        std::filesystem::path cfg_dir = build_root / cfg;
        if (std::filesystem::exists(cfg_dir) &&
            std::filesystem::is_directory(cfg_dir) &&
            std::filesystem::is_empty(cfg_dir)) {
          std::filesystem::remove(cfg_dir);
          cforge::logger::print_verbose("Removed empty cforge::workspaceconfig directory: " +
                                cfg_dir.string());
        }
      }
    }

    // Post-build script support (workspace)
    if (!cforge::run_post_build_scripts(workspace_dir, true, verbose)) {
      return 1;
    }

    return 0;
  } else {
    // Single project build outside workspace
    // Always check if CMakeLists.txt needs regeneration (hash comparison inside)
    std::filesystem::path toml_file = current_dir / CFORGE_FILE;
    if (std::filesystem::exists(toml_file)) {
      cforge::logger::print_verbose("Checking if CMakeLists.txt needs regeneration");
      cforge::toml_reader proj_cfg(toml::parse_file(toml_file.string()));
      if (!cforge::generate_cmakelists_from_toml(current_dir, proj_cfg, verbose)) {
        cforge::logger::print_error(
            "Failed to generate CMakeLists.txt for project build");
        return 1;
      }
    }
    // Build the standalone project
    if (!build_project(current_dir, config_name, num_jobs, verbose, target,
                       nullptr, skip_deps, cross_profile)) {
      return 1;
    }

    // Post-build script support (single project)
    if (!cforge::run_post_build_scripts(current_dir, false, verbose)) {
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
    const std::filesystem::path & /*project_dir*/, const cforge::toml_reader&project_config,
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
