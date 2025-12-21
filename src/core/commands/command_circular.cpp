/**
 * @file command_circular.cpp
 * @brief Implementation of the circular dependency detection command
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/include_analyzer.hpp"
#include "core/types.h"
#include "core/workspace.hpp"

#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <iostream>

namespace cforge {

static void print_help() {
  fmt::print("Usage: cforge circular [options]\n\n");
  fmt::print("Detect and display circular include dependencies\n\n");
  fmt::print("Options:\n");
  fmt::print("  --include-deps    Also check dependency headers\n");
  fmt::print("  --cforge::workspace      Check all cforge::workspaceprojects\n");
  fmt::print("  --json            Output as JSON\n");
  fmt::print("  --limit N         Limit output to first N chains\n");
  fmt::print("  -h, --help        Show this help message\n");
}

static cforge_int_t analyze_project(const std::filesystem::path &project_dir,
                           bool include_deps, bool json_output, cforge_int_t limit) {
  cforge::logger::print_action("Analyzing", project_dir.string());

  cforge::include_analyzer analyzer(project_dir);
  cforge::include_analysis_result result = analyzer.analyze(include_deps);

  cforge::logger::print_verbose("Analyzed " + std::to_string(result.total_files_analyzed) +
                        " files");

  if (!result.has_cycles) {
    if (json_output) {
      fmt::print("{}\n", cforge::format_circular_chains_json(result.chains));
    } else {
      fmt::print(fg(fmt::color::green), "{:>12}", "No cycles");
      fmt::print(" found in {}\n", project_dir.string());
    }
    return 0;
  }

  // Apply limit if specified
  std::vector<circular_chain> chains_to_show = result.chains;
  if (limit > 0 && static_cast<cforge_size_t>(limit) < chains_to_show.size()) {
    chains_to_show.resize(limit);
  }

  if (json_output) {
    fmt::print("{}\n", cforge::format_circular_chains_json(chains_to_show));
  } else {
    fmt::print("\n{}", cforge::format_circular_chains(chains_to_show));

    if (limit > 0 && static_cast<cforge_size_t>(limit) < result.chains.size()) {
      fmt::print(fg(fmt::color::yellow),
                 "... and {} more chains (use --limit to see more)\n",
                 result.chains.size() - limit);
    }
  }

  return 1; // Return non-zero if cycles found
}

} // namespace cforge

cforge_int_t cforge_cmd_circular(const cforge_context_t *ctx) {
  bool include_deps = false;
  bool check_workspace= false;
  bool json_output = false;
  cforge_int_t limit = 0;

  // Parse arguments
  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];

    if (arg == "-h" || arg == "--help") {
      cforge::print_help();
      return 0;
    } else if (arg == "--include-deps") {
      include_deps = true;
    } else if (arg == "--workspace") {
      check_workspace= true;
    } else if (arg == "--json") {
      json_output = true;
    } else if (arg == "--limit" && i + 1 < ctx->args.arg_count) {
      try {
        limit = std::stoi(ctx->args.args[++i]);
      } catch (...) {
        cforge::logger::print_error("Invalid limit value");
        return 1;
      }
    }
  }

  std::filesystem::path current_dir = std::filesystem::current_path();

  // Check if we're in a workspace
  auto [is_ws, workspace_dir] = cforge::is_in_workspace(current_dir);

  // Auto-detect workspace: if we're in a cforge::workspaceand at the cforge::workspaceroot, enable cforge::workspacemode
  if (is_ws && !check_workspace) {
    // Check if current directory is the cforge::workspaceroot
    if (current_dir == workspace_dir) {
      cforge::logger::print_verbose("Auto-detected cforge::workspaceroot, enabling cforge::workspacemode");
      check_workspace= true;
    }
  }

  if (check_workspace&& is_ws) {
    // Analyze all projects in workspace
    cforge::workspace ws;
    if (!ws.load(workspace_dir)) {
      cforge::logger::print_error("Failed to load workspace");
      return 1;
    }

    cforge::logger::print_action("Checking", "workspace " + ws.get_name());

    cforge_int_t total_cycles = 0;
    auto projects = ws.get_projects();

    for (const auto &project : projects) {
      if (std::filesystem::exists(project.path)) {
        cforge_int_t result = cforge::analyze_project(project.path, include_deps, json_output, limit);
        if (result > 0) {
          total_cycles++;
        }
      }
    }

    if (total_cycles > 0) {
      cforge::logger::print_warning(std::to_string(total_cycles) +
                            " project(s) have circular dependencies");
      return 1;
    }

    cforge::logger::print_success("No circular dependencies in workspace");
    return 0;
  }

  // Single project analysis
  std::filesystem::path project_dir = current_dir;

  // Check if it's a valid project
  if (!std::filesystem::exists(project_dir / CFORGE_FILE)) {
    // Try to find project in parent directories
    std::filesystem::path search = project_dir;
    while (search.has_parent_path() && search != search.parent_path()) {
      if (std::filesystem::exists(search / CFORGE_FILE)) {
        project_dir = search;
        break;
      }
      search = search.parent_path();
    }

    if (!std::filesystem::exists(project_dir / CFORGE_FILE)) {
      cforge::logger::print_error("Not in a cforge project directory");
      return 1;
    }
  }

  return cforge::analyze_project(project_dir, include_deps, json_output, limit);
}
