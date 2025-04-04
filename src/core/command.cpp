/**
 * @file command.cpp
 * @brief Implementation of command handling utilities
 */

#include <stdio.h>

#include "cforge/log.hpp"
#include "core/command.h"
#include "core/commands.hpp"

using namespace cforge; // Add namespace for logger

// Function to check if the current directory is a workspace
bool cforge_is_workspace_dir(void) {
  // Check if workspace file exists
  return access(WORKSPACE_FILE, F_OK) == 0;
}

// Function to parse command line arguments
void cforge_parse_args(cforge_int_t argc, cforge_string_t argv[],
                       cforge_command_args_t *args) {
  memset(args, 0, sizeof(cforge_command_args_t));

  // Need at least one argument (the command)
  if (argc < 2) {
    return;
  }

  args->command = argv[1];

  // Parse the rest of the arguments
  for (cforge_int_t i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      args->config = argv[++i];
    } else if (strcmp(argv[i], "--variant") == 0 && i + 1 < argc) {
      args->variant = argv[++i];
    } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
      args->target = argv[++i];
    } else if (strcmp(argv[i], "--verbosity") == 0 && i + 1 < argc) {
      args->verbosity = argv[++i];
    } else if (args->project == NULL && argv[i][0] != '-') {
      // The first non-option argument is the project
      args->project = argv[i];
    } else {
      // Remaining arguments are collected for commands like "run"
      // that need to pass them along
      if (args->args == NULL) {
        // Allocate space for the remaining arguments
        args->args =
            (cforge_string_t *)malloc((argc - i) * sizeof(cforge_string_t));
        args->arg_count = 0;
      }

      args->args[args->arg_count++] = argv[i];
    }
  }
}

// Function to free allocated resources
void cforge_free_args(cforge_command_args_t *args) {
  if (args->args) {
    free(args->args);
    args->args = NULL;
  }
}

// Verbosity handling
void cforge_set_verbosity(cforge_cstring_t level) {
  if (!level)
    return;

  if (strcmp(level, "quiet") == 0) {
    cforge_set_verbosity_impl(CFORGE_VERBOSITY_QUIET);
  } else if (strcmp(level, "verbose") == 0) {
    cforge_set_verbosity_impl(CFORGE_VERBOSITY_VERBOSE);
  } else {
    cforge_set_verbosity_impl(CFORGE_VERBOSITY_NORMAL);
  }
}

// Check verbosity status
bool cforge_is_quiet(void) {
  return cforge_get_verbosity() == CFORGE_VERBOSITY_QUIET;
}

bool cforge_is_verbose(void) {
  return cforge_get_verbosity() == CFORGE_VERBOSITY_VERBOSE;
}