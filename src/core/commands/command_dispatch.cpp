/**
 * @file command_dispatch.cpp
 * @brief Command parsing and dispatch using the command registry
 */

#include "cforge/log.hpp"
#include "core/command.h"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/types.h"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

/**
 * @brief Parse command line arguments into context structure
 *
 * @param argc Argument count
 * @param argv Argument array
 * @param ctx Context to populate
 * @return bool Success flag
 */
bool parse_command_line(int argc, char *argv[], cforge_context_t *ctx) {
  // Zero-initialize the context
  memset(ctx, 0, sizeof(cforge_context_t));

  // Get current working directory
  std::filesystem::path cwd = std::filesystem::current_path();
  strncpy(ctx->working_dir, cwd.string().c_str(), sizeof(ctx->working_dir) - 1);

  // Temporary vector to store args while we process them
  std::vector<std::string> remaining_args;

  // Process arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    remaining_args.push_back(arg);
  }

  // No command specified
  if (remaining_args.empty()) {
    // Show help instead of error
    ctx->args.command = nullptr;
    return true;
  }

  // First argument is the command
  ctx->args.command = strdup(remaining_args[0].c_str());

  // Process flags
  for (cforge_size_t i = 1; i < remaining_args.size(); i++) {
    std::string arg = remaining_args[i];

    if (arg == "-c" || arg == "--config") {
      if (i + 1 < remaining_args.size()) {
        ctx->args.config = strdup(remaining_args[i + 1].c_str());
        i++; // Skip the next argument
      }
    } else if (arg.rfind("--config=", 0) == 0) {
      ctx->args.config = strdup(arg.substr(9).c_str());
    } else if (arg == "-v" || arg == "--verbose") {
      ctx->args.verbosity = strdup("verbose");
    } else if (arg == "-q" || arg == "--quiet") {
      ctx->args.verbosity = strdup("quiet");
    }
  }

  // Store remaining arguments
  ctx->args.arg_count = static_cast<cforge_int_t>(remaining_args.size());
  if (ctx->args.arg_count > 0) {
    ctx->args.args = (cforge_string_t *)malloc(ctx->args.arg_count *
                                               sizeof(cforge_string_t));
    for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
      ctx->args.args[i] = strdup(remaining_args[i].c_str());
    }
  }

  return true;
}

/**
 * @brief Dispatch command based on command line arguments
 *
 * Uses the command registry for cleaner, table-based dispatch with
 * support for aliases, deprecation warnings, and command suggestions.
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
extern "C" cforge_int_t cforge_dispatch_command(const cforge_context_t *ctx) {
  // Ensure commands are registered
  static bool initialized = false;
  if (!initialized) {
    cforge::register_builtin_commands();
    initialized = true;
  }

  auto &registry = cforge::command_registry::instance();

  // No command specified - show help
  if (!ctx->args.command) {
    registry.print_general_help();
    return 0;
  }

  std::string cmd_name = ctx->args.command;

  // Handle --help and -h as commands
  if (cmd_name == "--help" || cmd_name == "-h") {
    registry.print_general_help();
    return 0;
  }

  // Handle --version
  if (cmd_name == "--version") {
    return cforge_cmd_version(ctx);
  }

  // Dispatch through registry
  return registry.dispatch(cmd_name, ctx);
}
