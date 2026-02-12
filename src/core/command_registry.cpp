/**
 * @file command_registry.cpp
 * @brief Command registry implementation
 */

#include "core/command_registry.hpp"
#include "cforge/log.hpp"
#include "core/commands.hpp"

#include <algorithm>
#include <iomanip>

namespace cforge {

// Global flags available to all commands
const std::vector<flag_def> global_flags = {
    {"-c", "--config", "Build configuration (Debug, Release, etc.)", "CONFIG", "", false},
    {"-v", "--verbose", "Enable verbose output", "", "", false},
    {"-q", "--quiet", "Suppress non-essential output", "", "", false},
    {"-h", "--help", "Show help for this command", "", "", false},
};

command_registry &command_registry::instance() {
  static command_registry instance;
  return instance;
}

void command_registry::register_command(const command_def &cmd) {
  cforge_size_t index = commands_.size();
  commands_.push_back(cmd);

  // Index primary name
  name_index_[cmd.name] = index;

  // Index aliases
  for (const auto &alias : cmd.aliases) {
    name_index_[alias] = index;
  }
}

void command_registry::register_deprecated(const deprecated_command &dep) {
  deprecated_.push_back(dep);
}

const command_def *command_registry::find(const std::string &name) const {
  auto it = name_index_.find(name);
  if (it != name_index_.end()) {
    return &commands_[it->second];
  }
  return nullptr;
}

const deprecated_command *command_registry::find_deprecated(const std::string &name) const {
  for (const auto &dep : deprecated_) {
    if (dep.old_name == name) {
      return &dep;
    }
  }
  return nullptr;
}

cforge_int_t command_registry::dispatch(const std::string &name, const cforge_context_t *ctx) {
  // Check for deprecated commands first
  if (auto dep = find_deprecated(name)) {
    logger::print_error("'" + name + "' has been removed");
    logger::print_plain("");
    logger::print_plain("  " + dep->message);
    logger::print_plain("  Run 'cforge help " + dep->new_name + "' for more information.");
    return 1;
  }

  // Find and execute command
  if (auto cmd = find(name)) {
    if (cmd->handler) {
      return cmd->handler(ctx);
    }
    logger::print_error("Command '" + name + "' has no handler");
    return 1;
  }

  // Command not found - suggest similar commands
  logger::print_error("Unknown command: " + name);

  auto suggestions = suggest_similar(name);
  if (!suggestions.empty()) {
    logger::print_plain("");
    logger::print_plain("  Did you mean one of these?");
    for (const auto &s : suggestions) {
      logger::print_plain("    " + s);
    }
  }

  logger::print_plain("");
  logger::print_plain("Run 'cforge help' for a list of available commands.");
  return 1;
}

std::vector<const command_def *> command_registry::list_commands(bool include_hidden) const {
  std::vector<const command_def *> result;
  for (const auto &cmd : commands_) {
    if (!cmd.hidden || include_hidden) {
      result.push_back(&cmd);
    }
  }
  return result;
}

std::vector<std::string> command_registry::get_completions(const std::string &partial) const {
  std::vector<std::string> result;
  for (const auto &cmd : commands_) {
    if (cmd.hidden) continue;
    if (cmd.name.find(partial) == 0) {
      result.push_back(cmd.name);
    }
    for (const auto &alias : cmd.aliases) {
      if (alias.find(partial) == 0) {
        result.push_back(alias);
      }
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

void command_registry::print_command_help(const std::string &name) const {
  auto cmd = find(name);
  if (!cmd) {
    logger::print_error("Unknown command: " + name);
    return;
  }

  // Header
  logger::print_cmd_header(cmd->name, cmd->brief);
  logger::print_usage("cforge " + (cmd->usage.empty() ? cmd->name : cmd->usage));

  // Description
  if (!cmd->description.empty()) {
    logger::print_help_section("DESCRIPTION");
    logger::print_dim(cmd->description, 4);
    logger::print_blank();
  }

  // Aliases
  if (!cmd->aliases.empty()) {
    std::string aliases_str;
    for (size_t i = 0; i < cmd->aliases.size(); i++) {
      if (i > 0) aliases_str += ", ";
      aliases_str += cmd->aliases[i];
    }
    logger::print_help_section("ALIASES");
    logger::print_dim(aliases_str, 4);
    logger::print_blank();
  }

  // Command-specific flags
  if (!cmd->flags.empty()) {
    logger::print_help_section("OPTIONS");
    for (const auto &flag : cmd->flags) {
      std::string flag_str;
      if (!flag.short_name.empty()) {
        flag_str += flag.short_name;
        if (!flag.long_name.empty()) flag_str += ", ";
      }
      if (!flag.long_name.empty()) {
        flag_str += flag.long_name;
        if (!flag.value_name.empty()) {
          flag_str += " <" + flag.value_name + ">";
        }
      }

      std::string desc = flag.description;
      if (!flag.default_value.empty()) {
        desc += " (default: " + flag.default_value + ")";
      }
      logger::print_option(flag_str, desc);
    }
    logger::print_blank();
  }

  // Global flags
  logger::print_help_section("GLOBAL OPTIONS");
  for (const auto &flag : global_flags) {
    std::string flag_str;
    if (!flag.short_name.empty()) {
      flag_str += flag.short_name;
      if (!flag.long_name.empty()) flag_str += ", ";
    }
    flag_str += flag.long_name;
    logger::print_option(flag_str, flag.description);
  }
  logger::print_blank();

  // Examples
  if (!cmd->examples.empty()) {
    logger::print_help_section("EXAMPLES");
    for (const auto &example : cmd->examples) {
      logger::print_example(example);
    }
    logger::print_blank();
  }

  // See also
  if (!cmd->see_also.empty()) {
    std::string see_also_str;
    for (size_t i = 0; i < cmd->see_also.size(); i++) {
      if (i > 0) see_also_str += ", ";
      see_also_str += cmd->see_also[i];
    }
    logger::print_help_section("SEE ALSO");
    logger::print_dim(see_also_str, 4);
  }
}

void command_registry::print_general_help() const {
  logger::print_emphasis("cforge - Modern C/C++ build tool and package manager");
  logger::print_blank();
  logger::print_dim("Usage: cforge <command> [options]");
  logger::print_blank();

  // Group commands by category
  struct category {
    std::string name;
    std::vector<std::string> commands;
  };

  std::vector<category> categories = {
      {"Project", {"init", "build", "run", "clean", "test", "bench"}},
      {"Dependencies", {"deps", "vcpkg"}},
      {"Code Quality", {"fmt", "lint", "circular"}},
      {"IDE & Tools", {"ide", "watch", "doc", "new"}},
      {"Package", {"package", "install"}},
      {"Cache", {"cache"}},
      {"Other", {"version", "upgrade", "doctor", "completions", "help"}},
  };

  for (const auto &cat : categories) {
    logger::print_section(cat.name + ":");
    for (const auto &cmd_name : cat.commands) {
      if (auto cmd = find(cmd_name)) {
        // Use formatted output: green command name, regular description
        logger::print_subcommand(cmd->name, cmd->brief, 14);
      }
    }
    logger::print_blank();
  }

  logger::print_dim("Run 'cforge <command> --help' for detailed help on a command.");
  logger::print_dim("Run 'cforge help <command>' for the same information.");
}

cforge_size_t command_registry::levenshtein_distance(const std::string &a, const std::string &b) {
  const cforge_size_t m = a.size();
  const cforge_size_t n = b.size();

  if (m == 0) return n;
  if (n == 0) return m;

  std::vector<std::vector<cforge_size_t>> dp(m + 1, std::vector<cforge_size_t>(n + 1));

  for (cforge_size_t i = 0; i <= m; i++) dp[i][0] = i;
  for (cforge_size_t j = 0; j <= n; j++) dp[0][j] = j;

  for (cforge_size_t i = 1; i <= m; i++) {
    for (cforge_size_t j = 1; j <= n; j++) {
      cforge_size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      dp[i][j] = std::min({dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost});
    }
  }

  return dp[m][n];
}

std::vector<std::string> command_registry::suggest_similar(const std::string &name,
                                                           cforge_size_t max_suggestions) const {
  std::vector<std::pair<cforge_size_t, std::string>> scored;

  for (const auto &cmd : commands_) {
    if (cmd.hidden) continue;
    cforge_size_t dist = levenshtein_distance(name, cmd.name);
    // Only suggest if reasonably close
    if (dist <= 3 || (dist <= name.length() / 2 + 1)) {
      scored.emplace_back(dist, cmd.name);
    }
  }

  std::sort(scored.begin(), scored.end());

  std::vector<std::string> result;
  for (cforge_size_t i = 0; i < std::min(max_suggestions, scored.size()); i++) {
    result.push_back(scored[i].second);
  }
  return result;
}

// ============================================================================
// Built-in command registration
// ============================================================================

void register_builtin_commands() {
  auto &reg = command_registry::instance();

  // Build command
  reg.register_command({
      "build",
      {},
      "Build the project",
      "Compile the project using CMake. Automatically detects the best generator\n"
      "for your platform and handles configuration changes.",
      "build [options] [target]",
      {
          {"", "--target", "Build specific target", "TARGET", "", false},
          {"", "--jobs", "Number of parallel jobs", "N", "", false},
          {"", "--force", "Force full rebuild", "", "", false},
      },
      {"cforge build", "cforge build --config Release", "cforge build --target mylib"},
      {"run", "clean", "test"},
      false,
      cforge_cmd_build,
      nullptr,
  });

  // Init command
  reg.register_command({
      "init",
      {},
      "Initialize a new project",
      "Create a new cforge project with the standard directory structure.",
      "init [name] [options]",
      {
          {"", "--lib", "Create a library project", "", "", false},
          {"", "--exe", "Create an executable project (default)", "", "", false},
          {"", "--cpp", "C++ standard to use", "STANDARD", "17", false},
      },
      {"cforge init myproject", "cforge init mylib --lib --cpp 20"},
      {"build"},
      false,
      cforge_cmd_init,
      nullptr,
  });

  // Run command
  reg.register_command({
      "run",
      {},
      "Build and run the project",
      "Compile the project and execute the resulting binary.",
      "run [options] [-- args]",
      {
          {"", "--release", "Build in release mode", "", "", false},
      },
      {"cforge run", "cforge run --config Release -- --arg1 value1"},
      {"build"},
      false,
      cforge_cmd_run,
      nullptr,
  });

  // Clean command
  reg.register_command({
      "clean",
      {},
      "Clean build artifacts",
      "Remove the build directory and generated files.",
      "clean [options]",
      {
          {"", "--all", "Also clean cached dependencies", "", "", false},
      },
      {"cforge clean", "cforge clean --all"},
      {"build"},
      false,
      cforge_cmd_clean,
      nullptr,
  });

  // Test command
  reg.register_command({
      "test",
      {},
      "Run project tests",
      "Build and run tests using the detected test framework.",
      "test [options] [filter]",
      {
          {"", "--filter", "Run only tests matching pattern", "PATTERN", "", false},
          {"", "--verbose", "Show test output", "", "", false},
      },
      {"cforge test", "cforge test --filter '*unit*'"},
      {"build", "bench"},
      false,
      cforge_cmd_test,
      nullptr,
  });

  // Deps command
  reg.register_command({
      "deps",
      {},
      "Manage dependencies",
      "Add, remove, and manage project dependencies from the registry.",
      "deps <subcommand> [options]",
      {},
      {"cforge deps add fmt", "cforge deps remove spdlog", "cforge deps search json"},
      {},
      false,
      cforge_cmd_deps,
      nullptr,
  });

  // Vcpkg command (convenience alias for 'deps vcpkg')
  reg.register_command({
      "vcpkg",
      {},
      "Manage vcpkg integration",
      "Set up, update, and manage vcpkg for the project.\n"
      "This is a convenience alias for 'cforge deps vcpkg'.",
      "vcpkg <setup|update|list>",
      {},
      {"cforge vcpkg setup", "cforge vcpkg update", "cforge vcpkg list"},
      {"deps"},
      false,
      cforge_cmd_vcpkg,
      nullptr,
  });

  // Cache command
  reg.register_command({
      "cache",
      {},
      "Manage binary cache",
      "View and manage the local and remote binary cache for dependencies.",
      "cache <subcommand> [options]",
      {},
      {"cforge cache list", "cforge cache stats", "cforge cache clean"},
      {"deps", "build"},
      false,
      cforge_cmd_cache,
      nullptr,
  });

  // Format command
  reg.register_command({
      "fmt",
      {"format"},
      "Format source code",
      "Run clang-format on project source files.",
      "fmt [options] [files]",
      {
          {"", "--check", "Check formatting without making changes", "", "", false},
      },
      {"cforge fmt", "cforge fmt --check"},
      {"lint"},
      false,
      cforge_cmd_fmt,
      nullptr,
  });

  // Lint command
  reg.register_command({
      "lint",
      {"check"},
      "Run static analysis",
      "Run clang-tidy static analysis on project source files.",
      "lint [options] [files]",
      {
          {"", "--fix", "Automatically apply fixes", "", "", false},
      },
      {"cforge lint", "cforge lint --fix"},
      {"fmt"},
      false,
      cforge_cmd_lint,
      nullptr,
  });

  // Bench command
  reg.register_command({
      "bench",
      {"benchmark"},
      "Run benchmarks",
      "Build and run benchmarks using the detected benchmark framework.\n"
      "Supports Google Benchmark, nanobench, and Catch2 BENCHMARK.\n"
      "Benchmarks run in Release mode by default for accurate timing.",
      "bench [options] [benchmark-name]",
      {
          {"", "--no-build", "Skip building before running", "", "", false},
          {"", "--filter", "Run only benchmarks matching pattern", "PATTERN", "", false},
          {"", "--json", "Output in JSON format", "", "", false},
          {"", "--csv", "Output in CSV format", "", "", false},
      },
      {"cforge bench", "cforge bench --filter 'BM_Sort'", "cforge bench --no-build", "cforge bench --json > results.json"},
      {"test"},
      false,
      cforge_cmd_bench,
      nullptr,
  });

  // Package command
  reg.register_command({
      "package",
      {"pack"},
      "Create distributable packages",
      "Generate installers and archives for distribution.",
      "package [options]",
      {
          {"", "--generator", "CPack generator to use", "GEN", "", false},
      },
      {"cforge package", "cforge package --generator ZIP"},
      {"build", "install"},
      false,
      cforge_cmd_package,
      nullptr,
  });

  // Install command
  reg.register_command({
      "install",
      {},
      "Install the project",
      "Build and install the project to the system.",
      "install [options]",
      {
          {"", "--prefix", "Installation prefix", "PATH", "", false},
      },
      {"cforge install", "cforge install --prefix /usr/local"},
      {"build", "package"},
      false,
      cforge_cmd_install,
      nullptr,
  });

  // IDE command
  reg.register_command({
      "ide",
      {},
      "Generate IDE configurations",
      "Generate project files for various IDEs.",
      "ide <vscode|clion|vs|xcode>",
      {},
      {"cforge ide vscode", "cforge ide vs"},
      {},
      false,
      cforge_cmd_ide,
      nullptr,
  });

  // Watch command
  reg.register_command({
      "watch",
      {},
      "Auto-rebuild on changes",
      "Watch for file changes and automatically rebuild.",
      "watch [options]",
      {},
      {"cforge watch"},
      {"build"},
      false,
      cforge_cmd_watch,
      nullptr,
  });

  // Doc command
  reg.register_command({
      "doc",
      {"docs"},
      "Generate documentation",
      "Generate API documentation using Doxygen.",
      "doc [options]",
      {
          {"", "--open", "Open docs in browser after generation", "", "", false},
      },
      {"cforge doc", "cforge doc --open"},
      {},
      false,
      cforge_cmd_doc,
      nullptr,
  });

  // New command
  reg.register_command({
      "new",
      {},
      "Create files from templates",
      "Generate source files from built-in templates.",
      "new <class|header|interface|test|struct> <name>",
      {},
      {"cforge new class MyClass", "cforge new test MyClass"},
      {},
      false,
      cforge_cmd_new,
      nullptr,
  });

  // Circular command
  reg.register_command({
      "circular",
      {},
      "Detect circular includes",
      "Analyze header files to find circular include dependencies.",
      "circular [options]",
      {
          {"", "--include-deps", "Also check dependency headers", "", "", false},
          {"", "--workspace", "Check all workspace projects", "", "", false},
          {"", "--json", "Output as JSON", "", "", false},
          {"", "--limit", "Limit output to first N chains", "N", "", false},
      },
      {"cforge circular", "cforge circular --workspace", "cforge circular --json"},
      {"lint"},
      false,
      cforge_cmd_circular,
      nullptr,
  });

  // Doctor command
  reg.register_command({
      "doctor",
      {},
      "Diagnose environment",
      "Check system for required tools and report any issues.",
      "doctor",
      {},
      {"cforge doctor"},
      {},
      false,
      cforge_cmd_doctor,
      nullptr,
  });

  // Version command
  reg.register_command({
      "version",
      {},
      "Show version information",
      "Display cforge version and build information.",
      "version",
      {},
      {"cforge version"},
      {},
      false,
      cforge_cmd_version,
      nullptr,
  });

  // Upgrade command (self-update)
  reg.register_command({
      "upgrade",
      {},
      "Upgrade cforge itself",
      "Download, build, and install the latest version of cforge from GitHub.",
      "upgrade [options]",
      {
          {"", "--path", "Custom installation path", "PATH", "", false},
          {"", "--add-to-path", "Add install location to PATH", "", "", false},
      },
      {"cforge upgrade", "cforge upgrade --path /opt/cforge"},
      {"version", "doctor"},
      false,
      cforge_cmd_upgrade,
      nullptr,
  });

  // Completions command
  reg.register_command({
      "completions",
      {},
      "Generate shell completions",
      "Output shell completion scripts for various shells.",
      "completions <bash|zsh|fish|powershell>",
      {},
      {"cforge completions bash >> ~/.bashrc"},
      {},
      false,
      cforge_cmd_completions,
      nullptr,
  });

  // Help command
  reg.register_command({
      "help",
      {},
      "Show help information",
      "Display general help or help for a specific command.",
      "help [command]",
      {},
      {"cforge help", "cforge help build"},
      {},
      false,
      cforge_cmd_help,
      nullptr,
  });

  // -------------------------------------------------------------------------
  // Register deprecated commands (removed in this version)
  // -------------------------------------------------------------------------

  reg.register_deprecated({
      "add", "deps add",
      "Use 'cforge deps add <package>' instead."
  });

  reg.register_deprecated({
      "remove", "deps remove",
      "Use 'cforge deps remove <package>' instead."
  });

  reg.register_deprecated({
      "update", "deps update",
      "Use 'cforge deps update' instead."
  });

  reg.register_deprecated({
      "search", "deps search",
      "Use 'cforge deps search <query>' instead."
  });

  reg.register_deprecated({
      "info", "deps info",
      "Use 'cforge deps info <package>' instead."
  });

  reg.register_deprecated({
      "list", "deps list",
      "Use 'cforge deps list' instead."
  });

  reg.register_deprecated({
      "tree", "deps tree",
      "Use 'cforge deps tree' instead."
  });

  reg.register_deprecated({
      "lock", "deps lock",
      "Use 'cforge deps lock' instead."
  });
}

} // namespace cforge
