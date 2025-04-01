/**
 * @file   main.cpp
 * @brief  Entry point for the CForge build system
 */

 #include "core/process.h"
 #include "core/file_system.h"
 #include "core/types.h"
 #include "core/command.h"
 #include "cforge/log.hpp"
 
 cforge_int_t main(cforge_int_t argc, cforge_string_t argv[]) {
     cforge_context_t ctx;
     memset(&ctx, 0, sizeof(ctx));
     
     // Parse command line arguments
     cforge_parse_args(argc, argv, &ctx.args);
     
     // Set verbosity level from command line or environment
     cforge_cstring_t env_verbose = getenv("CFORGE_VERBOSE");
     cforge_cstring_t env_quiet = getenv("CFORGE_QUIET");
     
     if (env_verbose && (strcmp(env_verbose, "1") == 0 || strcmp(env_verbose, "true") == 0)) {
        cforge_set_verbosity("verbose");
    } else if (env_quiet && (strcmp(env_quiet, "1") == 0 || strcmp(env_quiet, "true") == 0)) {
        cforge_set_verbosity("quiet");
    } else if (ctx.args.verbosity) {
        cforge_set_verbosity(ctx.args.verbosity);
    }
    
    // Get current working directory
    if (getcwd(ctx.working_dir, sizeof(ctx.working_dir)) == NULL) {
        cforge_print_error("Failed to get current directory");
        return 1;
    }
    
    // Check if in a workspace
    ctx.is_workspace = cforge_is_workspace_dir();
    
    // Show header only if not in quiet mode
    cforge_char_t header_buffer[64];

    snprintf(header_buffer, sizeof(header_buffer), "cforge - C/C++ Build System %s", CFORGE_VERSION);

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
    
    return result;
}