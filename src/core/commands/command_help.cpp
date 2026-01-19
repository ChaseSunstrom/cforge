/**
 * @file command_help.cpp
 * @brief Implementation of the 'help' command to provide usage information
 */

#include "cforge/log.hpp"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/constants.h"

#include <string>

using cforge::logger;

/**
 * @brief Handle the 'help' command
 */
cforge_int_t cforge_cmd_help(const cforge_context_t *ctx) {
  std::string cmd;

  // Check if a specific command was requested
  if (ctx->args.args && ctx->args.args[0] && ctx->args.args[0][0] != '-') {
    cmd = ctx->args.args[0];
  }

  auto &registry = cforge::command_registry::instance();

  if (cmd.empty()) {
    registry.print_general_help();
    return 0;
  }

  // Handle deprecated commands that are now subcommands of deps
  if (cmd == "add" || cmd == "remove" || cmd == "search" || cmd == "info" || cmd == "list") {
    logger::print_warning("'" + cmd + "' is now a subcommand of 'deps'");
    logger::print_blank();
    logger::print_hint("Use 'cforge deps " + cmd + "' instead");
    logger::print_hint("Run 'cforge help deps' for more information");
    return 0;
  }

  // Delegate to command registry for consistent help output
  // This ensures 'cforge help <cmd>' matches 'cforge <cmd> --help'
  registry.print_command_help(cmd);
  return 0;
}
