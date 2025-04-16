#include "core/commands.hpp"
#include "core/command.h"
#include "cforge/log.hpp"
#include <string>
#include <iostream>

using namespace cforge;

/**
 * @brief Dispatch command based on command line arguments
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
extern "C" cforge_int_t cforge_dispatch_command(const cforge_context_t* ctx) {
  // Check if command is null
  if (!ctx->args.command) {
    // No command specified, show help
    return cforge_cmd_help(ctx);
  }

  // Dispatch command based on name
  if (strcmp(ctx->args.command, "init") == 0) {
    return cforge_cmd_init(ctx);
  } else if (strcmp(ctx->args.command, "build") == 0) {
    return cforge_cmd_build(ctx);
  } else if (strcmp(ctx->args.command, "clean") == 0) {
    return cforge_cmd_clean(ctx);
  } else if (strcmp(ctx->args.command, "run") == 0) {
    return cforge_cmd_run(ctx);
  } else if (strcmp(ctx->args.command, "test") == 0) {
    return cforge_cmd_test(ctx);
  } else if (strcmp(ctx->args.command, "package") == 0) {
    return cforge_cmd_package(ctx);
  } else if (strcmp(ctx->args.command, "deps") == 0) {
    return cforge_cmd_deps(ctx);
  } else if (strcmp(ctx->args.command, "install") == 0) {
    return cforge_cmd_install(ctx);
  } else if (strcmp(ctx->args.command, "update") == 0) {
    return cforge_cmd_update(ctx);
  } else if (strcmp(ctx->args.command, "vcpkg") == 0) {
    return cforge_cmd_vcpkg(ctx);
  } else if (strcmp(ctx->args.command, "add") == 0) {
    return cforge_cmd_add(ctx);
  } else if (strcmp(ctx->args.command, "remove") == 0) {
    return cforge_cmd_remove(ctx);
  } else if (strcmp(ctx->args.command, "version") == 0) {
    return cforge_cmd_version(ctx);
  } else if (strcmp(ctx->args.command, "ide") == 0) {
    return cforge_cmd_ide(ctx);
  } else if (strcmp(ctx->args.command, "list") == 0) {
    return cforge_cmd_list(ctx);
  } else if (strcmp(ctx->args.command, "pack") == 0) {
    return cforge_cmd_package(ctx);
  } else if (strcmp(ctx->args.command, "help") == 0 ||
             strcmp(ctx->args.command, "--help") == 0 ||
             strcmp(ctx->args.command, "-h") == 0) {
    return cforge_cmd_help(ctx);
  } else {
    logger::print_error("Unknown command: " + std::string(ctx->args.command));
    logger::print_status("Run 'cforge help' for usage information");
    return 1;
  }
} 