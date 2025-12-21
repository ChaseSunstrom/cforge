/**
 * @file command_run.cpp
 * @brief Enhanced implementation of the 'run' command with proper workspace
 * support
 */

#include "cforge/log.hpp"
#include "core/build_utils.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/error_format.hpp"
#include "core/file_system.h"
#include "core/platform.hpp"
#include "core/process_utils.hpp"
#include "core/script_runner.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
#include "core/workspace.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// Note: get_cmake_generator() and get_build_dir_for_config() are now in
// build_utils.hpp

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
  cforge::logger::print_verbose("Searching for executable for project: " +
                        project_name);
  cforge::logger::print_verbose("Project path: " + project_path.string());
  cforge::logger::print_verbose("Build directory: " + build_dir);
  cforge::logger::print_verbose("Configuration: " + config);

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
      cforge::logger::print_verbose("Error checking executable permissions: " +
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

    cforge::logger::print_verbose("Searching in: " + search_path.string());

    for (const auto &pattern : executable_patterns) {
      std::filesystem::path exe_path = search_path / pattern;
      if (std::filesystem::exists(exe_path) && is_valid_executable(exe_path)) {
        cforge::logger::print_verbose("Found executable: " + exe_path.string());
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
          cforge::logger::print_verbose("Found executable: " + entry.path().string());
          return entry.path();
        }
      }
    } catch (const std::exception &ex) {
      cforge::logger::print_verbose(
          "Error scanning directory: " + search_path.string() + " - " +
          std::string(ex.what()));
    }
  }

  // Final attempt: recursive search in build directory
  cforge::logger::print_action("Searching", (project_path / build_dir).string());
  try {
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
             project_path / build_dir)) {
      if (!is_valid_executable(entry.path())) {
        continue;
      }

      if (is_likely_project_executable(entry.path())) {
        cforge::logger::print_verbose("Found executable in recursive search: " +
                              entry.path().string());
        return entry.path();
      }
    }
  } catch (const std::exception &ex) {
    cforge::logger::print_verbose("Error in recursive search: " +
                          std::string(ex.what()));
  }

  // List all valid executables found for debugging
  cforge::logger::print_error("no matching executable found for project: " +
                      project_name);
  cforge::logger::print_action("Listing", "all executables found");
  cforge_int_t found_count = 0;

  for (const auto &search_path : search_paths) {
    if (!std::filesystem::exists(search_path)) {
      continue;
    }

    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(search_path)) {
        if (is_valid_executable(entry.path())) {
          cforge::logger::print_action("", "  - " + entry.path().string());
          found_count++;
        }
      }
    } catch (...) {
    }
  }

  if (found_count == 0) {
    cforge::logger::print_action(
        "Info",
        "no executables found, project might not have been built correctly");
  }

  return std::filesystem::path();
}

/**
 * @brief Build a project before running it (with smart rebuild detection)
 *
 * Uses the smart rebuild pipeline:
 * 1. Check if CMakeLists.txt needs regeneration (cforge.toml changed)
 * 2. Check if CMake needs reconfiguration (CMakeCache.txt stale)
 * 3. Only regenerate/reconfigure when necessary
 * 4. Always run build (CMake handles incremental builds)
 */
static bool build_project_for_run(const std::filesystem::path &project_dir,
                                  const std::string &config, bool verbose) {
  // Determine build directory
  std::filesystem::path build_dir = cforge::get_build_dir_for_config(
      (project_dir / "build").string(), config);

  // Use smart rebuild detection to prepare the project
  cforge::build_preparation_result prep_result = cforge::prepare_project_for_build(
      project_dir, build_dir, config, verbose);

  if (!prep_result.success) {
    cforge::logger::print_error(prep_result.error_message);
    return false;
  }

  // Log what happened during preparation
  if (prep_result.cmakelists_regenerated) {
    cforge::logger::print_verbose("CMakeLists.txt was regenerated");
  }
  if (prep_result.cmake_reconfigured) {
    cforge::logger::print_verbose("CMake was reconfigured");
  }

  // Always run build (CMake handles incremental builds efficiently)
  cforge::logger::building(project_dir.filename().string());

  if (!cforge::run_cmake_build(build_dir, config, "", 0, verbose)) {
    return false;
  }

  cforge::logger::finished(config);
  return true;
}

// Spawn a command in a new terminal window across platforms
static bool spawn_in_terminal(const std::string &cmd) {
  if constexpr (cforge::platform::is_windows) {
    // Use start to open a new Command Prompt
    std::string winCmd = "start \"CForge Run\" cmd /k \"" + cmd + "\"";
    return std::system(winCmd.c_str()) == 0;
  } else if constexpr (cforge::platform::is_macos) {
    // Use AppleScript to open a new Terminal window
    std::string osa =
        "osascript -e 'tell application \"Terminal\" to do script \"" + cmd +
        "\"'";
    return std::system(osa.c_str()) == 0;
  } else {
    // Linux: Try multiple terminal emulators in order of preference
    auto terminals = cforge::platform::get_linux_terminals();

    for (const auto &terminal : terminals) {
      if (cforge::is_command_available(terminal, 3)) {
        cforge::logger::print_verbose("Using terminal emulator: " + terminal);

        std::string termCmd;
        // Different terminals have different argument formats
        if (terminal == "gnome-terminal" || terminal == "mate-terminal") {
          termCmd = terminal + " -- " + cmd + " &";
        } else if (terminal == "konsole") {
          termCmd = terminal + " -e " + cmd + " &";
        } else if (terminal == "alacritty" || terminal == "kitty") {
          termCmd = terminal + " -e " + cmd + " &";
        } else {
          // Default format works for most terminals
          termCmd = terminal + " -e '" + cmd + "' &";
        }

        cforge_int_t result = std::system(termCmd.c_str());
        if (result == 0) {
          return true;
        }
        // If this terminal failed, try the next one
        cforge::logger::print_verbose("Terminal " + terminal + " failed, trying next");
      }
    }

    // All terminals failed
    cforge::logger::print_warning("No suitable terminal emulator found");
    return false;
  }
}

cforge_int_t cforge_cmd_run(const cforge_context_t *ctx) {
  try {
    // Determine project directory
    std::filesystem::path project_dir =
        std::filesystem::absolute(ctx->working_dir);

    // Parse common parameters first
    // Get the build configuration
    std::string config = "Debug"; // Default is Debug instead of Release
    if (ctx->args.config && strlen(ctx->args.config) > 0) {
      config = ctx->args.config;
    }

    // Check for --config or -c flag
    if (ctx->args.args) {
      for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
        std::string arg = ctx->args.args[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < ctx->args.arg_count) {
          config = ctx->args.args[i + 1];
          break;
        }
      }
    }

    // Check verbosity
    bool verbose = cforge::logger::get_verbosity() == cforge::log_verbosity::VERBOSITY_VERBOSE;

    // Check if build should be skipped
    bool skip_build = false;
    if (ctx->args.args) {
      for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
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
      for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
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
      for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
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

    // Check if we're in a workspace (may be in a subdirectory)
    auto [is_workspace, workspace_root] = cforge::is_in_workspace(project_dir);

    // Handle workspace-run only when at the workspace root; subprojects fall
    // through to single-run
    if (is_workspace && project_dir == workspace_root) {
      cforge::logger::print_action("Running",
                           "in workspace context: " + project_dir.string());

      // Ensure workspace CMakeLists.txt exists (generate if needed)
      std::filesystem::path ws_cmake = project_dir / "CMakeLists.txt";
      if (!std::filesystem::exists(ws_cmake)) {
        cforge::logger::print_verbose("Generating workspace CMakeLists.txt for run");
        cforge::toml_reader ws_cfg(
            toml::parse_file((project_dir / WORKSPACE_FILE).string()));
        if (!cforge::generate_workspace_cmakelists(project_dir, ws_cfg, verbose)) {
          cforge::logger::print_error("failed to generate workspace CMakeLists.txt");
          return 1;
        }
      }

      // Determine workspace-level build directory
      std::filesystem::path ws_build_base = project_dir / DEFAULT_BUILD_DIR;
      std::filesystem::path ws_build_dir =
          cforge::get_build_dir_for_config(ws_build_base.string(), config);
      cforge::logger::print_verbose("Using workspace build directory: " +
                            ws_build_dir.string());

      // Build workspace if needed
      bool need_build = !skip_build;
      // If user skipped build but config not built, build anyway
      if (skip_build &&
          !std::filesystem::exists(ws_build_dir / "CMakeCache.txt")) {
        need_build = true;
        cforge::logger::print_action("Info",
                             "workspace build not found for config '" + config +
                                 "', configuring and building workspace");
      }
      if (need_build) {
        // Prepare context for build
        cforge_context_t build_ctx;
        memset(&build_ctx, 0, sizeof(build_ctx));
        snprintf(build_ctx.working_dir, sizeof(build_ctx.working_dir), "%s",
                 ctx->working_dir);
        build_ctx.args.command = strdup("build");
        build_ctx.args.config = strdup(config.c_str());
        if (verbose) {
          build_ctx.args.verbosity = strdup("verbose");
        }
        cforge_int_t build_res = cforge_cmd_build(&build_ctx);
        free((void *)build_ctx.args.command);
        free((void *)build_ctx.args.config);
        if (build_ctx.args.verbosity)
          free((void *)build_ctx.args.verbosity);
        if (build_res != 0) {
          cforge::logger::print_error("workspace build failed");
          return build_res;
        }
      } else {
        cforge::logger::print_action("Skipping", "workspace build as requested");
      }
      std::filesystem::path workspace_file = project_dir / WORKSPACE_FILE;
      cforge::toml_reader workspace_config(toml::parse_file(workspace_file.string()));
      if (config.empty()) {
        config = workspace_config.get_string("workspace.build_type", "Debug");
      }
      // Determine startup projects using workspace API
      cforge::workspace ws;
      ws.load(workspace_root);
      // Collect all projects flagged as startup
      std::vector<cforge::workspace_project> projects = ws.get_projects();
      std::vector<std::string> to_run;
      for (const auto &proj : projects) {
        if (proj.is_startup) {
          to_run.push_back(proj.name);
        }
      }
      // Fallback to main_project if none marked
      if (to_run.empty()) {
        cforge::workspace_project main_proj = ws.get_startup_project();
        if (!main_proj.name.empty()) {
          to_run.push_back(main_proj.name);
        }
      }
      if (to_run.empty()) {
        cforge::logger::print_error("no startup project set in workspace");
        return 1;
      }
      // Run each startup project
      bool overall_success = true;
      for (const auto &proj_name : to_run) {
        // Determine project directory
        std::filesystem::path proj_path = project_dir / proj_name;
        if (!std::filesystem::exists(proj_path / CFORGE_FILE)) {
          cforge::logger::print_warning("skipping missing project: " + proj_name);
          overall_success = false;
          continue;
        }
        // Load project config to get real name
        cforge::toml_reader pconf(toml::parse_file((proj_path / CFORGE_FILE).string()));
        std::string real_name = pconf.get_string("project.name", proj_name);
        // Find executable
        auto exe = find_project_executable(proj_path, ws_build_dir.string(),
                                           config, real_name);
        if (exe.empty()) {
          cforge::logger::print_error("executable not found: " + proj_name);
          overall_success = false;
          continue;
        }
        // Build command line
        std::ostringstream oss;
        // Quote the executable path to handle spaces
        oss << "\"" << exe.string() << "\"";
        for (const auto &arg : extra_args) {
          oss << " " << arg;
        }
        // Spawn in new terminal
        if (!spawn_in_terminal(oss.str())) {
          cforge::logger::print_error("failed to spawn terminal for: " + proj_name);
          overall_success = false;
        }
      }
      return overall_success ? 0 : 1;
    } else {
      // Handle single project run
      cforge::logger::print_action("Running", "in single project context");

      // Check if this is a valid cforge project
      std::filesystem::path config_path = project_dir / CFORGE_FILE;
      if (!std::filesystem::exists(config_path)) {
        cforge::logger::print_error("not a valid cforge project (missing " +
                            std::string(CFORGE_FILE) + ")");
        return 1;
      }

      // Load project configuration
      toml::table config_table;
      try {
        config_table = toml::parse_file(config_path.string());
      } catch (const toml::parse_error &e) {
        cforge::logger::print_error("failed to parse " + std::string(CFORGE_FILE) +
                            ": " + std::string(e.what()));
        return 1;
      }

      cforge::toml_reader project_config(config_table);

      // Get project name
      std::string project_name = project_config.get_string("project.name", "");
      if (project_name.empty()) {
        project_name = std::filesystem::path(project_dir).filename().string();
      }

      // Log project info
      cforge::logger::print_action("Project", project_name);
      cforge::logger::print_action("Configuration", config);

      // Check binary type
      std::string binary_type =
          project_config.get_string("project.binary_type", "executable");
      if (binary_type != "executable") {
        cforge::logger::print_error("project is not an executable (binary_type is '" +
                            binary_type + "')");
        return 1;
      }

      // Determine build directory
      std::string build_dir_name =
          project_config.get_string("build.build_dir", "build");

      // Build the project if needed
      if (!skip_build) {
        if (!build_project_for_run(project_dir, config, verbose)) {
          cforge::logger::print_error("failed to build project");
          return 1;
        }
      } else {
        cforge::logger::print_action("Skipping", "build step as requested");
      }

      // Find the executable
      std::filesystem::path executable = find_project_executable(
          project_dir, build_dir_name, config, project_name);

      if (executable.empty()) {
        cforge::logger::print_error("executable not found for project: " +
                            project_name);
        return 1;
      }

      cforge::logger::running(executable.string());

      // Display program output header
      cforge::logger::print_action("", "Program Output\n");

      // Capture stderr for runtime error formatting
      std::string captured_stderr;

      // Create callbacks to display raw program output
      std::function<void(const std::string &)> stdout_callback =
          [](const std::string &chunk) { std::cout << chunk << std::flush; };

      std::function<void(const std::string &)> stderr_callback =
          [&captured_stderr](const std::string &chunk) {
            std::cerr << chunk << std::flush;
            captured_stderr += chunk;
          };

      // Execute the program with output handling
      cforge::process_result result =
          cforge::execute_process(executable.string(), extra_args, project_dir.string(),
                          stdout_callback, stderr_callback,
                          0 // No timeout
          );

      // Add a separator line after program output
      std::cout << std::endl;

      if (result.success) {
        cforge::logger::finished(config);
        return 0;
      } else {
        // Combine stdout and stderr for error analysis
        std::string combined_output = result.stdout_output + "\n" +
                                      result.stderr_output + "\n" +
                                      captured_stderr;

        // Try to format any runtime errors
        std::string formatted = cforge::format_build_errors(combined_output);
        if (!formatted.empty()) {
          std::cout << "\n";
          std::cout << formatted;
        }

        cforge::logger::print_error("program exited with code: " +
                            std::to_string(result.exit_code));
        return result.exit_code;
      }
    }
  } catch (const std::exception &ex) {
    cforge::logger::print_error("exception: " + std::string(ex.what()));
    return 1;
  } catch (...) {
    cforge::logger::print_error("unknown exception occurred");
    return 1;
  }

  return 0;
}