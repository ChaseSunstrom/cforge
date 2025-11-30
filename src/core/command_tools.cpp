/**
 * @file command_tools.cpp
 * @brief Implementation of tool commands: fmt, lint, completions
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ============================================================
// Helper Functions
// ============================================================

/**
 * @brief Find all source files in a directory
 */
std::vector<fs::path> find_source_files(const fs::path &dir,
                                        bool include_headers = true) {
  std::vector<fs::path> files;

  std::vector<std::string> extensions = {".cpp", ".cc", ".cxx", ".c"};
  if (include_headers) {
    extensions.push_back(".hpp");
    extensions.push_back(".hxx");
    extensions.push_back(".h");
  }

  if (!fs::exists(dir)) {
    return files;
  }

  for (const auto &entry : fs::recursive_directory_iterator(dir)) {
    if (!entry.is_regular_file())
      continue;

    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    for (const auto &valid_ext : extensions) {
      if (ext == valid_ext) {
        files.push_back(entry.path());
        break;
      }
    }
  }

  return files;
}

/**
 * @brief Check if a tool is available in PATH
 */
bool tool_exists(const std::string &tool) {
#ifdef _WIN32
  std::string cmd = "where " + tool + " >nul 2>&1";
#else
  std::string cmd = "which " + tool + " >/dev/null 2>&1";
#endif
  return system(cmd.c_str()) == 0;
}

/**
 * @brief Find clang-format executable
 */
std::string find_clang_format() {
  // Try common names
  std::vector<std::string> names = {"clang-format",    "clang-format-18",
                                    "clang-format-17", "clang-format-16",
                                    "clang-format-15", "clang-format-14"};

  for (const auto &name : names) {
    if (tool_exists(name)) {
      return name;
    }
  }

  return "";
}

/**
 * @brief Find clang-tidy executable
 */
std::string find_clang_tidy() {
  std::vector<std::string> names = {"clang-tidy",    "clang-tidy-18",
                                    "clang-tidy-17", "clang-tidy-16",
                                    "clang-tidy-15", "clang-tidy-14"};

  for (const auto &name : names) {
    if (tool_exists(name)) {
      return name;
    }
  }

  return "";
}

// ============================================================
// Shell Completion Generators
// ============================================================

std::string generate_bash_completions() {
  return R"(# cforge bash completion script
# Add this to your ~/.bashrc or source it directly

_cforge_completions() {
    local cur prev opts commands
    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    commands="init build clean run test package deps vcpkg install add remove update ide list lock help version fmt lint watch completions"

    case "${prev}" in
        cforge)
            COMPREPLY=( $(compgen -W "${commands}" -- ${cur}) )
            return 0
            ;;
        build|run|test|clean)
            COMPREPLY=( $(compgen -W "-c --config -v --verbose -q --quiet -j --jobs --release --debug" -- ${cur}) )
            return 0
            ;;
        init)
            COMPREPLY=( $(compgen -W "--name --type --std --workspace --lib --exe" -- ${cur}) )
            return 0
            ;;
        add|remove)
            COMPREPLY=( $(compgen -W "--git --vcpkg --system" -- ${cur}) )
            return 0
            ;;
        fmt)
            COMPREPLY=( $(compgen -W "--check --dry-run --style" -- ${cur}) )
            return 0
            ;;
        lint)
            COMPREPLY=( $(compgen -W "--fix --checks" -- ${cur}) )
            return 0
            ;;
        ide)
            COMPREPLY=( $(compgen -W "vs vscode clion xcode" -- ${cur}) )
            return 0
            ;;
        completions)
            COMPREPLY=( $(compgen -W "bash zsh powershell fish" -- ${cur}) )
            return 0
            ;;
        -c|--config)
            COMPREPLY=( $(compgen -W "Debug Release RelWithDebInfo MinSizeRel" -- ${cur}) )
            return 0
            ;;
        *)
            ;;
    esac

    COMPREPLY=( $(compgen -W "${commands}" -- ${cur}) )
}

complete -F _cforge_completions cforge
)";
}

std::string generate_zsh_completions() {
  return R"(#compdef cforge
# cforge zsh completion script
# Add this to your fpath or source it directly

_cforge() {
    local -a commands
    commands=(
        'init:Initialize a new project or workspace'
        'build:Build the project'
        'clean:Clean build artifacts'
        'run:Build and run the project'
        'test:Run project tests'
        'package:Create distributable packages'
        'deps:Manage Git dependencies'
        'vcpkg:Manage vcpkg dependencies'
        'install:Install the project'
        'add:Add a dependency'
        'remove:Remove a dependency'
        'update:Update cforge'
        'ide:Generate IDE project files'
        'list:List projects or dependencies'
        'lock:Manage dependency lock file'
        'help:Show help information'
        'version:Show version information'
        'fmt:Format source code with clang-format'
        'lint:Run clang-tidy static analysis'
        'watch:Watch for changes and rebuild'
        'completions:Generate shell completions'
    )

    local -a common_opts
    common_opts=(
        '-c[Build configuration]:config:(Debug Release RelWithDebInfo MinSizeRel)'
        '--config[Build configuration]:config:(Debug Release RelWithDebInfo MinSizeRel)'
        '-v[Verbose output]'
        '--verbose[Verbose output]'
        '-q[Quiet output]'
        '--quiet[Quiet output]'
    )

    _arguments -C \
        '1:command:->command' \
        '*::arg:->args'

    case "$state" in
        command)
            _describe -t commands 'cforge commands' commands
            ;;
        args)
            case "${words[1]}" in
                build|run|test|clean)
                    _arguments $common_opts \
                        '-j[Number of parallel jobs]:jobs:' \
                        '--jobs[Number of parallel jobs]:jobs:' \
                        '--release[Build in release mode]' \
                        '--debug[Build in debug mode]'
                    ;;
                init)
                    _arguments \
                        '--name[Project name]:name:' \
                        '--type[Project type]:type:(exe lib header-only)' \
                        '--std[C++ standard]:std:(11 14 17 20 23)' \
                        '--workspace[Create a workspace]'
                    ;;
                fmt)
                    _arguments \
                        '--check[Check formatting without modifying]' \
                        '--dry-run[Show what would be changed]' \
                        '--style[Formatting style]:style:(file LLVM Google Chromium Mozilla WebKit)'
                    ;;
                lint)
                    _arguments \
                        '--fix[Apply suggested fixes]' \
                        '--checks[Checks to run]:checks:'
                    ;;
                ide)
                    _arguments '1:ide:(vs vscode clion xcode)'
                    ;;
                completions)
                    _arguments '1:shell:(bash zsh powershell fish)'
                    ;;
            esac
            ;;
    esac
}

_cforge "$@"
)";
}

std::string generate_powershell_completions() {
  return R"(# cforge PowerShell completion script
# Add this to your $PROFILE

Register-ArgumentCompleter -Native -CommandName cforge -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)

    $commands = @(
        @{ Name = 'init'; Description = 'Initialize a new project or workspace' }
        @{ Name = 'build'; Description = 'Build the project' }
        @{ Name = 'clean'; Description = 'Clean build artifacts' }
        @{ Name = 'run'; Description = 'Build and run the project' }
        @{ Name = 'test'; Description = 'Run project tests' }
        @{ Name = 'package'; Description = 'Create distributable packages' }
        @{ Name = 'deps'; Description = 'Manage Git dependencies' }
        @{ Name = 'vcpkg'; Description = 'Manage vcpkg dependencies' }
        @{ Name = 'install'; Description = 'Install the project' }
        @{ Name = 'add'; Description = 'Add a dependency' }
        @{ Name = 'remove'; Description = 'Remove a dependency' }
        @{ Name = 'update'; Description = 'Update cforge' }
        @{ Name = 'ide'; Description = 'Generate IDE project files' }
        @{ Name = 'list'; Description = 'List projects or dependencies' }
        @{ Name = 'lock'; Description = 'Manage dependency lock file' }
        @{ Name = 'help'; Description = 'Show help information' }
        @{ Name = 'version'; Description = 'Show version information' }
        @{ Name = 'fmt'; Description = 'Format source code with clang-format' }
        @{ Name = 'lint'; Description = 'Run clang-tidy static analysis' }
        @{ Name = 'watch'; Description = 'Watch for changes and rebuild' }
        @{ Name = 'completions'; Description = 'Generate shell completions' }
    )

    $configs = @('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')
    $ides = @('vs', 'vscode', 'clion', 'xcode')
    $shells = @('bash', 'zsh', 'powershell', 'fish')

    $elements = $commandAst.CommandElements
    $command = $null

    if ($elements.Count -gt 1) {
        $command = $elements[1].Extent.Text
    }

    switch ($command) {
        'build' {
            @('-c', '--config', '-v', '--verbose', '-q', '--quiet', '-j', '--jobs', '--release', '--debug') |
                Where-Object { $_ -like "$wordToComplete*" } |
                ForEach-Object { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_) }
        }
        'ide' {
            $ides | Where-Object { $_ -like "$wordToComplete*" } |
                ForEach-Object { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_) }
        }
        'completions' {
            $shells | Where-Object { $_ -like "$wordToComplete*" } |
                ForEach-Object { [System.Management.Automation.CompletionResult]::new($_, $_, 'ParameterValue', $_) }
        }
        default {
            $commands | Where-Object { $_.Name -like "$wordToComplete*" } |
                ForEach-Object { [System.Management.Automation.CompletionResult]::new($_.Name, $_.Name, 'Command', $_.Description) }
        }
    }
}
)";
}

std::string generate_fish_completions() {
  return R"(# cforge fish completion script
# Save to ~/.config/fish/completions/cforge.fish

# Disable file completion by default
complete -c cforge -f

# Commands
complete -c cforge -n __fish_use_subcommand -a init -d 'Initialize a new project or workspace'
complete -c cforge -n __fish_use_subcommand -a build -d 'Build the project'
complete -c cforge -n __fish_use_subcommand -a clean -d 'Clean build artifacts'
complete -c cforge -n __fish_use_subcommand -a run -d 'Build and run the project'
complete -c cforge -n __fish_use_subcommand -a test -d 'Run project tests'
complete -c cforge -n __fish_use_subcommand -a package -d 'Create distributable packages'
complete -c cforge -n __fish_use_subcommand -a deps -d 'Manage Git dependencies'
complete -c cforge -n __fish_use_subcommand -a vcpkg -d 'Manage vcpkg dependencies'
complete -c cforge -n __fish_use_subcommand -a install -d 'Install the project'
complete -c cforge -n __fish_use_subcommand -a add -d 'Add a dependency'
complete -c cforge -n __fish_use_subcommand -a remove -d 'Remove a dependency'
complete -c cforge -n __fish_use_subcommand -a update -d 'Update cforge'
complete -c cforge -n __fish_use_subcommand -a ide -d 'Generate IDE project files'
complete -c cforge -n __fish_use_subcommand -a list -d 'List projects or dependencies'
complete -c cforge -n __fish_use_subcommand -a lock -d 'Manage dependency lock file'
complete -c cforge -n __fish_use_subcommand -a help -d 'Show help information'
complete -c cforge -n __fish_use_subcommand -a version -d 'Show version information'
complete -c cforge -n __fish_use_subcommand -a fmt -d 'Format source code with clang-format'
complete -c cforge -n __fish_use_subcommand -a lint -d 'Run clang-tidy static analysis'
complete -c cforge -n __fish_use_subcommand -a watch -d 'Watch for changes and rebuild'
complete -c cforge -n __fish_use_subcommand -a completions -d 'Generate shell completions'

# Build options
complete -c cforge -n '__fish_seen_subcommand_from build run test clean' -s c -l config -d 'Build configuration' -xa 'Debug Release RelWithDebInfo MinSizeRel'
complete -c cforge -n '__fish_seen_subcommand_from build run test clean' -s v -l verbose -d 'Verbose output'
complete -c cforge -n '__fish_seen_subcommand_from build run test clean' -s q -l quiet -d 'Quiet output'
complete -c cforge -n '__fish_seen_subcommand_from build run test' -s j -l jobs -d 'Number of parallel jobs'
complete -c cforge -n '__fish_seen_subcommand_from build run test' -l release -d 'Build in release mode'
complete -c cforge -n '__fish_seen_subcommand_from build run test' -l debug -d 'Build in debug mode'

# IDE options
complete -c cforge -n '__fish_seen_subcommand_from ide' -a 'vs vscode clion xcode' -d 'IDE type'

# Completions options
complete -c cforge -n '__fish_seen_subcommand_from completions' -a 'bash zsh powershell fish' -d 'Shell type'

# Fmt options
complete -c cforge -n '__fish_seen_subcommand_from fmt' -l check -d 'Check formatting without modifying'
complete -c cforge -n '__fish_seen_subcommand_from fmt' -l dry-run -d 'Show what would be changed'
complete -c cforge -n '__fish_seen_subcommand_from fmt' -l style -d 'Formatting style' -xa 'file LLVM Google Chromium Mozilla WebKit'

# Lint options
complete -c cforge -n '__fish_seen_subcommand_from lint' -l fix -d 'Apply suggested fixes'
complete -c cforge -n '__fish_seen_subcommand_from lint' -l checks -d 'Checks to run'
)";
}

} // anonymous namespace

// ============================================================
// Command Implementations
// ============================================================

/**
 * @brief Handle the 'fmt' command for code formatting
 */
cforge_int_t cforge_cmd_fmt(const cforge_context_t *ctx) {
  using namespace cforge;

  fs::path project_dir = ctx->working_dir;

  // Parse arguments
  bool check_only = false;
  bool dry_run = false;
  std::string style = "file"; // Default: use .clang-format file

  for (int i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "--check") {
      check_only = true;
    } else if (arg == "--dry-run") {
      dry_run = true;
    } else if (arg == "--style" && i + 1 < ctx->args.arg_count) {
      style = ctx->args.args[++i];
    }
  }

  // Find clang-format
  std::string clang_format = find_clang_format();
  if (clang_format.empty()) {
    logger::print_error("clang-format not found in PATH");
    logger::print_status("Install clang-format or add it to your PATH");
    return 1;
  }

  logger::print_action("Formatting", "source files with " + clang_format);

  // Find source files
  std::vector<fs::path> files;

  // Check src and include directories
  for (const auto &dir : {"src", "include", "source", "lib"}) {
    fs::path dir_path = project_dir / dir;
    auto dir_files = find_source_files(dir_path, true);
    files.insert(files.end(), dir_files.begin(), dir_files.end());
  }

  if (files.empty()) {
    logger::print_warning("No source files found to format");
    return 0;
  }

  logger::print_status("Found " + std::to_string(files.size()) + " files");

  int formatted_count = 0;
  int failed_count = 0;

  for (const auto &file : files) {
    std::vector<std::string> args;
    args.push_back("-style=" + style);

    if (check_only) {
      args.push_back("--dry-run");
      args.push_back("--Werror");
    } else if (dry_run) {
      args.push_back("--dry-run");
    } else {
      args.push_back("-i"); // In-place
    }

    args.push_back(file.string());

    auto result = execute_process(clang_format, args, project_dir.string());

    if (result.exit_code == 0) {
      formatted_count++;
      if (!check_only && !dry_run) {
        logger::print_verbose("Formatted: " + file.filename().string());
      }
    } else {
      failed_count++;
      if (check_only) {
        logger::print_warning("Needs formatting: " + file.string());
      } else {
        logger::print_error("Failed to format: " + file.string());
      }
    }
  }

  if (check_only) {
    if (failed_count > 0) {
      logger::print_error(std::to_string(failed_count) +
                          " file(s) need formatting");
      logger::print_status("Run 'cforge fmt' to format them");
      return 1;
    } else {
      logger::print_success("All files are properly formatted");
    }
  } else if (dry_run) {
    logger::print_status("Would format " + std::to_string(formatted_count) +
                         " file(s)");
  } else {
    logger::finished(
        "formatted " + std::to_string(formatted_count) + " file(s)", "");
  }

  return 0;
}

/**
 * @brief Handle the 'lint' command for static analysis
 */
cforge_int_t cforge_cmd_lint(const cforge_context_t *ctx) {
  using namespace cforge;

  fs::path project_dir = ctx->working_dir;
  fs::path build_dir = project_dir / "build";

  // Parse arguments
  bool fix = false;
  std::string checks = "";

  for (int i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "--fix") {
      fix = true;
    } else if (arg == "--checks" && i + 1 < ctx->args.arg_count) {
      checks = ctx->args.args[++i];
    }
  }

  // Find clang-tidy
  std::string clang_tidy = find_clang_tidy();
  if (clang_tidy.empty()) {
    logger::print_error("clang-tidy not found in PATH");
    logger::print_status("Install clang-tidy or add it to your PATH");
    return 1;
  }

  logger::print_action("Analyzing", "source files with " + clang_tidy);

  // Check for compile_commands.json
  fs::path compile_commands = build_dir / "compile_commands.json";
  if (!fs::exists(compile_commands)) {
    logger::print_warning("compile_commands.json not found");
    logger::print_status(
        "Building project first to generate compilation database...");

    // Try to generate compile_commands.json
    std::vector<std::string> cmake_args = {
        "-B", build_dir.string(), "-S", project_dir.string(),
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"};

    auto result = execute_process("cmake", cmake_args, project_dir.string());
    if (result.exit_code != 0 || !fs::exists(compile_commands)) {
      logger::print_error("Could not generate compile_commands.json");
      logger::print_status(
          "Run 'cforge build' first, or create compile_commands.json manually");
      return 1;
    }
  }

  // Find source files (only .cpp files for clang-tidy)
  std::vector<fs::path> files;
  for (const auto &dir : {"src", "source", "lib"}) {
    fs::path dir_path = project_dir / dir;
    auto dir_files = find_source_files(dir_path, false); // No headers
    files.insert(files.end(), dir_files.begin(), dir_files.end());
  }

  if (files.empty()) {
    logger::print_warning("No source files found to analyze");
    return 0;
  }

  logger::print_status("Analyzing " + std::to_string(files.size()) +
                       " file(s)...");

  int warnings = 0;
  int errors = 0;

  for (const auto &file : files) {
    std::vector<std::string> args;
    args.push_back("-p");
    args.push_back(build_dir.string());

    if (!checks.empty()) {
      args.push_back("-checks=" + checks);
    }

    if (fix) {
      args.push_back("-fix");
    }

    args.push_back(file.string());

    logger::print_verbose("Checking: " + file.filename().string());

    auto result = execute_process(
        clang_tidy, args, project_dir.string(), [&](const std::string &line) {
          // Parse clang-tidy output
          if (line.find("warning:") != std::string::npos) {
            warnings++;
            fmt::print(fg(fmt::color::yellow), "{}\n", line);
          } else if (line.find("error:") != std::string::npos) {
            errors++;
            fmt::print(fg(fmt::color::red), "{}\n", line);
          } else if (!line.empty()) {
            fmt::print("{}\n", line);
          }
        });
  }

  // Summary
  fmt::print("\n");
  if (errors > 0 || warnings > 0) {
    logger::print_status("Analysis complete:");
    if (errors > 0) {
      logger::print_error(std::to_string(errors) + " error(s)");
    }
    if (warnings > 0) {
      logger::print_warning(std::to_string(warnings) + " warning(s)");
    }
    if (fix) {
      logger::print_status("Some issues may have been automatically fixed");
    } else {
      logger::print_status(
          "Run 'cforge lint --fix' to automatically fix some issues");
    }
    return errors > 0 ? 1 : 0;
  } else {
    logger::print_success("No issues found");
    return 0;
  }
}

/**
 * @brief Handle the 'completions' command
 */
cforge_int_t cforge_cmd_completions(const cforge_context_t *ctx) {
  using namespace cforge;

  std::string shell = "bash"; // Default

  // Parse shell argument
  for (int i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "bash" || arg == "zsh" || arg == "powershell" || arg == "fish" ||
        arg == "ps" || arg == "ps1") {
      shell = arg;
      if (shell == "ps" || shell == "ps1")
        shell = "powershell";
    }
  }

  std::string script;
  std::string install_hint;

  if (shell == "bash") {
    script = generate_bash_completions();
    install_hint = "Add to ~/.bashrc or save to /etc/bash_completion.d/cforge";
  } else if (shell == "zsh") {
    script = generate_zsh_completions();
    install_hint =
        "Save to a file in your fpath (e.g., ~/.zsh/completions/_cforge)";
  } else if (shell == "powershell") {
    script = generate_powershell_completions();
    install_hint = "Add to your $PROFILE";
  } else if (shell == "fish") {
    script = generate_fish_completions();
    install_hint = "Save to ~/.config/fish/completions/cforge.fish";
  } else {
    logger::print_error("Unknown shell: " + shell);
    logger::print_status("Supported shells: bash, zsh, powershell, fish");
    return 1;
  }

  // Output the script
  fmt::print("{}\n", script);

  // Print install hint to stderr so it doesn't pollute piped output
  fmt::print(stderr, fg(fmt::color::gray), "\n# {}\n", install_hint);

  return 0;
}
