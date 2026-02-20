/**
 * @file command_flash.cpp
 * @brief Implementation of the 'flash' command for uploading firmware to embedded targets
 */

#include "cforge/log.hpp"
#include "core/build_utils.hpp"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
#include "core/workspace.hpp"

#include <filesystem>
#include <string>
#include <vector>

cforge_int_t cforge_cmd_flash(const cforge_context_t *ctx) {
  // Check for help flag first
  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-h" || arg == "--help") {
      cforge::command_registry::instance().print_command_help("flash");
      return 0;
    }
  }

  std::filesystem::path current_dir = std::filesystem::path(ctx->working_dir);

  // Parse command line arguments
  std::string cross_profile;
  std::string config_name;
  bool verbose = cforge::logger::get_verbosity() ==
                 cforge::log_verbosity::VERBOSITY_VERBOSE;
  cforge_int_t num_jobs = 0;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];

    if (arg == "--profile" || arg == "-P") {
      if (i + 1 < ctx->args.arg_count) {
        cross_profile = ctx->args.args[++i];
      }
    } else if (arg.substr(0, 10) == "--profile=") {
      cross_profile = arg.substr(10);
    } else if (arg == "-c" || arg == "--config") {
      if (i + 1 < ctx->args.arg_count) {
        config_name = ctx->args.args[++i];
      }
    } else if (arg.substr(0, 9) == "--config=") {
      config_name = arg.substr(9);
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-j" || arg == "--jobs") {
      if (i + 1 < ctx->args.arg_count) {
        try {
          num_jobs = std::stoi(ctx->args.args[++i]);
        } catch (...) {
        }
      }
    }
  }

  // Profile is required for flash
  if (cross_profile.empty()) {
    cforge::logger::print_error(
        "No cross-compilation profile specified. Use --profile <name>");
    cforge::logger::print_plain(
        "  Example: cforge flash --profile avr");
    return 1;
  }

  // Load project configuration
  std::filesystem::path toml_path = current_dir / CFORGE_FILE;
  if (!std::filesystem::exists(toml_path)) {
    cforge::logger::print_error("No cforge.toml found in current directory");
    return 1;
  }

  cforge::toml_reader project_config(toml::parse_file(toml_path.string()));

  // Verify the profile exists and has a flash command
  std::string profile_key = "cross.profile." + cross_profile;
  std::string flash_cmd =
      project_config.get_string(profile_key + ".flash", "");

  if (flash_cmd.empty()) {
    cforge::logger::print_error(
        "Cross profile '" + cross_profile +
        "' does not have a flash command configured");
    cforge::logger::print_plain(
        "  Add 'flash = \"your-flash-command\"' to [cross.profile." +
        cross_profile + "] in cforge.toml");
    return 1;
  }

  // Determine build configuration
  if (config_name.empty()) {
    config_name =
        project_config.get_string("build.build_type", "Debug");
  }

  // Build the flash CMake target
  std::filesystem::path build_dir = current_dir / DEFAULT_BUILD_DIR;
  std::string flash_target = "flash_" + cross_profile;

  cforge::logger::print_action("Flashing",
                               "using profile '" + cross_profile + "'");

  if (!cforge::run_cmake_build(build_dir, config_name, flash_target, num_jobs,
                               verbose)) {
    cforge::logger::print_error("Flash failed for profile '" + cross_profile +
                                "'");
    return 1;
  }

  cforge::logger::finished("flash");
  return 0;
}
