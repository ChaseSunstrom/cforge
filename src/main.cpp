/**
 * @file   main.cpp
 * @brief  Entry point for the CForge build system
 */

#include "cforge/log.hpp"
#include "core/command.h"
#include "core/commands.hpp"
#include "core/file_system.h"
#include "core/process.h"
#include "core/types.h"

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * @brief Initialize the CForge context with command line arguments and
 * environment settings
 */
cforge_int_t cforge_init_context(cforge_int_t argc, cforge_string_t argv[],
                                 cforge_context_t *ctx) {
  // Initialize context
  memset(ctx, 0, sizeof(*ctx));

  // Parse command line arguments
  cforge_parse_args(argc, argv, &ctx->args);

  // Set verbosity level from command line or environment
  cforge_cstring_t env_verbose = getenv("CFORGE_VERBOSE");
  cforge_cstring_t env_quiet = getenv("CFORGE_QUIET");

  if (env_verbose &&
      (strcmp(env_verbose, "1") == 0 || strcmp(env_verbose, "true") == 0)) {
    cforge_set_verbosity("verbose");
  } else if (env_quiet &&
             (strcmp(env_quiet, "1") == 0 || strcmp(env_quiet, "true") == 0)) {
    cforge_set_verbosity("quiet");
  } else if (ctx->args.verbosity) {
    cforge_set_verbosity(ctx->args.verbosity);
  }

  // Get current working directory
  if (getcwd(ctx->working_dir, sizeof(ctx->working_dir)) == NULL) {
    cforge_print_error("Failed to get current directory");
    return 1;
  }

  // Check if in a workspace
  ctx->is_workspace = cforge_is_workspace_dir();

  return 0;
}

cforge_int_t cforge_main(cforge_int_t argc, cforge_string_t argv[]) {
  cforge_context_t ctx;
  if (cforge_init_context(argc, argv, &ctx) != 0) {
    return 1;
  }

  // Show header only if not in quiet mode
  cforge_char_t header_buffer[64];
  snprintf(header_buffer, sizeof(header_buffer),
           "cforge - C/C++ Build System %s", CFORGE_VERSION);

  if (!cforge_is_quiet()) {
    cforge_print_header(header_buffer);
  }

  // Dispatch command
  cforge_int_t result = cforge_dispatch_command(&ctx);

  // Free allocated resources
  cforge_free_args(&ctx.args);

  // Show completion message if command succeeded
  if (result == 0 && !cforge_is_quiet()) {
    cforge_print_success("Command completed successfully");
  }

  else if (result != 0) {
    cforge_print_error("Command failed");
  }

  return result;
}

// Win32 entry point
#ifdef _WIN32
cforge_int_t WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                            LPSTR lpCmdLine, cforge_int_t nCmdShow) {
  // Get command line arguments using Windows API
  cforge_int_t argc;
  LPWSTR *argvW = CommandLineToArgvW(GetCommandLineW(), &argc);

  if (argvW == NULL) {
    MessageBoxA(NULL, "Failed to parse command line", "CForge Error",
                MB_ICONERROR);
    return 1;
  }

  // Convert wide char arguments to regular char
  cforge_string_t *argv =
      (cforge_string_t *)malloc(argc * sizeof(cforge_string_t));
  if (!argv) {
    LocalFree(argvW);
    return 1;
  }

  for (cforge_int_t i = 0; i < argc; i++) {
    cforge_int_t size_needed =
        WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, NULL, 0, NULL, NULL);
    argv[i] = (cforge_string_t)malloc(size_needed);
    if (argv[i]) {
      WideCharToMultiByte(CP_UTF8, 0, argvW[i], -1, argv[i], size_needed, NULL,
                          NULL);
    }
  }

  // Call the main function
  cforge_int_t result = cforge_main(argc, argv);

  // Free allocated memory
  for (cforge_int_t i = 0; i < argc; i++) {
    if (argv[i])
      free(argv[i]);
  }

  free(argv);
  LocalFree(argvW);

  return result;
}
#endif

// Standard C/C++ entry point
cforge_int_t main(cforge_int_t argc, cforge_string_t argv[]) {
  return cforge_main(argc, argv);
}