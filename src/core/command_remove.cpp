/**
 * @file command_remove.cpp
 * @brief Implementation of the 'remove' command to remove components from a
 * project
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
#include <vector>
#include <string>

using namespace cforge;

/**
 * @brief Remove a dependency from the project configuration
 *
 * @param config_file Path to the configuration file
 * @param package_name Name of the package to remove
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool
remove_dependency_from_config(const std::filesystem::path &config_file,
                              const std::string &package_name, bool verbose) {
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

  // Create a regex pattern to match the dependency entry
  std::regex pattern("^\\s*" + package_name + "\\s*=\\s*\"[^\"]*\"\\s*$");

  // Replace the entry with an empty string
  std::string result;
  std::string line;
  std::istringstream iss(content);

  while (std::getline(iss, line)) {
    std::regex line_pattern("\\s*" + package_name + "\\s*=\\s*\"[^\"]*\"\\s*");
    if (!std::regex_match(line, line_pattern)) {
      result += line + "\n";
    }
  }

  // Check if the content was changed
  if (result == content) {
    logger::print_warning("Dependency '" + package_name +
                          "' not found in configuration file");
    return false;
  }

  // Remove empty dependency section if needed
  std::istringstream iss_result(result);
  std::string line_result;
  bool in_dependencies_section = false;
  bool dependencies_section_empty = true;
  std::stringstream cleaned;

  while (std::getline(iss_result, line_result)) {
    // Check for section header
    if (line_result.find("[dependencies]") != std::string::npos) {
      in_dependencies_section = true;
      dependencies_section_empty = true;

      // Don't add the section yet, wait to see if it's empty
      continue;
    } else if (in_dependencies_section &&
               line_result.find("[") != std::string::npos) {
      // New section, end of dependencies
      in_dependencies_section = false;

      // If dependencies section was empty, don't add it
      if (!dependencies_section_empty) {
        cleaned << "[dependencies]" << std::endl;
      }
    }

    // Skip empty lines in dependencies section
    if (in_dependencies_section && !line_result.empty() &&
        line_result.find_first_not_of(" \t\r\n") != std::string::npos) {
      dependencies_section_empty = false;
      cleaned << line_result << std::endl;
    } else if (!in_dependencies_section) {
      cleaned << line_result << std::endl;
    }
  }

  // Write back to file
  std::ofstream outfile(config_file);
  if (!outfile) {
    logger::print_error("Failed to open configuration file for writing: " +
                        config_file.string());
    return false;
  }

  outfile << cleaned.str();
  outfile.close();

  if (verbose) {
    logger::print_status("Removed dependency: " + package_name);
  }

  return true;
}

/**
 * @brief Run vcpkg to remove the package
 *
 * @param project_dir Directory containing the project
 * @param package_name Name of the package to remove
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool remove_package_with_vcpkg(const std::filesystem::path &project_dir,
                                      const std::string &package_name,
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
    return false;
  }

  // Build the command
  std::string command = vcpkg_exe.string();
  std::vector<std::string> args = {"remove", package_name};

  // Run the command
  logger::print_status("Removing package: " + package_name);

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
    logger::print_error("Failed to remove package with vcpkg. Exit code: " +
                        std::to_string(result.exit_code));
    return false;
  }

  return true;
}

// Helpers to remove a dependency entry from a specific TOML section
static bool remove_dependency_from_section(
    const std::filesystem::path &config_file,
    const std::string &section,
    const std::string &package_name,
    bool verbose) {
  // Read all lines
  std::vector<std::string> lines;
  {
    std::ifstream infile(config_file);
    if (!infile) {
      logger::print_error("Failed to read configuration file: " + config_file.string());
      return false;
    }
    std::string line;
    while (std::getline(infile, line)) {
      lines.push_back(line);
    }
  }
  bool in_section = false;
  bool removed = false;
  std::vector<std::string> out;
  std::regex sec_header("^\\s*\[" + section + "\]\\s*$");
  std::regex any_header("^\\s*\[.+\]\\s*$");
  std::regex entry_pattern;
  if (section == "dependencies.vcpkg") {
    entry_pattern = std::regex("^\\s*" + package_name + "\\s*=\\s*\"[^\"]*\"\\s*$");
  } else if (section == "dependencies.git") {
    entry_pattern = std::regex("^\\s*" + package_name + "\\s*=\\s*\\{.*\\}\\s*$");
  } else {
    entry_pattern = std::regex("^\\s*" + package_name + "\\s*=.*$");
  }
  for (auto &ln : lines) {
    if (std::regex_match(ln, sec_header)) {
      in_section = true;
      out.push_back(ln);
      continue;
    }
    if (in_section && std::regex_match(ln, any_header) && !std::regex_match(ln, sec_header)) {
      in_section = false;
      out.push_back(ln);
      continue;
    }
    if (in_section && std::regex_match(ln, entry_pattern)) {
      removed = true;
      continue;
    }
    out.push_back(ln);
  }
  if (!removed) {
    logger::print_warning("Dependency '" + package_name + "' not found in section [" + section + "]");
    return false;
  }
  std::ofstream outfile(config_file);
  if (!outfile) {
    logger::print_error("Failed to open configuration file for writing: " + config_file.string());
    return false;
  }
  for (auto &ln : out) {
    outfile << ln << "\n";
  }
  if (verbose) {
    logger::print_status("Removed dependency: " + package_name + " from [" + section + "]");
  }
  return true;
}

static bool remove_vcpkg_dependency_from_config(
    const std::filesystem::path &config_file,
    const std::string &package_name,
    bool verbose) {
  return remove_dependency_from_section(config_file,
                                        "dependencies.vcpkg",
                                        package_name, verbose);
}

static bool remove_git_dependency_from_config(
    const std::filesystem::path &config_file,
    const std::string &package_name,
    bool verbose) {
  return remove_dependency_from_section(config_file,
                                        "dependencies.git",
                                        package_name, verbose);
}

/**
 * @brief Handle the 'remove' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_remove(const cforge_context_t *ctx) {
  // Determine working context (project or workspace)
  std::filesystem::path project_dir = ctx->working_dir;
  auto [is_workspace, workspace_root] = cforge::is_in_workspace(project_dir);
  bool in_workspace_root = is_workspace && project_dir == workspace_root;
  std::filesystem::path config_file = project_dir / CFORGE_FILE;
  if (!in_workspace_root && !std::filesystem::exists(config_file)) {
    logger::print_error(
        "Not a cforge project directory (" + std::string(CFORGE_FILE) + " not found)");
    logger::print_status("Run 'cforge init' to create a new project");
    return 1;
  }

  // Build argument list
  std::vector<std::string> args;
  for (int i = 0; i < ctx->args.arg_count; ++i) args.push_back(ctx->args.args[i]);
  // Parse flags
  bool mode_git = false;
  std::vector<std::string> filtered;
  for (auto &arg : args) {
    if (arg == "--git") mode_git = true;
    else filtered.push_back(arg);
  }
  args.swap(filtered);
  // Check if package name was provided
  if (args.empty() || args[0].empty() || args[0][0] == '-') {
    logger::print_error("Package name not specified");
    logger::print_status("Usage: cforge remove <package> [--git]");
    return 1;
  }
  // Extract package name
  std::string package_name = args[0];
  // Check for verbosity
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // Workspace removal
  if (in_workspace_root) {
    bool all_success = true;
    auto projects = cforge::get_workspace_projects(workspace_root);
    for (const auto &proj : projects) {
      auto proj_dir = workspace_root / proj;
      auto proj_cfg = proj_dir / CFORGE_FILE;
      if (!std::filesystem::exists(proj_cfg)) continue;
      bool cfg_ok = false;
      bool rem_ok = true;
      if (mode_git) {
        cfg_ok = remove_git_dependency_from_config(proj_cfg, package_name, verbose);
      } else {
        cfg_ok = remove_vcpkg_dependency_from_config(proj_cfg, package_name, verbose);
        rem_ok = remove_package_with_vcpkg(proj_dir, package_name, verbose);
      }
      if (!cfg_ok || !rem_ok) all_success = false;
    }
    if (all_success) {
      logger::print_success("Removed dependency '" + package_name + "' from all workspace projects");
      return 0;
    } else {
      logger::print_error("Failed to remove dependency '" + package_name + "' from some workspace projects");
      return 1;
    }
  }

  // Single project removal
  bool cfg_ok = false;
  bool rem_ok = true;
  if (mode_git) {
    cfg_ok = remove_git_dependency_from_config(config_file, package_name, verbose);
  } else {
    cfg_ok = remove_vcpkg_dependency_from_config(config_file, package_name, verbose);
    rem_ok = remove_package_with_vcpkg(project_dir, package_name, verbose);
  }
  if (cfg_ok || rem_ok) {
    logger::print_success("Successfully removed dependency: " + package_name);
    return 0;
  } else {
    logger::print_error("Failed to remove dependency: " + package_name);
    return 1;
  }
}