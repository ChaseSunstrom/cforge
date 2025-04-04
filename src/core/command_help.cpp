/**
 * @file command_help.cpp
 * @brief Implementation of the 'help' command to provide usage information
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "cforge/log.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace cforge;

/**
 * @brief Handle the 'help' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_help(const cforge_context_t* ctx) {
    std::string specific_command;
    
    // Check if a specific command was requested
    if (ctx->args.args && ctx->args.args[0] && ctx->args.args[0][0] != '-') {
        specific_command = ctx->args.args[0];
    }
    
    if (specific_command.empty()) {
        // Display general help
        logger::print_status("cforge - C++ Project Management Tool");
        logger::print_status("");
        logger::print_status("Usage: cforge <command> [options]");
        logger::print_status("");
        logger::print_status("Commands:");
        logger::print_status("  init [name]      Initialize a new C++ project");
        logger::print_status("  build [options]  Build the project");
        logger::print_status("  clean [options]  Clean build artifacts");
        logger::print_status("  run [options]    Build and run the project");
        logger::print_status("  test [options]   Run tests");
        logger::print_status("  package [options] Create distributable packages");
        logger::print_status("  add <pkg>        Add a dependency");
        logger::print_status("  remove <pkg>     Remove a dependency");
        logger::print_status("  update           Update dependencies");
        logger::print_status("  vcpkg <args>     Run vcpkg commands");
        logger::print_status("  version          Show cforge version");
        logger::print_status("  help [command]   Show help information");
        logger::print_status("");
        logger::print_status("For more information on a specific command, run 'cforge help <command>'");
    } else if (specific_command == "init") {
        logger::print_status("cforge init - Initialize a new C++ project");
        logger::print_status("");
        logger::print_status("Usage: cforge init [name] [options]");
        logger::print_status("");
        logger::print_status("Arguments:");
        logger::print_status("  name             Project name (default: current directory name)");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  --std=c++XX      Set C++ standard (11, 14, 17, 20) (default: 17)");
        logger::print_status("  --git            Initialize git repository (disabled by default)");
        logger::print_status("  -v, --verbose    Show verbose output");
        logger::print_status("");
        logger::print_status("Notes:");
        logger::print_status("  - Hyphens in project names are replaced with underscores in code");
        logger::print_status("  - If no name is provided, the current directory name is used");
    } else if (specific_command == "build") {
        logger::print_status("cforge build - Build the project");
        logger::print_status("");
        logger::print_status("Usage: cforge build [options]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -c, --config <config>  Build configuration (Debug, Release, etc.)");
        logger::print_status("  --clean                Clean before building");
        logger::print_status("  -j <N>                 Number of parallel jobs (default: auto)");
        logger::print_status("  -v, --verbose         Show verbose output");
    } else if (specific_command == "clean") {
        logger::print_status("cforge clean - Clean build artifacts");
        logger::print_status("");
        logger::print_status("Usage: cforge clean [options]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -c, --config <config>  Clean specific configuration");
        logger::print_status("  --all                  Clean all configurations");
        logger::print_status("  --cmake-files          Also clean CMake temporary files");
        logger::print_status("  --regenerate           Regenerate CMake files after cleaning");
        logger::print_status("  -v, --verbose          Show verbose output");
    } else if (specific_command == "run") {
        logger::print_status("cforge run - Build and run the project");
        logger::print_status("");
        logger::print_status("Usage: cforge run [options] [-- <app arguments>]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -c, --config <config>  Build configuration (Debug, Release, etc.)");
        logger::print_status("  --no-build             Skip building before running");
        logger::print_status("  -v, --verbose          Show verbose output");
        logger::print_status("");
        logger::print_status("Arguments after -- are passed to the application");
    } else if (specific_command == "test") {
        logger::print_status("cforge test - Run tests");
        logger::print_status("");
        logger::print_status("Usage: cforge test [options] [-- <test arguments>]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -c, --config <config>  Build configuration (Debug, Release, etc.)");
        logger::print_status("  --no-build             Skip building before running tests");
        logger::print_status("  -v, --verbose          Show verbose output");
        logger::print_status("");
        logger::print_status("Arguments after -- are passed to the test executable");
    } else if (specific_command == "package") {
        logger::print_status("cforge package - Create distributable packages");
        logger::print_status("");
        logger::print_status("Usage: cforge package [options]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -c, --config <config>  Build configuration (Debug, Release, etc.)");
        logger::print_status("  -t, --type <type>      Package type (ZIP, TGZ, NSIS, etc.)");
        logger::print_status("  --no-build             Skip building before packaging");
        logger::print_status("  -v, --verbose          Show verbose output");
    } else if (specific_command == "add") {
        logger::print_status("cforge add - Add a dependency");
        logger::print_status("");
        logger::print_status("Usage: cforge add <package> [options]");
        logger::print_status("");
        logger::print_status("Arguments:");
        logger::print_status("  package          Package to add (format: name[:version])");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -v, --verbose    Show verbose output");
    } else if (specific_command == "remove") {
        logger::print_status("cforge remove - Remove a dependency");
        logger::print_status("");
        logger::print_status("Usage: cforge remove <package> [options]");
        logger::print_status("");
        logger::print_status("Arguments:");
        logger::print_status("  package          Package to remove");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -v, --verbose    Show verbose output");
    } else if (specific_command == "update") {
        logger::print_status("cforge update - Update dependencies");
        logger::print_status("");
        logger::print_status("Usage: cforge update [options]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -v, --verbose    Show verbose output");
    } else if (specific_command == "vcpkg") {
        logger::print_status("cforge vcpkg - Run vcpkg commands");
        logger::print_status("");
        logger::print_status("Usage: cforge vcpkg <args...>");
        logger::print_status("");
        logger::print_status("Arguments:");
        logger::print_status("  args...          Arguments to pass to vcpkg");
    } else if (specific_command == "version") {
        logger::print_status("cforge version - Show cforge version");
        logger::print_status("");
        logger::print_status("Usage: cforge version");
    } else if (specific_command == "help") {
        logger::print_status("cforge help - Show help information");
        logger::print_status("");
        logger::print_status("Usage: cforge help [command]");
        logger::print_status("");
        logger::print_status("Arguments:");
        logger::print_status("  command          Command to show help for");
    } else if (specific_command == "install") {
        logger::print_status("cforge install - Install cforge to the system");
        logger::print_status("");
        logger::print_status("Usage: cforge install [options]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  --prefix=<path>  Installation prefix (default: /usr/local on Unix, Program Files on Windows)");
        logger::print_status("  --user           Install to user directory instead of system directory");
        logger::print_status("  --add-to-path    Add installation directory to PATH environment variable");
        logger::print_status("  -v, --verbose    Show verbose output");
        logger::print_status("");
        logger::print_status("Installing a project:");
        logger::print_status("  cforge install <project_dir> [install_path] [options]");
        logger::print_status("");
        logger::print_status("  Options:");
        logger::print_status("  --add-to-path    Add installation directory to PATH environment variable");
        logger::print_status("  -v, --verbose    Show verbose output");
    } else {
        logger::print_error("Unknown command: " + specific_command);
        logger::print_status("Run 'cforge help' for a list of available commands");
        return 1;
    }
    
    return 0;
} 