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
    cforge::logger::print_plain("cforge - C++ project management tool");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Available commands:");
    cforge::logger::print_plain("  init      Initialize a new project or workspace");
    cforge::logger::print_plain("  build     Build the project");
    cforge::logger::print_plain("  clean     Clean build artifacts");
    cforge::logger::print_plain("  run       Build and run the project");
    cforge::logger::print_plain("  test      Run tests");
    cforge::logger::print_plain("  package   Create a package for distribution");
    cforge::logger::print_plain("  pack      Alias for package");
    cforge::logger::print_plain("  deps      Manage Git dependencies");
    cforge::logger::print_plain("  vcpkg     Manage vcpkg dependencies");
    cforge::logger::print_plain("  install   Install a cforge project to the system");
    cforge::logger::print_plain("  search    Search for packages in the registry");
    cforge::logger::print_plain("  info      Show detailed package information");
    cforge::logger::print_plain("  add       Add a dependency to the project");
    cforge::logger::print_plain("  remove    Remove a dependency from the project");
    cforge::logger::print_plain("  update    Update cforge or packages");
    cforge::logger::print_plain("  ide       Generate IDE project files");
    cforge::logger::print_plain("  list      List dependencies or projects");
    cforge::logger::print_plain(
        "  lock      Manage dependency lock file for reproducible builds");
    cforge::logger::print_plain("  fmt       Format source code with clang-format");
    cforge::logger::print_plain("  lint      Run static analysis with clang-tidy");
    cforge::logger::print_plain("  watch     Watch for changes and auto-rebuild");
    cforge::logger::print_plain("  completions  Generate shell completion scripts");
    cforge::logger::print_plain("  doc       Generate documentation with Doxygen");
    cforge::logger::print_plain("  tree      Visualize dependency tree");
    cforge::logger::print_plain("  new       Create files from templates");
    cforge::logger::print_plain("  bench     Run benchmarks");
    cforge::logger::print_plain("  version   Show version information");
    cforge::logger::print_plain("  help      Show help for a specific command");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge <command> [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("For more information on a specific command, run "
                        "'cforge help <command>'");
  } else if (specific_command == "init") {
    cforge::logger::print_plain("cforge init - Initialize a new C++ project");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge init [name] [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments:");
    cforge::logger::print_plain(
        "  name             Project name (default: current directory name)");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  --std=c++XX                   Set C++ standard "
                        "(11, 14, 17, 20) (default: 17)");
    cforge::logger::print_plain("  --git                         Initialize git "
                        "repository (disabled by default)");
    cforge::logger::print_plain("  --workspace [name]            Create a new "
                        "workspace with the given name");
    cforge::logger::print_plain(
        "  --projects [name1] [name2]... Create multiple projects");
    cforge::logger::print_plain("  --template [name]             Use specific project "
                        "template (app, lib, header-only)");
    cforge::logger::print_plain("  --with-tests                  Add test "
                        "infrastructure to the project");
    cforge::logger::print_plain(
        "  --with-git                    Initialize git repository");
    cforge::logger::print_plain(
        "  --type=[type]                 Set project binary type (executable, "
        "shared_lib, static_lib, header_only)");
    cforge::logger::print_plain("  -v, --verbose                 Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Configuration:");
    cforge::logger::print_plain("  Projects are initialized with a cforge.toml file "
                        "containing project settings:");
    cforge::logger::print_plain("  - Project metadata (name, version, C++ standard)");
    cforge::logger::print_plain(
        "  - Build configuration (build type, source directories)");
    cforge::logger::print_plain("  - Packaging settings");
    cforge::logger::print_plain("  - Dependency management");
    cforge::logger::print_plain("  - Test configuration");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("  Run 'cforge help config' for detailed "
                        "configuration format information");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge init                   Initialize project "
                        "in current directory");
    cforge::logger::print_plain("  cforge init my_project        Create new project "
                        "in a new directory");
    cforge::logger::print_plain(
        "  cforge init --type=shared_lib Create a shared library project");
    cforge::logger::print_plain(
        "  cforge init --projects a b c  Create multiple standalone projects");
    cforge::logger::print_plain("  cforge init --workspace myws  Create a workspace");
    cforge::logger::print_plain("  cforge init --workspace myws --projects app lib  "
                        "Create workspace with projects");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain(
        "  - Hyphens in project names are replaced with underscores in code");
    cforge::logger::print_plain("  - If no name is provided, the current directory "
                        "name is used as the project name");
  } else if (specific_command == "build") {
    cforge::logger::print_plain("cforge build - Build the project");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge build [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -c, --config <config>    Set build configuration "
                        "(Debug, Release, etc.)");
    cforge::logger::print_plain(
        "  -j, --jobs <n>           Set number of parallel build jobs");
    cforge::logger::print_plain("  -v, --verbose            Enable verbose output");
    cforge::logger::print_plain("  -t, --target <target>    Build specific target");
    cforge::logger::print_plain(
        "  -p, --project <project>  Build specific project in workspace");
    cforge::logger::print_plain(
        "  --gen-workspace-cmake    Generate a workspace-level CMakeLists.txt");
    cforge::logger::print_plain("  --force-regenerate       Force regeneration of "
                        "CMakeLists.txt and clean build");
    cforge::logger::print_plain(
        "  --skip-deps, --no-deps   Skip updating Git dependencies");
    cforge::logger::print_plain(
        "  -P, --profile <name>     Use cross-compilation profile");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain(
        "  cforge build             Build with default configuration (Debug)");
    cforge::logger::print_plain(
        "  cforge build -c Release  Build with Release configuration");
    cforge::logger::print_plain(
        "  cforge build -j 4        Build with 4 parallel jobs");
    cforge::logger::print_plain(
        "  cforge build -p mylib    Build only 'mylib' project in workspace");
    cforge::logger::print_plain("  cforge build --gen-workspace-cmake  Generate a "
                        "workspace CMakeLists.txt without building");
    cforge::logger::print_plain("  cforge build --force-regenerate     Rebuild with "
                        "fresh configuration");
    cforge::logger::print_plain("  cforge build --skip-deps           Build without "
                        "updating Git dependencies");
    cforge::logger::print_plain("  cforge build --profile android-arm64  Build using "
                        "cross-compilation profile");
  } else if (specific_command == "clean") {
    cforge::logger::print_plain("cforge clean - Clean build artifacts");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge clean [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  -c, --config <config>  Clean specific configuration");
    cforge::logger::print_plain("  --all                  Clean all configurations");
    cforge::logger::print_plain(
        "  --cmake-files          Also clean CMake temporary files");
    cforge::logger::print_plain(
        "  --regenerate           Regenerate CMake files after cleaning");
    cforge::logger::print_plain(
        "  --deep                 Remove dependencies directory (deep clean)");
    cforge::logger::print_plain("  -v, --verbose          Show verbose output");
  } else if (specific_command == "run") {
    cforge::logger::print_plain("cforge run - Build and run the project");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge run [options] [-- <app arguments>]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  -c, --config <config>  Build configuration (Debug, Release, etc.)");
    cforge::logger::print_plain(
        "  -p, --project <name>   Run specific project in workspace");
    cforge::logger::print_plain(
        "  --no-build             Skip building before running");
    cforge::logger::print_plain("  -v, --verbose          Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain(
        "  cforge run                    Build and run with default config");
    cforge::logger::print_plain(
        "  cforge run -c Debug           Build and run with Debug config");
    cforge::logger::print_plain(
        "  cforge run -- arg1 arg2       Pass arguments to the executable");
    cforge::logger::print_plain(
        "  cforge run -p app1 -- arg1    Run 'app1' from workspace with args");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments after -- are passed to the application");
  } else if (specific_command == "test") {
    cforge::logger::print_plain("cforge test - Build and run unit tests");
    cforge::logger::print_plain("");
    cforge::logger::print_plain(
        "Usage: cforge test [options] [<category> [<test1> <test2> ...]] [--]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -c, --config <config>    Build configuration "
                        "(Debug, Release, etc.)");
    cforge::logger::print_plain(
        "  -v, --verbose            Show verbose build & test output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Positional arguments:");
    cforge::logger::print_plain(
        "  <category>               Optional test category to run");
    cforge::logger::print_plain(
        "  <test_name> ...          Optional test names under the category");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge test");
    cforge::logger::print_plain("  cforge test Math");
    cforge::logger::print_plain("  cforge test -c Release Math Add");
    cforge::logger::print_plain("  cforge test -c Release -- Math Add");
    cforge::logger::print_plain("");
    cforge::logger::print_plain(
        "Use `--` to explicitly separate CForge flags from test filters");
  } else if (specific_command == "package") {
    cforge::logger::print_plain("cforge package - Create distributable packages");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge package [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -c, --config <name>      Specify the build "
                        "configuration (Debug/Release)");
    cforge::logger::print_plain("  -p, --project <name>     In a workspace, specify "
                        "which project to package");
    cforge::logger::print_plain(
        "  -t, --type <generator>   Specify the package generator to use");
    cforge::logger::print_plain("  --no-build               Skip building the project "
                        "before packaging");
    cforge::logger::print_plain("  -v, --verbose            Enable verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge package                  Package the "
                        "current project with default settings");
    cforge::logger::print_plain("  cforge package -c Release       Package using the "
                        "Release configuration");
    cforge::logger::print_plain("  cforge package --no-build       Skip building and "
                        "package with existing binaries");
    cforge::logger::print_plain("  cforge package -t ZIP           Package using the "
                        "ZIP generator only");
    cforge::logger::print_plain("  cforge package -p mylib         In a workspace, "
                        "package the 'mylib' project");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain("  - When run in a workspace:");
    cforge::logger::print_plain("    - By default, packages the main project "
                        "specified in workspace.toml");
    cforge::logger::print_plain("    - Use -p to package a specific project");
    cforge::logger::print_plain("    - Set package.all_projects=true in "
                        "workspace.toml to package all projects");
    cforge::logger::print_plain("");
    cforge::logger::print_plain(
        "  - Package settings can be configured in cforge.toml:");
    cforge::logger::print_plain("    [package]");
    cforge::logger::print_plain(
        "    enabled = true                # Enable/disable packaging");
    cforge::logger::print_plain(
        "    generators = [\"ZIP\", \"TGZ\"]   # List of generators to use");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("  - Workspace package settings in workspace.toml:");
    cforge::logger::print_plain("    [package]");
    cforge::logger::print_plain(
        "    all_projects = false          # Whether to package all projects");
    cforge::logger::print_plain("    generators = [\"ZIP\"]          # Default "
                        "generators for all projects");
  } else if (specific_command == "add") {
    cforge::logger::print_plain("cforge add - Add a dependency");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge add <package>[@version] [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments:");
    cforge::logger::print_plain("  package          Package name (e.g., fmt, spdlog)");
    cforge::logger::print_plain("  @version         Optional version (e.g., @11.1.4, @1.*)");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Source Options (default: registry):");
    cforge::logger::print_plain("  --git <url>      Add as Git dependency");
    cforge::logger::print_plain("  --tag <tag>      Git tag (with --git)");
    cforge::logger::print_plain("  --branch <name>  Git branch (with --git)");
    cforge::logger::print_plain("  --vcpkg          Add as vcpkg package");
    cforge::logger::print_plain("  --index          Add from registry (default)");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Other Options:");
    cforge::logger::print_plain("  --features <f>   Comma-separated features to enable");
    cforge::logger::print_plain("  --header-only    Mark as header-only library");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge add fmt                    Add fmt from registry");
    cforge::logger::print_plain("  cforge add fmt@11.1.4             Add specific version");
    cforge::logger::print_plain("  cforge add spdlog --features async");
    cforge::logger::print_plain("  cforge add boost --vcpkg          Add from vcpkg");
    cforge::logger::print_plain("  cforge add mylib --git https://github.com/user/mylib --tag v1.0");
  } else if (specific_command == "remove") {
    cforge::logger::print_plain("cforge remove - Remove a dependency");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge remove <package> [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments:");
    cforge::logger::print_plain("  package          Package to remove");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
  } else if (specific_command == "search") {
    cforge::logger::print_plain("cforge search - Search for packages in the registry");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge search <query> [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments:");
    cforge::logger::print_plain("  query            Search term to find packages");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  --limit <n>      Maximum results to show (default: 20)");
    cforge::logger::print_plain("  --update         Force update the package index first");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge search json          Find JSON-related packages");
    cforge::logger::print_plain("  cforge search logging       Find logging libraries");
    cforge::logger::print_plain("  cforge search --limit 5 ui  Show top 5 UI packages");
  } else if (specific_command == "info") {
    cforge::logger::print_plain("cforge info - Show detailed package information");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge info <package> [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments:");
    cforge::logger::print_plain("  package          Name of package to get info for");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  --versions       Show all available versions");
    cforge::logger::print_plain("  --update         Force update the package index first");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge info fmt             Show fmt package details");
    cforge::logger::print_plain("  cforge info spdlog --versions  Show all spdlog versions");
  } else if (specific_command == "update") {
    cforge::logger::print_plain("cforge update - Update cforge or packages");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge update <--self|--packages> [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -s, --self       Update cforge itself to latest version");
    cforge::logger::print_plain("  -p, --packages   Update the package registry index");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge update --self       Update cforge to latest");
    cforge::logger::print_plain("  cforge update --packages   Refresh package registry");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Note: You must specify either --self or --packages");
  } else if (specific_command == "vcpkg") {
    cforge::logger::print_plain("cforge vcpkg - Run vcpkg commands");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge vcpkg <args...>");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments:");
    cforge::logger::print_plain("  args...          Arguments to pass to vcpkg");
  } else if (specific_command == "version") {
    cforge::logger::print_plain("cforge version - Show cforge version");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge version");
  } else if (specific_command == "help") {
    cforge::logger::print_plain("cforge help - Show help information");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge help [command]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Arguments:");
    cforge::logger::print_plain("  command          Command to show help for");
  } else if (specific_command == "install") {
    cforge::logger::print_plain(
        "cforge install - Install a cforge project to the system");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge install [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -c, --config <config>       Build configuration to "
                        "install (Debug, Release)");
    cforge::logger::print_plain("  --from <path|URL>           Source directory or Git "
                        "URL to install from");
    cforge::logger::print_plain(
        "  --to <path>                 Target install directory");
    cforge::logger::print_plain(
        "  --add-to-path               Add the install/bin directory to PATH");
    cforge::logger::print_plain(
        "  -n, --name <name>           Override project name for installation");
    cforge::logger::print_plain("  --env <VAR>                 Environment variable to "
                        "set to install path");
    cforge::logger::print_plain("  -v, --verbose               Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge install                   Install current "
                        "project to default location");
    cforge::logger::print_plain(
        "  cforge install --to C:/Apps/Proj   Install to a custom path");
    cforge::logger::print_plain(
        "  cforge install --from https://github.com/org/repo.git");
    cforge::logger::print_plain(
        "  cforge install --add-to-path      Add install to PATH");
  } else if (specific_command == "ide") {
    cforge::logger::print_plain("cforge ide - Generate IDE project files");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge ide [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -p, --project <name>        Generate files for "
                        "specified project in workspace");
    cforge::logger::print_plain("  -v, --verbose               Show verbose output");
    cforge::logger::print_plain("");
  } else if (specific_command == "list") {
    cforge::logger::print_plain("cforge list - List projects or dependencies");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge list [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -p, --project <name>        List dependencies for a "
                        "specific project");
    cforge::logger::print_plain("  -v, --verbose               Show verbose output");
    cforge::logger::print_plain("");
  } else if (specific_command == "deps") {
    cforge::logger::print_plain("cforge deps - Manage Git dependencies");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge deps <command> [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Commands:");
    cforge::logger::print_plain("  fetch     Fetch updates for all Git dependencies");
    cforge::logger::print_plain(
        "  checkout  Checkout each dependency to its configured ref");
    cforge::logger::print_plain("  list      Show configured Git dependencies");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -v, --verbose               Show verbose output");
    cforge::logger::print_plain("");
  } else if (specific_command == "pack") {
    cforge::logger::print_plain("cforge pack - Alias for 'package'");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge pack [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("See 'cforge help package' for details");
  } else if (specific_command == "lock") {
    cforge::logger::print_plain("cforge lock - Manage dependency lock file");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge lock [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  --verify, -v   Verify dependencies match lock file");
    cforge::logger::print_plain("  --clean, -c    Remove the lock file");
    cforge::logger::print_plain(
        "  --force, -f    Force regeneration even if lock exists");
    cforge::logger::print_plain("  --help, -h     Show this help message");
    cforge::logger::print_plain("");
    cforge::logger::print_plain(
        "The lock file (cforge.lock) ensures reproducible builds by");
    cforge::logger::print_plain(
        "tracking exact versions (commit hashes) of all dependencies.");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge lock              Generate/update lock file");
    cforge::logger::print_plain(
        "  cforge lock --verify     Check if deps match lock file");
    cforge::logger::print_plain("  cforge lock --force      Regenerate lock file");
    cforge::logger::print_plain("  cforge lock --clean      Remove lock file");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Best practices:");
    cforge::logger::print_plain("  1. Commit cforge.lock to version control");
    cforge::logger::print_plain("  2. Run 'cforge lock --verify' in CI pipelines");
    cforge::logger::print_plain(
        "  3. Run 'cforge lock' after adding/updating dependencies");
  } else if (specific_command == "fmt" || specific_command == "format") {
    cforge::logger::print_plain("cforge fmt - Format source code with clang-format");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge fmt [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  --check          Check formatting without modifying files");
    cforge::logger::print_plain("  --diff           Show diff of formatting changes");
    cforge::logger::print_plain("  --style <style>  Use specific style (llvm, google, "
                        "chromium, mozilla, webkit)");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain(
        "  cforge fmt               Format all source files in place");
    cforge::logger::print_plain(
        "  cforge fmt --check       Check if files need formatting");
    cforge::logger::print_plain("  cforge fmt --diff        Show what would change");
    cforge::logger::print_plain("  cforge fmt --style=google  Use Google C++ style");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain("  - Requires clang-format to be installed");
    cforge::logger::print_plain(
        "  - Uses .clang-format file if present, otherwise uses --style");
    cforge::logger::print_plain(
        "  - Formats .cpp, .cc, .cxx, .c, .hpp, .hxx, .h files");
  } else if (specific_command == "lint" || specific_command == "check") {
    cforge::logger::print_plain("cforge lint - Run static analysis with clang-tidy");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge lint [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  --fix            Apply suggested fixes automatically");
    cforge::logger::print_plain("  --checks <list>  Specify checks to run (e.g., "
                        "'modernize-*,bugprone-*')");
    cforge::logger::print_plain(
        "  -c, --config <cfg>  Build configuration for compile_commands.json");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge lint              Run all enabled checks");
    cforge::logger::print_plain(
        "  cforge lint --fix        Run and apply automatic fixes");
    cforge::logger::print_plain(
        "  cforge lint --checks='modernize-*'  Run only modernize checks");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain("  - Requires clang-tidy to be installed");
    cforge::logger::print_plain("  - Uses .clang-tidy file if present");
    cforge::logger::print_plain(
        "  - Requires compile_commands.json (generated during build)");
  } else if (specific_command == "watch") {
    cforge::logger::print_plain("cforge watch - Watch for changes and auto-rebuild");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge watch [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  -c, --config <cfg>   Build configuration (Debug, Release)");
    cforge::logger::print_plain(
        "  --run, -r            Run the executable after successful build");
    cforge::logger::print_plain(
        "  --interval <ms>      Poll interval in milliseconds (default: 500)");
    cforge::logger::print_plain("  --release            Use Release configuration");
    cforge::logger::print_plain("  --debug              Use Debug configuration");
    cforge::logger::print_plain("  -v, --verbose        Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain(
        "  cforge watch             Watch and rebuild on changes");
    cforge::logger::print_plain(
        "  cforge watch --run       Watch, rebuild, and run executable");
    cforge::logger::print_plain(
        "  cforge watch -c Release  Watch with Release configuration");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain(
        "  - Watches .cpp, .cc, .cxx, .c, .hpp, .hxx, .h, .toml files");
    cforge::logger::print_plain(
        "  - Ignores build/, .git/, deps/, vendor/ directories");
    cforge::logger::print_plain("  - Press Ctrl+C to stop watching");
  } else if (specific_command == "completions") {
    cforge::logger::print_plain(
        "cforge completions - Generate shell completion scripts");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge completions <shell>");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Shells:");
    cforge::logger::print_plain("  bash         Generate bash completions");
    cforge::logger::print_plain("  zsh          Generate zsh completions");
    cforge::logger::print_plain("  powershell   Generate PowerShell completions");
    cforge::logger::print_plain("  fish         Generate fish completions");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge completions bash >> ~/.bashrc");
    cforge::logger::print_plain("  cforge completions zsh >> ~/.zshrc");
    cforge::logger::print_plain("  cforge completions powershell >> $PROFILE");
    cforge::logger::print_plain(
        "  cforge completions fish > ~/.config/fish/completions/cforge.fish");
  } else if (specific_command == "doc" || specific_command == "docs") {
    cforge::logger::print_plain("cforge doc - Generate documentation with Doxygen");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge doc [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  --init           Generate Doxyfile without building docs");
    cforge::logger::print_plain("  --open           Open generated docs in browser");
    cforge::logger::print_plain("  -o, --output     Output directory (default: docs)");
    cforge::logger::print_plain("  -v, --verbose    Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge doc               Generate documentation");
    cforge::logger::print_plain(
        "  cforge doc --init        Create Doxyfile for customization");
    cforge::logger::print_plain(
        "  cforge doc --open        Generate and open in browser");
    cforge::logger::print_plain(
        "  cforge doc -o api-docs   Output to api-docs/ directory");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain("  - Requires Doxygen to be installed");
    cforge::logger::print_plain("  - Creates Doxyfile if not present");
    cforge::logger::print_plain(
        "  - Edit Doxyfile to customize documentation settings");
  } else if (specific_command == "tree") {
    cforge::logger::print_plain("cforge tree - Visualize dependency tree");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge tree [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  -a, --all        Show all dependencies including transitive");
    cforge::logger::print_plain(
        "  -d, --depth <n>  Maximum depth to display (default: 10)");
    cforge::logger::print_plain("  -i, --inverted   Show inverted tree (dependents)");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge tree              Show dependency tree");
    cforge::logger::print_plain("  cforge tree -d 2         Limit to 2 levels deep");
    cforge::logger::print_plain(
        "  cforge tree --all        Show all transitive dependencies");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Output:");
    cforge::logger::print_plain("  Dependencies are color-coded by type:");
    cforge::logger::print_plain("    - Cyan:    Git dependencies");
    cforge::logger::print_plain("    - Magenta: vcpkg dependencies");
    cforge::logger::print_plain("    - Yellow:  System dependencies");
    cforge::logger::print_plain("    - Green:   Project dependencies (workspace)");
  } else if (specific_command == "new") {
    cforge::logger::print_plain("cforge new - Create files from templates");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge new <template> <name> [options]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Templates:");
    cforge::logger::print_plain(
        "  class       Create a class with header and source files");
    cforge::logger::print_plain("  header      Create a header-only file");
    cforge::logger::print_plain("  struct      Create a struct header file");
    cforge::logger::print_plain("  interface   Create an interface (abstract class)");
    cforge::logger::print_plain("  test        Create a test file");
    cforge::logger::print_plain("  main        Create a main.cpp file");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain("  -n, --namespace <name>   Wrap in namespace");
    cforge::logger::print_plain("  -o, --output <dir>       Output directory");
    cforge::logger::print_plain("  -f, --force              Overwrite existing files");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain("  cforge new class MyClass             Create "
                        "MyClass.hpp and MyClass.cpp");
    cforge::logger::print_plain(
        "  cforge new class MyClass -n myproj   With namespace 'myproj'");
    cforge::logger::print_plain(
        "  cforge new header utils              Create utils.hpp");
    cforge::logger::print_plain(
        "  cforge new interface IService        Create IService interface");
    cforge::logger::print_plain(
        "  cforge new test MyClass              Create test_my_class.cpp");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain("  - Headers go to include/, sources go to src/");
    cforge::logger::print_plain("  - Tests go to tests/");
    cforge::logger::print_plain("  - Names are converted to appropriate case");
  } else if (specific_command == "bench" || specific_command == "benchmark") {
    cforge::logger::print_plain("cforge bench - Run benchmarks");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Usage: cforge bench [options] [benchmark-name]");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Options:");
    cforge::logger::print_plain(
        "  -c, --config <cfg>   Build configuration (default: Release)");
    cforge::logger::print_plain("  --no-build           Skip building before running");
    cforge::logger::print_plain(
        "  --filter <pattern>   Run only benchmarks matching pattern");
    cforge::logger::print_plain("  --json               Output in JSON format");
    cforge::logger::print_plain("  --csv                Output in CSV format");
    cforge::logger::print_plain("  -v, --verbose        Show verbose output");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Examples:");
    cforge::logger::print_plain(
        "  cforge bench                      Run all benchmarks");
    cforge::logger::print_plain(
        "  cforge bench --filter 'BM_Sort'   Run only Sort benchmarks");
    cforge::logger::print_plain(
        "  cforge bench --no-build           Run without rebuilding");
    cforge::logger::print_plain("  cforge bench --json > results.json");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Configuration (cforge.toml):");
    cforge::logger::print_plain("  [benchmark]");
    cforge::logger::print_plain(
        "  directory = \"bench\"       # Benchmark source directory");
    cforge::logger::print_plain(
        "  target = \"my_benchmarks\"  # Specific target to run");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Notes:");
    cforge::logger::print_plain("  - Benchmarks run in Release mode by default");
    cforge::logger::print_plain("  - Supports Google Benchmark output format");
    cforge::logger::print_plain(
        "  - Look for executables with 'bench' or 'benchmark' in name");
  } else {
    cforge::logger::print_error("Unknown command: " + specific_command);
    cforge::logger::print_plain("Run 'cforge help' for a list of available commands");
    return 1;
  }

  return 0;
}