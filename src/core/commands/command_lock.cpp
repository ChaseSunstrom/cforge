/**
 * @file command_lock.cpp
 * @brief Implementation of the 'lock' command for dependency locking
 *
 * Commands:
 *   cforge lock          - Generate/update cforge.lock from current
 * dependencies cforge lock --verify - Verify dependencies match lock file
 *   cforge lock --clean  - Remove lock file
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/lockfile.hpp"
#include "core/registry.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
#include "core/workspace.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

/**
 * @brief Handle the 'lock' command
 */
cforge_int_t cforge_cmd_lock(const cforge_context_t *ctx) {
  std::filesystem::path current_dir = ctx->working_dir;
  bool verbose = cforge_is_verbose();

  // Parse command options
  bool verify_only = false;
  bool clean_lock = false;
  bool force_update = false;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    if (ctx->args.args[i] == nullptr)
      continue;
    std::string arg = ctx->args.args[i];

    if (arg == "--verify" || arg == "-v") {
      verify_only = true;
    } else if (arg == "--clean" || arg == "-c") {
      clean_lock = true;
    } else if (arg == "--force" || arg == "-f") {
      force_update = true;
    } else if (arg == "--help" || arg == "-h") {
      cforge::logger::print_plain("Usage: cforge lock [options]");
      cforge::logger::print_plain("");
      cforge::logger::print_plain(
          "Generate or verify dependency lock file (cforge.lock)");
      cforge::logger::print_plain("");
      cforge::logger::print_plain("Options:");
      cforge::logger::print_plain(
          "  --verify, -v   Verify dependencies match lock file");
      cforge::logger::print_plain("  --clean, -c    Remove the lock file");
      cforge::logger::print_plain(
          "  --force, -f    Force regeneration even if lock exists");
      cforge::logger::print_plain("  --help, -h     Show this help message");
      cforge::logger::print_plain("");
      cforge::logger::print_plain(
          "The lock file ensures reproducible builds by tracking");
      cforge::logger::print_plain(
          "exact versions (commit hashes) of all dependencies.");
      return 0;
    }
  }

  // Check if we're in a workspace or project
  bool is_workspace = ctx->is_workspace;
  std::filesystem::path config_path;

  if (is_workspace) {
    config_path = cforge::get_workspace_config_path(current_dir);
    if (config_path.empty()) {
      cforge::logger::print_error("No workspace configuration found in current directory");
      cforge::logger::print_error("Run 'cforge init' to create a new project");
      return 1;
    }
  } else {
    config_path = current_dir / CFORGE_FILE;
    if (!std::filesystem::exists(config_path)) {
      cforge::logger::print_error("No cforge project found in current directory");
      cforge::logger::print_error("Run 'cforge init' to create a new project");
      return 1;
    }
  }

  // Handle --clean option
  if (clean_lock) {
    std::filesystem::path lock_path = current_dir / cforge::LOCK_FILE;
    if (std::filesystem::exists(lock_path)) {
      try {
        std::filesystem::remove(lock_path);
        cforge::logger::removing(std::string(cforge::LOCK_FILE));
      } catch (const std::exception &e) {
        cforge::logger::print_error("Failed to remove lock file: " +
                            std::string(e.what()));
        return 1;
      }
    } else {
      cforge::logger::print_action("Skipping", "no lock file to remove");
    }
    return 0;
  }

  // Load project configuration
  cforge::toml_reader config;
  if (!config.load(config_path.string())) {
    cforge::logger::print_error("Failed to load configuration: " +
                        config_path.string());
    return 1;
  }

  // Get dependencies directory (default: deps)
  std::string deps_dir_str =
      config.get_string("dependencies.directory", "deps");
  std::filesystem::path deps_dir = current_dir / deps_dir_str;

  // Handle --verify option
  if (verify_only) {
    cforge::logger::print_action("Verifying", "dependencies against lock file");

    if (!cforge::lockfile::exists(current_dir)) {
      cforge::logger::print_warning(
          "No lock file found. Run 'cforge lock' to create one");
      return 1;
    }

    if (cforge::verify_lockfile(current_dir, deps_dir, verbose)) {
      cforge::logger::print_action("Verified", "all dependencies match lock file");
      return 0;
    } else {
      cforge::logger::print_error("Dependencies do not match lock file");
      cforge::logger::print_action(
          "Help", "run 'cforge lock' to update, or 'cforge deps' to restore");
      return 1;
    }
  }

  // Check if lock file already exists
  if (cforge::lockfile::exists(current_dir) && !force_update) {
    cforge::logger::print_action("Checking",
                         "lock file already exists. Use --force to regenerate");

    // Still verify it
    if (cforge::verify_lockfile(current_dir, deps_dir, verbose)) {
      cforge::logger::print_action("Verified", "dependencies match lock file");
      return 0;
    } else {
      cforge::logger::print_warning(
          "Dependencies have changed. Use --force to update lock file");
      return 1;
    }
  }

  // Generate/update lock file
  cforge::logger::print_action("Generating", "lock file");

  // Check if using FetchContent mode
  bool use_fetch_content = config.get_bool("dependencies.fetch_content", true);

  if (use_fetch_content) {
    // FetchContent mode: generate lock file from cforge.toml + registry
    cforge::registry reg;
    std::filesystem::path lock_path = current_dir / cforge::LOCK_FILE;
    std::ofstream lock_file(lock_path);

    if (!lock_file.is_open()) {
      cforge::logger::print_error("Failed to create lock file: " + lock_path.string());
      return 1;
    }

    // Write lock file header
    lock_file << "# cforge.lock - Dependency lock file for reproducible builds\n";
    lock_file << "# Generated by cforge - DO NOT EDIT MANUALLY\n";
    lock_file << "# Mode: FetchContent\n\n";

    // Get index dependencies from cforge.toml
    auto dep_keys = config.get_table_keys("dependencies");
    bool has_deps = false;

    for (const auto &key : dep_keys) {
      // Skip config keys
      if (key == "fetch_content" || key == "directory" || key == "git" || key == "vcpkg") {
        continue;
      }

      std::string version = config.get_string("dependencies." + key, "*");

      // Try to get package info from registry
      auto pkg_info = reg.get_package(key);
      std::string resolved_version = version;
      std::string repository;
      std::string git_tag;

      if (pkg_info) {
        repository = pkg_info->repository;
        // Resolve version
        resolved_version = reg.resolve_version(key, version);
        if (resolved_version.empty()) {
          resolved_version = version;
        }
        // Get git tag for the version
        for (const auto &ver : pkg_info->versions) {
          if (ver.version == resolved_version) {
            git_tag = ver.tag;
            break;
          }
        }
        // If no explicit tag, use tag pattern
        if (git_tag.empty() && !pkg_info->tags.pattern.empty()) {
          git_tag = pkg_info->tags.pattern;
          size_t pos = git_tag.find("{version}");
          if (pos != std::string::npos) {
            git_tag.replace(pos, 9, resolved_version);
          }
        }
      }

      lock_file << "[dependency." << key << "]\n";
      lock_file << "source_type = \"index\"\n";
      lock_file << "version = \"" << resolved_version << "\"\n";
      if (!repository.empty()) {
        lock_file << "url = \"" << repository << "\"\n";
      }
      if (!git_tag.empty()) {
        lock_file << "resolved = \"" << git_tag << "\"\n";
      }
      lock_file << "\n";
      has_deps = true;

      if (verbose) {
        cforge::logger::print_verbose("Locked " + key + " @ " + resolved_version);
      }
    }

    // Also handle git dependencies
    for (const auto &key : config.get_table_keys("dependencies.git")) {
      std::string url = config.get_string("dependencies.git." + key + ".url", "");
      std::string tag = config.get_string("dependencies.git." + key + ".tag", "");
      std::string branch = config.get_string("dependencies.git." + key + ".branch", "");
      std::string commit = config.get_string("dependencies.git." + key + ".commit", "");

      lock_file << "[dependency." << key << "]\n";
      lock_file << "source_type = \"git\"\n";
      if (!url.empty()) lock_file << "url = \"" << url << "\"\n";
      if (!tag.empty()) lock_file << "version = \"" << tag << "\"\n";
      if (!branch.empty()) lock_file << "branch = \"" << branch << "\"\n";
      if (!commit.empty()) lock_file << "resolved = \"" << commit << "\"\n";
      lock_file << "\n";
      has_deps = true;
    }

    lock_file.close();

    if (!has_deps) {
      cforge::logger::print_warning("No dependencies found to lock");
      std::filesystem::remove(lock_path);
      return 0;
    }

    cforge::logger::generated(std::string(cforge::LOCK_FILE));
    cforge::logger::print_action(
        "Note", "commit this file to version control for reproducible builds");
    return 0;
  }

  // Non-FetchContent mode: scan deps directory
  if (!std::filesystem::exists(deps_dir)) {
    cforge::logger::print_warning("Dependencies directory not found: " +
                          deps_dir.string());
    cforge::logger::print_action("Help",
                         "run 'cforge build' first to fetch dependencies");
    return 1;
  }

  if (cforge::update_lockfile(current_dir, deps_dir, verbose)) {
    cforge::logger::generated(std::string(cforge::LOCK_FILE));
    cforge::logger::print_action(
        "Note", "commit this file to version control for reproducible builds");
    return 0;
  } else {
    cforge::logger::print_error("Failed to create lock file");
    return 1;
  }
}
