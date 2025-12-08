/**
 * @file command_add.cpp
 * @brief Implementation of the 'add' command to add components to a project
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/registry.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"
#include "core/workspace_utils.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <thread>

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
[[maybe_unused]] static bool add_dependency_to_config(
    const std::filesystem::path & /*project_dir*/,
    const std::filesystem::path &config_file, const std::string &package_name,
    const std::string &package_version, bool verbose) {
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
    logger::print_action("Added", entry);
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
    std::filesystem::path global_dir =
        userprofile ? std::filesystem::path(userprofile) / "vcpkg"
                    : std::filesystem::path();
    std::filesystem::path global_exe = global_dir / "vcpkg.exe";
#else
    const char *home = std::getenv("HOME");
    std::filesystem::path global_dir =
        home ? std::filesystem::path(home) / "vcpkg" : std::filesystem::path();
    std::filesystem::path global_exe = global_dir / "vcpkg";
#endif
    if (!global_dir.empty() && std::filesystem::exists(global_exe)) {
      vcpkg_exe = global_exe;
    } else {
      logger::print_error(
          "vcpkg not found. Checked: " + project_vcpkg_exe.string() + " and " +
          global_exe.string());
      logger::print_action("Run",
                           "cforge vcpkg setup to set up vcpkg integration");
      return false;
    }
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
  logger::installing(package_spec);

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

/**
 * @brief Clone a Git dependency into the project's deps folder
 */
static bool clone_git_repo(const std::string &url,
                           const std::string &target_dir,
                           const std::string &tag, bool verbose) {
  // Ensure we're using HTTPS
  std::string https_url = url;
  // Check if URL starts with git@github.com:
  if (https_url.substr(0, 15) == "git@github.com:") {
    https_url = "https://github.com/" + https_url.substr(15);
  }

  // Check if directory already exists
  if (std::filesystem::exists(target_dir)) {
    logger::print_action("Updating", target_dir);

    // Fetch updates
    std::vector<std::string> fetch_args = {"fetch", "--tags"};
    if (!verbose) {
      fetch_args.push_back("--quiet");
    }

    auto fetch_result = execute_process(
        "git", fetch_args, target_dir,
        [verbose](const std::string &line) {
          if (verbose)
            logger::print_verbose(line);
        },
        [](const std::string &line) { logger::print_error(line); },
        30 // 30 second timeout
    );

    if (!fetch_result.success) {
      logger::print_warning(
          "Failed to fetch updates, continuing with existing version");
    }

    // If a tag is specified, check it out
    if (!tag.empty() && tag != "") {
      logger::print_action("Checking out", "tag " + tag);
      std::string tag_ref = "v" + tag; // Try with 'v' prefix first
      std::vector<std::string> checkout_args = {"checkout", tag_ref};
      if (!verbose) {
        checkout_args.push_back("--quiet");
      }

      auto checkout_result = execute_process(
          "git", checkout_args, target_dir,
          [verbose](const std::string &line) {
            if (verbose)
              logger::print_verbose(line);
          },
          [](const std::string &line) { logger::print_error(line); },
          30 // 30 second timeout
      );

      // If checkout failed with 'v' prefix, try without it
      if (!checkout_result.success) {
        checkout_args[1] = tag; // Use tag without 'v' prefix
        checkout_result = execute_process(
            "git", checkout_args, target_dir,
            [verbose](const std::string &line) {
              if (verbose)
                logger::print_verbose(line);
            },
            [](const std::string &line) { logger::print_error(line); },
            30 // 30 second timeout
        );
      }

      if (!checkout_result.success) {
        logger::print_error("Failed to checkout tag: " + tag);
        return false;
      }
    }

    return true;
  }

  // Create the target directory if it doesn't exist
  try {
    std::filesystem::create_directories(
        std::filesystem::path(target_dir).parent_path());
  } catch (const std::exception &e) {
    logger::print_error("Failed to create target directory: " +
                        std::string(e.what()));
    return false;
  }

  logger::print_action("Cloning", https_url);

  // Build the Git command arguments
  std::vector<std::string> args = {"clone", "--recursive"};
  if (!verbose) {
    args.push_back("--quiet");
  }
  args.push_back(https_url);
  args.push_back(target_dir);

  // If a tag is specified, add --branch flag
  if (!tag.empty() && tag != "") {
    args.push_back("--branch");
    args.push_back("v" + tag); // Try with 'v' prefix first
  }

  // Execute git clone
  auto result = execute_process(
      "git", args, "",
      [verbose](const std::string &line) {
        if (verbose)
          logger::print_verbose(line);
      },
      [](const std::string &line) { logger::print_error(line); },
      120 // 120 second timeout for initial clone
  );

  // If clone failed with 'v' prefix, try without it
  if (!result.success && !tag.empty() && tag != "") {
    args[args.size() - 1] = tag; // Use tag without 'v' prefix
    result = execute_process(
        "git", args, "",
        [verbose](const std::string &line) {
          if (verbose)
            logger::print_verbose(line);
        },
        [](const std::string &line) { logger::print_error(line); },
        120 // 120 second timeout for initial clone
    );
  }

  if (!result.success) {
    logger::print_error("Git clone failed with exit code: " +
                        std::to_string(result.exit_code));
    return false;
  }

  return true;
}

// Helpers to add dependencies to a specific TOML section
static bool add_dependency_to_section(const std::filesystem::path &config_file,
                                      const std::string &section,
                                      const std::string &entry, bool verbose) {
  // Read existing config file
  std::ifstream file(config_file);
  if (!file) {
    logger::print_error("Failed to read configuration file: " +
                        config_file.string());
    return false;
  }

  std::vector<std::string> lines;
  std::string line;
  bool in_section = false;
  bool section_found = false;
  int section_end = -1;

  // Read all lines and find the section
  while (std::getline(file, line)) {
    lines.push_back(line);

    // Check if this is our section
    if (line.find("[" + section + "]") != std::string::npos) {
      in_section = true;
      section_found = true;
      continue;
    }

    // Check if we're leaving the section (new section starts)
    if (in_section && !line.empty() && line[0] == '[') {
      section_end = lines.size() - 1;
      in_section = false;
    }
  }
  file.close();

  // If section not found, create it
  if (!section_found) {
    if (verbose) {
      logger::print_verbose("Creating new section: [" + section + "]");
    }
    lines.push_back(""); // Add blank line before new section
    lines.push_back("[" + section + "]");
    section_end = lines.size();
  }

  // If we never found the end of the section, it's at the end of the file
  if (section_end == -1) {
    section_end = lines.size();
  }

  // Insert the new entry at the end of the section
  lines.insert(lines.begin() + section_end, entry);

  // Write back to file
  std::ofstream outfile(config_file);
  if (!outfile) {
    logger::print_error("Failed to write configuration file: " +
                        config_file.string());
    return false;
  }

  for (const auto &l : lines) {
    outfile << l << "\n";
  }
  outfile.close();

  if (verbose) {
    logger::print_verbose("Added dependency to section [" + section +
                          "]: " + entry);
  }

  return true;
}

static bool add_vcpkg_dependency_to_config(
    const std::filesystem::path & /*project_dir*/,
    const std::filesystem::path &config_file, const std::string &package_name,
    const std::string &package_version, bool verbose) {
  // New consolidated format under [dependencies]
  std::string entry;
  if (package_version.empty()) {
    entry = package_name + " = { source = \"vcpkg\" }";
  } else {
    entry = package_name + " = { source = \"vcpkg\", version = \"" + package_version + "\" }";
  }
  return add_dependency_to_section(config_file, "dependencies", entry, verbose);
}

static bool add_git_dependency_to_config(
    const std::filesystem::path & /*project_dir*/,
    const std::filesystem::path &config_file, const std::string &package_name,
    const std::string &package_url, const std::string &tag, bool verbose) {
  // For git dependencies, we need both name and URL
  if (package_url.empty()) {
    logger::print_error("URL for git dependency not specified");
    return false;
  }

  // New consolidated format under [dependencies]
  std::string entry = package_name + " = { git = \"" + package_url + "\"";
  if (!tag.empty()) {
    entry += ", tag = \"" + tag + "\"";
  }
  entry += " }";
  return add_dependency_to_section(config_file, "dependencies", entry, verbose);
}

static bool add_index_dependency_to_config(
    const std::filesystem::path & /*project_dir*/,
    const std::filesystem::path &config_file, const std::string &package_name,
    const std::string &version, const std::vector<std::string> &features,
    bool header_only, bool verbose) {
  std::string entry;

  // Simple format if no special options
  if (features.empty() && !header_only) {
    entry = package_name + " = \"" + (version.empty() ? "*" : version) + "\"";
  } else {
    // Complex format with options
    entry = package_name + " = { version = \"" + (version.empty() ? "*" : version) + "\"";

    if (!features.empty()) {
      entry += ", features = [";
      for (size_t i = 0; i < features.size(); ++i) {
        if (i > 0) entry += ", ";
        entry += "\"" + features[i] + "\"";
      }
      entry += "]";
    }

    if (header_only) {
      entry += ", header_only = true";
    }

    entry += " }";
  }

  return add_dependency_to_section(config_file, "dependencies", entry, verbose);
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
        "not a cforge project directory (cforge.toml not found)");
    logger::print_action("Run", "cforge init to create a new project");
    return 1;
  }

  // Parse flags and arguments
  bool mode_git = false, mode_vcpkg = false, mode_index = false;
  bool header_only = false;
  std::string tag_value;
  std::vector<std::string> features;
  std::vector<std::string> args;
  for (int i = 0; i < ctx->args.arg_count; ++i)
    args.push_back(ctx->args.args[i]);
  std::vector<std::string> filtered;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--git")
      mode_git = true;
    else if (args[i] == "--vcpkg")
      mode_vcpkg = true;
    else if (args[i] == "--index")
      mode_index = true;
    else if (args[i] == "--header-only")
      header_only = true;
    else if (args[i] == "--tag") {
      if (i + 1 < args.size()) {
        tag_value = args[i + 1];
        ++i; // Skip the next argument since it's the tag value
      } else {
        logger::print_error("--tag flag requires a value");
        return 1;
      }
    } else if (args[i] == "--features" || args[i] == "-f") {
      if (i + 1 < args.size()) {
        // Parse comma-separated features
        std::string feat_str = args[i + 1];
        std::stringstream ss(feat_str);
        std::string feat;
        while (std::getline(ss, feat, ',')) {
          // Trim whitespace
          size_t start = feat.find_first_not_of(" \t");
          size_t end = feat.find_last_not_of(" \t");
          if (start != std::string::npos) {
            features.push_back(feat.substr(start, end - start + 1));
          }
        }
        ++i;
      } else {
        logger::print_error("--features flag requires a value");
        return 1;
      }
    } else
      filtered.push_back(args[i]);
  }
  args.swap(filtered);

  // Count modes
  int mode_count = (mode_git ? 1 : 0) + (mode_vcpkg ? 1 : 0) + (mode_index ? 1 : 0);
  if (mode_count > 1) {
    logger::print_error("Cannot use multiple source flags (--git, --vcpkg, --index)");
    return 1;
  }

  // Default to index mode if no mode specified
  if (mode_count == 0) {
    mode_index = true;
  }

  // Check if package name was provided
  if (args.empty() || args[0].empty() || args[0][0] == '-') {
    logger::print_error("package name not specified");
    logger::print_action("Usage", "cforge add <package>[@version] [options]");
    logger::print_action("From registry", "cforge add fmt@11.1.4");
    logger::print_action("With features", "cforge add spdlog --features async,fmt_external");
    logger::print_action("Git dependency", "cforge add --git mylib https://github.com/user/lib --tag v1.0");
    logger::print_action("vcpkg package", "cforge add openssl --vcpkg");
    return 1;
  }

  // Extract package name and version
  std::string package_name = args[0];
  std::string package_version;
  std::string package_url;

  if (mode_git) {
    if (args.size() < 2) {
      logger::print_error("URL for git dependency not specified");
      logger::print_action("Usage",
                           "cforge add --git <name> <url> [--tag <version>]");
      return 1;
    }
    package_url = args[1];
  } else {
    // Parse name@version format
    size_t at_pos = package_name.find('@');
    if (at_pos != std::string::npos) {
      package_version = package_name.substr(at_pos + 1);
      package_name = package_name.substr(0, at_pos);
    }
  }

  // Check for verbosity
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

  // For index mode, verify the package exists in the registry
  if (mode_index) {
    registry reg;

    // Update index if needed
    if (reg.needs_update()) {
      logger::print_status("Updating package index...");
      reg.update();
    }

    auto pkg = reg.get_package(package_name);
    if (!pkg) {
      logger::print_error("Package '" + package_name + "' not found in registry");
      logger::print_action("Hint", "Run 'cforge search " + package_name + "' to search for packages");
      logger::print_action("Hint", "Use --git or --vcpkg to add from other sources");
      return 1;
    }

    // Resolve version if not specified
    if (package_version.empty()) {
      package_version = reg.resolve_version(package_name, "*");
      if (package_version.empty() && !pkg->versions.empty()) {
        package_version = pkg->versions[0].version;
      }
    } else {
      // Validate version exists
      std::string resolved = reg.resolve_version(package_name, package_version);
      if (resolved.empty()) {
        logger::print_error("Version '" + package_version + "' not found for package '" + package_name + "'");
        logger::print_action("Hint", "Run 'cforge info " + package_name + " --versions' to see available versions");
        return 1;
      }
      package_version = resolved;
    }

    logger::print_action("Found", package_name + " " + package_version);
  }

  // Helper lambda to add dependency to a single project
  auto add_to_project = [&](const std::filesystem::path &proj_dir,
                            const std::filesystem::path &proj_config) -> bool {
    bool cfg_ok = false;
    bool inst_ok = true;

    if (mode_git) {
      // Get the configured dependency directory from cforge.toml
      toml_reader project_config;
      if (!project_config.load(proj_config.string())) {
        logger::print_error("Failed to read project configuration");
        return false;
      }

      std::string deps_dir =
          project_config.get_string("dependencies.directory", "deps");
      std::filesystem::path deps_path = proj_dir / deps_dir / package_name;

      cfg_ok = add_git_dependency_to_config(proj_dir, proj_config, package_name,
                                            package_url, tag_value, verbose);
      inst_ok = clone_git_repo(package_url, deps_path.string(), tag_value, verbose);

    } else if (mode_vcpkg) {
      cfg_ok = add_vcpkg_dependency_to_config(proj_dir, proj_config, package_name,
                                              package_version, verbose);
      inst_ok = install_package_with_vcpkg(proj_dir, package_name,
                                           package_version, verbose);

    } else if (mode_index) {
      // Add from registry - just update config (dependency will be fetched on build)
      cfg_ok = add_index_dependency_to_config(proj_dir, proj_config, package_name,
                                              package_version, features,
                                              header_only, verbose);
      // For index dependencies, we don't install immediately
      // They will be fetched when building
      inst_ok = true;
    }

    if (!cfg_ok) {
      logger::print_error("Failed to update configuration for dependency: " +
                          package_name);
      return false;
    }

    if (!inst_ok) {
      if (mode_vcpkg) {
        logger::print_warning("Dependency '" + package_name +
                              "' added to config, but installation failed. "
                              "Run 'cforge vcpkg setup' then rebuild");
      } else if (mode_git) {
        logger::print_warning("Dependency '" + package_name +
                              "' added to config, but clone failed");
      }
    }

    return true;
  };

  // Apply to each project in workspace or single project
  if (in_workspace_root) {
    bool all_ok = true;
    auto projects = cforge::get_workspace_projects(workspace_root);

    for (const auto &proj : projects) {
      auto proj_dir = workspace_root / proj;
      auto proj_config = proj_dir / CFORGE_FILE;
      if (!std::filesystem::exists(proj_config))
        continue;

      if (!add_to_project(proj_dir, proj_config)) {
        all_ok = false;
      }
    }

    if (!all_ok) {
      logger::print_error("Failed to add dependency to some workspace projects");
      return 1;
    }

    logger::print_action("Added", package_name + " to workspace projects");
    return 0;
  }

  // Single project addition
  if (!add_to_project(project_dir, config_file)) {
    return 1;
  }

  logger::print_action("Added", package_name + " " + package_version);
  return 0;
}