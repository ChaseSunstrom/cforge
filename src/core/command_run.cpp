/**
 * @file command_run.cpp
 * @brief Enhanced implementation of the 'run' command with proper workspace
 * support
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include <set>

using namespace cforge;

/**
 * @brief Determine CMake generator to detect multi-config support
 */
static std::string get_cmake_generator() {
#ifdef _WIN32
  if (is_command_available("ninja", 15)) {
    return "Ninja Multi-Config";
  }
  return "Visual Studio 17 2022";
#else
  return "Unix Makefiles";
#endif
}

/**
 * @brief Get build directory path based on base directory and configuration, respecting multi-config generators
 */
static std::filesystem::path
get_build_dir_for_config(const std::string &base_dir,
                         const std::string &config) {
  std::string generator = get_cmake_generator();
  bool multi = generator.find("Multi-Config") != std::string::npos ||
               generator.find("Visual Studio") != std::string::npos;
  std::filesystem::path build_path(base_dir);
  if (multi || config.empty()) {
    if (!std::filesystem::exists(build_path))
      std::filesystem::create_directories(build_path);
    return build_path;
  }
  // Single-config: append lowercase config
  std::string cfg = config;
  std::transform(cfg.begin(), cfg.end(), cfg.begin(), ::tolower);
  std::string dir = base_dir + "-" + cfg;
  build_path = dir;
  if (!std::filesystem::exists(build_path))
    std::filesystem::create_directories(build_path);
  return build_path;
}

/**
 * @brief Find the executable file for a project
 *
 * @param project_path Path to the project directory
 * @param build_dir Build directory name
 * @param config Build configuration
 * @param project_name Project name
 * @return std::filesystem::path Path to executable, empty if not found
 */
static std::filesystem::path
find_project_executable(const std::filesystem::path &project_path,
                        const std::string &build_dir, const std::string &config,
                        const std::string &project_name) {
  logger::print_verbose("Searching for executable for project: " +
                        project_name);
  logger::print_verbose("Project path: " + project_path.string());
  logger::print_verbose("Build directory: " + build_dir);
  logger::print_verbose("Configuration: " + config);

  // Convert config to lowercase for directory matching
  std::string config_lower = config;
  std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(),
                 ::tolower);

  // Determine actual build base directory (absolute or project-relative)
  std::filesystem::path build_base = build_dir;
  if (!build_base.is_absolute()) {
    build_base = project_path / build_dir;
  }

  // Define common executable locations to search
  std::vector<std::filesystem::path> search_paths = {
      build_base / "bin",
      build_base / "bin" / config,
      build_base / "bin" / config_lower,
      build_base / config,
      build_base / config_lower,
      build_base,
      project_path / "bin",
      project_path / "bin" / config,
      project_path / "bin" / config_lower};

  // Common executable name patterns to try
  std::vector<std::string> executable_patterns = {
      project_name + "_" + config_lower,
      project_name,
      project_name + "_" + config,
      project_name + "_d",       // Debug convention
      project_name + "_debug",   // Debug convention
      project_name + "_release", // Release convention
      project_name + "_r"        // Release convention
  };

#ifdef _WIN32
  // Add .exe extension for Windows
  for (auto &pattern : executable_patterns) {
    pattern += ".exe";
  }
#endif

  // Function to check if a file is a valid executable
  auto is_valid_executable = [](const std::filesystem::path &path) -> bool {
    try {
#ifdef _WIN32
      return path.extension() == ".exe";
#else
      return (std::filesystem::status(path).permissions() &
              std::filesystem::perms::owner_exec) !=
             std::filesystem::perms::none;
#endif
    } catch (const std::exception &ex) {
      logger::print_verbose("Error checking executable permissions: " +
                            std::string(ex.what()));
      return false;
    }
  };

  // Function to check if an executable is likely a project executable (not a
  // CMake/test executable)
  auto is_likely_project_executable =
      [&project_name](const std::filesystem::path &path) -> bool {
    std::string filename = path.filename().string();
    std::string filename_lower = filename;
    std::transform(filename_lower.begin(), filename_lower.end(),
                   filename_lower.begin(), ::tolower);

    // Skip CMake/test executables
    if (filename_lower.find("cmake") != std::string::npos ||
        filename_lower.find("compile") != std::string::npos ||
        filename_lower.find("test") != std::string::npos) {
      return false;
    }

    // Project name should be part of the executable name
    std::string project_name_lower = project_name;
    std::transform(project_name_lower.begin(), project_name_lower.end(),
                   project_name_lower.begin(), ::tolower);

    return filename_lower.find(project_name_lower) != std::string::npos;
  };

  // Search for exact matches first
  for (const auto &search_path : search_paths) {
    if (!std::filesystem::exists(search_path)) {
      continue;
    }

    logger::print_verbose("Searching in: " + search_path.string());

    for (const auto &pattern : executable_patterns) {
      std::filesystem::path exe_path = search_path / pattern;
      if (std::filesystem::exists(exe_path) && is_valid_executable(exe_path)) {
        logger::print_verbose("Found executable: " + exe_path.string());
        return exe_path;
      }
    }
  }

  // If exact match not found, search directories for executables with similar
  // names
  for (const auto &search_path : search_paths) {
    if (!std::filesystem::exists(search_path)) {
      continue;
    }

    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(search_path)) {
        if (!is_valid_executable(entry.path())) {
          continue;
        }

        if (is_likely_project_executable(entry.path())) {
          logger::print_verbose("Found executable: " + entry.path().string());
          return entry.path();
        }
      }
    } catch (const std::exception &ex) {
      logger::print_verbose(
          "Error scanning directory: " + search_path.string() + " - " +
          std::string(ex.what()));
    }
  }

  // Final attempt: recursive search in build directory
  logger::print_status("Performing recursive search for executable in: " +
                       (project_path / build_dir).string());
  try {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
             project_path / build_dir)) {
      if (!is_valid_executable(entry.path())) {
        continue;
      }

      if (is_likely_project_executable(entry.path())) {
        logger::print_verbose("Found executable in recursive search: " +
                              entry.path().string());
        return entry.path();
      }
    }
  } catch (const std::exception &ex) {
    logger::print_verbose("Error in recursive search: " +
                          std::string(ex.what()));
  }

  // List all valid executables found for debugging
  logger::print_error("No matching executable found for project: " +
                      project_name);
  logger::print_status("Listing all executables found:");
  int found_count = 0;

  for (const auto &search_path : search_paths) {
    if (!std::filesystem::exists(search_path)) {
      continue;
    }

    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(search_path)) {
        if (is_valid_executable(entry.path())) {
          logger::print_status("  - " + entry.path().string());
          found_count++;
        }
      }
    } catch (...) {
    }
  }

  if (found_count == 0) {
    logger::print_status("No executables found. The project might not have "
                         "been built correctly.");
  }

  return std::filesystem::path();
}

/**
 * @brief Get build configuration from various sources
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
  if (ctx->args.args) {
    for (int i = 0; i < ctx->args.arg_count; ++i) {
      if (strcmp(ctx->args.args[i], "--config") == 0 ||
          strcmp(ctx->args.args[i], "-c") == 0) {
        if (i + 1 < ctx->args.arg_count) {
          logger::print_verbose(
              "Using build configuration from command line: " +
              std::string(ctx->args.args[i + 1]));
          return std::string(ctx->args.args[i + 1]);
        }
      }
    }
  }

  // Priority 3: Configuration from cforge.toml
  if (project_config) {
    std::string config = project_config->get_string("build.build_type", "");
    if (!config.empty()) {
      logger::print_verbose("Using build configuration from cforge.toml: " +
                            config);
      return config;
    }
  }

  // Priority 4: Default to Release
  logger::print_verbose(
      "No build configuration specified, defaulting to Release");
  return "Release";
}

/**
 * @brief Build a project before running it
 */
static bool build_project_for_run(const std::filesystem::path &project_dir,
                                  const std::string &config, bool verbose) {
  std::string build_cmd = "cmake";
  // Source directory for CMake
  std::filesystem::path source_dir = project_dir;
  // If no CMakeLists.txt in project root, generate from cforge.toml
  std::filesystem::path project_toml = project_dir / CFORGE_FILE;
  if (!std::filesystem::exists(project_dir / "CMakeLists.txt") && std::filesystem::exists(project_toml)) {
    toml::table tbl;
    try {
      tbl = toml::parse_file(project_toml.string());
      toml_reader proj_cfg(tbl);
      if (!generate_cmakelists_from_toml(project_dir, proj_cfg, verbose)) {
        logger::print_error("Failed to generate CMakeLists.txt from cforge.toml");
        return false;
      }
    } catch (...) {
      logger::print_error("Error parsing cforge.toml for automatic CMakeLists generation");
      return false;
    }
  }
  std::filesystem::path build_dir = project_dir / "build";
  // If top-level CMakeLists.txt missing, try build directory
  if (!std::filesystem::exists(project_dir / "CMakeLists.txt")) {
    std::filesystem::path build_cmake = build_dir / "CMakeLists.txt";
    if (std::filesystem::exists(build_cmake)) {
      source_dir = build_dir;
      logger::print_verbose("Using CMakeLists.txt from build directory");
    } else {
      logger::print_error("CMakeLists.txt not found in project or build directory");
      return false;
    }
  }
  // Create build directory if it doesn't exist
  if (!std::filesystem::exists(build_dir)) {
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create build directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // Configure the project
  logger::print_status("Configuring project...");
  std::vector<std::string> config_args = {
      "-S", source_dir.string(),
      "-B", build_dir.string(),
      "-DCMAKE_BUILD_TYPE=" + config
  };

  bool config_success =
      execute_tool(build_cmd, config_args, "", "CMake Configure", verbose);
  if (!config_success) {
    logger::print_error("Failed to configure project");
    return false;
  }

  // Build the project
  logger::print_status("Building project...");
  std::vector<std::string> build_args = {"--build", build_dir.string(),
                                         "--config", config};

  bool build_success =
      execute_tool(build_cmd, build_args, "", "CMake Build", verbose);
  if (!build_success) {
    return false;
  }

  logger::print_success("Project built successfully");
  return true;
}

cforge_int_t cforge_cmd_run(const cforge_context_t *ctx) {
  try {
    // Determine project directory
    std::filesystem::path project_dir = ctx->working_dir;

    // Parse common parameters first
    // Get the build configuration
    std::string config = "Debug"; // Default is Debug instead of Release
    if (ctx->args.config && strlen(ctx->args.config) > 0) {
      config = ctx->args.config;
    }

    // Check for --config or -c flag
    if (ctx->args.args) {
      for (int i = 0; i < ctx->args.arg_count; ++i) {
        std::string arg = ctx->args.args[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < ctx->args.arg_count) {
          config = ctx->args.args[i + 1];
          break;
        }
      }
    }

    // Check verbosity
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;

    // Check if build should be skipped
    bool skip_build = false;
    if (ctx->args.args) {
      for (int i = 0; i < ctx->args.arg_count; ++i) {
        if (strcmp(ctx->args.args[i], "--no-build") == 0) {
          skip_build = true;
          break;
        }
      }
    }

    // Check for specific project
    std::string specific_project;
    if (ctx->args.project) {
      specific_project = ctx->args.project;
    } else if (ctx->args.args) {
      for (int i = 0; i < ctx->args.arg_count; ++i) {
        if ((strcmp(ctx->args.args[i], "--project") == 0 ||
             strcmp(ctx->args.args[i], "-p") == 0) &&
            i + 1 < ctx->args.arg_count) {
          specific_project = ctx->args.args[i + 1];
          break;
        }
      }
    }

    // Get extra arguments to pass to the executable
    std::vector<std::string> extra_args;
    bool found_dash_dash = false;

    if (ctx->args.args) {
      for (int i = 0; i < ctx->args.arg_count; ++i) {
        // Check for delimiter between command args and app args
        if (strcmp(ctx->args.args[i], "--") == 0) {
          found_dash_dash = true;
          continue;
        }

        if (found_dash_dash) {
          extra_args.push_back(ctx->args.args[i]);
        }
      }
    }

    // Check if this is a workspace or standalone project
    std::filesystem::path workspace_file = project_dir / WORKSPACE_FILE;
    bool is_workspace = std::filesystem::exists(workspace_file);

    // Handle workspace: run only the startup projects marked in workspace.toml
    if (is_workspace) {
      logger::print_status("Running in workspace context: " + project_dir.string());
      
      // Ensure workspace CMakeLists.txt exists (generate if needed)
      std::filesystem::path ws_cmake = project_dir / "CMakeLists.txt";
      if (!std::filesystem::exists(ws_cmake)) {
        logger::print_status("Generating workspace CMakeLists.txt for run");
        toml_reader ws_cfg(toml::parse_file((project_dir / WORKSPACE_FILE).string()));
        if (!generate_workspace_cmakelists(project_dir, ws_cfg, verbose)) {
          logger::print_error("Failed to generate workspace CMakeLists.txt");
          return 1;
        }
      }
      
      // Determine workspace-level build directory
      std::filesystem::path ws_build_base = project_dir / DEFAULT_BUILD_DIR;
      std::filesystem::path ws_build_dir = get_build_dir_for_config(ws_build_base.string(), config);
      logger::print_verbose("Using workspace build directory: " + ws_build_dir.string());
      
      // Build workspace if needed
      bool need_build = !skip_build;
      // If user skipped build but config not built, build anyway
      if (skip_build && !std::filesystem::exists(ws_build_dir / "CMakeCache.txt")) {
        need_build = true;
        logger::print_status("Workspace build not found for config '" + config + "', configuring and building workspace");
      }
      if (need_build) {
        // Prepare context for build
        cforge_context_t build_ctx;
        memset(&build_ctx, 0, sizeof(build_ctx));
        strncpy(build_ctx.working_dir, ctx->working_dir, sizeof(build_ctx.working_dir) - 1);
        build_ctx.working_dir[sizeof(build_ctx.working_dir) - 1] = '\0';
        build_ctx.args.command = strdup("build");
        build_ctx.args.config = strdup(config.c_str());
        if (verbose) {
          build_ctx.args.verbosity = strdup("verbose");
        }
        int build_res = cforge_cmd_build(&build_ctx);
        free((void*)build_ctx.args.command);
        free((void*)build_ctx.args.config);
        if (build_ctx.args.verbosity) free((void*)build_ctx.args.verbosity);
        if (build_res != 0) {
          logger::print_error("Workspace build failed");
          return build_res;
        }
      } else {
        logger::print_status("Skipping workspace build as requested");
      }
      
      toml_reader workspace_config(toml::parse_file(workspace_file.string()));
      if (config.empty()) {
        config = workspace_config.get_string("workspace.build_type", "Debug");
      }
      // Collect startup projects
      std::vector<std::string> entries = workspace_config.get_string_array("workspace.projects");
      std::vector<std::string> to_run;
      for (const auto &e : entries) {
        size_t p1 = e.find(':'); size_t p2 = e.find(':', p1+1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;
        std::string name = e.substr(0, p1);
        std::string flag = e.substr(p2+1);
        std::transform(flag.begin(), flag.end(), flag.begin(), ::tolower);
        if (flag == "true") to_run.push_back(name);
      }
      if (to_run.empty()) {
        std::string main = workspace_config.get_string("workspace.main_project", "");
        if (!main.empty()) to_run.push_back(main);
      }
      if (to_run.empty()) {
        logger::print_error("No startup projects defined in workspace.toml");
        return 1;
      }
      // Run each
      std::set<std::string> built;
      auto stdout_cb = [](const std::string &c){ std::cout << c << std::flush; };
      auto stderr_cb = [](const std::string &c){ std::cerr << c << std::flush; };
      for (const auto &proj_name : to_run) {
        auto proj_path = project_dir / proj_name;
        if (!std::filesystem::exists(proj_path / CFORGE_FILE)) {
          logger::print_warning("Skipping missing project: " + proj_name);
          continue;
        }
        logger::print_status("Running project: " + proj_name);
        toml_reader pconf(toml::parse_file((proj_path / CFORGE_FILE).string()));
        if (pconf.get_string("project.binary_type", "executable") != "executable") {
          continue;
        }
        std::string real = pconf.get_string("project.name", proj_name);
        // Look for executable in shared workspace build directory
        auto exe = find_project_executable(proj_path, ws_build_dir.string(), config, real);
        if (exe.empty()) {
          logger::print_error("Executable not found: " + proj_name);
          continue;
        }
        logger::print_status("Running executable: " + exe.string());
        // Display program output header
        logger::print_status("Program Output\n────────────");
        auto res = execute_process(exe.string(), extra_args, proj_path.string(), stdout_cb, stderr_cb, 0);
        std::cout << std::endl;
        if (!res.success)
          logger::print_error("Exited " + std::to_string(res.exit_code) + ": " + proj_name);
        else
          logger::print_success("Exited successfully: " + proj_name);
      }
      return 0;
    } else {
      // Handle single project run
      logger::print_status("Running in single project context");

      // Check if this is a valid cforge project
      std::filesystem::path config_path = project_dir / CFORGE_FILE;
      if (!std::filesystem::exists(config_path)) {
        logger::print_error("Not a valid cforge project (missing " +
                            std::string(CFORGE_FILE) + ")");
        return 1;
      }

      // Load project configuration
      toml::table config_table;
      try {
        config_table = toml::parse_file(config_path.string());
      } catch (const toml::parse_error &e) {
        logger::print_error("Failed to parse " + std::string(CFORGE_FILE) +
                            ": " + std::string(e.what()));
        return 1;
      }

      toml_reader project_config(config_table);

      // Get project name
      std::string project_name = project_config.get_string("project.name", "");
      if (project_name.empty()) {
        project_name = std::filesystem::path(project_dir).filename().string();
      }

      // Log project info
      logger::print_status("Project: " + project_name);
      logger::print_status("Configuration: " + config);

      // Check binary type
      std::string binary_type =
          project_config.get_string("project.binary_type", "executable");
      if (binary_type != "executable") {
        logger::print_error("Project is not an executable (binary_type is '" +
                            binary_type + "')");
        return 1;
      }

      // Determine build directory
      std::string build_dir_name =
          project_config.get_string("build.build_dir", "build");

      // Build the project if needed
      if (!skip_build) {
        if (!build_project_for_run(project_dir, config, verbose)) {
          logger::print_error("Failed to build project");
          return 1;
        }
      } else {
        logger::print_status("Skipping build step as requested");
      }

      // Find the executable
      std::filesystem::path executable = find_project_executable(
          project_dir, build_dir_name, config, project_name);

      if (executable.empty()) {
        logger::print_error("Executable not found for project: " +
                            project_name);
        return 1;
      }

      logger::print_status("Running executable: " + executable.string());

      // Display program output header
      logger::print_status("Program Output\n────────────");

      // Create callbacks to display raw program output
      std::function<void(const std::string &)> stdout_callback =
          [](const std::string &chunk) { std::cout << chunk << std::flush; };

      std::function<void(const std::string &)> stderr_callback =
          [](const std::string &chunk) { std::cerr << chunk << std::flush; };

      // Execute the program with output handling
      process_result result =
          execute_process(executable.string(), extra_args, project_dir.string(),
                          stdout_callback, stderr_callback,
                          0 // No timeout
          );

      // Add a separator line after program output
      std::cout << std::endl;

      if (result.success) {
        logger::print_success("Program exited with code 0");
        return 0;
      } else {
        logger::print_error("Program exited with code: " +
                            std::to_string(result.exit_code));
        return result.exit_code;
      }
    }
  } catch (const std::exception &ex) {
    logger::print_error("Exception: " + std::string(ex.what()));
    return 1;
  } catch (...) {
    logger::print_error("Unknown exception occurred");
    return 1;
  }

  return 0;
}