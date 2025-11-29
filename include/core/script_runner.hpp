/**
 * @file script_runner.hpp
 * @brief Consolidated script execution for pre/post build hooks
 */

#pragma once

#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "cforge/log.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Script execution phases
 */
enum class script_phase {
  PRE_BUILD,
  POST_BUILD,
  PRE_TEST,
  POST_TEST,
  PRE_RUN,
  POST_RUN,
  PRE_CLEAN,
  POST_CLEAN,
  PRE_INSTALL,
  POST_INSTALL
};

/**
 * @brief Convert script phase to TOML key
 */
inline std::string phase_to_key(script_phase phase) {
  switch (phase) {
  case script_phase::PRE_BUILD:
    return "scripts.pre_build";
  case script_phase::POST_BUILD:
    return "scripts.post_build";
  case script_phase::PRE_TEST:
    return "scripts.pre_test";
  case script_phase::POST_TEST:
    return "scripts.post_test";
  case script_phase::PRE_RUN:
    return "scripts.pre_run";
  case script_phase::POST_RUN:
    return "scripts.post_run";
  case script_phase::PRE_CLEAN:
    return "scripts.pre_clean";
  case script_phase::POST_CLEAN:
    return "scripts.post_clean";
  case script_phase::PRE_INSTALL:
    return "scripts.pre_install";
  case script_phase::POST_INSTALL:
    return "scripts.post_install";
  default:
    return "";
  }
}

/**
 * @brief Convert script phase to human-readable name
 */
inline std::string phase_to_name(script_phase phase) {
  switch (phase) {
  case script_phase::PRE_BUILD:
    return "pre-build";
  case script_phase::POST_BUILD:
    return "post-build";
  case script_phase::PRE_TEST:
    return "pre-test";
  case script_phase::POST_TEST:
    return "post-test";
  case script_phase::PRE_RUN:
    return "pre-run";
  case script_phase::POST_RUN:
    return "post-run";
  case script_phase::PRE_CLEAN:
    return "pre-clean";
  case script_phase::POST_CLEAN:
    return "post-clean";
  case script_phase::PRE_INSTALL:
    return "pre-install";
  case script_phase::POST_INSTALL:
    return "post-install";
  default:
    return "unknown";
  }
}

/**
 * @brief Determine script interpreter based on file extension
 *
 * @param script_path Path to the script
 * @return Interpreter command (python, bash, etc.) or empty for native executables
 */
inline std::string get_script_interpreter(const std::filesystem::path &script_path) {
  std::string ext = script_path.extension().string();

  // Convert to lowercase for comparison
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".py") {
    return "python";
  } else if (ext == ".sh") {
#ifdef _WIN32
    return "bash"; // Requires Git Bash or WSL
#else
    return "bash";
#endif
  } else if (ext == ".bat" || ext == ".cmd") {
#ifdef _WIN32
    return "cmd /c";
#else
    return ""; // Can't run batch files on Unix
#endif
  } else if (ext == ".ps1") {
#ifdef _WIN32
    return "powershell -ExecutionPolicy Bypass -File";
#else
    return "pwsh"; // PowerShell Core on Unix
#endif
  }

  // No interpreter needed (native executable or unknown)
  return "";
}

/**
 * @brief Execute a single script
 *
 * @param script_path Path to the script (relative or absolute)
 * @param working_dir Working directory for execution
 * @param phase Script phase (for logging)
 * @param verbose Verbose output
 * @param timeout Timeout in seconds
 * @return true if script executed successfully
 */
inline bool execute_script(const std::filesystem::path &script_path,
                           const std::filesystem::path &working_dir,
                           script_phase phase, bool verbose = false,
                           int timeout = 300) {
  std::filesystem::path full_path = script_path;

  // Make path absolute if relative
  if (script_path.is_relative()) {
    full_path = working_dir / script_path;
  }

  // Check if script exists
  if (!std::filesystem::exists(full_path)) {
    logger::print_error("Script not found: " + full_path.string());
    return false;
  }

  std::string phase_name = phase_to_name(phase);
  logger::print_status("Running " + phase_name + " script: " +
                       script_path.string());

  std::string interpreter = get_script_interpreter(full_path);
  std::string command;
  std::vector<std::string> args;

  if (interpreter.empty()) {
    // Direct execution
    command = full_path.string();
  } else if (interpreter.find(' ') != std::string::npos) {
    // Interpreter with arguments (e.g., "cmd /c" or "powershell -File")
    size_t space_pos = interpreter.find(' ');
    command = interpreter.substr(0, space_pos);
    std::string interp_args = interpreter.substr(space_pos + 1);

    // Split interpreter arguments
    std::istringstream iss(interp_args);
    std::string arg;
    while (iss >> arg) {
      args.push_back(arg);
    }
    args.push_back(full_path.string());
  } else {
    // Simple interpreter
    command = interpreter;
    args.push_back(full_path.string());
  }

  bool success = execute_tool(command, args, working_dir.string(),
                              phase_name + " script", verbose, timeout);

  if (!success) {
    logger::print_error(phase_name + " script failed: " + script_path.string());
  }

  return success;
}

/**
 * @brief Run all scripts for a given phase from a config file
 *
 * @param config_path Path to the TOML config file (cforge.toml or cforge.workspace.toml)
 * @param working_dir Working directory for script execution
 * @param phase Script phase to run
 * @param verbose Verbose output
 * @return true if all scripts succeeded (or no scripts defined)
 */
inline bool run_phase_scripts(const std::filesystem::path &config_path,
                              const std::filesystem::path &working_dir,
                              script_phase phase, bool verbose = false) {
  toml_reader config;
  if (!config.load(config_path.string())) {
    // Config file not found or invalid - not an error, just no scripts
    return true;
  }

  std::string key = phase_to_key(phase);
  if (!config.has_key(key)) {
    // No scripts defined for this phase
    return true;
  }

  std::vector<std::string> scripts = config.get_string_array(key);
  if (scripts.empty()) {
    return true;
  }

  for (const auto &script : scripts) {
    if (!execute_script(script, working_dir, phase, verbose)) {
      return false;
    }
  }

  return true;
}

/**
 * @brief Run pre-build scripts for a project or workspace
 *
 * @param project_dir Project or workspace directory
 * @param is_workspace True if this is a workspace
 * @param verbose Verbose output
 * @return true if all scripts succeeded
 */
inline bool run_pre_build_scripts(const std::filesystem::path &project_dir,
                                  bool is_workspace, bool verbose = false) {
  std::filesystem::path config_path =
      project_dir / (is_workspace ? WORKSPACE_FILE : CFORGE_FILE);
  return run_phase_scripts(config_path, project_dir, script_phase::PRE_BUILD,
                           verbose);
}

/**
 * @brief Run post-build scripts for a project or workspace
 *
 * @param project_dir Project or workspace directory
 * @param is_workspace True if this is a workspace
 * @param verbose Verbose output
 * @return true if all scripts succeeded
 */
inline bool run_post_build_scripts(const std::filesystem::path &project_dir,
                                   bool is_workspace, bool verbose = false) {
  std::filesystem::path config_path =
      project_dir / (is_workspace ? WORKSPACE_FILE : CFORGE_FILE);
  return run_phase_scripts(config_path, project_dir, script_phase::POST_BUILD,
                           verbose);
}

/**
 * @brief Run pre-test scripts for a project
 *
 * @param project_dir Project directory
 * @param verbose Verbose output
 * @return true if all scripts succeeded
 */
inline bool run_pre_test_scripts(const std::filesystem::path &project_dir,
                                 bool verbose = false) {
  std::filesystem::path config_path = project_dir / CFORGE_FILE;
  return run_phase_scripts(config_path, project_dir, script_phase::PRE_TEST,
                           verbose);
}

/**
 * @brief Run post-test scripts for a project
 *
 * @param project_dir Project directory
 * @param verbose Verbose output
 * @return true if all scripts succeeded
 */
inline bool run_post_test_scripts(const std::filesystem::path &project_dir,
                                  bool verbose = false) {
  std::filesystem::path config_path = project_dir / CFORGE_FILE;
  return run_phase_scripts(config_path, project_dir, script_phase::POST_TEST,
                           verbose);
}

} // namespace cforge
