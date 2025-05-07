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
  // Determine vcpkg executable: try project-local then global
  std::filesystem::path project_vcpkg_exe;
#ifdef _WIN32
  project_vcpkg_exe = project_dir / "vcpkg" / "vcpkg.exe";
#else
  project_vcpkg_exe = project_dir / "vcpkg" / "vcpkg";
#endif
  std::filesystem::path vcpkg_exe;
  if (std::filesystem::exists(project_vcpkg_exe)) {
    vcpkg_exe = project_vcpkg_exe;
  } else {
    // Try default global vcpkg location
#ifdef _WIN32
    const char *userprofile = std::getenv("USERPROFILE");
    std::filesystem::path global_dir = userprofile ? std::filesystem::path(userprofile) / "vcpkg" : std::filesystem::path();
    std::filesystem::path global_exe = global_dir / "vcpkg.exe";
#else
    const char *home = std::getenv("HOME");
    std::filesystem::path global_dir = home ? std::filesystem::path(home) / "vcpkg" : std::filesystem::path();
    std::filesystem::path global_exe = global_dir / "vcpkg";
#endif
    if (!global_dir.empty() && std::filesystem::exists(global_exe)) {
      vcpkg_exe = global_exe;
    } else {
      logger::print_error("vcpkg not found. Checked: " + project_vcpkg_exe.string() + " and " + global_exe.string());
      logger::print_status("Run 'cforge vcpkg setup' to set up vcpkg integration");
      return false;
    }
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

// Helper to remove cloned git repository
static bool remove_git_repo(const std::filesystem::path &project_dir,
                            const std::string &package_name,
                            bool verbose) {
  // Get the configured dependency directory from cforge.toml
  toml_reader project_config;
  if (!project_config.load((project_dir / "cforge.toml").string())) {
    logger::print_error("Failed to read project configuration");
    return false;
  }
  
  std::string deps_dir = project_config.get_string("dependencies.directory", "deps");
  std::filesystem::path repo_path = project_dir / deps_dir / package_name;
  
  if (std::filesystem::exists(repo_path)) {
    try {
      std::filesystem::remove_all(repo_path);
      if (verbose) {
        logger::print_status("Removed cloned git dependency: " + package_name);
      }
      return true;
    } catch (const std::exception &e) {
      logger::print_error("Failed to remove cloned git dependency: " + std::string(e.what()));
      return false;
    }
  }
  if (verbose) {
    logger::print_warning("Cloned git dependency not found: " + package_name);
  }
  return true;
}

// Helpers to remove a dependency entry from a specific TOML section
static bool remove_dependency_from_section(const std::filesystem::path &config_file,
                                          const std::string &section,
                                          const std::string &package_name,
                                          bool verbose) {
    // Read existing config file
    std::ifstream file(config_file);
    if (!file) {
        logger::print_error("Failed to read configuration file: " + config_file.string());
        return false;
    }
    
    std::vector<std::string> lines;
    std::string line;
    bool in_section = false;
    bool found_dependency = false;
    int line_to_remove = -1;
    
    // Read all lines and find the dependency
    while (std::getline(file, line)) {
        // Check if this is our section
        if (line.find("[" + section + "]") != std::string::npos) {
            in_section = true;
            lines.push_back(line);
            continue;
        }
        
        // Check if we're leaving the section (new section starts)
        if (in_section && !line.empty() && line[0] == '[') {
            in_section = false;
        }
        
        // If we're in the right section, look for the dependency
        if (in_section) {
            // Extract package name from the line
            std::string line_copy = line;
            line_copy = line_copy.substr(0, line_copy.find('=')); // Get everything before =
            line_copy.erase(0, line_copy.find_first_not_of(" \t")); // Trim left
            line_copy.erase(line_copy.find_last_not_of(" \t") + 1); // Trim right
            
            if (line_copy == package_name) {
                found_dependency = true;
                line_to_remove = lines.size();
            }
        }
        
        lines.push_back(line);
    }
    file.close();
    
    if (!found_dependency) {
        logger::print_warning("Dependency '" + package_name + "' not found in section [" + section + "]");
        return false;
    }
    
    // Remove the dependency line
    if (line_to_remove >= 0) {
        lines.erase(lines.begin() + line_to_remove);
        
        // Clean up empty lines around the removed dependency
        if (line_to_remove < lines.size() && lines[line_to_remove].empty() &&
            (line_to_remove == 0 || lines[line_to_remove - 1].empty())) {
            lines.erase(lines.begin() + line_to_remove);
        }
    }
    
    // Write back to file
    std::ofstream outfile(config_file);
    if (!outfile) {
        logger::print_error("Failed to write configuration file: " + config_file.string());
        return false;
    }
    
    bool last_was_empty = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        // Avoid multiple consecutive empty lines
        if (l.empty()) {
            if (!last_was_empty) {
                outfile << l << "\n";
            }
            last_was_empty = true;
        } else {
            outfile << l << "\n";
            last_was_empty = false;
        }
    }
    outfile.close();
    
    if (verbose) {
        logger::print_verbose("Removed dependency '" + package_name + "' from section [" + section + "]");
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
  // Check if package name was provided
  if (ctx->args.arg_count < 1) {
    logger::print_error("Package name not specified");
    logger::print_status("Usage: cforge remove <package>");
    return 1;
  }

  std::string package_name = ctx->args.args[0];
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // Get project directory and config file
  std::filesystem::path project_dir = ctx->working_dir;
  std::filesystem::path config_file = project_dir / CFORGE_FILE;

  if (!std::filesystem::exists(config_file)) {
    logger::print_error("Not a cforge project directory (cforge.toml not found)");
    return 1;
  }

  // Try to remove from both git and vcpkg sections
  bool git_removed = remove_dependency_from_section(config_file, "dependencies.git", package_name, verbose);
  bool vcpkg_removed = remove_dependency_from_section(config_file, "dependencies.vcpkg", package_name, verbose);

  // If it was in git dependencies, also remove the cloned repository
  if (git_removed) {
    if (!remove_git_repo(project_dir, package_name, verbose)) {
      logger::print_warning("Failed to remove git repository for: " + package_name);
    }
  }

  // If it was in vcpkg dependencies, try to remove the package
  if (vcpkg_removed) {
    if (!remove_package_with_vcpkg(project_dir, package_name, verbose)) {
      logger::print_warning("Failed to remove vcpkg package: " + package_name);
    }
  }

  if (!git_removed && !vcpkg_removed) {
    logger::print_error("Failed to remove dependency: " + package_name);
    return 1;
  }

  logger::print_success("Successfully removed dependency: " + package_name);
  return 0;
}