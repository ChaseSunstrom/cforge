/**
 * @file commands.hpp
 * @brief Declarations for cforge command handlers
 */

#pragma once

#include "core/command.h" // Include for cforge_context_t
#include "core/types.h"
#include <string>

/**
 * @brief Initialize command handlers
 */
void cforge_init_commands();

/**
 * @brief Dispatch a command based on command line arguments
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
extern "C" cforge_int_t cforge_dispatch_command(const cforge_context_t *ctx);

/**
 * @brief Handle the 'install' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_install(const cforge_context_t *ctx);

/**
 * @brief Handle the 'update' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_update(const cforge_context_t *ctx);

/**
 * @brief Handle the 'build' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_build(const cforge_context_t *ctx);

/**
 * @brief Handle the 'init' command to create a new project
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_init(const cforge_context_t *ctx);

/**
 * @brief Handle the 'clean' command to clean build files
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_clean(const cforge_context_t *ctx);

/**
 * @brief Handle the 'run' command to build and run the project
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_run(const cforge_context_t *ctx);

/**
 * @brief Handle the 'test' command to run project tests
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_test(const cforge_context_t *ctx);

/**
 * @brief Handle the 'add' command to add dependencies
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_add(const cforge_context_t *ctx);

/**
 * @brief Handle the 'remove' command to remove dependencies
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_remove(const cforge_context_t *ctx);

/**
 * @brief Handle the 'vcpkg' command to manage dependencies via vcpkg
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_vcpkg(const cforge_context_t *ctx);

/**
 * @brief Handle the 'version' command to display version info
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_version(const cforge_context_t *ctx);

/**
 * @brief Handle the 'help' command to display help
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_help(const cforge_context_t *ctx);

/**
 * @brief Handle the 'deps' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_deps(const cforge_context_t *ctx);

/**
 * @brief Handle the 'ide' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_ide(const cforge_context_t *ctx);

/**
 * @brief Handle the 'list' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_list(const cforge_context_t *ctx);

/**
 * @brief Handle the 'package' command to create distributable packages
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_package(const cforge_context_t *ctx);