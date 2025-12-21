/**
 * @file command_vcpkg.cpp
 * @brief Implementation of the 'vcpkg' command to manage dependencies
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/types.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
static std::filesystem::path get_vcpkg_path(const cforge::toml_reader *project_config) {
  std::filesystem::path vcpkg_path;

  // First check project config
  if (project_config) {
    std::string config_path =
        project_config->get_string("dependencies.vcpkg.path", "");
    if (!config_path.empty()) {
      vcpkg_path = config_path;
      return vcpkg_path;
    }
  }

  // Then check environment variable
  const char *env_path = std::getenv("VCPKG_ROOT");
  if (env_path && *env_path) {
    vcpkg_path = env_path;
    return vcpkg_path;
  }

  // Finally try default locations
#ifdef _WIN32
  const char *userprofile = std::getenv("USERPROFILE");
  if (userprofile) {
    vcpkg_path = std::filesystem::path(userprofile) / "vcpkg";
  }
#else
  const char *home = std::getenv("HOME");
  if (home) {
    vcpkg_path = std::filesystem::path(home) / "vcpkg";
  }
#endif

  return vcpkg_path;
}

/**
 * @brief Check if vcpkg is installed in the project directory
 *
 * @param project_dir Directory containing the project
 * @return true if installed, false otherwise
 */
[[maybe_unused]] static bool is_vcpkg_installed(const std::filesystem::path &project_dir) {
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
      if ((file_perms & std::filesystem::perms::owner_exec) ==
          std::filesystem::perms::none) {
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
[[maybe_unused]] static bool clone_vcpkg(const std::filesystem::path &project_dir,
                        bool verbose) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";

  if (std::filesystem::exists(vcpkg_dir)) {
    cforge::logger::print_action("Done", "vcpkg is already installed");
    return true;
  }

  // Create directory if not exists
  try {
    std::filesystem::create_directory(vcpkg_dir);
  } catch (const std::exception &e) {
    cforge::logger::print_error("Failed to create vcpkg directory: " +
                        std::string(e.what()));
    return false;
  }

  // Clone the repository
  std::string git_cmd = "git";
  std::vector<std::string> git_args = {
      "clone", "https://github.com/microsoft/vcpkg.git", vcpkg_dir.string()};

  cforge::logger::fetching("vcpkg repository");

  auto result = cforge::execute_process(
      git_cmd, git_args,
      "", // working directory
      [verbose](const std::string &line) {
        if (verbose) {
          cforge::logger::print_verbose(line);
        }
      },
      [](const std::string &line) { cforge::logger::print_error(line); });

  if (!result.success) {
    cforge::logger::print_error("Failed to clone vcpkg repository. Exit code: " +
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

  cforge::logger::installing("vcpkg");

  auto bootstrap_result = cforge::execute_process(
      bootstrap_cmd, bootstrap_args, vcpkg_dir.string(),
      [verbose](const std::string &line) {
        if (verbose) {
          cforge::logger::print_verbose(line);
        }
      },
      [](const std::string &line) { cforge::logger::print_error(line); });

  if (!bootstrap_result.success) {
    cforge::logger::print_error("Failed to bootstrap vcpkg. Exit code: " +
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
[[maybe_unused]] static bool setup_vcpkg_integration(const std::filesystem::path &project_dir,
                                    bool verbose) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
  std::filesystem::path vcpkg_exe;

#ifdef _WIN32
  vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
  vcpkg_exe = vcpkg_dir / "vcpkg";
#endif

  if (!std::filesystem::exists(vcpkg_exe)) {
    cforge::logger::print_error("vcpkg not found at: " + vcpkg_exe.string());
    return false;
  }

  // Set up vcpkg integration
  std::string command = vcpkg_exe.string();
  std::vector<std::string> args = {"integrate", "install"};

  cforge::logger::print_action("Integrating", "vcpkg");

  auto result = cforge::execute_process(
      command, args,
      "", // working directory
      [verbose](const std::string &line) {
        if (verbose) {
          cforge::logger::print_verbose(line);
        } else {
          cforge::logger::print_status(line);
        }
      },
      [](const std::string &line) { cforge::logger::print_error(line); });

  if (!result.success) {
    cforge::logger::print_error("Failed to set up vcpkg integration. Exit code: " +
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
      cforge::logger::print_warning("Failed to create vcpkg-configuration.json");
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
[[maybe_unused]] static bool forward_to_vcpkg(const std::filesystem::path &project_dir,
                             const cforge_command_args_t &args, [[maybe_unused]] bool verbose) {
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
  std::filesystem::path vcpkg_exe;

#ifdef _WIN32
  vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
  vcpkg_exe = vcpkg_dir / "vcpkg";
#endif

  if (!std::filesystem::exists(vcpkg_exe)) {
    cforge::logger::print_error("vcpkg not found at: " + vcpkg_exe.string());
    return false;
  }

  // Build command line
  std::string command = vcpkg_exe.string();
  std::vector<std::string> vcpkg_args;

  // Forward all arguments to vcpkg
  if (args.args) {
    for (cforge_int_t i = 0; args.args[i]; ++i) {
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

  cforge::logger::print_action("Running", "vcpkg command: " + command_str);

  auto result = cforge::execute_process(
      command, vcpkg_args,
      "", // working directory
      [](const std::string &line) { cforge::logger::print_status(line); },
      [](const std::string &line) { cforge::logger::print_error(line); });

  if (!result.success) {
    cforge::logger::print_error("vcpkg command failed. Exit code: " +
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
  // Parse arguments
  std::vector<std::string> args;
  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    args.push_back(ctx->args.args[i]);
  }

  if (args.empty()) {
    cforge::logger::print_error("No command specified");
    cforge::logger::print_status("Usage: cforge vcpkg <command>");
    cforge::logger::print_status("Commands:");
    cforge::logger::print_status("  setup    - Set up vcpkg integration");
    cforge::logger::print_status("  update   - Update vcpkg and installed packages");
    cforge::logger::print_status("  list     - List installed packages");
    return 1;
  }

  std::string command = args[0];
  if (command == "setup") {
    // Get vcpkg path
    std::filesystem::path vcpkg_path = get_vcpkg_path(nullptr);
    if (vcpkg_path.empty()) {
      cforge::logger::print_error("Could not determine vcpkg path");
      return 1;
    }

    // Check if vcpkg is already installed
    std::filesystem::path vcpkg_exe;
#ifdef _WIN32
    vcpkg_exe = vcpkg_path / "vcpkg.exe";
#else
    vcpkg_exe = vcpkg_path / "vcpkg";
#endif

    if (std::filesystem::exists(vcpkg_exe)) {
      cforge::logger::print_action("Done", "vcpkg is already installed");
    } else {
      // Clone vcpkg
      cforge::logger::fetching("vcpkg");
      std::string git_cmd = "git";
      std::vector<std::string> git_args = {
          "clone", "https://github.com/Microsoft/vcpkg.git",
          vcpkg_path.string()};

      auto result = cforge::execute_process(
          git_cmd, git_args,
          "", // working directory
          [](const std::string &line) { cforge::logger::print_verbose(line); },
          [](const std::string &line) { cforge::logger::print_error(line); });

      if (!result.success) {
        cforge::logger::print_error("Failed to clone vcpkg");
        return 1;
      }

      // Bootstrap vcpkg
      cforge::logger::installing("vcpkg");
#ifdef _WIN32
      std::string bootstrap_cmd = (vcpkg_path / "bootstrap-vcpkg.bat").string();
#else
      std::string bootstrap_cmd = (vcpkg_path / "bootstrap-vcpkg.sh").string();
#endif

      auto bootstrap_result = cforge::execute_process(
          bootstrap_cmd, {}, vcpkg_path.string(),
          [](const std::string &line) { cforge::logger::print_verbose(line); },
          [](const std::string &line) { cforge::logger::print_error(line); });

      if (!bootstrap_result.success) {
        cforge::logger::print_error("Failed to bootstrap vcpkg");
        return 1;
      }
    }

    // Set up environment variable
    std::string vcpkg_root = vcpkg_path.string();
#ifdef _WIN32
    std::string cmd = "setx VCPKG_ROOT \"" + vcpkg_root + "\"";
#else
    std::string cmd =
        "echo 'export VCPKG_ROOT=\"" + vcpkg_root + "\"' >> ~/.bashrc";
#endif

    auto env_result = cforge::execute_process(
        cmd, {},
        "", // working directory
        [](const std::string &line) { cforge::logger::print_verbose(line); },
        [](const std::string &line) { cforge::logger::print_error(line); });

    if (!env_result.success) {
      cforge::logger::print_warning("Failed to set VCPKG_ROOT environment variable");
      cforge::logger::print_status("Please set VCPKG_ROOT to: " + vcpkg_root);
    }

    cforge::logger::finished("vcpkg has been successfully installed");
    return 0;
  } else if (command == "update") {
    // Get vcpkg path
    std::filesystem::path vcpkg_path = get_vcpkg_path(nullptr);
    if (vcpkg_path.empty()) {
      cforge::logger::print_error("Could not determine vcpkg path");
      return 1;
    }

    // Update vcpkg
    cforge::logger::updating("vcpkg");
    std::string git_cmd = "git";
    std::vector<std::string> git_args = {"pull"};

    auto result = cforge::execute_process(
        git_cmd, git_args, vcpkg_path.string(),
        [](const std::string &line) { cforge::logger::print_verbose(line); },
        [](const std::string &line) { cforge::logger::print_error(line); });

    if (!result.success) {
      cforge::logger::print_error("Failed to update vcpkg");
      return 1;
    }

    // Update packages
    cforge::logger::updating("packages");
    std::string vcpkg_cmd = (vcpkg_path / "vcpkg").string();
    std::vector<std::string> vcpkg_args = {"upgrade", "--no-dry-run"};

    auto update_result = cforge::execute_process(
        vcpkg_cmd, vcpkg_args,
        "", // working directory
        [](const std::string &line) { cforge::logger::print_verbose(line); },
        [](const std::string &line) { cforge::logger::print_error(line); });

    if (!update_result.success) {
      cforge::logger::print_error("Failed to update packages");
      return 1;
    }

    cforge::logger::finished("vcpkg and packages have been updated");
    return 0;
  } else if (command == "list") {
    // Get vcpkg path
    std::filesystem::path vcpkg_path = get_vcpkg_path(nullptr);
    if (vcpkg_path.empty()) {
      cforge::logger::print_error("Could not determine vcpkg path");
      return 1;
    }

    // List packages
    std::string vcpkg_cmd = (vcpkg_path / "vcpkg").string();
    std::vector<std::string> vcpkg_args = {"list"};

    auto result = cforge::execute_process(
        vcpkg_cmd, vcpkg_args,
        "", // working directory
        [](const std::string &line) { cforge::logger::print_status(line); },
        [](const std::string &line) { cforge::logger::print_error(line); });

    if (!result.success) {
      cforge::logger::print_error("Failed to list packages");
      return 1;
    }

    return 0;
  } else {
    cforge::logger::print_error("Unknown command: " + command);
    return 1;
  }
}