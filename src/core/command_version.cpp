/**
 * @file command_version.cpp
 * @brief Implementation of the 'version' command to display cforge version
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"

#include <iostream>
#include <string>

using namespace cforge;

/**
 * @brief Display cforge version information
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_version(const cforge_context_t *ctx) {
  logger::print_action("Version",
                       "cforge version " + std::string(CFORGE_VERSION));
  logger::print_action("Info", "C++ Project Management Tool");
  logger::print_action("Info", "Copyright (c) 2023-2024");

  return 0;
}