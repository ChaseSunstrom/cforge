#include "cforge/log.hpp"
#include "core/command.h"
#include "core/commands.hpp"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace cforge;

/**
 * @brief Parse command line arguments into context structure
 *
 * @param argc Argument count
 * @param argv Argument array
 * @param ctx Context to populate
 * @return bool Success flag
 */
bool parse_command_line(int argc, char *argv[], cforge_context_t *ctx) {
  // Zero-initialize the context
  memset(ctx, 0, sizeof(cforge_context_t));

  // Get current working directory
  std::filesystem::path cwd = std::filesystem::current_path();
  strncpy(ctx->working_dir, cwd.string().c_str(), sizeof(ctx->working_dir) - 1);

  // Temporary vector to store args while we process them
  std::vector<std::string> remaining_args;

  // Process arguments
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    remaining_args.push_back(arg);
  }

  // Process remaining arguments as before
  if (remaining_args.empty()) {
    logger::print_error("No command specified");
    return false;
  }

  // First argument is the command
  ctx->args.command = strdup(remaining_args[0].c_str());

  // Process flags
  for (size_t i = 1; i < remaining_args.size(); i++) {
    std::string arg = remaining_args[i];

    if (arg == "-c" || arg == "--config") {
      if (i + 1 < remaining_args.size()) {
        ctx->args.config = strdup(remaining_args[i + 1].c_str());
        i++; // Skip the next argument
      }
    } else if (arg == "-v" || arg == "--verbose") {
      ctx->args.verbosity = strdup("verbose");
    } else if (arg == "-q" || arg == "--quiet") {
      ctx->args.verbosity = strdup("quiet");
    }
  }

  // Store remaining arguments
  ctx->args.arg_count = static_cast<int>(remaining_args.size());
  if (ctx->args.arg_count > 0) {
    ctx->args.args = (cforge_string_t *)malloc(ctx->args.arg_count *
                                               sizeof(cforge_string_t));
    for (int i = 0; i < ctx->args.arg_count; i++) {
      ctx->args.args[i] = strdup(remaining_args[i].c_str());
    }
  }

  return true;
}

/**
 * @brief Dispatch command based on command line arguments
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
extern "C" cforge_int_t cforge_dispatch_command(const cforge_context_t *ctx) {
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
  } else if (strcmp(ctx->args.command, "lock") == 0) {
    return cforge_cmd_lock(ctx);
  } else if (strcmp(ctx->args.command, "fmt") == 0 ||
             strcmp(ctx->args.command, "format") == 0) {
    return cforge_cmd_fmt(ctx);
  } else if (strcmp(ctx->args.command, "lint") == 0 ||
             strcmp(ctx->args.command, "check") == 0) {
    return cforge_cmd_lint(ctx);
  } else if (strcmp(ctx->args.command, "watch") == 0) {
    return cforge_cmd_watch(ctx);
  } else if (strcmp(ctx->args.command, "completions") == 0) {
    return cforge_cmd_completions(ctx);
  } else if (strcmp(ctx->args.command, "doc") == 0 ||
             strcmp(ctx->args.command, "docs") == 0) {
    return cforge_cmd_doc(ctx);
  } else if (strcmp(ctx->args.command, "tree") == 0) {
    return cforge_cmd_tree(ctx);
  } else if (strcmp(ctx->args.command, "new") == 0) {
    return cforge_cmd_new(ctx);
  } else if (strcmp(ctx->args.command, "bench") == 0 ||
             strcmp(ctx->args.command, "benchmark") == 0) {
    return cforge_cmd_bench(ctx);
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