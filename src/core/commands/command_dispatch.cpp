/**
 * @file command_dispatch.cpp
 * @brief Command parsing and dispatch using the command registry
 */

#include "cforge/log.hpp"
#include "core/command.h"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/types.h"

#include <string>

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
