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

// New: single directed edge in the inter-project graph
struct project_graph_edge {
  std::string from; // workspace project name
  std::string to;   // target workspace project name
};

// New: one external dep occurrence, keyed per project
struct dep_occurrence {
  std::string project; // workspace project that owns this dep
  std::string version; // version string (may be empty)
  std::string type;    // "index", "git", "vcpkg", "system", "project"
};

// New: conflict report for a single dep name
struct dep_conflict {
  std::string dep_name;
  std::vector<dep_occurrence> occurrences; // only entries with mismatched versions
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
    cforge::logger::print_plain(prefix + (is_last ? "`-- " : "|-- ") + name + "[circular]");
    return;
  }
  visited.insert(name);

  // Determine color based on type
  fmt::color color = fmt::color::white;
  std::string type_indicator;
  if (info.type == "index") {
    color = fmt::color::light_blue;
    type_indicator = " (index)";
  } else if (info.type == "git") {
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

  // Use logger for output with colored name
  std::string branch = prefix + (is_last ? "`-- " : "|-- ");
  std::string output = fmt::format("{}{}{}{}", branch,
                                   fmt::format(fg(color), "{}", name),
                                   version_str, type_indicator);
  cforge::logger::print_plain(output);

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
      cforge::logger::print_plain(child_prefix + (child_is_last ? "`-- " : "|-- ") + child);
    }
  }
}

/**
 * @brief Collect dependencies from a project
 */
[[maybe_unused]] void collect_dependencies(const fs::path & /*project_dir*/,
                          const cforge::toml_reader &config,
                          std::map<std::string, dependency_info> &deps) {
  // Special keys that are not package names
  static const std::set<std::string> special_keys = {
      "directory", "git", "vcpkg", "system", "project", "fetch_content"};

  // First, collect registry/index packages (direct key = "version" entries)
  if (config.has_key("dependencies")) {
    auto dep_keys = config.get_table_keys("dependencies");
    for (const auto &dep : dep_keys) {
      // Skip special keys
      if (special_keys.count(dep))
        continue;

      std::string key = "dependencies." + dep;

      // Check if it's a simple string value (registry package)
      std::string version = config.get_string(key, "");
      if (!version.empty()) {
        // It's a registry package like: fmt = "11.1.4"
        dependency_info info;
        info.name = dep;
        info.type = "index";
        info.version = version;
        deps[dep] = info;
        continue;
      }

      // Check if it's a table with version (registry package with options)
      if (config.has_key(key + ".version")) {
        // Check what type of dependency it is
        if (config.has_key(key + ".git")) {
          // Git dependency in new format
          dependency_info info;
          info.name = dep;
          info.type = "git";
          info.url = config.get_string(key + ".git", "");
          info.version = config.get_string(key + ".tag", "");
          if (info.version.empty()) {
            info.version = config.get_string(key + ".branch", "");
          }
          if (info.version.empty()) {
            info.version = config.get_string(key + ".commit", "");
            if (!info.version.empty() && info.version.length() > 8) {
              info.version = info.version.substr(0, 8);
            }
          }
          deps[dep] = info;
        } else if (config.has_key(key + ".vcpkg") ||
                   config.get_bool(key + ".vcpkg", false)) {
          // vcpkg dependency
          dependency_info info;
          info.name = dep;
          info.type = "vcpkg";
          info.version = config.get_string(key + ".version", "");
          deps[dep] = info;
        } else if (config.has_key(key + ".system") ||
                   config.get_bool(key + ".system", false)) {
          // System dependency
          dependency_info info;
          info.name = dep;
          info.type = "system";
          info.version = config.get_string(key + ".version", "");
          deps[dep] = info;
        } else if (config.has_key(key + ".path") ||
                   config.has_key(key + ".project")) {
          // Project/path dependency
          dependency_info info;
          info.name = dep;
          info.type = "project";
          info.version = config.get_string(key + ".version", "");
          deps[dep] = info;
        } else {
          // Registry package with options (e.g., features)
          dependency_info info;
          info.name = dep;
          info.type = "index";
          info.version = config.get_string(key + ".version", "");
          deps[dep] = info;
        }
      }
    }
  }

  // Git dependencies (old style: [dependencies.git])
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

  // vcpkg dependencies (old style)
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

  // System dependencies (old style)
  if (config.has_key("dependencies.system")) {
    auto sys_deps = config.get_string_array("dependencies.system");
    for (const auto &dep : sys_deps) {
      dependency_info info;
      info.name = dep;
      info.type = "system";
      deps[dep] = info;
    }
  }

  // Project dependencies (old style: [dependencies.project])
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

/**
 * @brief Scan all workspace projects for inter-project dependency edges.
 *
 * A dep is "inter-project" when:
 *   1. [dependencies.project] subtable exists with a key matching another
 *      workspace project name, OR
 *   2. A new-style dep entry has .path or .project key whose resolved name
 *      matches a sibling workspace project name.
 * Returns one edge per (from, to) pair found. Duplicates suppressed.
 */
std::vector<project_graph_edge>
collect_project_dependencies(const cforge::workspace &ws) {
  static const std::set<std::string> special_keys = {
      "directory", "git", "vcpkg", "system", "project", "fetch_content"};

  // Build a set of workspace project names for fast lookup
  std::set<std::string> ws_names;
  for (const auto &p : ws.get_projects()) {
    ws_names.insert(p.name);
  }

  std::vector<project_graph_edge> edges;
  // Track (from,to) pairs already added
  std::set<std::pair<std::string, std::string>> seen;

  auto add_edge = [&](const std::string &from, const std::string &to) {
    if (from == to) return; // no self-edges
    auto key = std::make_pair(from, to);
    if (seen.insert(key).second) {
      edges.push_back({from, to});
    }
  };

  for (const auto &proj : ws.get_projects()) {
    fs::path proj_toml = proj.path / "cforge.toml";
    if (!fs::exists(proj_toml)) continue;

    cforge::toml_reader config;
    config.load(proj_toml.string());

    // Old-style: [dependencies.project] subtable
    if (config.has_key("dependencies.project")) {
      auto proj_deps = config.get_table_keys("dependencies.project");
      for (const auto &dep : proj_deps) {
        if (ws_names.count(dep)) {
          add_edge(proj.name, dep);
        }
      }
    }

    // New-style: top-level [dependencies] entries with .path or .project key
    if (config.has_key("dependencies")) {
      auto dep_keys = config.get_table_keys("dependencies");
      for (const auto &dep : dep_keys) {
        if (special_keys.count(dep)) continue;
        std::string key = "dependencies." + dep;
        if (config.has_key(key + ".path") || config.has_key(key + ".project")) {
          // The dep name itself may match a workspace project
          if (ws_names.count(dep)) {
            add_edge(proj.name, dep);
          }
        }
      }
    }
  }

  return edges;
}

/**
 * @brief Render the "Project graph:" block.
 */
void print_project_graph(const std::vector<std::string> &project_names,
                         const std::vector<project_graph_edge> &edges) {
  // Build adjacency: name -> sorted list of targets
  std::map<std::string, std::vector<std::string>> adj;
  for (const auto &n : project_names) adj[n] = {};
  for (const auto &e : edges) adj[e.from].push_back(e.to);

  size_t max_len = 0;
  for (const auto &n : project_names)
    max_len = std::max(max_len, n.size());

  cforge::logger::print_plain("  Project graph:");
  for (const auto &n : project_names) {
    std::string padded = fmt::format("{:>{}}", "", max_len - n.size())
                       + fmt::format(fg(fmt::color::green) | fmt::emphasis::bold, "{}", n);
    const auto &targets = adj[n];
    std::string rhs;
    if (targets.empty()) {
      rhs = fmt::format(fg(fmt::color::gray) | fmt::emphasis::faint, "(none)");
    } else {
      for (size_t i = 0; i < targets.size(); ++i) {
        if (i) rhs += ", ";
        rhs += fmt::format(fg(fmt::color::green) | fmt::emphasis::bold, "{}", targets[i]);
      }
    }
    cforge::logger::print_plain("  " + padded + " -> " + rhs);
  }
  cforge::logger::print_blank();
}

/**
 * @brief Groups external deps across all workspace projects by name.
 * Returns only names that appear at two or more distinct non-empty versions.
 * "project"-type deps are excluded (inter-project refs are not conflicts).
 */
std::vector<dep_conflict>
detect_conflicts(const std::vector<std::pair<std::string, dependency_info>> &roots,
                 const std::map<std::string, dependency_info> &all_deps) {
  std::map<std::string, std::vector<dep_occurrence>> by_name;

  for (const auto &[proj_name, proj_info] : roots) {
    for (const auto &child_name : proj_info.children) {
      auto it = all_deps.find(child_name);
      if (it == all_deps.end()) continue;
      const auto &dep = it->second;
      if (dep.type == "project") continue;
      by_name[child_name].push_back({proj_name, dep.version, dep.type});
    }
  }

  std::vector<dep_conflict> conflicts;
  for (const auto &[name, occurrences] : by_name) {
    std::set<std::string> versions;
    for (const auto &o : occurrences) {
      if (!o.version.empty()) versions.insert(o.version);
    }
    if (versions.size() > 1) {
      conflicts.push_back({name, occurrences});
    }
  }

  return conflicts;
}

/**
 * @brief Print conflict warnings.
 * brief=true  -> single-line warning per conflict, no exit code change.
 * brief=false -> detailed block output (used with --check).
 */
void print_conflicts(const std::vector<dep_conflict> &conflicts, bool brief) {
  if (brief) {
    for (const auto &dep : conflicts) {
      std::string msg = dep.dep_name + " has conflicting versions: ";
      for (size_t i = 0; i < dep.occurrences.size(); ++i) {
        if (i) msg += ", ";
        msg += dep.occurrences[i].version + " (" + dep.occurrences[i].project + ")";
      }
      cforge::logger::print_warning(msg);
    }
  } else {
    cforge::logger::print_plain("  Conflicts:");
    for (const auto &c : conflicts) {
      cforge::logger::print_warning(c.dep_name + " has conflicting versions");
      for (const auto &o : c.occurrences) {
        cforge::logger::print_plain(
            fmt::format("             {} requires {} ({})",
                        o.project, o.version, o.type));
      }
    }
    cforge::logger::print_plain(
        fmt::format("  {} dependency conflict{} found",
                    conflicts.size(), conflicts.size() == 1 ? "" : "s"));
  }
}

/**
 * @brief Write a Graphviz DOT representation to stdout.
 * Project nodes are box/green; external dep nodes are ellipse/blue.
 * Only direct external deps of each project are emitted.
 */
void emit_dot(const std::string &workspace_name,
              const std::vector<std::string> &project_names,
              const std::vector<project_graph_edge> &edges,
              const std::vector<std::pair<std::string, dependency_info>> &roots,
              const std::map<std::string, dependency_info> &all_deps) {
  cforge::logger::print_plain("digraph \"" + workspace_name + "\" {");
  cforge::logger::print_plain("  rankdir=LR;");

  // Declare all project nodes
  for (const auto &n : project_names) {
    cforge::logger::print_plain("  \"" + n + "\" [shape=box, color=green];");
  }

  // Inter-project edges
  for (const auto &e : edges) {
    cforge::logger::print_plain("  \"" + e.from + "\" -> \"" + e.to + "\";");
  }

  // External dep nodes and edges
  std::set<std::string> declared_ext;
  for (const auto &[proj_name, proj_info] : roots) {
    for (const auto &child_name : proj_info.children) {
      auto it = all_deps.find(child_name);
      if (it == all_deps.end()) continue;
      const auto &dep = it->second;
      if (dep.type == "project") continue; // already covered by inter-project edges

      if (declared_ext.insert(child_name).second) {
        cforge::logger::print_plain("  \"" + child_name + "\" [shape=ellipse, color=blue];");
      }
      cforge::logger::print_plain("  \"" + proj_name + "\" -> \"" + child_name + "\";");
    }
  }

  cforge::logger::print_plain("}");
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
  bool check_mode = false;
  bool dot_format = false;

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
    } else if (arg == "--check") {
      check_mode = true;
    } else if (arg == "--format" && i + 1 < ctx->args.arg_count) {
      std::string fmt_val = ctx->args.args[++i];
      if (fmt_val == "dot") dot_format = true;
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

    cforge::logger::print_emphasis(ws.get_name() + " (workspace)");

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

    // 1. Collect inter-project graph
    std::vector<project_graph_edge> proj_edges = collect_project_dependencies(ws);

    // 2. Build project name list (workspace order)
    std::vector<std::string> proj_names;
    for (const auto &p : ws.get_projects()) proj_names.push_back(p.name);

    // 3. DOT short-circuit (replaces all normal output)
    if (dot_format) {
      emit_dot(ws.get_name(), proj_names, proj_edges, roots, all_deps);
      return 0;
    }

    // 4. Print project graph section (before "External dependencies:")
    cforge::logger::print_blank();
    print_project_graph(proj_names, proj_edges);
    cforge::logger::print_plain("  External dependencies:");

    if (roots.empty() && all_deps.empty()) {
      cforge::logger::print_dim("  (no dependencies)");
      return 0;
    }

    // Print tree
    std::set<std::string> visited;
    for (cforge_size_t i = 0; i < roots.size(); i++) {
      bool is_last = (i == roots.size() - 1);
      print_tree_branch(roots[i].first, roots[i].second, all_deps, "  ", is_last,
                        visited, max_depth, 0);
    }

    // Print summary
    cforge::logger::print_blank();
    cforge_int_t index_count = 0, git_count = 0, vcpkg_count = 0, sys_count = 0,
                 proj_count = 0;
    for (const auto &[name, info] : all_deps) {
      if (info.type == "index")
        index_count++;
      else if (info.type == "git")
        git_count++;
      else if (info.type == "vcpkg")
        vcpkg_count++;
      else if (info.type == "system")
        sys_count++;
      else if (info.type == "project")
        proj_count++;
    }

    std::vector<std::string> summary_parts;
    if (index_count > 0)
      summary_parts.push_back(std::to_string(index_count) + " index");
    if (git_count > 0)
      summary_parts.push_back(std::to_string(git_count) + " git");
    if (vcpkg_count > 0)
      summary_parts.push_back(std::to_string(vcpkg_count) + " vcpkg");
    if (sys_count > 0)
      summary_parts.push_back(std::to_string(sys_count) + " system");
    if (proj_count > 0)
      summary_parts.push_back(std::to_string(proj_count) + " project");

    if (!summary_parts.empty()) {
      std::string summary = "Dependencies: ";
      for (cforge_size_t i = 0; i < summary_parts.size(); i++) {
        if (i > 0)
          summary += ", ";
        summary += summary_parts[i];
      }
      cforge::logger::print_plain(summary);
    }

    // 5. Conflict detection (always run after summary)
    auto conflicts = detect_conflicts(roots, all_deps);
    if (!conflicts.empty()) {
      if (check_mode) {
        print_conflicts(conflicts, /*brief=*/false);
        return 1;
      } else {
        print_conflicts(conflicts, /*brief=*/true);
      }
    }

    return 0;

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

    std::string title = project_name;
    if (!version.empty()) {
      title += " v" + version;
    }
    cforge::logger::print_emphasis(title);

    // Collect dependencies
    collect_dependencies(current_dir, config, all_deps);

    // Create root entries for top-level deps
    for (const auto &[name, info] : all_deps) {
      roots.push_back({name, info});
    }

    // DOT format for single project
    if (dot_format) {
      std::vector<std::string> proj_names = {project_name};
      std::vector<project_graph_edge> no_edges;
      // Build a synthetic root list for emit_dot
      dependency_info proj_info;
      proj_info.name = project_name;
      proj_info.type = "project";
      for (const auto &[name, info] : all_deps) {
        proj_info.children.push_back(name);
      }
      std::vector<std::pair<std::string, dependency_info>> single_roots = {
          {project_name, proj_info}};
      emit_dot(project_name, proj_names, no_edges, single_roots, all_deps);
      return 0;
    }

    if (roots.empty() && all_deps.empty()) {
      cforge::logger::print_dim("  (no dependencies)");
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
    cforge::logger::print_blank();
    cforge_int_t index_count = 0, git_count = 0, vcpkg_count = 0, sys_count = 0,
                 proj_count = 0;
    for (const auto &[name, info] : all_deps) {
      if (info.type == "index")
        index_count++;
      else if (info.type == "git")
        git_count++;
      else if (info.type == "vcpkg")
        vcpkg_count++;
      else if (info.type == "system")
        sys_count++;
      else if (info.type == "project")
        proj_count++;
    }

    std::vector<std::string> summary_parts;
    if (index_count > 0)
      summary_parts.push_back(std::to_string(index_count) + " index");
    if (git_count > 0)
      summary_parts.push_back(std::to_string(git_count) + " git");
    if (vcpkg_count > 0)
      summary_parts.push_back(std::to_string(vcpkg_count) + " vcpkg");
    if (sys_count > 0)
      summary_parts.push_back(std::to_string(sys_count) + " system");
    if (proj_count > 0)
      summary_parts.push_back(std::to_string(proj_count) + " project");

    if (!summary_parts.empty()) {
      std::string summary = "Dependencies: ";
      for (cforge_size_t i = 0; i < summary_parts.size(); i++) {
        if (i > 0)
          summary += ", ";
        summary += summary_parts[i];
      }
      cforge::logger::print_plain(summary);
    }

    return 0;
  }
}
