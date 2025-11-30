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
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <cstring>
#include <filesystem>
#include <string>

using namespace cforge;

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

  for (int i = 0; i < ctx->args.arg_count; ++i) {
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
      logger::print_plain("Usage: cforge lock [options]");
      logger::print_plain("");
      logger::print_plain(
          "Generate or verify dependency lock file (cforge.lock)");
      logger::print_plain("");
      logger::print_plain("Options:");
      logger::print_plain(
          "  --verify, -v   Verify dependencies match lock file");
      logger::print_plain("  --clean, -c    Remove the lock file");
      logger::print_plain(
          "  --force, -f    Force regeneration even if lock exists");
      logger::print_plain("  --help, -h     Show this help message");
      logger::print_plain("");
      logger::print_plain(
          "The lock file ensures reproducible builds by tracking");
      logger::print_plain(
          "exact versions (commit hashes) of all dependencies.");
      return 0;
    }
  }

  // Check if we're in a workspace or project
  bool is_workspace = ctx->is_workspace;
  std::filesystem::path config_path;

  if (is_workspace) {
    config_path = current_dir / WORKSPACE_FILE;
  } else {
    config_path = current_dir / CFORGE_FILE;
  }

  if (!std::filesystem::exists(config_path)) {
    logger::print_error("No cforge project found in current directory");
    logger::print_error("Run 'cforge init' to create a new project");
    return 1;
  }

  // Handle --clean option
  if (clean_lock) {
    std::filesystem::path lock_path = current_dir / LOCK_FILE;
    if (std::filesystem::exists(lock_path)) {
      try {
        std::filesystem::remove(lock_path);
        logger::removing(std::string(LOCK_FILE));
      } catch (const std::exception &e) {
        logger::print_error("Failed to remove lock file: " +
                            std::string(e.what()));
        return 1;
      }
    } else {
      logger::print_action("Skipping", "no lock file to remove");
    }
    return 0;
  }

  // Load project configuration
  toml_reader config;
  if (!config.load(config_path.string())) {
    logger::print_error("Failed to load configuration: " +
                        config_path.string());
    return 1;
  }

  // Get dependencies directory
  std::string deps_dir_str =
      config.get_string("dependencies.directory", "vendor");
  std::filesystem::path deps_dir = current_dir / deps_dir_str;

  // Handle --verify option
  if (verify_only) {
    logger::print_action("Verifying", "dependencies against lock file");

    if (!lockfile::exists(current_dir)) {
      logger::print_warning(
          "No lock file found. Run 'cforge lock' to create one");
      return 1;
    }

    if (verify_lockfile(current_dir, deps_dir, verbose)) {
      logger::print_action("Verified", "all dependencies match lock file");
      return 0;
    } else {
      logger::print_error("Dependencies do not match lock file");
      logger::print_action(
          "Help", "run 'cforge lock' to update, or 'cforge deps' to restore");
      return 1;
    }
  }

  // Check if lock file already exists
  if (lockfile::exists(current_dir) && !force_update) {
    logger::print_action("Checking",
                         "lock file already exists. Use --force to regenerate");

    // Still verify it
    if (verify_lockfile(current_dir, deps_dir, verbose)) {
      logger::print_action("Verified", "dependencies match lock file");
      return 0;
    } else {
      logger::print_warning(
          "Dependencies have changed. Use --force to update lock file");
      return 1;
    }
  }

  // Generate/update lock file
  logger::print_action("Generating", "lock file");

  if (!std::filesystem::exists(deps_dir)) {
    logger::print_warning("Dependencies directory not found: " +
                          deps_dir.string());
    logger::print_action("Help",
                         "run 'cforge build' first to fetch dependencies");
    return 1;
  }

  if (update_lockfile(current_dir, deps_dir, verbose)) {
    logger::generated(std::string(LOCK_FILE));
    logger::print_action(
        "Note", "commit this file to version control for reproducible builds");
    return 0;
  } else {
    logger::print_error("Failed to create lock file");
    return 1;
  }
}
