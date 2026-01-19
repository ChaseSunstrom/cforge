/**
 * @file command_list.cpp
 * @brief Implementation of the 'list' command to display available options
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"
#include <filesystem>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

/**
 * @brief Lists available build configurations
 */
static void list_build_configs() {
  cforge::logger::print_section("Available build configurations:");
  cforge::logger::print_list_item("Debug        (Development with debug symbols)");
  cforge::logger::print_list_item("Release      (Optimized release build)");
  cforge::logger::print_list_item("RelWithDebInfo (Release with debug information)");
  cforge::logger::print_list_item("MinSizeRel   (Minimal size release build)");
  cforge::logger::print_blank();
}

/**
 * @brief Lists available CMake generators
 */
static void list_generators() {
  cforge::logger::print_section("Available CMake generators for IDE integration:");
  cforge::logger::print_list_item("Visual Studio (vs)         Visual Studio project files");
  cforge::logger::print_list_item("CodeBlocks (cb)            CodeBlocks project files");
  cforge::logger::print_list_item("Xcode                      Xcode project files (macOS only)");
  cforge::logger::print_list_item("CLion (clion)              For CLion IDE");
  cforge::logger::print_blank();
}

/**
 * @brief Lists common build targets
 */
static void list_build_targets() {
  cforge::logger::print_section("Common build targets:");
  cforge::logger::print_list_item("all          Build all targets");
  cforge::logger::print_list_item("clean        Clean all build files");
  cforge::logger::print_list_item("install      Install the project");
  cforge::logger::print_list_item("package      Create distribution packages");
  cforge::logger::print_list_item("test         Build and run tests");
  cforge::logger::print_list_item("doc          Generate documentation (if configured)");
  cforge::logger::print_blank();
}

/**
 * @brief Lists available cforge commands
 */
static void list_commands() {
  cforge::logger::print_section("Available cforge commands:");
  cforge::logger::print_list_item("init         Initialize a new project");
  cforge::logger::print_list_item("build        Build the project");
  cforge::logger::print_list_item("run          Build and run the project");
  cforge::logger::print_list_item("clean        Clean build artifacts");
  cforge::logger::print_list_item("test         Run project tests");
  cforge::logger::print_list_item("deps/vcpkg   Manage dependencies");
  cforge::logger::print_list_item("install      Install the project");
  cforge::logger::print_list_item("update       Update cforge");
  cforge::logger::print_list_item("add          Add components to the project");
  cforge::logger::print_list_item("remove       Remove components from the project");
  cforge::logger::print_list_item("ide          Generate IDE project files");
  cforge::logger::print_list_item("version      Display version information");
  cforge::logger::print_list_item("help         Display help information");
  cforge::logger::print_blank();
  cforge::logger::print_dim("Run 'cforge help <command>' for more information about a specific command.");
  cforge::logger::print_blank();
}

/**
 * @brief Lists available project settings in cforge.toml
 */
static void list_project_settings() {
  cforge::logger::print_section("Available project settings in cforge.toml:");
  cforge::logger::print_config_block({
    "[project]",
    "name = \"project-name\"        # Required: Project name",
    "version = \"0.1.0\"           # Optional: Project version",
    "cpp_standard = \"17\"         # Optional: C++ standard version",
    "",
    "[build]",
    "build_dir = \"build\"         # Optional: Build directory name",
    "build_type = \"Release\"      # Optional: Default build type",
    "",
    "[dependencies]",
    "vcpkg = [\"fmt\", \"spdlog\"] # Optional: vcpkg dependencies",
    "vcpkg_triplet = \"x64-windows\" # Optional: vcpkg triplet",
    "vcpkg_path = \"/path/to/vcpkg\" # Optional: Custom vcpkg path"
  });
  cforge::logger::print_blank();
}

/**
 * @brief Handle the 'list' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_list(const cforge_context_t *ctx) {
  // Extract the list category from arguments
  std::string category;

  if (ctx->args.args && ctx->args.args[0]) {
    category = ctx->args.args[0];
  }

  cforge::logger::print_header("cforge - Available options and configurations");

  // If category is specified, list only that category
  if (!category.empty()) {
    if (category == "configs" || category == "configurations") {
      list_build_configs();
    } else if (category == "generators" || category == "ides") {
      list_generators();
    } else if (category == "targets") {
      list_build_targets();
    } else if (category == "commands") {
      list_commands();
    } else if (category == "settings") {
      list_project_settings();
    } else if (category == "projects") {
      if (!ctx->is_workspace) {
        cforge::logger::print_error("Not in a workspace");
        return 1;
      }
      cforge::workspace ws;
      if (!ws.load(ctx->working_dir)) {
        cforge::logger::print_error("Failed to load workspace configuration");
        return 1;
      }
      cforge::logger::print_section("Workspace projects:");
      for (const auto &proj : ws.get_projects()) {
        std::string proj_info = proj.name;
        if (proj.is_startup_project)
          proj_info += " (startup)";
        proj_info += " (" + proj.path.string() + ")";
        cforge::logger::print_list_item(proj_info);
      }
      cforge::logger::print_blank();
    } else if (category == "order" || category == "build-order") {
      if (!ctx->is_workspace) {
        cforge::logger::print_error("Not in a workspace");
        return 1;
      }
      cforge::workspace ws;
      if (!ws.load(ctx->working_dir)) {
        cforge::logger::print_error("Failed to load workspace configuration");
        return 1;
      }
      cforge::logger::print_section("Workspace build order:");
      for (const auto &name : ws.get_build_order()) {
        cforge::logger::print_list_item(name);
      }
      cforge::logger::print_blank();
    } else if (category == "dependencies" || category == "deps") {
      if (ctx->is_workspace) {
        // Workspace project dependencies
        cforge::workspace ws;
        if (!ws.load(ctx->working_dir)) {
          cforge::logger::print_error("Failed to load workspace configuration");
          return 1;
        }
        cforge::logger::print_section("Workspace project dependencies:");
        for (const auto &proj : ws.get_projects()) {
          if (!proj.dependencies.empty()) {
            std::string deps_str;
            for (const auto &dep : proj.dependencies) {
              deps_str += dep + " ";
            }
            cforge::logger::print_kv(proj.name, deps_str);
          }
        }
        cforge::logger::print_blank();
      } else {
        // Project-level dependencies from cforge.toml
        std::filesystem::path toml_path =
            std::filesystem::path(ctx->working_dir) / CFORGE_FILE;
        cforge::toml_reader cfg;
        if (!cfg.load(toml_path.string())) {
          cforge::logger::print_error("Failed to load cforge.toml");
          return 1;
        }
        if (cfg.has_key("dependencies.vcpkg")) {
          auto vdeps = cfg.get_table_keys("dependencies.vcpkg");
          if (!vdeps.empty()) {
            cforge::logger::print_section("vcpkg dependencies:");
            for (const auto &dep : vdeps) {
              cforge::logger::print_list_item(dep);
            }
            cforge::logger::print_blank();
          }
        }
        if (cfg.has_key("dependencies.git")) {
          auto gdeps = cfg.get_table_keys("dependencies.git");
          if (!gdeps.empty()) {
            cforge::logger::print_section("Git dependencies:");
            for (const auto &dep : gdeps) {
              cforge::logger::print_list_item(dep);
            }
            cforge::logger::print_blank();
          }
        }
        if (cfg.has_key("dependencies.system")) {
          auto sdeps = cfg.get_string_array("dependencies.system");
          if (!sdeps.empty()) {
            cforge::logger::print_section("System dependencies:");
            for (const auto &lib : sdeps) {
              cforge::logger::print_list_item(lib);
            }
            cforge::logger::print_blank();
          }
        }
      }
    } else if (category == "graph" || category == "dep-graph") {
      if (!ctx->is_workspace) {
        cforge::logger::print_error("Not in a workspace");
        return 1;
      }
      cforge::workspace ws;
      if (!ws.load(ctx->working_dir)) {
        cforge::logger::print_error("Failed to load workspace configuration");
        return 1;
      }
      // Output Mermaid dependency graph
      cforge::logger::print_plain("graph TD");
      std::vector<std::string> all_deps;
      for (const auto &proj : ws.get_projects()) {
        for (const auto &dep : proj.dependencies) {
          cforge::logger::print_plain("  " + proj.name + " --> " + dep);
          all_deps.push_back(dep);
        }
      }
      // Include isolated projects
      for (const auto &proj : ws.get_projects()) {
        if (proj.dependencies.empty() &&
            std::find(all_deps.begin(), all_deps.end(), proj.name) ==
                all_deps.end()) {
          cforge::logger::print_plain("  " + proj.name);
        }
      }
      cforge::logger::print_blank();
    } else if (category == "scripts") {
      // List configured scripts
      std::filesystem::path toml_path =
          std::filesystem::path(ctx->working_dir) /
          (ctx->is_workspace ? WORKSPACE_FILE : CFORGE_FILE);
      cforge::toml_reader cfg;
      if (!cfg.load(toml_path.string())) {
        cforge::logger::print_error("Failed to load configuration: " +
                            toml_path.string());
        return 1;
      }
      cforge::logger::print_section("Configured scripts:");
      if (cfg.has_key("scripts.pre_build")) {
        auto scripts = cfg.get_string_array("scripts.pre_build");
        cforge::logger::print_plain("  pre_build:");
        for (const auto &s : scripts) {
          cforge::logger::print_list_item(s, "-", 4);
        }
      }
      if (cfg.has_key("scripts.post_build")) {
        auto scripts = cfg.get_string_array("scripts.post_build");
        cforge::logger::print_plain("  post_build:");
        for (const auto &s : scripts) {
          cforge::logger::print_list_item(s, "-", 4);
        }
      }
      cforge::logger::print_blank();
    } else {
      cforge::logger::print_error("Unknown list category: " + category);
      cforge::logger::print_plain(
          "Available categories: configs, generators, targets, commands, "
          "settings, projects, order, dependencies, graph, scripts");
      return 1;
    }
  } else {
    // List all categories
    list_commands();
    list_build_configs();
    list_generators();
    list_build_targets();
    list_project_settings();
    if (ctx->is_workspace) {
      cforge::workspace ws;
      if (ws.load(ctx->working_dir)) {
        cforge::logger::print_section("Workspace projects:");
        for (const auto &proj : ws.get_projects()) {
          cforge::logger::print_list_item(proj.name + " (" + proj.path.string() + ")");
        }
        cforge::logger::print_blank();
        cforge::logger::print_section("Workspace project dependencies:");
        for (const auto &proj : ws.get_projects()) {
          if (!proj.dependencies.empty()) {
            std::string deps_str;
            for (const auto &dep : proj.dependencies) {
              deps_str += dep + " ";
            }
            cforge::logger::print_kv(proj.name, deps_str);
          }
        }
        cforge::logger::print_blank();
      }
    }
  }

  return 0;
}