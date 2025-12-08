/**
 * @file command_update.cpp
 * @brief Implementation of the 'update' command
 */
#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/installer.hpp"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace cforge;

/**
 * @brief Get dependencies from cforge.toml file
 *
 * @param config_file Path to the cforge.toml file
 * @return std::map<std::string, std::string> Map of dependencies (name ->
 * version)
 */
[[maybe_unused]] static std::map<std::string, std::string>
get_dependencies_from_config(const std::filesystem::path &config_file) {
  std::map<std::string, std::string> dependencies;

  toml_reader config;
  if (!config.load(config_file.string())) {
    logger::print_warning("Failed to parse configuration file: " +
                          config_file.string());
    return dependencies;
  }

  // Get all dependency sections in the TOML file
  std::vector<std::string> dependency_keys =
      config.get_table_keys("dependencies");

  for (const auto &key : dependency_keys) {
    std::string version = config.get_string("dependencies." + key, "*");
    dependencies[key] = version;
  }

  return dependencies;
}

/**
 * @brief Update vcpkg itself
 *
 * @param project_dir Project directory
 * @param verbose Verbose output
 * @return true if successful, false otherwise
 */
static bool update_vcpkg(const std::filesystem::path &project_dir,
                         bool verbose) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";

  if (!std::filesystem::exists(vcpkg_dir)) {
    logger::print_error("vcpkg not found at: " + vcpkg_dir.string());
    logger::print_action("Run", "'cforge vcpkg' to set up vcpkg integration");
    return false;
  }

  // Update vcpkg repository
  std::string git_cmd = "git";
  std::vector<std::string> git_args = {"pull", "--rebase"};

  logger::updating("vcpkg");

  auto result = execute_process(
      git_cmd, git_args, vcpkg_dir.string(),
      [verbose](const std::string &line) {
        if (verbose) {
          logger::print_verbose(line);
        }
      },
      [](const std::string &line) { logger::print_error(line); });

  if (!result.success) {
    logger::print_error("Failed to update vcpkg. Exit code: " +
                        std::to_string(result.exit_code));
    return false;
  }

  // Run bootstrap again to ensure it's up to date
  std::string bootstrap_cmd;
  std::vector<std::string> bootstrap_args;

#ifdef _WIN32
  bootstrap_cmd = (vcpkg_dir / "bootstrap-vcpkg.bat").string();
#else
  bootstrap_cmd = (vcpkg_dir / "bootstrap-vcpkg.sh").string();
  bootstrap_args = {"-disableMetrics"};
#endif

  logger::print_action("Running", "vcpkg bootstrap");

  auto bootstrap_result = execute_process(
      bootstrap_cmd, bootstrap_args, vcpkg_dir.string(),
      [verbose](const std::string &line) {
        if (verbose) {
          logger::print_verbose(line);
        }
      },
      [](const std::string &line) { logger::print_error(line); });

  if (!bootstrap_result.success) {
    logger::print_error("Failed to bootstrap vcpkg. Exit code: " +
                        std::to_string(bootstrap_result.exit_code));
    return false;
  }

  return true;
}

/**
 * @brief Update dependencies with vcpkg
 *
 * @param project_dir Project directory
 * @param dependencies Map of dependencies
 * @param verbose Verbose output
 * @return true if successful, false otherwise
 */
static bool update_dependencies_with_vcpkg(
    const std::filesystem::path &project_dir,
    const std::map<std::string, std::string> &dependencies, bool verbose) {
  if (dependencies.empty()) {
    logger::print_action("Skipping", "No dependencies to update");
    return true;
  }

  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
  std::filesystem::path vcpkg_exe;

#ifdef _WIN32
  vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
  vcpkg_exe = vcpkg_dir / "vcpkg";
#endif

  if (!std::filesystem::exists(vcpkg_exe)) {
    logger::print_error("vcpkg not found at: " + vcpkg_exe.string());
    logger::print_action("Run", "'cforge vcpkg' to set up vcpkg integration");
    return false;
  }

  // First, run vcpkg update
  std::string command = vcpkg_exe.string();
  std::vector<std::string> args = {"update"};

  logger::print_action("Running", "vcpkg update");

  auto result = execute_process(
      command, args, "",
      [verbose](const std::string &line) {
        if (verbose) {
          logger::print_verbose(line);
        }
      },
      [](const std::string &line) { logger::print_error(line); });

  if (!result.success) {
    logger::print_warning("vcpkg update failed. Exit code: " +
                          std::to_string(result.exit_code));
    // Continue anyway, might still be able to update packages
  }

  // Update each dependency
  bool all_successful = true;

  for (const auto &[name, version] : dependencies) {
    std::string package_spec = name;
    if (version != "*") {
      package_spec += ":" + version;
    }

    std::vector<std::string> install_args = {"install", package_spec};

    logger::updating(package_spec);

    auto install_result = execute_process(
        command, install_args, "",
        [verbose](const std::string &line) {
          if (verbose) {
            logger::print_verbose(line);
          }
        },
        [](const std::string &line) { logger::print_error(line); });

    if (!install_result.success) {
      logger::print_error("Failed to update package with vcpkg. Exit code: " +
                          std::to_string(install_result.exit_code));
      all_successful = false;
    }
  }

  return all_successful;
}

// Add function to update Git dependencies based on [dependencies.git] table
static bool
update_dependencies_with_git(const std::filesystem::path &project_dir,
                             const toml_reader &config, bool verbose) {
  std::filesystem::path deps_dir = project_dir / "deps";
  bool all_success = true;
  for (const auto &name : config.get_table_keys("dependencies.git")) {
    std::string url =
        config.get_string("dependencies.git." + name + ".url", "");
    if (url.empty()) {
      if (verbose)
        logger::print_warning("No URL specified for git dependency: " + name);
      continue;
    }
    std::filesystem::path repo_path = deps_dir / name;
    if (std::filesystem::exists(repo_path)) {
      logger::updating(name);
      auto result = execute_process(
          "git", {"pull"}, repo_path.string(),
          [verbose](const std::string &line) {
            if (verbose)
              logger::print_verbose(line);
          },
          [](const std::string &line) { logger::print_error(line); });
      if (!result.success) {
        logger::print_error("Failed to update git dependency: " + name);
        all_success = false;
      }
    } else {
      logger::print_action("Cloning", name);
      auto result = execute_process(
          "git", {"clone", url, repo_path.string()}, "",
          [verbose](const std::string &line) {
            if (verbose)
              logger::print_verbose(line);
          },
          [](const std::string &line) { logger::print_error(line); });
      if (!result.success) {
        logger::print_error("Failed to clone git dependency: " + name);
        all_success = false;
      }
    }
  }
  return all_success;
}

/**
 * @brief Handle the 'update' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_update(const cforge_context_t *ctx) {
  std::filesystem::path cwd = ctx->working_dir;
  std::filesystem::path config_file = cwd / "cforge.toml";
  bool project_present = std::filesystem::exists(config_file);
  installer installer_instance;

  // Parse flags
  std::string install_path;
  bool add_to_path = false;
  bool update_self = false;
  bool update_packages = false;

  for (int i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];
    if (arg == "--self" || arg == "-s") {
      update_self = true;
    } else if (arg == "--packages" || arg == "-p") {
      update_packages = true;
    } else if (arg == "--path") {
      if (i + 1 < ctx->args.arg_count) {
        install_path = ctx->args.args[++i];
      }
    } else if (arg == "--add-to-path") {
      add_to_path = true;
    }
  }

  // Require explicit flag
  if (!update_self && !update_packages) {
    logger::print_error("Please specify what to update:");
    logger::print_action("--self, -s", "Update cforge itself");
    logger::print_action("--packages, -p", "Update project dependencies");
    logger::print_action("Usage", "cforge update --self");
    logger::print_action("Usage", "cforge update --packages");
    return 1;
  }

  // Cannot use both flags at once
  if (update_self && update_packages) {
    logger::print_error("Cannot use both --self and --packages at the same time");
    return 1;
  }

  // Handle --packages when no project exists
  if (update_packages && !project_present) {
    logger::print_error("No cforge.toml found in current directory");
    logger::print_action("Hint", "Run 'cforge init' to create a new project, or use 'cforge update --self' to update cforge");
    return 1;
  }

  if (update_self) {
    // Self-update: clone, build, and install from GitHub
    logger::print_header("Updating cforge itself");

    // Determine install location
    if (install_path.empty()) {
      // Check env variable CFORGE_INSTALL_PATH
      const char *env_path = std::getenv("CFORGE_INSTALL_PATH");
      if (env_path && *env_path) {
        install_path = env_path;
      } else {
        install_path = installer_instance.get_default_install_path();
      }
    }
    logger::print_action("Install path", install_path);

    // Prepare temporary clone directory
    std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "cforge_update_temp";
    if (std::filesystem::exists(temp_dir))
      std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    // Clone repository
    const std::string repo_url = "https://github.com/ChaseSunstrom/cforge.git";
    logger::print_action("Cloning", "cforge from GitHub: " + repo_url);
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    if (!execute_tool("git",
                      {"clone", "--branch", "master", repo_url, temp_dir.string()},
                      "", "Git Clone", verbose)) {
      logger::print_error("Failed to clone cforge repository");
      return 1;
    }

    // Fetch dependencies
    logger::print_action("Fetching", "dependencies");
    std::filesystem::path vendor_dir = temp_dir / "vendor";
    std::filesystem::create_directories(vendor_dir);

    if (!execute_tool("git",
                      {"clone", "https://github.com/fmtlib/fmt.git",
                       (vendor_dir / "fmt").string()},
                      "", "Clone fmt", verbose)) {
      logger::print_error("Failed to clone fmt dependency");
      return 1;
    }
    // Checkout fmt version
    execute_tool("git", {"checkout", "11.1.4"}, (vendor_dir / "fmt").string(),
                 "Checkout fmt", verbose);

    if (!execute_tool("git",
                      {"clone", "https://github.com/marzer/tomlplusplus.git",
                       (vendor_dir / "tomlplusplus").string()},
                      "", "Clone tomlplusplus", verbose)) {
      logger::print_error("Failed to clone tomlplusplus dependency");
      return 1;
    }
    // Checkout tomlplusplus version
    execute_tool("git", {"checkout", "v3.4.0"},
                 (vendor_dir / "tomlplusplus").string(), "Checkout tomlplusplus",
                 verbose);

    // Configure with CMake
    logger::print_action("Configuring", "build with CMake");
    std::filesystem::path build_dir = temp_dir / "build";
    std::filesystem::create_directories(build_dir);

    std::vector<std::string> cmake_args = {
        "-S", temp_dir.string(), "-B", build_dir.string(),
        "-DCMAKE_BUILD_TYPE=Release", "-DFMT_HEADER_ONLY=ON",
        "-DBUILD_SHARED_LIBS=OFF"};

#ifdef _WIN32
    // Use Ninja if available on Windows
    if (is_command_available("ninja")) {
      cmake_args.push_back("-G");
      cmake_args.push_back("Ninja");
    }
#endif

    if (!execute_tool("cmake", cmake_args, "", "CMake Configure", verbose, 180)) {
      logger::print_error("CMake configuration failed");
      std::filesystem::remove_all(temp_dir);
      return 1;
    }

    // Build
    logger::print_action("Building", "cforge");
    std::vector<std::string> build_args = {"--build", build_dir.string(),
                                           "--config", "Release"};

    if (!execute_tool("cmake", build_args, "", "CMake Build", verbose, 600)) {
      logger::print_error("Build failed");
      std::filesystem::remove_all(temp_dir);
      return 1;
    }

    // Find the built binary
    std::filesystem::path built_exe;
    std::vector<std::filesystem::path> possible_paths = {
        build_dir / "bin" / "Release" / "cforge.exe",
        build_dir / "bin" / "Release" / "cforge",
        build_dir / "bin" / "cforge.exe",
        build_dir / "bin" / "cforge",
        build_dir / "Release" / "cforge.exe",
        build_dir / "Release" / "cforge",
        build_dir / "cforge.exe",
        build_dir / "cforge"};

    for (const auto &path : possible_paths) {
      if (std::filesystem::exists(path)) {
        built_exe = path;
        break;
      }
    }

    if (built_exe.empty()) {
      logger::print_error("Could not find built cforge executable");
      std::filesystem::remove_all(temp_dir);
      return 1;
    }

    logger::print_verbose("Found built executable: " + built_exe.string());

    // Install the binary to the same location as `cforge install`
    // This is: <platform_path>/installed/cforge/bin/
    std::filesystem::path install_bin_dir =
        std::filesystem::path(install_path) / "installed" / "cforge" / "bin";
    std::filesystem::create_directories(install_bin_dir);

#ifdef _WIN32
    std::filesystem::path target_exe = install_bin_dir / "cforge.exe";
#else
    std::filesystem::path target_exe = install_bin_dir / "cforge";
#endif

    // Install the binary
    bool install_success = false;
    try {
      std::filesystem::path backup = target_exe;
      backup += ".old";

      // Remove old backup if it exists (ignore errors)
      if (std::filesystem::exists(backup)) {
        try {
          std::filesystem::remove(backup);
        } catch (...) {
          // Ignore - might be locked
        }
      }

      // Rename current exe to backup (if it exists)
      if (std::filesystem::exists(target_exe)) {
        try {
          std::filesystem::rename(target_exe, backup);
        } catch (...) {
          // If rename fails, try direct overwrite
        }
      }

      // Copy new binary
      std::filesystem::copy_file(
          built_exe, target_exe,
          std::filesystem::copy_options::overwrite_existing);
      logger::print_action("Installed", target_exe.string());
      install_success = true;

      // Try to remove backup (ignore errors - Windows may have it locked)
      if (std::filesystem::exists(backup)) {
        try {
          std::filesystem::remove(backup);
        } catch (...) {
          // Ignore - will be cleaned up on next update
        }
      }
    } catch (const std::exception &e) {
      logger::print_error("Failed to install binary: " + std::string(e.what()));
    }

    // Update PATH if requested (only if install succeeded)
    if (install_success && add_to_path) {
      installer_instance.update_path_env(install_bin_dir);
      logger::print_action("Updated", "PATH environment variable");
    }

    // Clean up temporary directory (handle read-only git files on Windows)
    try {
#ifdef _WIN32
      // On Windows, git objects are often read-only, need to remove that attribute
      for (auto &entry :
           std::filesystem::recursive_directory_iterator(temp_dir)) {
        try {
          std::filesystem::permissions(entry.path(),
                                       std::filesystem::perms::owner_write,
                                       std::filesystem::perm_options::add);
        } catch (...) {
        }
      }
#endif
      std::filesystem::remove_all(temp_dir);
    } catch (...) {
      // Ignore cleanup errors - temp files will be cleaned up by OS eventually
    }

    logger::finished("cforge updated successfully!");
    logger::print_action("Location", target_exe.string());
    return 0;
  }

  // Update project dependencies (update_packages == true)
  logger::print_header("Updating project dependencies");
  // Check for verbosity
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // Update vcpkg first
  bool vcpkg_updated = update_vcpkg(cwd, verbose);
  if (!vcpkg_updated) {
    logger::print_warning("Failed to update vcpkg");
  }

  // Load project configuration
  toml_reader config;
  if (!config.load(config_file.string())) {
    logger::print_error("Failed to parse configuration file: " +
                        config_file.string());
    return 1;
  }
  // Update vcpkg-managed dependencies from [dependencies.vcpkg]
  std::map<std::string, std::string> vcpkg_deps;
  for (const auto &key : config.get_table_keys("dependencies.vcpkg")) {
    vcpkg_deps[key] =
        config.get_string(std::string("dependencies.vcpkg.") + key, "*");
  }
  bool deps_updated = update_dependencies_with_vcpkg(cwd, vcpkg_deps, verbose);
  if (!deps_updated) {
    logger::print_warning("Failed to update some vcpkg dependencies");
  }
  // Update Git dependencies
  bool git_ok = update_dependencies_with_git(cwd, config, verbose);
  if (!git_ok) {
    logger::print_warning("Failed to update some git dependencies");
  }
  if (vcpkg_updated && deps_updated && git_ok) {
    logger::finished("Successfully updated all dependencies");
    return 0;
  } else {
    logger::print_error("Failed to update one or more dependencies");
    return 1;
  }
}