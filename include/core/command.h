/**
 * @file command.h
 * @brief Command line argument parsing and command dispatching for CForge
 */

#ifndef CFORGE_COMMAND_H
#define CFORGE_COMMAND_H

#include <stdbool.h>

#include "core/constants.h"
#include "core/types.h"

// Platform-specific includes
#ifdef _WIN32
#include <direct.h> // For _getcwd
#include <io.h>     // For _access
#include <windows.h>
#define F_OK 0
#define access _access
#define getcwd _getcwd
#else
#include <unistd.h> // For getcwd, access
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Command line argument structure
 * @details This structure holds the command line arguments parsed from the
 * input.
 */
typedef struct {
  cforge_string_t command;   // Primary command (build, run, clean, etc.)
  cforge_string_t project;   // Optional project name
  cforge_string_t config;    // Optional build configuration
  cforge_string_t variant;   // Optional build variant
  cforge_string_t target;    // Optional cross-compile target
  cforge_string_t *args;     // Additional arguments for the command
  cforge_int_t arg_count;    // Number of additional arguments
  cforge_string_t verbosity; // Verbosity level (quiet, normal, verbose)
} cforge_command_args_t;

/**
 * @brief Context structure for command execution
 * @details This structure holds the context for executing a command, including
 * the command arguments and working directory.
 */
typedef struct {
  cforge_command_args_t args;
  bool is_workspace;
  cforge_char_t working_dir[256];
} cforge_context_t;

/**
 * @brief Check if the current directory is a workspace
 */
bool cforge_is_workspace_dir(void);

/**
 * * @brief Parse command line arguments
 */
void cforge_parse_args(cforge_int_t argc, cforge_string_t argv[],
                       cforge_command_args_t *args);

/**
 * * @brief Free allocated resources in command arguments
 */
void cforge_free_args(cforge_command_args_t *args);

/**
 * @brief Set the verbosity level for logging
 * @details This function sets the verbosity level for logging output. It can be
 * set to quiet, normal, or verbose.
 */
void cforge_set_verbosity(cforge_cstring_t level);

/**
 * @brief Check if the current verbosity level is quiet
 */
bool cforge_is_quiet(void);

/**
 * * @brief Check if the current verbosity level is verbose
 */
bool cforge_is_verbose(void);

#ifdef __cplusplus
}
#endif

#endif