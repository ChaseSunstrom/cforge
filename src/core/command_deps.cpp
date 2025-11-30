/**
 * @file command_deps.cpp
 * @brief Implementation of the 'deps' command to manage dependencies
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"

#include <string>

using namespace cforge;

/**
 * @brief Handle the 'deps' command - redirects to vcpkg
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_deps(const cforge_context_t *ctx) {
  logger::print_action("Info",
                       "The 'deps' command is a shorthand for 'vcpkg install'");
  logger::print_action("Redirecting", "to 'vcpkg install'");

  // Simply redirect to vcpkg install command
  return cforge_cmd_vcpkg(ctx);
}