/**
 * @file command_tree.cpp
 * @brief Implementation of the tree command for visualizing dependencies
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
#include "core/workspace.hpp"

#include <filesystem>
#include <map>
#include <set>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct dependency_info {
  std::string name;
  std::string type; // "git", "vcpkg", "system", "project"
  std::string version;
  std::string url;
  std::vector<std::string> children;
};

/**
 * @brief Print a tree branch
 */
void print_tree_branch(const std::string &name, const dependency_info &info,
                       const std::map<std::string, dependency_info> &all_deps,
                       const std::string &prefix, bool is_last,
                       std::set<std::string> &visited, cforge_int_t max_depth,
                       cforge_int_t current_depth) {
  if (current_depth > max_depth)
    return;
  if (visited.count(name)) {
    fmt::print("{}{}{}[circular]\n", prefix, is_last ? "`-- " : "|-- ", name);
    return;
  }
  visited.insert(name);

  // Determine color based on type
  fmt::color color = fmt::color::white;
  std::string type_indicator;
  if (info.type == "git") {
    color = fmt::color::cyan;
    type_indicator = " (git)";
  } else if (info.type == "vcpkg") {
    color = fmt::color::magenta;
    type_indicator = " (vcpkg)";
  } else if (info.type == "system") {
    color = fmt::color::yellow;
    type_indicator = " (system)";
  } else if (info.type == "project") {
    color = fmt::color::green;
    type_indicator = " (project)";
  }

  std::string version_str;
  if (!info.version.empty()) {
    version_str = " @ " + info.version;
  }

  fmt::print("{}{}", prefix, is_last ? "`-- " : "|-- ");
  fmt::print(fg(color), "{}", name);
  fmt::print("{}{}\n", version_str, type_indicator);

  // Print children
  std::string child_prefix = prefix + (is_last ? "    " : "|   ");
  for (cforge_size_t i = 0; i < info.children.size(); i++) {
    const auto &child = info.children[i];
    bool child_is_last = (i == info.children.size() - 1);

    auto it = all_deps.find(child);
    if (it != all_deps.end()) {
      print_tree_branch(child, it->second, all_deps, child_prefix,
                        child_is_last, visited, max_depth, current_depth + 1);
    } else {
      fmt::print("{}{}{}\n", child_prefix, child_is_last ? "`-- " : "|-- ",
                 child);
    }
  }
}

/**
 * @brief Collect dependencies from a project
 */
[[maybe_unused]] void collect_dependencies(const fs::path & /*project_dir*/,
                          const cforge::toml_reader &config,
                          std::map<std::string, dependency_info> &deps) {
  // Git dependencies
  if (config.has_key("dependencies.git")) {
    auto git_deps = config.get_table_keys("dependencies.git");
    for (const auto &dep : git_deps) {
      dependency_info info;
      info.name = dep;
      info.type = "git";
      info.url = config.get_string("dependencies.git." + dep + ".url", "");
      info.version = config.get_string("dependencies.git." + dep + ".tag", "");
      if (info.version.empty()) {
        info.version =
            config.get_string("dependencies.git." + dep + ".branch", "");
      }
      if (info.version.empty()) {
        info.version =
            config.get_string("dependencies.git." + dep + ".commit", "");
        if (!info.version.empty() && info.version.length() > 8) {
          info.version = info.version.substr(0, 8); // Shorten commit hash
        }
      }
      deps[dep] = info;
    }
  }

  // vcpkg dependencies
  if (config.has_key("dependencies.vcpkg.packages")) {
    auto vcpkg_deps = config.get_string_array("dependencies.vcpkg.packages");
    for (const auto &dep : vcpkg_deps) {
      dependency_info info;
      // Parse name:version format
      cforge_size_t colon = dep.find(':');
      if (colon != std::string::npos) {
        info.name = dep.substr(0, colon);
        info.version = dep.substr(colon + 1);
      } else {
        info.name = dep;
      }
      info.type = "vcpkg";
      deps[info.name] = info;
    }
  }

  // System dependencies
  if (config.has_key("dependencies.system")) {
    auto sys_deps = config.get_string_array("dependencies.system");
    for (const auto &dep : sys_deps) {
      dependency_info info;
      info.name = dep;
      info.type = "system";
      deps[dep] = info;
    }
  }

  // Project dependencies (workspace)
  if (config.has_key("dependencies.project")) {
    auto proj_deps = config.get_table_keys("dependencies.project");
    for (const auto &dep : proj_deps) {
      dependency_info info;
      info.name = dep;
      info.type = "project";
      deps[dep] = info;
    }
  }
}

} // anonymous namespace

/**
 * @brief Handle the 'tree' command for visualizing dependencies
 */
cforge_int_t cforge_cmd_tree(const cforge_context_t *ctx) {
  fs::path current_dir = ctx->working_dir;

  // Parse arguments
  [[maybe_unused]] bool show_all = false;
  cforge_int_t max_depth = 10;
  [[maybe_unused]] bool inverted = false;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-a" || arg == "--all") {
      show_all = true;
    } else if (arg == "-d" || arg == "--depth") {
      if (i + 1 < ctx->args.arg_count) {
        max_depth = std::stoi(ctx->args.args[++i]);
      }
    } else if (arg == "-i" || arg == "--inverted") {
      inverted = true;
    }
  }

  // Check for workspace or project
  auto [is_workspace, workspace_dir] = cforge::is_in_workspace(current_dir);

  std::map<std::string, dependency_info> all_deps;
  std::vector<std::pair<std::string, dependency_info>> roots;

  if (is_workspace) {
    // Load workspace
    cforge::workspace ws;
    if (!ws.load(workspace_dir)) {
      cforge::logger::print_error("Failed to load workspace");
      return 1;
    }

    fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold, "{}", ws.get_name());
    fmt::print(" (workspace)\n");

    // Process each project
    for (const auto &proj : ws.get_projects()) {
      fs::path proj_toml = proj.path / "cforge.toml";
      if (fs::exists(proj_toml)) {
        cforge::toml_reader config;
        config.load(proj_toml.string());

        dependency_info proj_info;
        proj_info.name = proj.name;
        proj_info.type = "project";
        proj_info.version = config.get_string("project.version", "");

        // Collect this project's dependencies
        std::map<std::string, dependency_info> proj_deps;
        collect_dependencies(proj.path, config, proj_deps);

        for (const auto &[name, info] : proj_deps) {
          proj_info.children.push_back(name);
          all_deps[name] = info;
        }

        roots.push_back({proj.name, proj_info});
      }
    }
  } else {
    // Single project
    fs::path config_file = current_dir / "cforge.toml";
    if (!fs::exists(config_file)) {
      cforge::logger::print_error("No cforge.toml found in current directory");
      return 1;
    }

    cforge::toml_reader config;
    config.load(config_file.string());

    std::string project_name =
        config.get_string("project.name", current_dir.filename().string());
    std::string version = config.get_string("project.version", "");

    fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold, "{}", project_name);
    if (!version.empty()) {
      fmt::print(" v{}", version);
    }
    fmt::print("\n");

    // Collect dependencies
    collect_dependencies(current_dir, config, all_deps);

    // Create root entries for top-level deps
    for (const auto &[name, info] : all_deps) {
      roots.push_back({name, info});
    }
  }

  if (roots.empty() && all_deps.empty()) {
    fmt::print("  (no dependencies)\n");
    return 0;
  }

  // Print tree
  std::set<std::string> visited;
  for (cforge_size_t i = 0; i < roots.size(); i++) {
    bool is_last = (i == roots.size() - 1);
    print_tree_branch(roots[i].first, roots[i].second, all_deps, "", is_last,
                      visited, max_depth, 0);
  }

  // Print summary
  fmt::print("\n");
  cforge_int_t git_count = 0, vcpkg_count = 0, sys_count = 0, proj_count = 0;
  for (const auto &[name, info] : all_deps) {
    if (info.type == "git")
      git_count++;
    else if (info.type == "vcpkg")
      vcpkg_count++;
    else if (info.type == "system")
      sys_count++;
    else if (info.type == "project")
      proj_count++;
  }

  std::vector<std::string> summary_parts;
  if (git_count > 0)
    summary_parts.push_back(std::to_string(git_count) + " git");
  if (vcpkg_count > 0)
    summary_parts.push_back(std::to_string(vcpkg_count) + " vcpkg");
  if (sys_count > 0)
    summary_parts.push_back(std::to_string(sys_count) + " system");
  if (proj_count > 0)
    summary_parts.push_back(std::to_string(proj_count) + " project");

  if (!summary_parts.empty()) {
    fmt::print("Dependencies: ");
    for (cforge_size_t i = 0; i < summary_parts.size(); i++) {
      if (i > 0)
        fmt::print(", ");
      fmt::print("{}", summary_parts[i]);
    }
    fmt::print("\n");
  }

  return 0;
}
