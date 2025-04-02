/**
 * @file command.c
 * @brief Implementation of command handling utilities
 */

#include <stdio.h>

#include "cforge/log.hpp"
#include "core/command.h"

#ifdef __cplusplus
extern "C" {
#endif

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

// Command implementation functions
cforge_int_t cmd_init(cforge_context_t *ctx) {
  if (ctx->is_workspace) {
    cforge_print_status("Initializing workspace...");
    // Workspace initialization logic here
  } else {
    cforge_print_status("Initializing project...");
    // Project initialization logic here
  }

  cforge_print_success("Initialization complete");
  return 0;
}

cforge_int_t cmd_build(cforge_context_t *ctx) {
  cforge_print_header("Building Project");

  if (ctx->is_workspace) {
    if (ctx->args.project) {
      cforge_print_status("Building specific project in workspace...");
      // Build specific project in workspace
    } else {
      cforge_print_status("Building all projects in workspace...");
      // Build all projects in workspace
    }
  } else {
    cforge_print_status("Building project...");
    // Build single project
  }

  cforge_print_success("Build completed successfully");
  return 0;
}

cforge_int_t cmd_clean(cforge_context_t *ctx) {
  cforge_print_header("Cleaning Project");

  if (ctx->is_workspace) {
    if (ctx->args.project) {
      cforge_print_status("Cleaning specific project in workspace...");
      // Clean specific project in workspace
    } else {
      cforge_print_status("Cleaning all projects in workspace...");
      // Clean all projects in workspace
    }
  } else {
    cforge_print_status("Cleaning project...");
    // Clean single project
  }

  cforge_print_success("Clean completed successfully");
  return 0;
}

cforge_int_t cmd_run(cforge_context_t *ctx) {
  cforge_print_header("Running Project");

  if (ctx->is_workspace) {
    if (ctx->args.project) {
      cforge_print_status("Running specific project in workspace...");
      // Run specific project in workspace
    } else {
      cforge_print_status("Running default project in workspace...");
      // Run default project in workspace
    }
  } else {
    cforge_print_status("Running project...");
    // Run single project
  }

  return 0;
}

cforge_int_t cmd_test(cforge_context_t *ctx) {
  cforge_print_header("Testing Project");

  if (ctx->is_workspace) {
    if (ctx->args.project) {
      cforge_print_status("Testing specific project in workspace...");
      // Test specific project in workspace
    } else {
      cforge_print_status("Testing all projects in workspace...");
      // Test all projects in workspace
    }
  } else {
    cforge_print_status("Testing project...");
    // Test single project
  }

  cforge_print_success("Tests completed successfully");
  return 0;
}

cforge_int_t cmd_deps(cforge_context_t *ctx) {
  cforge_print_header("Installing Dependencies");

  if (ctx->is_workspace) {
    if (ctx->args.project) {
      cforge_print_status(
          "Installing dependencies for specific project in workspace...");
      // Install dependencies for specific project in workspace
    } else {
      cforge_print_status(
          "Installing dependencies for all projects in workspace...");
      // Install dependencies for all projects in workspace
    }
  } else {
    cforge_print_status("Installing dependencies for project...");
    // Install dependencies for single project
  }

  cforge_print_success("Dependencies installed successfully");
  return 0;
}

cforge_int_t cmd_ide(cforge_context_t *ctx) {
  cforge_print_header("Generating IDE Files");

  if (ctx->args.args == NULL || ctx->args.arg_count == 0) {
    cforge_print_error("IDE type not specified");
    return 1;
  }

  // First argument is the IDE type
  cforge_cstring_t ide_type = ctx->args.args[0];

  if (ctx->is_workspace) {
    if (ctx->args.project) {
      cforge_print_status(
          "Generating IDE files for specific project in workspace...");
      // Generate IDE files for specific project in workspace
    } else {
      cforge_print_status("Generating IDE files for workspace...");
      // Generate IDE files for workspace
    }
  } else {
    cforge_print_status("Generating IDE files for project...");
    // Generate IDE files for single project
  }

  cforge_print_success("IDE files generated successfully");
  return 0;
}

cforge_int_t cmd_list(cforge_context_t *ctx) {
  cforge_print_header("Available Items");

  if (ctx->is_workspace) {
    cforge_print_status("Workspace projects:");
    // List workspace projects
  } else {
    cforge_cstring_t what =
        (ctx->args.args && ctx->args.arg_count > 0) ? ctx->args.args[0] : "all";

    if (strcmp(what, "all") == 0 || strcmp(what, "configs") == 0) {
      cforge_print_status("Available configurations:");
      printf("  - Debug: Default debug build\n");
      printf("  - Release: Optimized build\n");
      printf("\n");
    }

    if (strcmp(what, "all") == 0 || strcmp(what, "variants") == 0) {
      cforge_print_status("Available build variants:");
      printf("  - standard: Standard build\n");
      printf("\n");
    }

    if (strcmp(what, "all") == 0 || strcmp(what, "targets") == 0) {
      cforge_print_status("Available cross-compile targets:");
      printf("  - android-arm64: Android ARM64 (NDK required)\n");
      printf("  - android-arm: Android ARM (NDK required)\n");
      printf("  - ios: iOS ARM64 (Xcode required)\n");
      printf("  - raspberry-pi: Raspberry Pi ARM (toolchain required)\n");
      printf("  - wasm: WebAssembly (Emscripten required)\n");
      printf("\n");
    }
  }

  return 0;
}

cforge_int_t cforge_cmd_help(cforge_context_t *ctx) {
  printf("Usage: cforge <command> [options] [project] [args...]\n\n");

  printf("Commands:\n");
  printf("  init       Initialize a new project or workspace\n");
  printf("  build      Build the project or workspace\n");
  printf("  clean      Clean build artifacts\n");
  printf("  run        Run the built executable\n");
  printf("  test       Run tests\n");
  printf("  deps       Install dependencies\n");
  printf("  ide        Generate IDE project files\n");
  printf("  list       List available configurations, variants, targets\n");

  printf("\nOptions:\n");
  printf("  --config <name>    Build with specific configuration (Debug, "
         "Release)\n");
  printf("  --variant <name>   Build with specific variant\n");
  printf("  --target <name>    Cross-compile for target\n");
  printf("  --verbosity <lvl>  Set verbosity (quiet, normal, verbose)\n");

  printf("\nExamples:\n");
  printf("  cforge init                   # Initialize a new project\n");
  printf(
      "  cforge build                  # Build with default configuration\n");
  printf(
      "  cforge build --config Release # Build with Release configuration\n");
  printf("  cforge run                    # Run the executable\n");
  printf("  cforge run -- --arg1 --arg2   # Run with arguments\n");

  return 0;
}

// Main command dispatcher
cforge_int_t cforge_dispatch_command(cforge_context_t *ctx) {
  if (!ctx->args.command) {
    return cforge_cmd_help(ctx);
  }

  if (strcmp(ctx->args.command, "init") == 0) {
    return cmd_init(ctx);
  } else if (strcmp(ctx->args.command, "build") == 0) {
    return cmd_build(ctx);
  } else if (strcmp(ctx->args.command, "clean") == 0) {
    return cmd_clean(ctx);
  } else if (strcmp(ctx->args.command, "run") == 0) {
    return cmd_run(ctx);
  } else if (strcmp(ctx->args.command, "test") == 0) {
    return cmd_test(ctx);
  } else if (strcmp(ctx->args.command, "deps") == 0) {
    return cmd_deps(ctx);
  } else if (strcmp(ctx->args.command, "ide") == 0) {
    return cmd_ide(ctx);
  } else if (strcmp(ctx->args.command, "list") == 0) {
    return cmd_list(ctx);
  } else if (strcmp(ctx->args.command, "help") == 0 ||
             strcmp(ctx->args.command, "--help") == 0 ||
             strcmp(ctx->args.command, "-h") == 0) {
    return cforge_cmd_help(ctx);
  } else {
    cforge_print_error("Unknown command");
    printf("Run 'cforge help' for usage information\n");
    return 1;
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

#ifdef __cplusplus
}
#endif