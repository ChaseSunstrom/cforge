/**
 * @file command_vcpkg.cpp
 * @brief Implementation of the 'vcpkg' command to manage dependencies
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace cforge;

// Default vcpkg directory location relative to user home
#ifdef _WIN32
const char *DEFAULT_VCPKG_DIR = "%USERPROFILE%\\vcpkg";
#else
const char *DEFAULT_VCPKG_DIR = "~/vcpkg";
#endif

/**
 * @brief Get the path to vcpkg directory
 *
 * @param project_config Optional project configuration
 * @return std::filesystem::path Path to vcpkg
 */
static std::filesystem::path get_vcpkg_path(const toml_reader *project_config) {
  std::filesystem::path vcpkg_path;

  // First check if vcpkg path is specified in project config
  if (project_config && project_config->has_key("dependencies.vcpkg_path")) {
    vcpkg_path = project_config->get_string("dependencies.vcpkg_path");

// Expand environment variables if needed
#ifdef _WIN32
    if (vcpkg_path.string().find('%') != std::string::npos) {
      char expanded_path[MAX_PATH];
      ExpandEnvironmentStringsA(vcpkg_path.string().c_str(), expanded_path,
                                MAX_PATH);
      vcpkg_path = expanded_path;
    }
#else
    // Handle tilde expansion for Unix paths
    if (vcpkg_path.string().find('~') == 0) {
      const char *home = getenv("HOME");
      if (home) {
        vcpkg_path =
            std::filesystem::path(home) / vcpkg_path.string().substr(1);
      }
    }
#endif
  } else {
// Use default path
#ifdef _WIN32
    char expanded_path[MAX_PATH];
    ExpandEnvironmentStringsA(DEFAULT_VCPKG_DIR, expanded_path, MAX_PATH);
    vcpkg_path = expanded_path;
#else
    // Handle tilde expansion for Unix paths
    if (std::string(DEFAULT_VCPKG_DIR).find('~') == 0) {
      const char *home = getenv("HOME");
      if (home) {
        vcpkg_path = std::filesystem::path(home) /
                     std::string(DEFAULT_VCPKG_DIR).substr(1);
      } else {
        vcpkg_path = DEFAULT_VCPKG_DIR;
      }
    } else {
      vcpkg_path = DEFAULT_VCPKG_DIR;
    }
#endif
  }

  return vcpkg_path;
}

/**
 * @brief Check if vcpkg is installed in the project directory
 *
 * @param project_dir Directory containing the project
 * @return true if installed, false otherwise
 */
static bool is_vcpkg_installed(const std::filesystem::path &project_dir) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
  std::filesystem::path vcpkg_exe;

#ifdef _WIN32
  vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
  vcpkg_exe = vcpkg_dir / "vcpkg";
#endif

  if (std::filesystem::exists(vcpkg_exe)) {
// Check if the executable has the execute permission
#ifndef _WIN32
    {
      auto file_perms = std::filesystem::status(vcpkg_exe).permissions();
      if ((file_perms & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) {
        return false;
      }
    }
#endif

    return true;
  }

  return false;
}

/**
 * @brief Clone the vcpkg repository into the project directory
 *
 * @param project_dir Directory containing the project
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool clone_vcpkg(const std::filesystem::path &project_dir,
                        bool verbose) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";

  if (std::filesystem::exists(vcpkg_dir)) {
    logger::print_status("vcpkg is already installed");
    return true;
  }

  // Create directory if not exists
  try {
    std::filesystem::create_directory(vcpkg_dir);
  } catch (const std::exception &e) {
    logger::print_error("Failed to create vcpkg directory: " +
                        std::string(e.what()));
    return false;
  }

  // Clone the repository
  std::string git_cmd = "git";
  std::vector<std::string> git_args = {
      "clone", "https://github.com/microsoft/vcpkg.git", vcpkg_dir.string()};

  logger::print_status("Cloning vcpkg repository...");

  auto result = execute_process(
      git_cmd, git_args,
      "", // working directory
      [verbose](const std::string &line) {
        if (verbose) {
          logger::print_verbose(line);
        }
      },
      [](const std::string &line) { logger::print_error(line); });

  if (!result.success) {
    logger::print_error("Failed to clone vcpkg repository. Exit code: " +
                        std::to_string(result.exit_code));
    return false;
  }

  // Bootstrap vcpkg
  std::string bootstrap_cmd;
  std::vector<std::string> bootstrap_args;

#ifdef _WIN32
  bootstrap_cmd = (vcpkg_dir / "bootstrap-vcpkg.bat").string();
#else
  bootstrap_cmd = (vcpkg_dir / "bootstrap-vcpkg.sh").string();
  bootstrap_args = {"-disableMetrics"};
#endif

  logger::print_status("Bootstrapping vcpkg...");

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
 * @brief Set up vcpkg integration for the project
 *
 * @param project_dir Directory containing the project
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool setup_vcpkg_integration(const std::filesystem::path &project_dir,
                                    bool verbose) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
  std::filesystem::path vcpkg_exe;

#ifdef _WIN32
  vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
  vcpkg_exe = vcpkg_dir / "vcpkg";
#endif

  if (!std::filesystem::exists(vcpkg_exe)) {
    logger::print_error("vcpkg not found at: " + vcpkg_exe.string());
    return false;
  }

  // Set up vcpkg integration
  std::string command = vcpkg_exe.string();
  std::vector<std::string> args = {"integrate", "install"};

  logger::print_status("Setting up vcpkg integration...");

  auto result = execute_process(
      command, args,
      "", // working directory
      [verbose](const std::string &line) {
        if (verbose) {
          logger::print_verbose(line);
        } else {
          logger::print_status(line);
        }
      },
      [](const std::string &line) { logger::print_error(line); });

  if (!result.success) {
    logger::print_error("Failed to set up vcpkg integration. Exit code: " +
                        std::to_string(result.exit_code));
    return false;
  }

  // Create vcpkg-configuration.json
  std::filesystem::path vcpkg_config = project_dir / "vcpkg-configuration.json";

  if (!std::filesystem::exists(vcpkg_config)) {
    std::ofstream file(vcpkg_config);
    if (file.is_open()) {
      file << "{\n";
      file << "  \"default-registry\": {\n";
      file << "    \"kind\": \"git\",\n";
      file << "    \"repository\": \"https://github.com/microsoft/vcpkg\",\n";
      file << "    \"baseline\": \"latest\"\n";
      file << "  },\n";
      file << "  \"registries\": [],\n";
      file << "  \"overlay-ports\": [],\n";
      file << "  \"overlay-triplets\": []\n";
      file << "}\n";
      file.close();
    } else {
      logger::print_warning("Failed to create vcpkg-configuration.json");
    }
  }

  return true;
}

/**
 * @brief Forward arguments to vcpkg
 *
 * @param project_dir Directory containing the project
 * @param args Command arguments
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool forward_to_vcpkg(const std::filesystem::path &project_dir,
                             const cforge_command_args_t &args, bool verbose) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
  std::filesystem::path vcpkg_exe;

#ifdef _WIN32
  vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
  vcpkg_exe = vcpkg_dir / "vcpkg";
#endif

  if (!std::filesystem::exists(vcpkg_exe)) {
    logger::print_error("vcpkg not found at: " + vcpkg_exe.string());
    return false;
  }

  // Build command line
  std::string command = vcpkg_exe.string();
  std::vector<std::string> vcpkg_args;

  // Forward all arguments to vcpkg
  if (args.args) {
    for (int i = 0; args.args[i]; ++i) {
      vcpkg_args.push_back(args.args[i]);
    }
  }

  // Debug output
  std::string command_str = command;
  for (const auto &arg : vcpkg_args) {
    if (arg.find(' ') != std::string::npos) {
      command_str += " \"" + arg + "\"";
    } else {
      command_str += " " + arg;
    }
  }

  logger::print_status("Running vcpkg command: " + command_str);

  auto result = execute_process(
      command, vcpkg_args,
      "", // working directory
      [](const std::string &line) { logger::print_status(line); },
      [](const std::string &line) { logger::print_error(line); });

  if (!result.success) {
    logger::print_error("vcpkg command failed. Exit code: " +
                        std::to_string(result.exit_code));
    return false;
  }

  return true;
}

/**
 * @brief Handle the 'vcpkg' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_vcpkg(const cforge_context_t *ctx) {
  // Get project directory
  std::filesystem::path project_dir = ctx->working_dir;

  // Check for verbosity
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // Check the first argument
  if (ctx->args.args && ctx->args.args[0]) {
    if (strcmp(ctx->args.args[0], "setup") == 0) {
      // Check if vcpkg is already installed
      if (is_vcpkg_installed(project_dir)) {
        logger::print_status(
            "vcpkg is already installed in the project directory");

        // Set up vcpkg integration
        if (setup_vcpkg_integration(project_dir, verbose)) {
          logger::print_success(
              "vcpkg integration has been set up successfully");
          return 0;
        } else {
          logger::print_error("Failed to set up vcpkg integration");
          return 1;
        }
      }

      // Clone vcpkg repository
      if (clone_vcpkg(project_dir, verbose)) {
        logger::print_status("vcpkg has been successfully installed");

        // Set up vcpkg integration
        if (setup_vcpkg_integration(project_dir, verbose)) {
          logger::print_success("vcpkg has been set up successfully");
          return 0;
        } else {
          logger::print_error("Failed to set up vcpkg integration");
          return 1;
        }
      } else {
        logger::print_error("Failed to install vcpkg");
        return 1;
      }
    } else {
      // Check if vcpkg is installed
      if (!is_vcpkg_installed(project_dir)) {
        logger::print_error("vcpkg is not installed in the project directory");
        logger::print_status("Run 'cforge vcpkg setup' to install vcpkg");
        return 1;
      }

      // Forward arguments to vcpkg
      if (forward_to_vcpkg(project_dir, ctx->args, verbose)) {
        return 0;
      } else {
        return 1;
      }
    }
  } else {
    // No arguments provided, set up vcpkg
    if (is_vcpkg_installed(project_dir)) {
      logger::print_status(
          "vcpkg is already installed in the project directory");

      // Set up vcpkg integration
      if (setup_vcpkg_integration(project_dir, verbose)) {
        logger::print_success("vcpkg integration has been set up successfully");
        return 0;
      } else {
        logger::print_error("Failed to set up vcpkg integration");
        return 1;
      }
    }

    // Clone vcpkg repository
    if (clone_vcpkg(project_dir, verbose)) {
      logger::print_status("vcpkg has been successfully installed");

      // Set up vcpkg integration
      if (setup_vcpkg_integration(project_dir, verbose)) {
        logger::print_success("vcpkg has been set up successfully");
        return 0;
      } else {
        logger::print_error("Failed to set up vcpkg integration");
        return 1;
      }
    } else {
      logger::print_error("Failed to install vcpkg");
      return 1;
    }
  }

  return 0;
}