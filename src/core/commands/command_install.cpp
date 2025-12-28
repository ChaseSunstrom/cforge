/**
 * @file command_install.cpp
 * @brief Implementation of the 'install' command to install cforge or projects
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/installer.hpp"
#include "core/process_utils.hpp"
#include "core/types.h"
#include "core/workspace.hpp"
#include "core/workspace_utils.hpp"

#include <cstring>
#include <filesystem>
#include <string>

/**
 * @brief Handle the 'install' command: install the current project or specified
 * source
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_install(const cforge_context_t *ctx) {
  // Check if help was requested
  if (ctx->args.args) {
    for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
      if (strcmp(ctx->args.args[i], "--help") == 0 ||
          strcmp(ctx->args.args[i], "-h") == 0) {
        // Show install usage
        cforge_context_t help_ctx = *ctx;
        static const char *help_args[] = {"install", "--help", NULL};
        help_ctx.args.args = (cforge_string_t *)help_args;
        help_ctx.args.arg_count = 2;
        return cforge_cmd_help(&help_ctx);
      }
    }
  }

  // Parse install flags
  cforge::installer installer_instance;

  std::string project_source;
  std::string install_path;
  std::string project_name_override;
  bool add_to_path = false;
  bool have_from = false;
  [[maybe_unused]] bool have_to = false;
  std::string build_config;
  std::string env_var;
  bool skip_build = false;

  if (ctx->args.args) {
    for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
      std::string arg = ctx->args.args[i];
      // Build configuration for install
      if ((arg == "--config" || arg == "-c") && i + 1 < ctx->args.arg_count) {
        build_config = ctx->args.args[++i];
        cforge::logger::print_action("Config", build_config);
        continue;
      } else if (arg.rfind("--config=", 0) == 0) {
        build_config = arg.substr(9);
        cforge::logger::print_action("Config", build_config);
        continue;
      }
      if (arg == "--add-to-path") {
        add_to_path = true;
        cforge::logger::print_action("Option", "will add to PATH environment variable");
      } else if (arg == "--no-build") {
        skip_build = true;
        cforge::logger::print_action("Option", "skipping build, using existing binaries");
      } else if (arg == "--from" && i + 1 < ctx->args.arg_count) {
        project_source = ctx->args.args[++i];
        have_from = true;
        cforge::logger::print_action("Source", project_source);
      } else if (arg == "--to" && i + 1 < ctx->args.arg_count) {
        install_path = ctx->args.args[++i];
        have_to = true;
        cforge::logger::print_action("Target", install_path);
      } else if ((arg == "--name" || arg == "-n") &&
                 i + 1 < ctx->args.arg_count) {
        project_name_override = ctx->args.args[++i];
        cforge::logger::print_action("Name", project_name_override);
      } else if (arg == "--env" && i + 1 < ctx->args.arg_count) {
        // Set environment variable name for installation
        env_var = ctx->args.args[++i];
        cforge::logger::print_action("Env", env_var);
      } else if (!have_from && project_source.empty() &&
                 arg.rfind("-", 0) != 0) {
        // positional first => source
        project_source = arg;
      }
    }
  }

  // Determine project source: explicit or cwd
  if (!have_from) {
    std::filesystem::path cwd(ctx->working_dir);
    if (std::filesystem::exists(cwd / CFORGE_FILE) ||
        std::filesystem::exists(cwd / WORKSPACE_FILE)) {
      project_source = cwd.string();
      cforge::logger::print_verbose("Detected project in current directory: " +
                            project_source);
    } else {
      cforge::logger::print_error("No cforge project or workspace found. Provide "
                          "source with '--from'.");
      return 1;
    }
  }

  std::filesystem::path source_path(project_source);
  // Detect if we are inside a workspace, and adjust to workspace root
  auto [is_workspace, workspace_root] = cforge::is_in_workspace(source_path);
  if (is_workspace) {
    source_path = workspace_root;
  }

  // Helper to install a single project path
  auto install_proj = [&](const std::string &proj_dir) {
    installer_instance.install_project(proj_dir, install_path, add_to_path,
                                       project_name_override, build_config,
                                       env_var, skip_build);
  };

  if (is_workspace) {
    if (!skip_build) {
      cforge::logger::print_action("Building", "workspace before installation");
      // Build the workspace
      cforge_context_t build_ctx;
      memset(&build_ctx, 0, sizeof(build_ctx));
      // Use same working dir and config
      snprintf(build_ctx.working_dir, sizeof(build_ctx.working_dir), "%s",
               ctx->working_dir);
      build_ctx.args.command = strdup("build");
      if (!build_config.empty()) {
        build_ctx.args.config = strdup(build_config.c_str());
      }
      if (cforge::logger::get_verbosity() == cforge::log_verbosity::VERBOSITY_VERBOSE) {
        build_ctx.args.verbosity = strdup("verbose");
      }
      cforge_int_t build_res = cforge_cmd_build(&build_ctx);
      free((void *)build_ctx.args.command);
      if (build_ctx.args.config)
        free((void *)build_ctx.args.config);
      if (build_ctx.args.verbosity)
        free((void *)build_ctx.args.verbosity);
      if (build_res != 0) {
        cforge::logger::print_error("Workspace build failed");
        return build_res;
      }
      cforge::logger::finished("workspace build");
    } else {
      cforge::logger::print_action("Skipping", "workspace build (--no-build specified)");
    }
    // Now install projects from build artifacts
    cforge::logger::installing("workspace projects from " + project_source);
    // Load workspace config and determine main startup project
    auto ws_config_path = cforge::get_workspace_config_path(source_path);
    if (ws_config_path.empty()) {
      cforge::logger::print_error("No workspace configuration found");
      return 1;
    }
    cforge::toml_reader ws_cfg(toml::parse_file(ws_config_path.string()));
    std::string main_project = ws_cfg.get_string("workspace.main_project", "");
    // Get sorted project list
    auto names = cforge::get_workspace_projects(source_path);
    auto sorted = cforge::topo_sort_projects(source_path, names);
    // Install libraries and the main executable
    for (const auto &name : sorted) {
      std::filesystem::path proj_path = source_path / name;
      std::filesystem::path cfg = proj_path / CFORGE_FILE;
      if (!std::filesystem::exists(cfg)) {
        cforge::logger::print_warning("Skipping non-project directory: " + name);
        continue;
      }
      cforge::toml_reader proj_cfg(toml::parse_file(cfg.string()));
      std::string proj_type = proj_cfg.get_string("project.type", "executable");
      // Skip executables that are not the main startup
      if (proj_type == "executable" && name != main_project) {
        cforge::logger::print_verbose("Skipping non-startup executable project: " +
                              name);
        continue;
      }
      cforge::logger::installing(name);
      install_proj(proj_path.string());
    }
  } else {
    // Install single project or cforge itself
    // Install the project source
    {
      std::string source = project_source;
      bool needs_cleanup = false;
      // Git clone if URL
      if (source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0 ||
          source.find("@") != std::string::npos) {
        auto temp_dir =
            std::filesystem::temp_directory_path() / "cforge_install_temp";
        if (std::filesystem::exists(temp_dir))
          std::filesystem::remove_all(temp_dir);
        std::filesystem::create_directories(temp_dir);
        cforge::logger::print_action("Cloning", source);
        if (!cforge::execute_tool("git", {"clone", source, temp_dir.string()}, "",
                          "Git Clone", add_to_path)) {
          cforge::logger::print_error("Git clone failed: " + source);
          return 1;
        }
        source = temp_dir.string();
        needs_cleanup = true;
      }
      // Verbose install source logging
      cforge::logger::print_verbose("Installing project from: " + source);
      bool success = installer_instance.install_project(
          source, install_path, add_to_path, project_name_override,
          build_config, env_var, skip_build);
      if (needs_cleanup) {
        try {
          std::filesystem::remove_all(source);
        } catch (...) {
        }
      }
      if (!success) {
        cforge::logger::print_error("Project installation failed");
        return 1;
      }
    }
    cforge::logger::finished("project installation");
  }
  cforge::logger::finished("install");
  return 0;
}