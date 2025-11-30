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
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace cforge;

/**
 * @brief Lists available build configurations
 */
static void list_build_configs() {
  std::cout << "Available build configurations:\n";
  std::cout << "  - Debug        (Development with debug symbols)\n";
  std::cout << "  - Release      (Optimized release build)\n";
  std::cout << "  - RelWithDebInfo (Release with debug information)\n";
  std::cout << "  - MinSizeRel   (Minimal size release build)\n";
  std::cout << "\n";
}

/**
 * @brief Lists available CMake generators
 */
static void list_generators() {
  std::cout << "Available CMake generators for IDE integration:\n";
  std::cout << "  - Visual Studio (vs)         Visual Studio project files\n";
  std::cout << "  - CodeBlocks (cb)            CodeBlocks project files\n";
  std::cout
      << "  - Xcode                      Xcode project files (macOS only)\n";
  std::cout << "  - CLion (clion)              For CLion IDE\n";
  std::cout << "\n";
}

/**
 * @brief Lists common build targets
 */
static void list_build_targets() {
  std::cout << "Common build targets:\n";
  std::cout << "  - all          Build all targets\n";
  std::cout << "  - clean        Clean all build files\n";
  std::cout << "  - install      Install the project\n";
  std::cout << "  - package      Create distribution packages\n";
  std::cout << "  - test         Build and run tests\n";
  std::cout << "  - doc          Generate documentation (if configured)\n";
  std::cout << "\n";
}

/**
 * @brief Lists available cforge commands
 */
static void list_commands() {
  std::cout << "Available cforge commands:\n";
  std::cout << "  - init         Initialize a new project\n";
  std::cout << "  - build        Build the project\n";
  std::cout << "  - run          Build and run the project\n";
  std::cout << "  - clean        Clean build artifacts\n";
  std::cout << "  - test         Run project tests\n";
  std::cout << "  - deps/vcpkg   Manage dependencies\n";
  std::cout << "  - install      Install the project\n";
  std::cout << "  - update       Update cforge\n";
  std::cout << "  - add          Add components to the project\n";
  std::cout << "  - remove       Remove components from the project\n";
  std::cout << "  - ide          Generate IDE project files\n";
  std::cout << "  - version      Display version information\n";
  std::cout << "  - help         Display help information\n";
  std::cout << "\n";
  std::cout << "Run 'cforge help <command>' for more information about a "
               "specific command.\n";
  std::cout << "\n";
}

/**
 * @brief Lists available project settings in cforge.toml
 */
static void list_project_settings() {
  std::cout << "Available project settings in cforge.toml:\n";
  std::cout << "[project]\n";
  std::cout << "name = \"project-name\"        # Required: Project name\n";
  std::cout << "version = \"0.1.0\"           # Optional: Project version\n";
  std::cout
      << "cpp_standard = \"17\"         # Optional: C++ standard version\n";
  std::cout << "\n";
  std::cout << "[build]\n";
  std::cout
      << "build_dir = \"build\"         # Optional: Build directory name\n";
  std::cout << "build_type = \"Release\"      # Optional: Default build type\n";
  std::cout << "\n";
  std::cout << "[dependencies]\n";
  std::cout << "vcpkg = [\"fmt\", \"spdlog\"] # Optional: vcpkg dependencies\n";
  std::cout << "vcpkg_triplet = \"x64-windows\" # Optional: vcpkg triplet\n";
  std::cout
      << "vcpkg_path = \"/path/to/vcpkg\" # Optional: Custom vcpkg path\n";
  std::cout << "\n";
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

  std::cout << "cforge - Available options and configurations\n";

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
        logger::print_error("Not in a workspace");
        return 1;
      }
      workspace ws;
      if (!ws.load(ctx->working_dir)) {
        logger::print_error("Failed to load workspace configuration");
        return 1;
      }
      std::cout << "Workspace projects:\n";
      for (const auto &proj : ws.get_projects()) {
        std::cout << "  - " << proj.name;
        if (proj.is_startup_project)
          std::cout << " (startup)";
        std::cout << " (" << proj.path.string() << ")\n";
      }
      std::cout << "\n";
    } else if (category == "order" || category == "build-order") {
      if (!ctx->is_workspace) {
        logger::print_error("Not in a workspace");
        return 1;
      }
      workspace ws;
      if (!ws.load(ctx->working_dir)) {
        logger::print_error("Failed to load workspace configuration");
        return 1;
      }
      std::cout << "Workspace build order:\n";
      for (const auto &name : ws.get_build_order()) {
        std::cout << "  - " << name << "\n";
      }
      std::cout << "\n";
    } else if (category == "dependencies" || category == "deps") {
      if (ctx->is_workspace) {
        // Workspace project dependencies
        workspace ws;
        if (!ws.load(ctx->working_dir)) {
          logger::print_error("Failed to load workspace configuration");
          return 1;
        }
        std::cout << "Workspace project dependencies:\n";
        for (const auto &proj : ws.get_projects()) {
          if (!proj.dependencies.empty()) {
            std::cout << "  " << proj.name << ": ";
            for (const auto &dep : proj.dependencies) {
              std::cout << dep << " ";
            }
            std::cout << "\n";
          }
        }
        std::cout << "\n";
      } else {
        // Project-level dependencies from cforge.toml
        std::filesystem::path toml_path =
            std::filesystem::path(ctx->working_dir) / CFORGE_FILE;
        toml_reader cfg;
        if (!cfg.load(toml_path.string())) {
          logger::print_error("Failed to load cforge.toml");
          return 1;
        }
        if (cfg.has_key("dependencies.vcpkg")) {
          auto vdeps = cfg.get_table_keys("dependencies.vcpkg");
          if (!vdeps.empty()) {
            std::cout << "vcpkg dependencies:\n";
            for (const auto &dep : vdeps) {
              std::cout << "  - " << dep << "\n";
            }
            std::cout << "\n";
          }
        }
        if (cfg.has_key("dependencies.git")) {
          auto gdeps = cfg.get_table_keys("dependencies.git");
          if (!gdeps.empty()) {
            std::cout << "Git dependencies:\n";
            for (const auto &dep : gdeps) {
              std::cout << "  - " << dep << "\n";
            }
            std::cout << "\n";
          }
        }
        if (cfg.has_key("dependencies.system")) {
          auto sdeps = cfg.get_string_array("dependencies.system");
          if (!sdeps.empty()) {
            std::cout << "System dependencies:\n";
            for (const auto &lib : sdeps) {
              std::cout << "  - " << lib << "\n";
            }
            std::cout << "\n";
          }
        }
      }
    } else if (category == "graph" || category == "dep-graph") {
      if (!ctx->is_workspace) {
        logger::print_error("Not in a workspace");
        return 1;
      }
      workspace ws;
      if (!ws.load(ctx->working_dir)) {
        logger::print_error("Failed to load workspace configuration");
        return 1;
      }
      // Output Mermaid dependency graph
      std::cout << "graph TD\n";
      std::vector<std::string> all_deps;
      for (const auto &proj : ws.get_projects()) {
        for (const auto &dep : proj.dependencies) {
          std::cout << "  " << proj.name << " --> " << dep << "\n";
          all_deps.push_back(dep);
        }
      }
      // Include isolated projects
      for (const auto &proj : ws.get_projects()) {
        if (proj.dependencies.empty() &&
            std::find(all_deps.begin(), all_deps.end(), proj.name) ==
                all_deps.end()) {
          std::cout << "  " << proj.name << "\n";
        }
      }
      std::cout << "\n";
    } else if (category == "scripts") {
      // List configured scripts
      std::filesystem::path toml_path =
          std::filesystem::path(ctx->working_dir) /
          (ctx->is_workspace ? WORKSPACE_FILE : CFORGE_FILE);
      toml_reader cfg;
      if (!cfg.load(toml_path.string())) {
        logger::print_error("Failed to load configuration: " +
                            toml_path.string());
        return 1;
      }
      std::cout << "Configured scripts:\n";
      if (cfg.has_key("scripts.pre_build")) {
        auto scripts = cfg.get_string_array("scripts.pre_build");
        std::cout << "  pre_build:\n";
        for (const auto &s : scripts) {
          std::cout << "    - " << s << "\n";
        }
      }
      if (cfg.has_key("scripts.post_build")) {
        auto scripts = cfg.get_string_array("scripts.post_build");
        std::cout << "  post_build:\n";
        for (const auto &s : scripts) {
          std::cout << "    - " << s << "\n";
        }
      }
      std::cout << "\n";
    } else {
      logger::print_error("Unknown list category: " + category);
      std::cout
          << "Available categories: configs, generators, targets, commands, "
             "settings, projects, order, dependencies, graph, scripts\n";
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
      workspace ws;
      if (ws.load(ctx->working_dir)) {
        std::cout << "Workspace projects:\n";
        for (const auto &proj : ws.get_projects()) {
          std::cout << "  - " << proj.name << " (" << proj.path.string()
                    << ")\n";
        }
        std::cout << "\n";
        std::cout << "Workspace project dependencies:\n";
        for (const auto &proj : ws.get_projects()) {
          if (!proj.dependencies.empty()) {
            std::cout << "  " << proj.name << ": ";
            for (const auto &dep : proj.dependencies) {
              std::cout << dep << " ";
            }
            std::cout << "\n";
          }
        }
        std::cout << "\n";
      }
    }
  }

  return 0;
}