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

  // Allocate memory for additional arguments
  // We allocate for all possible arguments to simplify management
  args->args = (cforge_string_t *)malloc((argc - 1) * sizeof(cforge_string_t));
  args->arg_count = 0;

  if (!args->args) {
    // Handle memory allocation failure
    args->command = NULL;
    return;
  }

  // Parse the rest of the arguments
  for (cforge_int_t i = 2; i < argc; i++) {
    // Store all arguments in args->args for better access in command handlers
    args->args[args->arg_count++] = argv[i];

    // Also check for specific options
    if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      args->config = argv[++i];
      // Also add this to args array
      args->args[args->arg_count++] = argv[i];
    } else if (strcmp(argv[i], "--variant") == 0 && i + 1 < argc) {
      args->variant = argv[++i];
      // Also add this to args array
      args->args[args->arg_count++] = argv[i];
    } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
      args->target = argv[++i];
      // Also add this to args array
      args->args[args->arg_count++] = argv[i];
    } else if (strcmp(argv[i], "--verbosity") == 0 && i + 1 < argc) {
      args->verbosity = argv[++i];
      // Also add this to args array
      args->args[args->arg_count++] = argv[i];
    } else if (args->project == NULL && argv[i][0] != '-') {
      // The first non-option argument is the project
      args->project = argv[i];
    }

    // Handle --config=value format
    if (strncmp(argv[i], "--config=", 9) == 0) {
      args->config = argv[i] + 9;
    }
    // Handle --variant=value format
    else if (strncmp(argv[i], "--variant=", 10) == 0) {
      args->variant = argv[i] + 10;
    }
    // Handle --target=value format
    else if (strncmp(argv[i], "--target=", 9) == 0) {
      args->target = argv[i] + 9;
    }
    // Handle --verbosity=value format
    else if (strncmp(argv[i], "--verbosity=", 12) == 0) {
      args->verbosity = argv[i] + 12;
    }
  }

  // Null-terminate the args array to avoid crashes when no arguments are
  // present
  if (args->args) {
    args->args[args->arg_count] = NULL;
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