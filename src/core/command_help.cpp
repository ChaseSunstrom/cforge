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
    logger::print_plain("cforge - C++ project management tool");
    logger::print_plain("");
    logger::print_plain("Available commands:");
    logger::print_plain("  init      Initialize a new project or workspace");
    logger::print_plain("  build     Build the project");
    logger::print_plain("  clean     Clean build artifacts");
    logger::print_plain("  run       Build and run the project");
    logger::print_plain("  test      Run tests");
    logger::print_plain("  package   Create a package for distribution");
    logger::print_plain("  pack      Alias for package");
    logger::print_plain("  deps      Manage Git dependencies");
    logger::print_plain("  vcpkg     Manage vcpkg dependencies");
    logger::print_plain("  install   Install a cforge project to the system");
    logger::print_plain("  add       Add a dependency to the project");
    logger::print_plain("  remove    Remove a dependency from the project");
    logger::print_plain("  update    Update cforge");
    logger::print_plain("  ide       Generate IDE project files");
    logger::print_plain("  list      List dependencies or projects");
    logger::print_plain("  lock      Manage dependency lock file for reproducible builds");
    logger::print_plain("  version   Show version information");
    logger::print_plain("  help      Show help for a specific command");
    logger::print_plain("");
    logger::print_plain("Usage: cforge <command> [options]");
    logger::print_plain("");
    logger::print_plain("For more information on a specific command, run "
                         "'cforge help <command>'");
  } else if (specific_command == "init") {
    logger::print_plain("cforge init - Initialize a new C++ project");
    logger::print_plain("");
    logger::print_plain("Usage: cforge init [name] [options]");
    logger::print_plain("");
    logger::print_plain("Arguments:");
    logger::print_plain(
        "  name             Project name (default: current directory name)");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  --std=c++XX                   Set C++ standard "
                         "(11, 14, 17, 20) (default: 17)");
    logger::print_plain("  --git                         Initialize git "
                         "repository (disabled by default)");
    logger::print_plain("  --workspace [name]            Create a new "
                         "workspace with the given name");
    logger::print_plain(
        "  --projects [name1] [name2]... Create multiple projects");
    logger::print_plain("  --template [name]             Use specific project "
                         "template (app, lib, header-only)");
    logger::print_plain("  --with-tests                  Add test "
                         "infrastructure to the project");
    logger::print_plain(
        "  --with-git                    Initialize git repository");
    logger::print_plain(
        "  --type=[type]                 Set project binary type (executable, "
        "shared_lib, static_lib, header_only)");
    logger::print_plain("  -v, --verbose                 Show verbose output");
    logger::print_plain("");
    logger::print_plain("Configuration:");
    logger::print_plain("  Projects are initialized with a cforge.toml file "
                         "containing project settings:");
    logger::print_plain("  - Project metadata (name, version, C++ standard)");
    logger::print_plain("  - Build configuration (build type, source directories)");
    logger::print_plain("  - Packaging settings");
    logger::print_plain("  - Dependency management");
    logger::print_plain("  - Test configuration");
    logger::print_plain("");
    logger::print_plain("  Run 'cforge help config' for detailed "
                         "configuration format information");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain("  cforge init                   Initialize project "
                         "in current directory");
    logger::print_plain("  cforge init my_project        Create new project "
                         "in a new directory");
    logger::print_plain(
        "  cforge init --type=shared_lib Create a shared library project");
    logger::print_plain(
        "  cforge init --projects a b c  Create multiple standalone projects");
    logger::print_plain("  cforge init --workspace myws  Create a workspace");
    logger::print_plain("  cforge init --workspace myws --projects app lib  "
                         "Create workspace with projects");
    logger::print_plain("");
    logger::print_plain("Notes:");
    logger::print_plain(
        "  - Hyphens in project names are replaced with underscores in code");
    logger::print_plain("  - If no name is provided, the current directory "
                         "name is used as the project name");
  } else if (specific_command == "build") {
    logger::print_plain("cforge build - Build the project");
    logger::print_plain("");
    logger::print_plain("Usage: cforge build [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -c, --config <config>    Set build configuration "
                         "(Debug, Release, etc.)");
    logger::print_plain(
        "  -j, --jobs <n>           Set number of parallel build jobs");
    logger::print_plain("  -v, --verbose            Enable verbose output");
    logger::print_plain("  -t, --target <target>    Build specific target");
    logger::print_plain(
        "  -p, --project <project>  Build specific project in workspace");
    logger::print_plain(
        "  --gen-workspace-cmake    Generate a workspace-level CMakeLists.txt");
    logger::print_plain("  --force-regenerate       Force regeneration of "
                         "CMakeLists.txt and clean build");
    logger::print_plain("  --skip-deps, --no-deps   Skip updating Git dependencies");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain(
        "  cforge build             Build with default configuration (Debug)");
    logger::print_plain(
        "  cforge build -c Release  Build with Release configuration");
    logger::print_plain(
        "  cforge build -j 4        Build with 4 parallel jobs");
    logger::print_plain(
        "  cforge build -p mylib    Build only 'mylib' project in workspace");
    logger::print_plain("  cforge build --gen-workspace-cmake  Generate a "
                         "workspace CMakeLists.txt without building");
    logger::print_plain("  cforge build --force-regenerate     Rebuild with "
                         "fresh configuration");
    logger::print_plain("  cforge build --skip-deps           Build without updating Git dependencies");
  } else if (specific_command == "clean") {
    logger::print_plain("cforge clean - Clean build artifacts");
    logger::print_plain("");
    logger::print_plain("Usage: cforge clean [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain(
        "  -c, --config <config>  Clean specific configuration");
    logger::print_plain("  --all                  Clean all configurations");
    logger::print_plain(
        "  --cmake-files          Also clean CMake temporary files");
    logger::print_plain(
        "  --regenerate           Regenerate CMake files after cleaning");
    logger::print_plain(
        "  --deep                 Remove dependencies directory (deep clean)");
    logger::print_plain("  -v, --verbose          Show verbose output");
  } else if (specific_command == "run") {
    logger::print_plain("cforge run - Build and run the project");
    logger::print_plain("");
    logger::print_plain("Usage: cforge run [options] [-- <app arguments>]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain(
        "  -c, --config <config>  Build configuration (Debug, Release, etc.)");
    logger::print_plain(
        "  -p, --project <name>   Run specific project in workspace");
    logger::print_plain(
        "  --no-build             Skip building before running");
    logger::print_plain("  -v, --verbose          Show verbose output");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain(
        "  cforge run                    Build and run with default config");
    logger::print_plain(
        "  cforge run -c Debug           Build and run with Debug config");
    logger::print_plain(
        "  cforge run -- arg1 arg2       Pass arguments to the executable");
    logger::print_plain(
        "  cforge run -p app1 -- arg1    Run 'app1' from workspace with args");
    logger::print_plain("");
    logger::print_plain("Arguments after -- are passed to the application");
  } else if (specific_command == "test") {
    logger::print_plain("cforge test - Build and run unit tests");
    logger::print_plain("");
    logger::print_plain("Usage: cforge test [options] [<category> [<test1> <test2> ...]] [--]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -c, --config <config>    Build configuration (Debug, Release, etc.)");
    logger::print_plain("  -v, --verbose            Show verbose build & test output");
    logger::print_plain("");
    logger::print_plain("Positional arguments:");
    logger::print_plain("  <category>               Optional test category to run");
    logger::print_plain("  <test_name> ...          Optional test names under the category");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain("  cforge test");
    logger::print_plain("  cforge test Math");
    logger::print_plain("  cforge test -c Release Math Add");
    logger::print_plain("  cforge test -c Release -- Math Add");
    logger::print_plain("");
    logger::print_plain("Use `--` to explicitly separate CForge flags from test filters");
  } else if (specific_command == "package") {
    logger::print_plain("cforge package - Create distributable packages");
    logger::print_plain("");
    logger::print_plain("Usage: cforge package [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -c, --config <name>      Specify the build "
                         "configuration (Debug/Release)");
    logger::print_plain("  -p, --project <name>     In a workspace, specify "
                         "which project to package");
    logger::print_plain(
        "  -t, --type <generator>   Specify the package generator to use");
    logger::print_plain("  --no-build               Skip building the project "
                         "before packaging");
    logger::print_plain("  -v, --verbose            Enable verbose output");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain("  cforge package                  Package the "
                         "current project with default settings");
    logger::print_plain("  cforge package -c Release       Package using the "
                         "Release configuration");
    logger::print_plain("  cforge package --no-build       Skip building and "
                         "package with existing binaries");
    logger::print_plain("  cforge package -t ZIP           Package using the "
                         "ZIP generator only");
    logger::print_plain("  cforge package -p mylib         In a workspace, "
                         "package the 'mylib' project");
    logger::print_plain("");
    logger::print_plain("Notes:");
    logger::print_plain("  - When run in a workspace:");
    logger::print_plain("    - By default, packages the main project "
                         "specified in workspace.toml");
    logger::print_plain("    - Use -p to package a specific project");
    logger::print_plain("    - Set package.all_projects=true in "
                         "workspace.toml to package all projects");
    logger::print_plain("");
    logger::print_plain(
        "  - Package settings can be configured in cforge.toml:");
    logger::print_plain("    [package]");
    logger::print_plain(
        "    enabled = true                # Enable/disable packaging");
    logger::print_plain(
        "    generators = [\"ZIP\", \"TGZ\"]   # List of generators to use");
    logger::print_plain("");
    logger::print_plain("  - Workspace package settings in workspace.toml:");
    logger::print_plain("    [package]");
    logger::print_plain(
        "    all_projects = false          # Whether to package all projects");
    logger::print_plain("    generators = [\"ZIP\"]          # Default "
                         "generators for all projects");
  } else if (specific_command == "add") {
    logger::print_plain("cforge add - Add a dependency");
    logger::print_plain("");
    logger::print_plain("Usage: cforge add <package> [options]");
    logger::print_plain("");
    logger::print_plain("Arguments:");
    logger::print_plain(
        "  package          Package to add (format: name[:version])");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -v, --verbose    Show verbose output");
  } else if (specific_command == "remove") {
    logger::print_plain("cforge remove - Remove a dependency");
    logger::print_plain("");
    logger::print_plain("Usage: cforge remove <package> [options]");
    logger::print_plain("");
    logger::print_plain("Arguments:");
    logger::print_plain("  package          Package to remove");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -v, --verbose    Show verbose output");
  } else if (specific_command == "update") {
    logger::print_plain("cforge update - Update cforge");
    logger::print_plain("");
    logger::print_plain("Usage: cforge update [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -v, --verbose    Show verbose output");
  } else if (specific_command == "vcpkg") {
    logger::print_plain("cforge vcpkg - Run vcpkg commands");
    logger::print_plain("");
    logger::print_plain("Usage: cforge vcpkg <args...>");
    logger::print_plain("");
    logger::print_plain("Arguments:");
    logger::print_plain("  args...          Arguments to pass to vcpkg");
  } else if (specific_command == "version") {
    logger::print_plain("cforge version - Show cforge version");
    logger::print_plain("");
    logger::print_plain("Usage: cforge version");
  } else if (specific_command == "help") {
    logger::print_plain("cforge help - Show help information");
    logger::print_plain("");
    logger::print_plain("Usage: cforge help [command]");
    logger::print_plain("");
    logger::print_plain("Arguments:");
    logger::print_plain("  command          Command to show help for");
  } else if (specific_command == "install") {
    logger::print_plain("cforge install - Install a cforge project to the system");
    logger::print_plain("");
    logger::print_plain("Usage: cforge install [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -c, --config <config>       Build configuration to install (Debug, Release)");
    logger::print_plain("  --from <path|URL>           Source directory or Git URL to install from");
    logger::print_plain("  --to <path>                 Target install directory");
    logger::print_plain("  --add-to-path               Add the install/bin directory to PATH");
    logger::print_plain("  -n, --name <name>           Override project name for installation");
    logger::print_plain("  --env <VAR>                 Environment variable to set to install path");
    logger::print_plain("  -v, --verbose               Show verbose output");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain("  cforge install                   Install current project to default location");
    logger::print_plain("  cforge install --to C:/Apps/Proj   Install to a custom path");
    logger::print_plain("  cforge install --from https://github.com/org/repo.git");
    logger::print_plain("  cforge install --add-to-path      Add install to PATH");
  } else if (specific_command == "ide") {
    logger::print_plain("cforge ide - Generate IDE project files");
    logger::print_plain("");
    logger::print_plain("Usage: cforge ide [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -p, --project <name>        Generate files for specified project in workspace");
    logger::print_plain("  -v, --verbose               Show verbose output");
    logger::print_plain("");
  } else if (specific_command == "list") {
    logger::print_plain("cforge list - List projects or dependencies");
    logger::print_plain("");
    logger::print_plain("Usage: cforge list [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -p, --project <name>        List dependencies for a specific project");
    logger::print_plain("  -v, --verbose               Show verbose output");
    logger::print_plain("");
  } else if (specific_command == "deps") {
    logger::print_plain("cforge deps - Manage Git dependencies");
    logger::print_plain("");
    logger::print_plain("Usage: cforge deps <command> [options]");
    logger::print_plain("");
    logger::print_plain("Commands:");
    logger::print_plain("  fetch     Fetch updates for all Git dependencies");
    logger::print_plain("  checkout  Checkout each dependency to its configured ref");
    logger::print_plain("  list      Show configured Git dependencies");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  -v, --verbose               Show verbose output");
    logger::print_plain("");
  } else if (specific_command == "pack") {
    logger::print_plain("cforge pack - Alias for 'package'");
    logger::print_plain("");
    logger::print_plain("Usage: cforge pack [options]");
    logger::print_plain("");
    logger::print_plain("See 'cforge help package' for details");
  } else if (specific_command == "lock") {
    logger::print_plain("cforge lock - Manage dependency lock file");
    logger::print_plain("");
    logger::print_plain("Usage: cforge lock [options]");
    logger::print_plain("");
    logger::print_plain("Options:");
    logger::print_plain("  --verify, -v   Verify dependencies match lock file");
    logger::print_plain("  --clean, -c    Remove the lock file");
    logger::print_plain("  --force, -f    Force regeneration even if lock exists");
    logger::print_plain("  --help, -h     Show this help message");
    logger::print_plain("");
    logger::print_plain("The lock file (cforge.lock) ensures reproducible builds by");
    logger::print_plain("tracking exact versions (commit hashes) of all dependencies.");
    logger::print_plain("");
    logger::print_plain("Examples:");
    logger::print_plain("  cforge lock              Generate/update lock file");
    logger::print_plain("  cforge lock --verify     Check if deps match lock file");
    logger::print_plain("  cforge lock --force      Regenerate lock file");
    logger::print_plain("  cforge lock --clean      Remove lock file");
    logger::print_plain("");
    logger::print_plain("Best practices:");
    logger::print_plain("  1. Commit cforge.lock to version control");
    logger::print_plain("  2. Run 'cforge lock --verify' in CI pipelines");
    logger::print_plain("  3. Run 'cforge lock' after adding/updating dependencies");
  } else {
    logger::print_error("Unknown command: " + specific_command);
    logger::print_plain("Run 'cforge help' for a list of available commands");
    return 1;
  }

  return 0;
}