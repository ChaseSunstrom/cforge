/**
 * @file command_tools.cpp
 * @brief Implementation of tool commands: fmt, lint, completions
 */

#include "cforge/log.hpp"

#include "core/commands.hpp"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/tool_installer.hpp"
#include "core/types.h"

#include <fmt/core.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Helper Functions

/**
 * @brief Find all source files in a directory
 */
std::vector<fs::path> find_source_files(const fs::path &dir, bool include_headers = true) {
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
    if (!entry.is_regular_file()) {
      continue;
    }

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
 * @brief Find clang-format executable.
 *
 * Checks PATH first (cheap, common case). Then falls back to the registry's
 * known install paths so a freshly-installed (or installed-but-PATH-not-yet-
 * refreshed) clang-format on Windows is still found.
 */
std::string find_clang_format() {
  std::vector<std::string> names = {"clang-format",
                                    "clang-format-18",
                                    "clang-format-17",
                                    "clang-format-16",
                                    "clang-format-15",
                                    "clang-format-14"};

  for (const auto &name : names) {
    if (tool_exists(name)) {
      return name;
    }
  }

  return cforge::locate_installed_tool("clang-format");
}

/**
 * @brief Find clang-tidy executable (same PATH-then-registry fallback as
 *        find_clang_format).
 */
std::string find_clang_tidy() {
  std::vector<std::string> names = {"clang-tidy",
                                    "clang-tidy-18",
                                    "clang-tidy-17",
                                    "clang-tidy-16",
                                    "clang-tidy-15",
                                    "clang-tidy-14"};

  for (const auto &name : names) {
    if (tool_exists(name)) {
      return name;
    }
  }

  return cforge::locate_installed_tool("clang-tidy");
}

// Shell Completion Generators

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

}  // anonymous namespace

// Command Implementations

/**
 * @brief Handle the 'fmt' command for code formatting
 */
cforge_int_t cforge_cmd_fmt(const cforge_context_t *ctx) {
  fs::path project_dir = ctx->working_dir;

  // Parse arguments
  bool check_only   = false;
  bool dry_run      = false;
  std::string style = "file";  // Default: use .clang-format file

  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "--check") {
      check_only = true;
    } else if (arg == "--dry-run") {
      dry_run = true;
    } else if (arg == "--style" && i + 1 < ctx->args.arg_count) {
      style = ctx->args.args[++i];
    }
  }

  // Find clang-format. If missing, offer to install via the platform's
  // package manager. After a successful install, prefer the absolute path the
  // installer dropped (PATH in this process won't reflect the new install
  // until the user opens a fresh shell).
  std::string clang_format = find_clang_format();
  if (clang_format.empty()) {
    cforge::logger::print_error("clang-format not found in PATH");
    auto r = cforge::offer_install_tool("clang-format");
    if (r.status == cforge::install_result::installed) {
      if (!r.path.empty()) {
        clang_format = r.path;
      } else {
        clang_format = find_clang_format();
      }
    }
    if (clang_format.empty()) {
      if (r.status != cforge::install_result::declined) {
        cforge::logger::print_hint("Install clang-format or add it to your PATH");
      }
      return 1;
    }
  }

  cforge::logger::print_action("Formatting", "source files with " + clang_format);

  // Find source files
  std::vector<fs::path> files;

  // Check src and include directories
  for (const auto &dir : {"src", "include", "source", "lib"}) {
    fs::path dir_path = project_dir / dir;
    auto dir_files    = find_source_files(dir_path, true);
    files.insert(files.end(), dir_files.begin(), dir_files.end());
  }

  if (files.empty()) {
    cforge::logger::print_warning("No source files found to format");
    return 0;
  }

  cforge::logger::print_verbose("Found " + std::to_string(files.size()) + " files");

  cforge_int_t formatted_count = 0;
  cforge_int_t failed_count    = 0;

  // Cargo-style: each file gets a permanent "Formatting <name>" row, with a
  // progress bar pinned at the bottom that updates as we go. The label on the
  // bar depends on the mode (check / dry-run / write).
  const std::string step_action = check_only ? "Checking" : (dry_run ? "Previewing" : "Formatting");
  const cforge_int_t total      = static_cast<cforge_int_t>(files.size());
  auto bar_start                = std::chrono::steady_clock::now();
  auto elapsed_secs             = [&]() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - bar_start).count();
  };

  for (cforge_size_t i = 0; i < files.size(); ++i) {
    const auto &file = files[i];
    std::vector<std::string> args;
    args.push_back("-style=" + style);

    if (check_only) {
      args.push_back("--dry-run");
      args.push_back("--Werror");
    } else if (dry_run) {
      args.push_back("--dry-run");
    } else {
      args.push_back("-i");  // In-place
    }

    args.push_back(file.string());

    auto result = cforge::execute_process(clang_format, args, project_dir.string());

    if (result.exit_code == 0) {
      formatted_count++;
    } else {
      failed_count++;
    }

    // Permanent per-file line + bar refresh (bar lives on the row below).
    cforge::logger::progress_step(
        step_action, file.filename().string(), static_cast<cforge_int_t>(i + 1), total);
    cforge::logger::progress_bar(
        static_cast<cforge_int_t>(i + 1), total, true, elapsed_secs(), step_action);

    if (result.exit_code != 0) {
      // Surface per-file failure on its own line (after the bar redraw so
      // the bar position is still on the bottom row).
      if (check_only) {
        cforge::logger::print_warning("Needs formatting: " + file.string());
      } else {
        cforge::logger::print_error("Failed to format: " + file.string());
      }
    }
  }

  // Clear the pinned bar so the summary lands on its own row.
  if (total > 0) {
    fmt::print(stderr, "\r\033[K");
    std::fflush(stderr);
    cforge::logger::reset_progress_display();
  }

  if (check_only) {
    if (failed_count > 0) {
      cforge::logger::print_error(std::to_string(failed_count) + " file(s) need formatting");
      cforge::logger::print_hint("Run 'cforge fmt' to format them");
      return 1;
    } else {
      cforge::logger::print_success("All files are properly formatted");
    }
  } else if (dry_run) {
    cforge::logger::print_status("Would format " + std::to_string(formatted_count) + " file(s)");
  } else {
    cforge::logger::finished("formatted " + std::to_string(formatted_count) + " file(s)", "");
  }

  return 0;
}

/**
 * @brief Handle the 'lint' command for static analysis
 */
cforge_int_t cforge_cmd_lint(const cforge_context_t *ctx) {
  fs::path project_dir = ctx->working_dir;
  fs::path build_dir   = project_dir / "build";

  // Parse arguments
  bool fix           = false;
  std::string checks = "";

  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "--fix") {
      fix = true;
    } else if (arg == "--checks" && i + 1 < ctx->args.arg_count) {
      checks = ctx->args.args[++i];
    }
  }

  // Find clang-tidy. If missing, offer to install via the platform's package
  // manager. On install success we prefer the absolute path returned by the
  // installer — the current process's PATH won't reflect the new entry until
  // the user opens a fresh shell, so we'd otherwise loop on "not found".
  std::string clang_tidy = find_clang_tidy();
  if (clang_tidy.empty()) {
    cforge::logger::print_error("clang-tidy not found in PATH");
    auto r = cforge::offer_install_tool("clang-tidy");
    if (r.status == cforge::install_result::installed) {
      if (!r.path.empty()) {
        clang_tidy = r.path;
      } else {
        clang_tidy = find_clang_tidy();
      }
    }
    if (clang_tidy.empty()) {
      if (r.status != cforge::install_result::declined) {
        cforge::logger::print_hint("Install clang-tidy or add it to your PATH");
      }
      return 1;
    }
  }

  cforge::logger::print_action("Analyzing", "source files with " + clang_tidy);

  // Check for compile_commands.json. Treat "missing" and "exists but is an
  // empty/almost-empty JSON array" the same way — both happen with the
  // Visual Studio generator on Windows (which silently ignores
  // CMAKE_EXPORT_COMPILE_COMMANDS), and clang-tidy with no usable database
  // just spams its own USAGE help text for every source file.
  fs::path compile_commands = build_dir / "compile_commands.json";
  auto cdb_usable           = [&]() {
    std::error_code ec;
    if (!fs::exists(compile_commands, ec)) {
      return false;
    }
    return fs::file_size(compile_commands, ec) > 16;  // "[]" is 2 bytes
  };
  if (!cdb_usable()) {
    cforge::logger::print_warning("compile_commands.json missing or empty in "
                                  + build_dir.string());
    cforge::logger::print_verbose(
        "Re-running cmake configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON…");

    std::vector<std::string> cmake_args = {
        "-B", build_dir.string(), "-S", project_dir.string(), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"};
    auto result = cforge::execute_process("cmake", cmake_args, project_dir.string());
    if (result.exit_code != 0 || !cdb_usable()) {
      cforge::logger::print_error("Could not generate a usable compile_commands.json");
      cforge::logger::print_hint(
          "The Visual Studio generator doesn't produce compile_commands.json. "
          "Try regenerating with Ninja:");
      cforge::logger::print_plain("  cforge clean && cforge build  # using Ninja generator");
      return 1;
    }
  }

  // Find source files (only .cpp files for clang-tidy). Scan project source
  // dirs but never the build tree — fetched third-party deps land under
  // build/_deps and we don't want to lint them. (compile_commands.json
  // typically includes them as build entries, so without this skip they'd
  // sneak through the cdb filter below.)
  std::vector<fs::path> files;
  for (const auto &dir : {"src", "source", "lib"}) {
    fs::path dir_path = project_dir / dir;
    auto dir_files    = find_source_files(dir_path, false);  // No headers
    files.insert(files.end(), dir_files.begin(), dir_files.end());
  }

  if (files.empty()) {
    cforge::logger::print_warning("No source files found to analyze");
    return 0;
  }

  // Restrict the candidate list to files that actually appear in
  // compile_commands.json. Anything else (helper .c files dropped into src/
  // but not added to any CMake target, generated stubs, etc.) would cause
  // clang-tidy to dump its USAGE banner instead of running.
  //
  // We don't need a real JSON parser — compile_commands.json is a flat array
  // of objects with `"file": "..."` entries, so a textual scan is enough.
  // Normalize to lowercase forward-slash absolute paths so the comparison
  // works regardless of how each side spells the path.
  auto normalize = [](const fs::path &p) {
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(p, ec);
    if (ec) {
      abs = fs::absolute(p);
    }
    std::string s = abs.generic_string();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
  };

  std::vector<std::string> cdb_files;
  {
    std::ifstream in(compile_commands);
    std::ostringstream oss;
    oss << in.rdbuf();
    std::string text = oss.str();
    static const std::regex file_re("\"file\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    auto begin = std::sregex_iterator(text.begin(), text.end(), file_re);
    auto end   = std::sregex_iterator();
    cdb_files.reserve(static_cast<size_t>(std::distance(begin, end)));
    for (auto it = begin; it != end; ++it) {
      std::string p = (*it)[1].str();
      // Un-escape \\ -> \ and \" -> " so the path matches what's on disk.
      std::string unesc;
      unesc.reserve(p.size());
      for (cforge_size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '\\' && i + 1 < p.size()) {
          unesc.push_back(p[++i]);
        } else {
          unesc.push_back(p[i]);
        }
      }
      cdb_files.push_back(normalize(unesc));
    }
  }

  std::vector<fs::path> filtered;
  filtered.reserve(files.size());
  cforge_int_t skipped = 0;
  for (const auto &f : files) {
    auto n      = normalize(f);
    bool in_cdb = std::find(cdb_files.begin(), cdb_files.end(), n) != cdb_files.end();
    if (in_cdb) {
      filtered.push_back(f);
    } else {
      ++skipped;
      cforge::logger::print_verbose("Skipping " + f.filename().string()
                                    + " (not in compile_commands.json)");
    }
  }
  files.swap(filtered);

  if (skipped > 0) {
    cforge::logger::print_verbose("Skipped " + std::to_string(skipped)
                                  + " file(s) not part of any build target");
  }

  if (files.empty()) {
    cforge::logger::print_warning(
        "No analyzable source files (none matched compile_commands.json)");
    return 0;
  }

  cforge::logger::print_verbose("Analyzing " + std::to_string(files.size()) + " file(s)...");

  cforge_int_t warnings = 0;
  cforge_int_t errors   = 0;
  // Capture clang-tidy output to surface after each file so it doesn't fight
  // for the bottom row with the progress bar. Each TU's diagnostics are
  // printed once the file is done, then the bar redraws underneath.
  const cforge_int_t total = static_cast<cforge_int_t>(files.size());
  auto bar_start           = std::chrono::steady_clock::now();
  auto elapsed_secs        = [&]() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - bar_start).count();
  };

  // If the user didn't pass --checks and there's no .clang-tidy file anywhere
  // up the tree, clang-tidy bails with "Error: no checks enabled." and prints
  // its USAGE help instead of running. Provide a sensible default check set
  // (clang's static analyzer + bugprone) so `cforge lint` does something
  // useful out of the box. The user can still override with --checks.
  //
  // Suppress `clang-diagnostic-*` — those are clang's own compile warnings,
  // and they fire for every translation unit on Windows/MinGW because
  // clang-tidy uses its bundled clang to parse and it doesn't know where the
  // gcc/MSVC system headers live ("cstddef file not found" etc.). The real
  // compiler diagnostics already come through `cforge build` so this is pure
  // noise here. The value of clang-tidy is in its extra checks, not in
  // re-litigating compile errors.
  auto has_clang_tidy_config = [&]() {
    for (fs::path p = project_dir;; p = p.parent_path()) {
      std::error_code ec;
      if (fs::exists(p / ".clang-tidy", ec)) {
        return true;
      }
      if (p == p.parent_path()) {
        return false;  // hit filesystem root
      }
    }
  };
  std::string effective_checks = checks;
  if (effective_checks.empty() && !has_clang_tidy_config()) {
    effective_checks = "clang-analyzer-*,bugprone-*,-clang-diagnostic-*";
    cforge::logger::print_verbose("No .clang-tidy found; using default checks: "
                                  + effective_checks);
  }

  for (cforge_size_t i = 0; i < files.size(); ++i) {
    const auto &file = files[i];
    std::vector<std::string> args;
    args.push_back("-p");
    args.push_back(build_dir.string());

    if (!effective_checks.empty()) {
      args.push_back("-checks=" + effective_checks);
    }

    if (fix) {
      args.push_back("-fix");
    }

    args.push_back(file.string());

    // Collect lines silently while clang-tidy runs, then emit after we
    // print the cargo-style step + bar so the order is:
    //   Analyzing X [i/N]
    //   Analyzing [=====>     ] i/N (t)
    //   <any warnings/errors for X>
    std::vector<std::string> deferred;
    auto result = cforge::execute_process(
        clang_tidy, args, project_dir.string(), [&deferred](const std::string &chunk) {
          // Split into lines but keep them buffered until the file finishes.
          std::string buf     = chunk;
          cforge_size_t start = 0;
          for (cforge_size_t p = 0; p <= buf.size(); ++p) {
            if (p == buf.size() || buf[p] == '\n') {
              if (p > start) {
                std::string line = buf.substr(start, p - start);
                while (!line.empty() && line.back() == '\r') {
                  line.pop_back();
                }
                if (!line.empty()) {
                  deferred.push_back(std::move(line));
                }
              }
              start = p + 1;
            }
          }
        });

    cforge::logger::progress_step(
        "Analyzing", file.filename().string(), static_cast<cforge_int_t>(i + 1), total);
    cforge::logger::progress_bar(
        static_cast<cforge_int_t>(i + 1), total, true, elapsed_secs(), "Analyzing");

    // First-file sanity check: if clang-tidy dumps its own USAGE help, it
    // means it couldn't find the file in compile_commands.json (or the
    // database is missing) and is falling back to printing the manpage.
    // Streaming that for every file produces thousands of lines of noise —
    // detect it once and bail with a clear message.
    bool dumped_usage = false;
    for (const auto &line : deferred) {
      if (line.rfind("USAGE: ", 0) == 0 || line.rfind("Usage: ", 0) == 0) {
        dumped_usage = true;
        break;
      }
    }
    if (dumped_usage) {
      // Drop the bar line so the error lands on its own row.
      fmt::print(stderr, "\r\033[K");
      std::fflush(stderr);
      cforge::logger::reset_progress_display();
      cforge::logger::print_error("clang-tidy could not analyze " + file.filename().string()
                                  + " (printed its help text instead of running)");
      cforge::logger::print_hint(
          "compile_commands.json at '" + compile_commands.string()
          + "' likely doesn't include this file. Try `cforge clean && cforge "
            "build` with a Ninja generator (Visual Studio generator doesn't "
            "produce a usable compile_commands.json).");
      return 1;
    }

    // Drop two classes of noise from clang-tidy's output before showing it:
    //
    //   1. Diagnostics that point inside the build tree — those come from
    //      third-party headers (fmt, etc.) pulled in by user TUs and aren't
    //      actionable from the user's codebase.
    //   2. Diagnostics tagged [clang-diagnostic-*] — those are clang's own
    //      compile errors/warnings being re-emitted by clang-tidy. On Windows
    //      with MinGW, clang-tidy's bundled clang doesn't know where gcc's
    //      stdlib headers live ("'cstddef' file not found" for every TU) so
    //      this would otherwise drown out actual lint findings. The real
    //      compiler diagnostics come through `cforge build` separately.
    //
    // We carry through continuation lines (notes, source snippets, carets)
    // until the next top-level diagnostic.
    std::string build_dir_marker     = build_dir.filename().string() + "/_deps/";
    std::string build_dir_marker_alt = build_dir.filename().string() + "\\_deps\\";
    auto looks_like_diag_start       = [](const std::string &l) {
      return l.find("warning:") != std::string::npos || l.find("error:") != std::string::npos
          || l.find("note:") != std::string::npos;
    };
    auto is_third_party = [&](const std::string &l) {
      return l.find(build_dir_marker) != std::string::npos
          || l.find(build_dir_marker_alt) != std::string::npos;
    };
    auto is_compiler_diag = [](const std::string &l) {
      return l.find("[clang-diagnostic-") != std::string::npos;
    };
    std::vector<std::string> kept;
    kept.reserve(deferred.size());
    bool dropping = false;
    for (const auto &line : deferred) {
      if (looks_like_diag_start(line)) {
        dropping = is_third_party(line) || is_compiler_diag(line);
      }
      if (!dropping) {
        kept.push_back(line);
      }
    }

    // The bar is sitting on the current row with no trailing newline. If
    // we have anything to print for this file, move past it first so the
    // first diagnostic isn't pasted onto the bar text.
    if (!kept.empty()) {
      fmt::print(stderr, "\n");
      std::fflush(stderr);
      cforge::logger::reset_progress_display();
    }
    for (const auto &line : kept) {
      if (line.find("warning:") != std::string::npos) {
        warnings++;
        cforge::logger::print_warning(line);
      } else if (line.find("error:") != std::string::npos) {
        errors++;
        cforge::logger::print_error(line);
      } else {
        cforge::logger::print_plain(line);
      }
    }
  }

  if (total > 0) {
    fmt::print(stderr, "\r\033[K");
    std::fflush(stderr);
    cforge::logger::reset_progress_display();
  }

  // Summary
  cforge::logger::print_blank();
  if (errors > 0 || warnings > 0) {
    cforge::logger::print_verbose("Analysis complete:");
    if (errors > 0) {
      cforge::logger::print_error(std::to_string(errors) + " error(s)");
    }
    if (warnings > 0) {
      cforge::logger::print_warning(std::to_string(warnings) + " warning(s)");
    }
    if (fix) {
      cforge::logger::print_verbose("Some issues may have been automatically fixed");
    } else {
      cforge::logger::print_hint("Run 'cforge lint --fix' to automatically fix some issues");
    }
    return errors > 0 ? 1 : 0;
  } else {
    cforge::logger::print_success("No issues found");
    return 0;
  }
}

/**
 * @brief Handle the 'completions' command
 */
cforge_int_t cforge_cmd_completions(const cforge_context_t *ctx) {
  std::string shell = "bash";  // Default

  // Parse shell argument
  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "bash" || arg == "zsh" || arg == "powershell" || arg == "fish" || arg == "ps"
        || arg == "ps1") {
      shell = arg;
      if (shell == "ps" || shell == "ps1") {
        shell = "powershell";
      }
    }
  }

  std::string script;
  std::string install_hint;

  if (shell == "bash") {
    script       = generate_bash_completions();
    install_hint = "Add to ~/.bashrc or save to /etc/bash_completion.d/cforge";
  } else if (shell == "zsh") {
    script       = generate_zsh_completions();
    install_hint = "Save to a file in your fpath (e.g., ~/.zsh/completions/_cforge)";
  } else if (shell == "powershell") {
    script       = generate_powershell_completions();
    install_hint = "Add to your $PROFILE";
  } else if (shell == "fish") {
    script       = generate_fish_completions();
    install_hint = "Save to ~/.config/fish/completions/cforge.fish";
  } else {
    cforge::logger::print_error("Unknown shell: " + shell);
    cforge::logger::print_hint("Supported shells: bash, zsh, powershell, fish");
    return 1;
  }

  // Output the script (use raw print to stdout, not logger, since it's being
  // piped)
  cforge::logger::print_plain(script);

  // Print install hint to stderr so it doesn't pollute piped output
  cforge::logger::print_dim("# " + install_hint);

  return 0;
}
