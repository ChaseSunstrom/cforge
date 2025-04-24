/**
 * @file command_install.cpp
 * @brief Implementation of the 'install' command to install cforge or projects
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/installer.hpp"
#include "core/process_utils.hpp"

#include <cstring>
#include <filesystem>
#include <string>

using namespace cforge;

/**
 * @brief Handle the 'install' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_install(const cforge_context_t *ctx) {
  // Check if help was requested
  if (ctx->args.args) {
    for (int i = 0; i < ctx->args.arg_count; i++) {
      if (strcmp(ctx->args.args[i], "--help") == 0 ||
          strcmp(ctx->args.args[i], "-h") == 0) {
        // Create a new context with "install" as the first argument
        cforge_context_t help_ctx = *ctx;
        static const char *help_args[] = {"install", NULL};
        help_ctx.args.args = (cforge_string_t *)help_args;
        help_ctx.args.arg_count = 1;
        return cforge_cmd_help(&help_ctx);
      }
    }
  }

  logger::print_header("Installing cforge");

  installer installer_instance;

  // Parse arguments
  std::string project_source;
  std::string install_path;
  std::string project_name_override;
  bool add_to_path = false;

  // Check for flags and positional args
  if (ctx->args.args) {
    for (int i = 0; i < ctx->args.arg_count; ++i) {
      std::string arg = ctx->args.args[i];
      if (arg == "--add-to-path") {
        add_to_path = true;
        logger::print_status("Will add to PATH environment variable");
      } else if ((arg == "--path" || arg == "-p") &&
                 i + 1 < ctx->args.arg_count) {
        install_path = ctx->args.args[++i];
        logger::print_status("Installation path: " + install_path);
      } else if ((arg == "--name" || arg == "-n") &&
                 i + 1 < ctx->args.arg_count) {
        project_name_override = ctx->args.args[++i];
        logger::print_status("Project name override: " + project_name_override);
      } else if (project_source.empty()) {
        // First non-flag argument as project source
        project_source = arg;
      }
    }
  }

  bool success = false;

  if (project_source.empty()) {
    // Installing cforge itself
    logger::print_status("Installing cforge");
    success = installer_instance.install(install_path, add_to_path);
  } else {
    // Installing a project from local path or git URL
    std::string source = project_source;
    bool needs_cleanup = false;

    // Detect git URL
    if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0 ||
        source.find("@") != std::string::npos) {
      // Clone to temporary directory
      std::filesystem::path temp_dir =
          std::filesystem::temp_directory_path() / "cforge_install_temp";
      if (std::filesystem::exists(temp_dir)) {
        std::filesystem::remove_all(temp_dir);
      }
      std::filesystem::create_directories(temp_dir);

      logger::print_status("Cloning project from Git: " + source);
      bool clone_ok = execute_tool("git", {"clone", source, temp_dir.string()},
                                   "", "Git Clone", add_to_path);
      if (!clone_ok) {
        logger::print_error("Git clone failed: " + source);
        return 1;
      }
      source = temp_dir.string();
      needs_cleanup = true;
    }

    logger::print_status("Installing project from: " + source);
    success = installer_instance.install_project(
        source, install_path, add_to_path, project_name_override);

    // Clean up cloned repo
    if (needs_cleanup) {
      try {
        std::filesystem::remove_all(source);
      } catch (...) {
      }
    }
  }

  if (success) {
    if (project_source.empty()) {
      logger::print_success("cforge has been installed successfully");
      if (install_path.empty()) {
        logger::print_status("Installation path: " +
                             installer_instance.get_default_install_path());
      }
    } else {
      logger::print_success("Project has been installed successfully");
    }
    return 0;
  } else {
    logger::print_error("Installation failed");
    return 1;
  }
}