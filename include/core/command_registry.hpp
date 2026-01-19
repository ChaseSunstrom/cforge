/**
 * @file command_registry.hpp
 * @brief Command table pattern for cforge CLI
 *
 * Provides a registry of commands with metadata for dispatch, help generation,
 * and shell completions. Replaces the if/else chain in command_dispatch.cpp.
 */

#ifndef CFORGE_COMMAND_REGISTRY_HPP
#define CFORGE_COMMAND_REGISTRY_HPP

#include "core/command.h"
#include "core/types.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Flag definition for command help
 */
struct flag_def {
  std::string short_name;     // "-c"
  std::string long_name;      // "--config"
  std::string description;
  std::string value_name;     // "CONFIG" for "--config <CONFIG>"
  std::string default_value;
  bool required = false;
};

/**
 * @brief Command definition
 */
struct command_def {
  std::string name;                         // Primary command name
  std::vector<std::string> aliases;         // Alternative names
  std::string brief;                        // One-line description
  std::string description;                  // Detailed description
  std::string usage;                        // Usage pattern (e.g., "build [options] [target]")
  std::vector<flag_def> flags;              // Command-specific flags
  std::vector<std::string> examples;        // Usage examples
  std::vector<std::string> see_also;        // Related commands
  bool hidden = false;                      // Don't show in help listing

  // Handler function
  std::function<cforge_int_t(const cforge_context_t *)> handler;

  // Completion function (returns possible completions for args)
  std::function<std::vector<std::string>(const std::vector<std::string> &)> completer;
};

/**
 * @brief Deprecated command entry
 */
struct deprecated_command {
  std::string old_name;
  std::string new_name;
  std::string message;
};

/**
 * @brief Command registry singleton
 *
 * Manages all cforge commands and provides dispatch, help, and completion.
 */
class command_registry {
public:
  /**
   * @brief Get the singleton instance
   */
  static command_registry &instance();

  /**
   * @brief Register a command
   * @param cmd Command definition
   */
  void register_command(const command_def &cmd);

  /**
   * @brief Register a deprecated command that shows error
   * @param dep Deprecated command entry
   */
  void register_deprecated(const deprecated_command &dep);

  /**
   * @brief Dispatch a command by name
   * @param name Command name or alias
   * @param ctx Execution context
   * @return Exit code from command handler
   */
  cforge_int_t dispatch(const std::string &name, const cforge_context_t *ctx);

  /**
   * @brief Find a command by name or alias
   * @param name Command name or alias
   * @return Pointer to command definition, or nullptr if not found
   */
  const command_def *find(const std::string &name) const;

  /**
   * @brief Get all registered commands
   * @param include_hidden Include hidden commands
   * @return Vector of command definitions
   */
  std::vector<const command_def *> list_commands(bool include_hidden = false) const;

  /**
   * @brief Get completions for a partial command
   * @param partial Partial input
   * @return Possible completions
   */
  std::vector<std::string> get_completions(const std::string &partial) const;

  /**
   * @brief Print help for a specific command
   * @param name Command name
   */
  void print_command_help(const std::string &name) const;

  /**
   * @brief Print general help (command listing)
   */
  void print_general_help() const;

  /**
   * @brief Check if a command name is deprecated
   * @param name Command name
   * @return Pointer to deprecated entry, or nullptr
   */
  const deprecated_command *find_deprecated(const std::string &name) const;

  /**
   * @brief Suggest similar command names (for typo correction)
   * @param name Misspelled command name
   * @param max_suggestions Maximum suggestions to return
   * @return Vector of similar command names
   */
  std::vector<std::string> suggest_similar(const std::string &name,
                                           cforge_size_t max_suggestions = 3) const;

private:
  command_registry() = default;
  ~command_registry() = default;
  command_registry(const command_registry &) = delete;
  command_registry &operator=(const command_registry &) = delete;

  std::vector<command_def> commands_;
  std::map<std::string, cforge_size_t> name_index_;  // name/alias -> command index
  std::vector<deprecated_command> deprecated_;

  /**
   * @brief Calculate edit distance between two strings
   */
  static cforge_size_t levenshtein_distance(const std::string &a, const std::string &b);
};

/**
 * @brief Initialize all built-in commands
 *
 * Call this at startup to register all cforge commands.
 */
void register_builtin_commands();

/**
 * @brief Global command flags (available to all commands)
 */
extern const std::vector<flag_def> global_flags;

} // namespace cforge

#endif // CFORGE_COMMAND_REGISTRY_HPP
