/**
 * @file commands.hpp
 * @brief Declarations for cforge command handlers
 */

#pragma once

#include "core/command.h" // Include for cforge_context_t
#include "core/types.h"
#include <cstdint>
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

/**
 * @brief Handle the 'lock' command to manage dependency lock file
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_lock(const cforge_context_t *ctx);

/**
 * @brief Handle the 'fmt' command to format source code with clang-format
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_fmt(const cforge_context_t *ctx);

/**
 * @brief Handle the 'lint' command to run clang-tidy static analysis
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_lint(const cforge_context_t *ctx);

/**
 * @brief Handle the 'watch' command for auto-rebuild on file changes
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_watch(const cforge_context_t *ctx);

/**
 * @brief Handle the 'completions' command to generate shell completions
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_completions(const cforge_context_t *ctx);

/**
 * @brief Handle the 'doc' command to generate documentation with Doxygen
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_doc(const cforge_context_t *ctx);

/**
 * @brief Handle the 'tree' command to visualize dependencies
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_tree(const cforge_context_t *ctx);

/**
 * @brief Handle the 'new' command to create files from templates
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_new(const cforge_context_t *ctx);

/**
 * @brief Handle the 'bench' command to run benchmarks
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_bench(const cforge_context_t *ctx);