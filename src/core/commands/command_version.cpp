/**
 * @file command_version.cpp
 * @brief Implementation of the 'version' command to display cforge version
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"

#include <iostream>
#include <string>

/**
 * @brief Display cforge version information
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_version([[maybe_unused]] const cforge_context_t *ctx) {
  cforge::logger::print_action("Version",
                       "cforge version " + std::string(CFORGE_VERSION));
  cforge::logger::print_action("Info", "C++ Project Management Tool");
  cforge::logger::print_action("Info", "Copyright (c) 2023-2024");

  return 0;
}