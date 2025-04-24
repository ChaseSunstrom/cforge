/**
 * @file command_help.cpp
 * @brief Implementation of the 'help' command to provide usage information
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace cforge;

/**
 * @brief Handle the 'help' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_help(const cforge_context_t *ctx) {
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
    logger::print_status("  update   Update cforge");
    logger::print_status("  vcpkg    Manage vcpkg dependencies");
    logger::print_status("  version  Show version information");
    logger::print_status("  help     Show help for a specific command");
    logger::print_status("  install  Install a cforge project to the system");
    logger::print_status(
        "  config   Show information about configuration file format");
    logger::print_status("");
    logger::print_status("Usage: cforge <command> [options]");
    logger::print_status("");
    logger::print_status("For more information on a specific command, run "
                         "'cforge help <command>'");
  } else if (specific_command == "init") {
    logger::print_status("cforge init - Initialize a new C++ project");
    logger::print_status("");
    logger::print_status("Usage: cforge init [name] [options]");
    logger::print_status("");
    logger::print_status("Arguments:");
    logger::print_status(
        "  name             Project name (default: current directory name)");
    logger::print_status("");
    logger::print_status("Options:");
    logger::print_status("  --std=c++XX                   Set C++ standard "
                         "(11, 14, 17, 20) (default: 17)");
    logger::print_status("  --git                         Initialize git "
                         "repository (disabled by default)");
    logger::print_status("  --workspace [name]            Create a new "
                         "workspace with the given name");
    logger::print_status(
        "  --projects [name1] [name2]... Create multiple projects");
    logger::print_status("  --template [name]             Use specific project "
                         "template (app, lib, header-only)");
    logger::print_status("  --with-tests                  Add test "
                         "infrastructure to the project");
    logger::print_status(
        "  --with-git                    Initialize git repository");
    logger::print_status(
        "  --type=[type]                 Set project binary type (executable, "
        "shared_lib, static_lib, header_only)");
    logger::print_status("  -v, --verbose                 Show verbose output");
    logger::print_status("");
    logger::print_status("Configuration:");
    logger::print_status("  Projects are initialized with a cforge.toml file "
                         "containing project settings:");
    logger::print_status("  - Project metadata (name, version, C++ standard)");
    logger::print_status(
        "  - Build configuration (build type, source directories)");
    logger::print_status("  - Packaging settings");
    logger::print_status("  - Dependency management");
    logger::print_status("  - Test configuration");
    logger::print_status("");
    logger::print_status("  Run 'cforge help config' for detailed "
                         "configuration format information");
    logger::print_status("");
    logger::print_status("Examples:");
    logger::print_status("  cforge init                   Initialize project "
                         "in current directory");
    logger::print_status("  cforge init my_project        Create new project "
                         "in a new directory");
    logger::print_status(
        "  cforge init --type=shared_lib Create a shared library project");
    logger::print_status(
        "  cforge init --projects a b c  Create multiple standalone projects");
    logger::print_status("  cforge init --workspace myws  Create a workspace");
    logger::print_status("  cforge init --workspace myws --projects app lib  "
                         "Create workspace with projects");
    logger::print_status("");
    logger::print_status("Notes:");
    logger::print_status(
        "  - Hyphens in project names are replaced with underscores in code");
    logger::print_status("  - If no name is provided, the current directory "
                         "name is used as the project name");
  } else if (specific_command == "build") {
    logger::print_status("cforge build - Build the project");
    logger::print_status("");
    logger::print_status("Usage: cforge build [options]");
    logger::print_status("");
    logger::print_status("Options:");
    logger::print_status("  -c, --config <config>    Set build configuration "
                         "(Debug, Release, etc.)");
    logger::print_status(
        "  -j, --jobs <n>           Set number of parallel build jobs");
    logger::print_status("  -v, --verbose            Enable verbose output");
    logger::print_status("  -t, --target <target>    Build specific target");
    logger::print_status(
        "  -p, --project <project>  Build specific project in workspace");
    logger::print_status(
        "  --gen-workspace-cmake    Generate a workspace-level CMakeLists.txt");
    logger::print_status("  --force-regenerate       Force regeneration of "
                         "CMakeLists.txt and clean build");
    logger::print_status("");
    logger::print_status("Examples:");
    logger::print_status(
        "  cforge build             Build with default configuration (Debug)");
    logger::print_status(
        "  cforge build -c Release  Build with Release configuration");
    logger::print_status(
        "  cforge build -j 4        Build with 4 parallel jobs");
    logger::print_status(
        "  cforge build -p mylib    Build only 'mylib' project in workspace");
    logger::print_status("  cforge build --gen-workspace-cmake  Generate a "
                         "workspace CMakeLists.txt without building");
    logger::print_status("  cforge build --force-regenerate     Rebuild with "
                         "fresh configuration");
  } else if (specific_command == "clean") {
    logger::print_status("cforge clean - Clean build artifacts");
    logger::print_status("");
    logger::print_status("Usage: cforge clean [options]");
    logger::print_status("");
    logger::print_status("Options:");
    logger::print_status(
        "  -c, --config <config>  Clean specific configuration");
    logger::print_status("  --all                  Clean all configurations");
    logger::print_status(
        "  --cmake-files          Also clean CMake temporary files");
    logger::print_status(
        "  --regenerate           Regenerate CMake files after cleaning");
    logger::print_status(
        "  --deep                 Remove dependencies directory (deep clean)");
    logger::print_status("  -v, --verbose          Show verbose output");
  } else if (specific_command == "run") {
    logger::print_status("cforge run - Build and run the project");
    logger::print_status("");
    logger::print_status("Usage: cforge run [options] [-- <app arguments>]");
    logger::print_status("");
    logger::print_status("Options:");
    logger::print_status(
        "  -c, --config <config>  Build configuration (Debug, Release, etc.)");
    logger::print_status(
        "  -p, --project <name>   Run specific project in workspace");
    logger::print_status(
        "  --no-build             Skip building before running");
    logger::print_status("  -v, --verbose          Show verbose output");
    logger::print_status("");
    logger::print_status("Examples:");
    logger::print_status(
        "  cforge run                    Build and run with default config");
    logger::print_status(
        "  cforge run -c Debug           Build and run with Debug config");
    logger::print_status(
        "  cforge run -- arg1 arg2       Pass arguments to the executable");
    logger::print_status(
        "  cforge run -p app1 -- arg1    Run 'app1' from workspace with args");
    logger::print_status("");
    logger::print_status("Arguments after -- are passed to the application");
  } else if (specific_command == "test") {
    logger::print_status("cforge test - Run tests");
    logger::print_status("");
    logger::print_status("Usage: cforge test [options] [-- <test arguments>]");
    logger::print_status("");
    logger::print_status("Options:");
    logger::print_status(
        "  -c, --config <config>  Build configuration (Debug, Release, etc.)");
    logger::print_status(
        "  --no-build             Skip building before running tests");
    logger::print_status("  -v, --verbose          Show verbose output");
    logger::print_status("");
    logger::print_status(
        "Arguments after -- are passed to the test executable");
  } else if (specific_command == "package") {
    logger::print_status("cforge package - Create distributable packages");
    logger::print_status("");
    logger::print_status("Usage: cforge package [options]");
    logger::print_status("");
    logger::print_status("Options:");
    logger::print_status("  -c, --config <name>      Specify the build "
                         "configuration (Debug/Release)");
    logger::print_status("  -p, --project <name>     In a workspace, specify "
                         "which project to package");
    logger::print_status(
        "  -t, --type <generator>   Specify the package generator to use");
    logger::print_status("  --no-build               Skip building the project "
                         "before packaging");
    logger::print_status("  -v, --verbose            Enable verbose output");
    logger::print_status("");
    logger::print_status("Examples:");
    logger::print_status("  cforge package                  Package the "
                         "current project with default settings");
    logger::print_status("  cforge package -c Release       Package using the "
                         "Release configuration");
    logger::print_status("  cforge package --no-build       Skip building and "
                         "package with existing binaries");
    logger::print_status("  cforge package -t ZIP           Package using the "
                         "ZIP generator only");
    logger::print_status("  cforge package -p mylib         In a workspace, "
                         "package the 'mylib' project");
    logger::print_status("");
    logger::print_status("Notes:");
    logger::print_status("  - When run in a workspace:");
    logger::print_status("    - By default, packages the main project "
                         "specified in workspace.toml");
    logger::print_status("    - Use -p to package a specific project");
    logger::print_status("    - Set package.all_projects=true in "
                         "workspace.toml to package all projects");
    logger::print_status("");
    logger::print_status(
        "  - Package settings can be configured in cforge.toml:");
    logger::print_status("    [package]");
    logger::print_status(
        "    enabled = true                # Enable/disable packaging");
    logger::print_status(
        "    generators = [\"ZIP\", \"TGZ\"]   # List of generators to use");
    logger::print_status("");
    logger::print_status("  - Workspace package settings in workspace.toml:");
    logger::print_status("    [package]");
    logger::print_status(
        "    all_projects = false          # Whether to package all projects");
    logger::print_status("    generators = [\"ZIP\"]          # Default "
                         "generators for all projects");
  } else if (specific_command == "add") {
    logger::print_status("cforge add - Add a dependency");
    logger::print_status("");
    logger::print_status("Usage: cforge add <package> [options]");
    logger::print_status("");
    logger::print_status("Arguments:");
    logger::print_status(
        "  package          Package to add (format: name[:version])");
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
    logger::print_status("cforge update - Update cforge");
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
    logger::print_status("cforge install - Install a project");
    logger::print_status("");
    logger::print_status("Usage: cforge install [options]");
    logger::print_status("");
    logger::print_status("Options:");
    logger::print_status("  --to <path>    Install to a specific path");
    logger::print_status("  --from <path>  Install from a specific path");
    logger::print_status("  -v, --verbose    Show verbose output");
  } else if (specific_command == "config") {
    logger::print_lines(
        {"cforge.toml Configuration Guide:",
         "",
         "The cforge.toml file is the main configuration file for your "
         "project. It defines",
         "project metadata, build settings, dependencies, and more.",
         "",
         "[project]",
         "name = \"my-project\"        # Project name",
         "version = \"0.1.0\"          # Project version",
         "description = \"...\"        # Project description",
         "authors = [\"Your Name\"]    # List of authors",
         "cpp_standard = \"17\"        # C++ standard (11, 14, 17, 20, 23)",
         "binary_type = \"executable\" # Type: executable, shared_lib, "
         "static_lib, header_only",
         "",
         "[build]",
         "build_dir = \"build\"        # Build directory",
         "build_type = \"Debug\"       # Default build type (Debug, Release, "
         "etc.)",
         "include_dirs = [\"include\"] # Include directories",
         "source_dirs = [\"src\"]      # Source directories",
         "libraries = []               # Additional libraries to link against",
         "",
         "[package]",
         "enabled = true               # Enable packaging",
         "generators = [\"ZIP\"]       # Package generators (ZIP, TGZ, DEB, "
         "RPM, NSIS, etc.)",
         "",
         "[dependencies]",
         "directory = \"deps\"         # Dependencies directory",
         "",
         "[dependencies.git.fmt]       # Git dependency example",
         "url = \"https://github.com/fmtlib/fmt.git\"",
         "tag = \"9.1.0\"              # A tag, branch, or commit hash to "
         "checkout",
         "include = true               # Include in project (default: true)",
         "link = true                  # Link with project (default: true)",
         "",
         "[dependencies.project.utils] # Project dependency within workspace",
         "include_dirs = [\"include\"] # Include directories (relative to "
         "project)",
         "include = true               # Include in project (default: true)",
         "link = true                  # Link with project (default: true)",
         "target_name = \"utils\"      # Target name (default: project name)",
         "",
         "[dependencies.vcpkg.sdl2]    # vcpkg dependency example",
         "version = \"2.0.14\"         # Optional version",
         "components = [\"main\"]      # Optional components",
         "",
         "[test]",
         "enabled = true               # Enable testing",
         "framework = \"Catch2\"       # Test framework",
         "",
         "workspace.toml Configuration (for multi-project workspaces):",
         "",
         "[workspace]",
         "name = \"my-workspace\"      # Workspace name",
         "description = \"...\"        # Workspace description",
         "projects = [\"proj1\", \"proj2\"] # Projects in workspace",
         "main_project = \"proj1\"     # Main project (used as default for "
         "commands)",
         "cpp_standard = \"17\"        # Default C++ standard for workspace "
         "projects",
         "build_type = \"Debug\"       # Default build type for workspace "
         "projects"});
  } else {
    logger::print_error("Unknown command: " + specific_command);
    logger::print_status("Run 'cforge help' for a list of available commands");
    return 1;
  }

  return 0;
}