/**
 * @file command_deps.cpp
 * @brief Unified dependency management command with subcommands
 *
 * Consolidates all dependency-related commands under 'cforge deps':
 *   deps add      - Add a dependency
 *   deps remove   - Remove a dependency
 *   deps update   - Update the package registry
 *   deps search   - Search for packages
 *   deps info     - Show package information
 *   deps tree     - Visualize dependency tree
 *   deps lock     - Manage lock file
 *   deps outdated - Show outdated dependencies
 *   deps list     - List current dependencies
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/registry.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

/**
 * @brief Print deps subcommand help
 */
void print_deps_help() {
  cforge::logger::print_plain("cforge deps - Unified dependency management");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Usage: cforge deps <subcommand> [options]");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Subcommands:");
  cforge::logger::print_plain("  add <pkg>[@ver]    Add a dependency to the project");
  cforge::logger::print_plain("  remove <pkg>       Remove a dependency from the project");
  cforge::logger::print_plain("  update             Update the package registry index");
  cforge::logger::print_plain("  search <query>     Search for packages in the registry");
  cforge::logger::print_plain("  info <pkg>         Show detailed package information");
  cforge::logger::print_plain("  tree               Visualize dependency tree");
  cforge::logger::print_plain("  lock               Manage dependency lock file");
  cforge::logger::print_plain("  outdated           Show dependencies with newer versions");
  cforge::logger::print_plain("  list               List current project dependencies");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Examples:");
  cforge::logger::print_plain("  cforge deps add fmt@11.1.4");
  cforge::logger::print_plain("  cforge deps add spdlog --features async");
  cforge::logger::print_plain("  cforge deps add mylib --git https://github.com/user/mylib --tag v1.0");
  cforge::logger::print_plain("  cforge deps remove fmt");
  cforge::logger::print_plain("  cforge deps search json");
  cforge::logger::print_plain("  cforge deps info nlohmann_json");
  cforge::logger::print_plain("  cforge deps tree");
  cforge::logger::print_plain("  cforge deps outdated");
  cforge::logger::print_plain("  cforge deps lock --verify");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Run 'cforge deps <subcommand> --help' for subcommand details");
}

/**
 * @brief Create a modified context for subcommand dispatch
 */
cforge_context_t create_subcommand_context(const cforge_context_t *ctx,
                                            const char *subcommand,
                                            cforge_int_t arg_offset) {
  cforge_context_t sub_ctx = *ctx;
  sub_ctx.args.command = const_cast<char *>(subcommand);

  // Shift arguments to skip the subcommand
  if (ctx->args.arg_count > arg_offset) {
    sub_ctx.args.arg_count = ctx->args.arg_count - arg_offset;
    sub_ctx.args.args = ctx->args.args + arg_offset;
  } else {
    sub_ctx.args.arg_count = 0;
    sub_ctx.args.args = nullptr;
  }

  return sub_ctx;
}

/**
 * @brief Implementation of 'deps outdated' subcommand
 */
cforge_int_t deps_outdated(const cforge_context_t *ctx) {
  fs::path project_dir = ctx->working_dir;
  fs::path config_file = project_dir / "cforge.toml";

  if (!fs::exists(config_file)) {
    cforge::logger::print_error("No cforge.toml found in current directory");
    return 1;
  }

  // Parse arguments
  bool verbose = false;
  bool update_first = false;

  for (cforge_int_t i = 1; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-u" || arg == "--update") {
      update_first = true;
    } else if (arg == "-h" || arg == "--help") {
      cforge::logger::print_plain("cforge deps outdated - Show outdated dependencies");
      cforge::logger::print_plain("");
      cforge::logger::print_plain("Usage: cforge deps outdated [options]");
      cforge::logger::print_plain("");
      cforge::logger::print_plain("Options:");
      cforge::logger::print_plain("  -u, --update     Update registry before checking");
      cforge::logger::print_plain("  -v, --verbose    Show verbose output");
      return 0;
    }
  }

  // Initialize registry
  cforge::registry reg;

  if (update_first || reg.needs_update()) {
    cforge::logger::print_action("Updating", "package registry...");
    reg.update(update_first);
  }

  // Load project configuration
  cforge::toml_reader reader;
  if (!reader.load(config_file.string())) {
    cforge::logger::print_error("Failed to load cforge.toml");
    return 1;
  }

  cforge::logger::print_header("Checking for outdated dependencies");
  fmt::print("\n");

  // Track outdated packages
  struct outdated_info {
    std::string name;
    std::string current;
    std::string latest;
    std::string source;
  };
  std::vector<outdated_info> outdated;

  // Check index/registry dependencies
  auto deps = reader.get_table_keys("dependencies");
  for (const auto &dep_name : deps) {
    // Skip special sections
    if (dep_name == "git" || dep_name == "vcpkg" || dep_name == "system" ||
        dep_name == "directory" || dep_name == "fetch_content" ||
        dep_name == "project" || dep_name == "subdirectory") {
      continue;
    }

    std::string current_version = reader.get_string("dependencies." + dep_name, "");

    // Handle table format: dependencies.name = { version = "x.y.z" }
    if (current_version.empty()) {
      current_version = reader.get_string("dependencies." + dep_name + ".version", "*");
    }

    // Get package info from registry
    auto pkg_info = reg.get_package(dep_name);
    if (!pkg_info) {
      if (verbose) {
        cforge::logger::print_warning("Package '" + dep_name + "' not found in registry");
      }
      continue;
    }

    // Find latest version
    std::string latest_version;
    for (const auto &ver : pkg_info->versions) {
      if (!ver.yanked) {
        latest_version = ver.version;
        break; // First non-yanked is latest
      }
    }

    if (latest_version.empty()) {
      continue;
    }

    // Compare versions
    std::string resolved_current = reg.resolve_version(dep_name, current_version);
    if (resolved_current.empty()) {
      resolved_current = current_version;
    }

    // Check if outdated (simple string comparison won't work for semver)
    // Use the registry's version comparison
    if (!resolved_current.empty() && resolved_current != latest_version) {
      // Parse and compare versions properly
      auto parse_ver = [](const std::string &v) -> std::vector<int> {
        std::vector<int> parts;
        std::stringstream ss(v);
        std::string segment;
        while (std::getline(ss, segment, '.')) {
          try {
            parts.push_back(std::stoi(segment));
          } catch (...) {
            parts.push_back(0);
          }
        }
        while (parts.size() < 3) parts.push_back(0);
        return parts;
      };

      auto current_parts = parse_ver(resolved_current);
      auto latest_parts = parse_ver(latest_version);

      bool is_outdated = false;
      for (size_t i = 0; i < 3; i++) {
        if (latest_parts[i] > current_parts[i]) {
          is_outdated = true;
          break;
        } else if (latest_parts[i] < current_parts[i]) {
          break;
        }
      }

      if (is_outdated) {
        outdated.push_back({dep_name, resolved_current, latest_version, "index"});
      }
    }
  }

  // Display results
  if (outdated.empty()) {
    cforge::logger::print_success("All dependencies are up to date!");
    return 0;
  }

  // Print table header
  fmt::print("  {:<25} {:<15} {:<15} {:<10}\n",
             "Package", "Current", "Latest", "Source");
  fmt::print("  {:-<25} {:-<15} {:-<15} {:-<10}\n", "", "", "", "");

  for (const auto &info : outdated) {
    fmt::print("  {:<25} ", fmt::format(fg(fmt::color::white), "{}", info.name));
    fmt::print("{:<15} ", fmt::format(fg(fmt::color::yellow), "{}", info.current));
    fmt::print("{:<15} ", fmt::format(fg(fmt::color::green), "{}", info.latest));
    fmt::print("{:<10}\n", info.source);
  }

  fmt::print("\n");
  cforge::logger::print_action("Found", std::to_string(outdated.size()) +
                                            " outdated package(s)");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Run 'cforge deps add <package>@<version>' to update");

  return 0;
}

/**
 * @brief Implementation of 'deps list' subcommand
 */
cforge_int_t deps_list(const cforge_context_t *ctx) {
  fs::path project_dir = ctx->working_dir;
  fs::path config_file = project_dir / "cforge.toml";

  if (!fs::exists(config_file)) {
    cforge::logger::print_error("No cforge.toml found in current directory");
    return 1;
  }

  // Parse arguments
  bool verbose = false;
  std::string format = "table"; // table, json, simple

  for (cforge_int_t i = 1; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "--json") {
      format = "json";
    } else if (arg == "--simple") {
      format = "simple";
    } else if (arg == "-h" || arg == "--help") {
      cforge::logger::print_plain("cforge deps list - List project dependencies");
      cforge::logger::print_plain("");
      cforge::logger::print_plain("Usage: cforge deps list [options]");
      cforge::logger::print_plain("");
      cforge::logger::print_plain("Options:");
      cforge::logger::print_plain("  --json       Output as JSON");
      cforge::logger::print_plain("  --simple     Simple list format");
      cforge::logger::print_plain("  -v, --verbose Show verbose output");
      return 0;
    }
  }

  // Load project configuration
  cforge::toml_reader reader;
  if (!reader.load(config_file.string())) {
    cforge::logger::print_error("Failed to load cforge.toml");
    return 1;
  }

  // Collect dependencies by type
  struct dep_entry {
    std::string name;
    std::string version;
    std::string source;
    std::vector<std::string> features;
  };
  std::vector<dep_entry> all_deps;

  // Index dependencies
  auto deps = reader.get_table_keys("dependencies");
  for (const auto &dep_name : deps) {
    if (dep_name == "git" || dep_name == "vcpkg" || dep_name == "system" ||
        dep_name == "directory" || dep_name == "fetch_content" ||
        dep_name == "project" || dep_name == "subdirectory") {
      continue;
    }

    dep_entry entry;
    entry.name = dep_name;
    entry.source = "index";

    std::string ver = reader.get_string("dependencies." + dep_name, "");
    if (ver.empty()) {
      ver = reader.get_string("dependencies." + dep_name + ".version", "*");
      // Get features if table format
      auto features_str = reader.get_string("dependencies." + dep_name + ".features", "");
      if (!features_str.empty()) {
        std::stringstream ss(features_str);
        std::string feat;
        while (std::getline(ss, feat, ',')) {
          entry.features.push_back(feat);
        }
      }
    }
    entry.version = ver;
    all_deps.push_back(entry);
  }

  // Git dependencies
  auto git_deps = reader.get_table_keys("dependencies.git");
  for (const auto &dep_name : git_deps) {
    dep_entry entry;
    entry.name = dep_name;
    entry.source = "git";
    entry.version = reader.get_string("dependencies.git." + dep_name + ".tag",
                    reader.get_string("dependencies.git." + dep_name + ".branch", "HEAD"));
    all_deps.push_back(entry);
  }

  // vcpkg dependencies
  auto vcpkg_packages = reader.get_string_array("dependencies.vcpkg.packages");
  for (const auto &pkg : vcpkg_packages) {
    dep_entry entry;
    entry.name = pkg;
    entry.source = "vcpkg";
    entry.version = "-";
    all_deps.push_back(entry);
  }

  // System dependencies
  auto system_deps = reader.get_table_keys("dependencies.system");
  for (const auto &dep_name : system_deps) {
    dep_entry entry;
    entry.name = dep_name;
    entry.source = "system";
    entry.version = "-";
    all_deps.push_back(entry);
  }

  // Output
  if (format == "json") {
    fmt::print("{{\n  \"dependencies\": [\n");
    for (size_t i = 0; i < all_deps.size(); i++) {
      const auto &dep = all_deps[i];
      fmt::print("    {{ \"name\": \"{}\", \"version\": \"{}\", \"source\": \"{}\" }}",
                 dep.name, dep.version, dep.source);
      if (i < all_deps.size() - 1) fmt::print(",");
      fmt::print("\n");
    }
    fmt::print("  ]\n}}\n");
  } else if (format == "simple") {
    for (const auto &dep : all_deps) {
      fmt::print("{}@{} ({})\n", dep.name, dep.version, dep.source);
    }
  } else {
    // Table format
    cforge::logger::print_header("Project Dependencies");
    fmt::print("\n");

    if (all_deps.empty()) {
      cforge::logger::print_plain("  No dependencies configured");
      return 0;
    }

    fmt::print("  {:<25} {:<15} {:<10} {:<20}\n",
               "Package", "Version", "Source", "Features");
    fmt::print("  {:-<25} {:-<15} {:-<10} {:-<20}\n", "", "", "", "");

    for (const auto &dep : all_deps) {
      std::string features_str = dep.features.empty() ? "-" : "";
      for (size_t i = 0; i < dep.features.size(); i++) {
        features_str += dep.features[i];
        if (i < dep.features.size() - 1) features_str += ", ";
      }

      fmt::terminal_color name_color = fmt::terminal_color::white;
      if (dep.source == "git") name_color = fmt::terminal_color::cyan;
      else if (dep.source == "vcpkg") name_color = fmt::terminal_color::magenta;
      else if (dep.source == "system") name_color = fmt::terminal_color::yellow;

      fmt::print("  {:<25} {:<15} {:<10} {:<20}\n",
                 fmt::format(fg(name_color), "{}", dep.name),
                 dep.version, dep.source, features_str);
    }

    fmt::print("\n  Total: {} dependencies\n", all_deps.size());
  }

  return 0;
}

} // anonymous namespace

/**
 * @brief Handle the 'deps' command - unified dependency management
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_deps(const cforge_context_t *ctx) {
  // Get subcommand - first non-flag argument
  // Note: When called from dispatcher, args[0] is already the subcommand (not "deps")
  std::string subcommand;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    // Skip "deps" command name and flags
    if (arg != "deps" && !arg.empty() && arg[0] != '-') {
      subcommand = arg;
      break;
    }
  }

  // If no subcommand or help requested, show help
  if (subcommand.empty() || subcommand == "-h" || subcommand == "--help" ||
      subcommand == "help") {
    print_deps_help();
    return 0;
  }

  // Dispatch to appropriate subcommand
  // We need to create a modified context that shifts the arguments

  // Note: When called from dispatcher, args[0] is the subcommand (not "deps")
  // So we shift by 1 to skip the subcommand itself

  if (subcommand == "add") {
    cforge_context_t sub_ctx = create_subcommand_context(ctx, "add", 1);
    return cforge_cmd_add(&sub_ctx);
  } else if (subcommand == "remove" || subcommand == "rm") {
    cforge_context_t sub_ctx = create_subcommand_context(ctx, "remove", 1);
    return cforge_cmd_remove(&sub_ctx);
  } else if (subcommand == "update") {
    // deps update is like 'update --packages'
    cforge_context_t sub_ctx = *ctx;
    // Inject --packages flag
    std::vector<char *> new_args;
    new_args.push_back(strdup("update"));
    new_args.push_back(strdup("--packages"));
    // Copy any additional args (skip "update")
    for (cforge_int_t i = 1; i < ctx->args.arg_count; i++) {
      new_args.push_back(strdup(ctx->args.args[i]));
    }
    sub_ctx.args.command = new_args[0];
    sub_ctx.args.args = new_args.data();
    sub_ctx.args.arg_count = static_cast<cforge_int_t>(new_args.size());

    cforge_int_t result = cforge_cmd_update(&sub_ctx);

    // Clean up
    for (auto arg : new_args) {
      free(arg);
    }

    return result;
  } else if (subcommand == "search") {
    cforge_context_t sub_ctx = create_subcommand_context(ctx, "search", 1);
    return cforge_cmd_search(&sub_ctx);
  } else if (subcommand == "info") {
    cforge_context_t sub_ctx = create_subcommand_context(ctx, "info", 1);
    return cforge_cmd_info(&sub_ctx);
  } else if (subcommand == "tree") {
    cforge_context_t sub_ctx = create_subcommand_context(ctx, "tree", 1);
    return cforge_cmd_tree(&sub_ctx);
  } else if (subcommand == "lock") {
    cforge_context_t sub_ctx = create_subcommand_context(ctx, "lock", 1);
    return cforge_cmd_lock(&sub_ctx);
  } else if (subcommand == "outdated") {
    return deps_outdated(ctx);
  } else if (subcommand == "list" || subcommand == "ls") {
    return deps_list(ctx);
  } else if (subcommand == "vcpkg") {
    cforge_context_t sub_ctx = create_subcommand_context(ctx, "vcpkg", 1);
    return cforge_cmd_vcpkg(&sub_ctx);
  } else {
    cforge::logger::print_error("Unknown deps subcommand: " + subcommand);
    cforge::logger::print_plain("");
    print_deps_help();
    return 1;
  }
}
