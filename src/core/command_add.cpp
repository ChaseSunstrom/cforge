/**
 * @file command_add.cpp
 * @brief Implementation of the 'add' command to add components to a project
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace_utils.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

using namespace cforge;

/**
 * @brief Add a dependency to the project configuration
 *
 * @param project_dir Directory containing the project
 * @param config_file Path to the configuration file
 * @param package_name Name of the package to add
 * @param package_version Version of the package (optional)
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool add_dependency_to_config(const std::filesystem::path &project_dir,
                                     const std::filesystem::path &config_file,
                                     const std::string &package_name,
                                     const std::string &package_version,
                                     bool verbose) {
  // Read existing config file
  std::string content;
  std::ifstream file(config_file);
  if (!file) {
    logger::print_error("Failed to read configuration file: " +
                        config_file.string());
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  content = buffer.str();
  file.close();

  // Check if the dependency section exists
  bool has_dependencies_section =
      content.find("[dependencies]") != std::string::npos;
  std::string entry;

  if (!package_version.empty()) {
    entry = package_name + " = \"" + package_version + "\"";
  } else {
    entry = package_name + " = \"*\""; // Use * for latest version
  }

  std::ofstream outfile(config_file, std::ios::app);
  if (!outfile) {
    logger::print_error("Failed to open configuration file for writing: " +
                        config_file.string());
    return false;
  }

  if (!has_dependencies_section) {
    outfile << "\n[dependencies]\n";
  }

  outfile << entry << "\n";
  outfile.close();

  if (verbose) {
    logger::print_status("Added dependency: " + entry);
  }

  return true;
}

/**
 * @brief Run vcpkg to install the package
 *
 * @param project_dir Directory containing the project
 * @param package_name Name of the package to install
 * @param package_version Version of the package (optional)
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool install_package_with_vcpkg(const std::filesystem::path &project_dir,
                                       const std::string &package_name,
                                       const std::string &package_version,
                                       bool verbose) {
  // Determine vcpkg executable
  std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
  std::filesystem::path vcpkg_exe;

#ifdef _WIN32
  vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
  vcpkg_exe = vcpkg_dir / "vcpkg";
#endif

  if (!std::filesystem::exists(vcpkg_exe)) {
    logger::print_error("vcpkg not found at: " + vcpkg_exe.string());
    logger::print_status("Run 'cforge vcpkg' to set up vcpkg integration");
    return false;
  }

  // Prepare the package spec
  std::string package_spec = package_name;
  if (!package_version.empty()) {
    package_spec += ":" + package_version;
  }

  // Build the command
  std::string command = vcpkg_exe.string();
  std::vector<std::string> args = {"install", package_spec};

  // Run the command
  logger::print_status("Installing package: " + package_spec);

  auto result = execute_process(
      command, args,
      "", // working directory
      [verbose](const std::string &line) {
        if (verbose) {
          logger::print_verbose(line);
        }
      },
      [](const std::string &line) { logger::print_error(line); });

  if (!result.success) {
    logger::print_error("Failed to install package with vcpkg. Exit code: " +
                        std::to_string(result.exit_code));
    return false;
  }

  return true;
}

// Helpers to add dependencies to a specific TOML section
static bool add_dependency_to_section(const std::filesystem::path &config_file,
                                      const std::string &section,
                                      const std::string &entry,
                                      bool verbose) {
  std::ifstream file(config_file);
  if (!file) {
    logger::print_error("Failed to read configuration file: " + config_file.string());
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();
  bool has_section = content.find("[" + section + "]") != std::string::npos;
  std::ofstream outfile(config_file, std::ios::app);
  if (!outfile) {
    logger::print_error("Failed to open configuration file for writing: " + config_file.string());
    return false;
  }
  if (!has_section) {
    outfile << "\n[" << section << "]\n";
  }
  outfile << entry << "\n";
  outfile.close();
  if (verbose) {
    logger::print_status("Added dependency: " + entry + " to [" + section + "]");
  }
  return true;
}

static bool add_vcpkg_dependency_to_config(const std::filesystem::path &project_dir,
                                           const std::filesystem::path &config_file,
                                           const std::string &package_name,
                                           const std::string &package_version,
                                           bool verbose) {
  std::string entry = package_name + " = \"" + (package_version.empty() ? "*" : package_version) + "\"";
  return add_dependency_to_section(config_file, "dependencies.vcpkg", entry, verbose);
}

static bool add_git_dependency_to_config(const std::filesystem::path &project_dir,
                                        const std::filesystem::path &config_file,
                                        const std::string &package_name,
                                        const std::string &package_url,
                                        bool verbose) {
  std::string entry = package_name + " = { url = \"" + package_url + "\" }";
  return add_dependency_to_section(config_file, "dependencies.git", entry, verbose);
}

/**
 * @brief Handle the 'add' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_add(const cforge_context_t *ctx) {
  // Determine working context (project or workspace)
  std::filesystem::path project_dir = ctx->working_dir;
  auto [is_workspace, workspace_root] = cforge::is_in_workspace(project_dir);
  bool in_workspace_root = is_workspace && project_dir == workspace_root;
  std::filesystem::path config_file = project_dir / CFORGE_FILE;
  // If not in workspace root, ensure this is a project directory
  if (!in_workspace_root && !std::filesystem::exists(config_file)) {
    logger::print_error(
        "Not a cforge project directory (cforge.toml not found)");
    logger::print_status("Run 'cforge init' to create a new project");
    return 1;
  }

  // Parse flags
  bool mode_git = false, mode_vcpkg = false;
  std::vector<std::string> args;
  for (int i = 0; i < ctx->args.arg_count; ++i) args.push_back(ctx->args.args[i]);
  std::vector<std::string> filtered;
  for (auto &arg : args) {
    if (arg == "--git") mode_git = true;
    else if (arg == "--vcpkg") mode_vcpkg = true;
    else filtered.push_back(arg);
  }
  args.swap(filtered);
  if (mode_git && mode_vcpkg) {
    logger::print_error("Cannot use both --git and --vcpkg"); return 1;
  }

  // Check if package name was provided
  if (args.empty() || args[0].empty() || args[0][0] == '-') {
    logger::print_error("Package name not specified");
    logger::print_status("Usage: cforge add <package> [--git|--vcpkg]");
    return 1;
  }

  // Extract package name and version or URL
  std::string package_name = args[0];
  std::string package_version_or_url;
  if (mode_git) {
    if (args.size() < 2) {
      logger::print_error("URL for git dependency not specified");
      logger::print_status("Usage: cforge add --git <name> <url>"); return 1;
    }
    package_version_or_url = args[1];
  } else {
    // vcpkg mode or default: parse version
    size_t colon_pos = package_name.find(':');
    std::string package_version;
    if (colon_pos != std::string::npos) {
      package_version = package_name.substr(colon_pos + 1);
      package_name = package_name.substr(0, colon_pos);
    }
    package_version_or_url = package_version;
  }

  // Check for verbosity
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // Apply to each project in workspace or single project
  if (in_workspace_root) {
    bool all_success = true;
    auto projects = cforge::get_workspace_projects(workspace_root);
    for (const auto &proj : projects) {
      auto proj_dir = workspace_root / proj;
      auto proj_config = proj_dir / CFORGE_FILE;
      if (!std::filesystem::exists(proj_config)) continue;
      bool cfg_ok = false, inst_ok = true;
      if (mode_git) {
        cfg_ok = add_git_dependency_to_config(proj_dir, proj_config, package_name, package_version_or_url, verbose);
      } else {
        cfg_ok = add_vcpkg_dependency_to_config(proj_dir, proj_config, package_name, package_version_or_url, verbose);
        inst_ok = install_package_with_vcpkg(proj_dir, package_name, package_version_or_url, verbose);
      }
      if (!cfg_ok || !inst_ok) all_success = false;
    }
    if (all_success) {
      logger::print_success("Successfully added dependency: " + package_name + " to workspace projects");
      return 0;
    } else {
      logger::print_error("Failed to add dependency: " + package_name + " to some workspace projects");
      return 1;
    }
  }

  // Single project addition
  bool cfg_ok = false, inst_ok = true;
  if (mode_git) {
    cfg_ok = add_git_dependency_to_config(project_dir, config_file, package_name, package_version_or_url, verbose);
  } else {
    cfg_ok = add_vcpkg_dependency_to_config(project_dir, config_file, package_name, package_version_or_url, verbose);
    inst_ok = install_package_with_vcpkg(project_dir, package_name, package_version_or_url, verbose);
  }
  if (cfg_ok && inst_ok) {
    logger::print_success("Successfully added dependency: " + package_name);
    return 0;
  } else {
    logger::print_error("Failed to add dependency: " + package_name);
    return 1;
  }
}