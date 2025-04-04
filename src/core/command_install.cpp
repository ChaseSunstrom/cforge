/**
 * @file command_install.cpp
 * @brief Implementation of the 'install' command to install cforge or projects
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "core/installer.hpp"
#include "cforge/log.hpp"

#include <filesystem>
#include <string>
#include <cstring>

using namespace cforge;

/**
 * @brief Handle the 'install' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_install(const cforge_context_t* ctx) {
    // Check if help was requested
    if (ctx->args.args) {
        for (int i = 0; i < ctx->args.arg_count; i++) {
            if (strcmp(ctx->args.args[i], "--help") == 0 || 
                strcmp(ctx->args.args[i], "-h") == 0) {
                // Create a new context with "install" as the first argument
                cforge_context_t help_ctx = *ctx;
                static const char* help_args[] = { "install", NULL };
                help_ctx.args.args = (cforge_string_t*)help_args;
                help_ctx.args.arg_count = 1;
                return cforge_cmd_help(&help_ctx);
            }
        }
    }

    logger::print_header("Installing cforge");
    
    installer installer_instance;
    
    // Parse arguments
    std::string project_path;
    std::string install_path;
    bool add_to_path = false;
    
    // Check for --add-to-path flag
    if (ctx->args.args) {
        for (int i = 0; i < ctx->args.arg_count; i++) {
            if (strcmp(ctx->args.args[i], "--add-to-path") == 0) {
                add_to_path = true;
                logger::print_status("Will add to PATH environment variable");
            }
        }
    }
    
    if (ctx->args.project) {
        // Installing a project to a path
        project_path = ctx->args.project;
        logger::print_status("Installing project: " + project_path);
    }
    
    // Check for installation path in remaining arguments
    if (ctx->args.args && ctx->args.arg_count > 0) {
        // Skip the path if it's the --add-to-path argument
        if (strcmp(ctx->args.args[0], "--add-to-path") != 0) {
            install_path = ctx->args.args[0];
            if (!install_path.empty()) {
                logger::print_status("Installation path: " + install_path);
            }
        }
    }
    
    bool success = false;
    
    if (!project_path.empty()) {
        // Installing a project
        success = installer_instance.install_project(project_path, install_path, add_to_path);
    } else {
        // Installing cforge itself
        success = installer_instance.install(install_path, add_to_path);
    }
    
    if (success) {
        if (project_path.empty()) {
            logger::print_success("cforge has been installed successfully");
            if (install_path.empty()) {
                logger::print_status("Installation path: " + installer_instance.get_default_install_path());
            }
        } else {
            logger::print_success("Project has been installed successfully");
        }
        return 0;
    } else {
        logger::print_error("Installation failed");
        return 1;
    }
} 