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
        logger::print_status("cforge - C++ project management tool");
        logger::print_status("");
        logger::print_status("Available commands:");
        logger::print_status("  init     Initialize a new project or workspace");
        logger::print_status("  build    Build the project");
        logger::print_status("  clean    Clean the build directory");
        logger::print_status("  run      Run the project");
        logger::print_status("  test     Run tests");
        logger::print_status("  package  Create a package for distribution");
        logger::print_status("  add      Add a dependency to the project");
        logger::print_status("  remove   Remove a dependency from the project");
        logger::print_status("  update   Update dependencies");
        logger::print_status("  vcpkg    Manage vcpkg dependencies");
        logger::print_status("  version  Show version information");
        logger::print_status("  help     Show help for a specific command");
        logger::print_status("  install  Install a cforge project to the system");
        logger::print_status("  config   Show information about configuration file format");
        logger::print_status("");
        logger::print_status("Usage: cforge <command> [options]");
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
        logger::print_status("  --std=c++XX                   Set C++ standard (11, 14, 17, 20) (default: 17)");
        logger::print_status("  --git                         Initialize git repository (disabled by default)");
        logger::print_status("  --workspace [name]            Create a new workspace with the given name");
        logger::print_status("  --projects [name1] [name2]... Create multiple projects");
        logger::print_status("  --template [name]             Use specific project template (app, lib, header-only)");
        logger::print_status("  --with-tests                  Add test infrastructure to the project");
        logger::print_status("  --with-git                    Initialize git repository");
        logger::print_status("  --type=[type]                 Set project binary type (executable, shared_lib, static_lib, header_only)");
        logger::print_status("  -v, --verbose                 Show verbose output");
        logger::print_status("");
        logger::print_status("Configuration:");
        logger::print_status("  Projects are initialized with a cforge.toml file containing project settings:");
        logger::print_status("  - Project metadata (name, version, C++ standard)");
        logger::print_status("  - Build configuration (build type, source directories)");
        logger::print_status("  - Packaging settings");
        logger::print_status("  - Dependency management");
        logger::print_status("  - Test configuration");
        logger::print_status("");
        logger::print_status("  Run 'cforge help config' for detailed configuration format information");
        logger::print_status("");
        logger::print_status("Examples:");
        logger::print_status("  cforge init                   Initialize project in current directory");
        logger::print_status("  cforge init my_project        Create new project in a new directory");
        logger::print_status("  cforge init --type=shared_lib Create a shared library project");
        logger::print_status("  cforge init --projects a b c  Create multiple standalone projects");
        logger::print_status("  cforge init --workspace myws  Create a workspace");
        logger::print_status("  cforge init --workspace myws --projects app lib  Create workspace with projects");
        logger::print_status("");
        logger::print_status("Notes:");
        logger::print_status("  - Hyphens in project names are replaced with underscores in code");
        logger::print_status("  - If no name is provided, the current directory name is used as the project name");
    } else if (specific_command == "build") {
        logger::print_status("cforge build - Build the project");
        logger::print_status("");
        logger::print_status("Usage: cforge build [options]");
        logger::print_status("");
        logger::print_status("Options:");
        logger::print_status("  -c, --config <config>    Set build configuration (Debug, Release, etc.)");
        logger::print_status("  -j, --jobs <n>           Set number of parallel build jobs");
        logger::print_status("  -v, --verbose            Enable verbose output");
        logger::print_status("  -t, --target <target>    Build specific target");
        logger::print_status("  -p, --project <project>  Build specific project in workspace");
        logger::print_status("  --gen-workspace-cmake    Generate a workspace-level CMakeLists.txt");
        logger::print_status("  --force-regenerate       Force regeneration of CMakeLists.txt and clean build");
        logger::print_status("");
        logger::print_status("Examples:");
        logger::print_status("  cforge build             Build with default configuration (Debug)");
        logger::print_status("  cforge build -c Release  Build with Release configuration");
        logger::print_status("  cforge build -j 4        Build with 4 parallel jobs");
        logger::print_status("  cforge build -p mylib    Build only 'mylib' project in workspace");
        logger::print_status("  cforge build --gen-workspace-cmake  Generate a workspace CMakeLists.txt without building");
        logger::print_status("  cforge build --force-regenerate     Rebuild with fresh configuration");
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
        logger::print_status("  -p, --project <name>   Run specific project in workspace");
        logger::print_status("  --no-build             Skip building before running");
        logger::print_status("  -v, --verbose          Show verbose output");
        logger::print_status("");
        logger::print_status("Examples:");
        logger::print_status("  cforge run                    Build and run with default config");
        logger::print_status("  cforge run -c Debug           Build and run with Debug config");
        logger::print_status("  cforge run -- arg1 arg2       Pass arguments to the executable");
        logger::print_status("  cforge run -p app1 -- arg1    Run 'app1' from workspace with args");
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
        logger::print_status("  -c, --config <name>      Specify the build configuration (Debug/Release)");
        logger::print_status("  -p, --project <name>     In a workspace, specify which project to package");
        logger::print_status("  -t, --type <generator>   Specify the package generator to use");
        logger::print_status("  --no-build               Skip building the project before packaging");
        logger::print_status("  -v, --verbose            Enable verbose output");
        logger::print_status("");
        logger::print_status("Examples:");
        logger::print_status("  cforge package                  Package the current project with default settings");
        logger::print_status("  cforge package -c Release       Package using the Release configuration");
        logger::print_status("  cforge package --no-build       Skip building and package with existing binaries");
        logger::print_status("  cforge package -t ZIP           Package using the ZIP generator only");
        logger::print_status("  cforge package -p mylib         In a workspace, package the 'mylib' project");
        logger::print_status("");
        logger::print_status("Notes:");
        logger::print_status("  - When run in a workspace:");
        logger::print_status("    - By default, packages the main project specified in workspace.toml");
        logger::print_status("    - Use -p to package a specific project");
        logger::print_status("    - Set package.all_projects=true in workspace.toml to package all projects");
        logger::print_status("");
        logger::print_status("  - Package settings can be configured in cforge.toml:");
        logger::print_status("    [package]");
        logger::print_status("    enabled = true                # Enable/disable packaging");
        logger::print_status("    generators = [\"ZIP\", \"TGZ\"]   # List of generators to use");
        logger::print_status("");
        logger::print_status("  - Workspace package settings in workspace.toml:");
        logger::print_status("    [package]");
        logger::print_status("    all_projects = false          # Whether to package all projects");
        logger::print_status("    generators = [\"ZIP\"]          # Default generators for all projects");
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
        logger::print_status("cforge install - Install a cforge project to the system");
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
    } else if (specific_command == "config") {
        logger::print_status("cforge.toml - Configuration file format");
        logger::print_status("");
        logger::print_status("The cforge.toml file is used to configure your project. It contains the following sections:");
        logger::print_status("");
        logger::print_status("[project]");
        logger::print_status("  name = \"project-name\"       # Project name");
        logger::print_status("  version = \"0.1.0\"         # Project version");
        logger::print_status("  cpp_standard = \"17\"       # C++ standard to use");
        logger::print_status("  binary_type = \"executable\" # Type of binary: executable, shared_lib, static_lib, or header_only");
        logger::print_status("");
        logger::print_status("[build]");
        logger::print_status("  type = \"Release\"          # Build type (Debug/Release/RelWithDebInfo/MinSizeRel)");
        logger::print_status("  directory = \"build\"       # Build directory");
        logger::print_status("  source_dirs = [\"src\"]     # Directories containing source files");
        logger::print_status("  source_files = [\"src/main.cpp\"] # Specific source files to include");
        logger::print_status("  include_dirs = [\"include\"] # Directories containing header files");
        logger::print_status("  libraries = [\"SDL2\"]      # Additional libraries to link");
        logger::print_status("");
        logger::print_status("[package]");
        logger::print_status("  enabled = true             # Enable packaging");
        logger::print_status("  generators = [\"ZIP\", \"NSIS\"] # Package generators");
        logger::print_status("  vendor = \"Your Company\"   # Vendor name for packages");
        logger::print_status("");
        logger::print_status("[dependencies]");
        logger::print_status("  directory = \"deps\"        # Custom directory for dependencies (default: deps)");
        logger::print_status("  libraries = [\"SDL2\", \"OpenGL32\"] # Additional libraries to link");
        logger::print_status("");
        logger::print_status("  # vcpkg dependencies");
        logger::print_status("  [dependencies.vcpkg]");
        logger::print_status("  fmt = \"8.0.1\"             # Package name = version");
        logger::print_status("  curl = { version = \"7.80.0\", components = [\"ssl\"] } # With components");
        logger::print_status("");
        logger::print_status("  # git dependencies - cloned to the dependencies directory");
        logger::print_status("  [dependencies.git]");
        logger::print_status("  json = { url = \"https://github.com/nlohmann/json.git\", tag = \"v3.11.2\" }");
        logger::print_status("  spdlog = { url = \"https://github.com/gabime/spdlog.git\", branch = \"v1.x\" }");
        logger::print_status("  fmt = { url = \"https://github.com/fmtlib/fmt.git\", tag = \"9.1.0\",");
        logger::print_status("          make_available = true, # Include in CMake with FetchContent_MakeAvailable");
        logger::print_status("          include = true,       # Add include directories");
        logger::print_status("          link = true,         # Link against the library");
        logger::print_status("          target_name = \"fmt::fmt\", # Custom target name for linking (optional)");
        logger::print_status("          include_dirs = [\"include\"] # Custom include directories within the repo");
        logger::print_status("        }");
        logger::print_status("");
        logger::print_status("  # Additional libraries to link against (not tied to dependencies)");
        logger::print_status("  libraries = [\"SDL2\", \"OpenGL32\"]");
        logger::print_status("");
        logger::print_status("  # system dependencies");
        logger::print_status("  [dependencies.system]");
        logger::print_status("  OpenGL = true              # System-provided dependency");
        logger::print_status("");
        logger::print_status("[test]");
        logger::print_status("  enabled = true             # Enable testing");
        logger::print_status("  framework = \"Catch2\"      # Test framework to use");
        logger::print_status("");
        logger::print_status("For more information, refer to the docs/cforge.toml.example file in the repository.");
        logger::print_status("");
        logger::print_status("");
        logger::print_status("workspace.toml - Workspace Configuration File Format");
        logger::print_status("");
        logger::print_status("The workspace.toml file is used to configure a multi-project workspace:");
        logger::print_status("");
        logger::print_status("[workspace]");
        logger::print_status("  name = \"my-workspace\"      # Workspace name");
        logger::print_status("  cpp_standard = \"17\"       # Default C++ standard for all projects");
        logger::print_status("  projects = [\"app\", \"lib\"] # List of projects in the workspace");
        logger::print_status("");
        logger::print_status("[build]");
        logger::print_status("  directory = \"build\"       # Build directory for the workspace");
        logger::print_status("  parallel = true           # Build projects in parallel when possible");
        logger::print_status("");
        logger::print_status("[dependencies]");
        logger::print_status("  directory = \"deps\"        # Shared dependencies directory for all projects");
        logger::print_status("  libraries = [\"pthread\"]   # Global libraries to link in all projects");
        logger::print_status("");
        logger::print_status("  # Common git dependencies for all projects");
        logger::print_status("  [dependencies.git]");
        logger::print_status("  fmt = { url = \"https://github.com/fmtlib/fmt.git\", tag = \"9.1.0\" }");
        logger::print_status("");
        logger::print_status("  # Common vcpkg dependencies for all projects");
        logger::print_status("  [dependencies.vcpkg]");
        logger::print_status("  spdlog = \"1.11.0\"");
        logger::print_status("");
        logger::print_status("[dependencies.project]");
        logger::print_status("  app = [\"lib\"]            # Project 'app' depends on project 'lib'");
        logger::print_status("");
        logger::print_status("Each project in the workspace can also have its own cforge.toml file.");
    } else {
        logger::print_error("Unknown command: " + specific_command);
        logger::print_status("Run 'cforge help' for a list of available commands");
        return 1;
    }
    
    return 0;
} 