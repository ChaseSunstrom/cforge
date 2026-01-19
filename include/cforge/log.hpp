/**
 * @file log.hpp
 * @brief Cargo-style logging utilities for cforge
 *
 * Output format matches Rust's CARGO:
 *   - 12-character right-aligned status word (colored)
 *   - Message follows in default color
 *   - No emojis, no brackets
 */

#ifndef CFORGE_LOG_HPP
#define CFORGE_LOG_HPP

#include "core/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum cforge_log_verbosity_t
 * @brief C-compatible enum for logging verbosity levels
 */
typedef enum {
  CFORGE_VERBOSITY_QUIET,  /**< Minimal output, only errors */
  CFORGE_VERBOSITY_NORMAL, /**< Standard output level */
  CFORGE_VERBOSITY_VERBOSE /**< Detailed output for debugging */
} cforge_log_verbosity_t;

// C wrapper functions
void cforge_set_verbosity_impl(cforge_log_verbosity_t level);
cforge_log_verbosity_t cforge_get_verbosity(void);
void cforge_print_header(cforge_cstring_t message);
void cforge_print_status(cforge_cstring_t message);
void cforge_print_success(cforge_cstring_t message);
void cforge_print_warning(cforge_cstring_t message);
void cforge_print_error(cforge_cstring_t message);
void cforge_print_step(cforge_cstring_t action, cforge_cstring_t target);
void cforge_print_verbose(cforge_cstring_t message);

#ifdef __cplusplus
} // extern "C"

#include <fmt/color.h>
#include <fmt/core.h>
#include <string>
#include <vector>

namespace cforge {

/**
 * @enum log_verbosity
 * @brief C++ enum class for logging verbosity levels
 */
enum class log_verbosity {
  VERBOSITY_QUIET,  /**< Minimal output, only errors */
  VERBOSITY_NORMAL, /**< Standard output level */
  VERBOSITY_VERBOSE /**< Detailed output for debugging */
};

/**
 * @class logger
 * @brief Static class providing Cargo-style logging functionality
 *
 * All output follows CARGO's format:
 *   {status:>12} {message}
 *
 * Where status is a colored action word like "Compiling", "Building", etc.
 */
class logger {
public:

  // Configuration


  /**
   * @brief Sets the global verbosity level for logging
   * @param level The verbosity level to set
   */
  static void set_verbosity(log_verbosity level);

  /**
   * @brief Gets the current verbosity level
   * @return The current verbosity level
   */
  static log_verbosity get_verbosity();

  // CARGO-style status messages (right-aligned status word)


  /**
   * @brief Print a status message with custom action word
   *
   * Format: "{action:>12} {message}"
   * Color: Green for the action word
   *
   * @param action The action word (e.g., "Compiling", "Building")
   * @param message The message to print
   */
  static void print_action(const std::string &action,
                           const std::string &message);

  /**
   * @brief Print a cyan status message (info/progress)
   *
   * Common actions: "Checking", "Fetching", "Updating", "RUNNING"
   */
  static void print_status(const std::string &message);

  /**
   * @brief Print a green success message
   *
   * Common actions: "Finished", "Installed", "Created"
   */
  static void print_success(const std::string &message);

  /**
   * @brief Print a yellow warning message
   */
  static void print_warning(const std::string &message);

  /**
   * @brief Print a red error message
   */
  static void print_error(const std::string &message);

  /**
   * @brief Print a gray verbose/debug message
   */
  static void print_verbose(const std::string &message);

  // Specific action helpers (CARGO-style)

  /**
   * @brief Print "Compiling {target}"
   */
  static void compiling(const std::string &target);

  /**
   * @brief Print "Building {target}"
   */
  static void building(const std::string &target);

  /**
   * @brief Print "RUNNING {command}"
   */
  static void running(const std::string &command);

  /**
   * @brief Print "Finished {config} target(s) in {time}"
   */
  static void finished(const std::string &config, const std::string &time = "");

  /**
   * @brief Print "Fetching {target}"
   */
  static void fetching(const std::string &target);

  /**
   * @brief Print "Updating {target}"
   */
  static void updating(const std::string &target);

  /**
   * @brief Print "Installing {target}"
   */
  static void installing(const std::string &target);

  /**
   * @brief Print "Removing {target}"
   */
  static void removing(const std::string &target);

  /**
   * @brief Print "Creating {target}"
   */
  static void creating(const std::string &target);

  /**
   * @brief Print "Created {target}" (past tense for completed actions)
   */
  static void created(const std::string &target);

  /**
   * @brief Print "Generated {target}"
   */
  static void generated(const std::string &target);

  /**
   * @brief Print "Configuring {target}"
   */
  static void configuring(const std::string &target);

  /**
   * @brief Print "Linking {target}"
   */
  static void linking(const std::string &target);

  /**
   * @brief Print "Testing {target}"
   */
  static void testing(const std::string &target);

  /**
   * @brief Print "Packaging {target}"
   */
  static void packaging(const std::string &target);

  /**
   * @brief Print "Cleaning {target}"
   */
  static void cleaning(const std::string &target);


  // Build progress display (Rust-style)


  /**
   * @brief Print "Compiling {file}" with optional timing
   * @param file The file being compiled
   * @param duration_secs Optional duration in seconds (for completed files)
   */
  static void compiling_file(const std::string &file,
                             cforge_double_t duration_secs = -1.0);

  /**
   * @brief Display a progress bar
   * @param current Current step (1-based)
   * @param total Total steps
   * @param in_place If true, update the line in place (use carriage return)
   */
  static void progress_bar(int current, int total, bool in_place = true);

  /**
   * @brief Clear the current terminal line
   */
  static void clear_line();

  /**
   * @brief Print build timing summary
   * @param total_duration Total build time in seconds
   * @param slowest_files Vector of (filename, duration) pairs for slowest files
   */
  static void print_timing_summary(
      cforge_double_t total_duration,
      const std::vector<std::pair<std::string, cforge_double_t>> &slowest_files);


  // Legacy compatibility (maps to new style)


  /**
   * @brief Print a header/banner (simplified, no box drawing)
   */
  static void print_header(const std::string &message);

  /**
   * @brief Print a step message (maps to print_action)
   */
  static void print_step(const std::string &action, const std::string &target);

  /**
   * @brief Print a plain message (no status prefix)
   */
  static void print_plain(const std::string &message);

  /**
   * @brief Print multiple lines
   */
  static void print_lines(const std::vector<std::string> &messages);

  // =========================================================================
  // Enhanced formatting utilities for Rust-like styling
  // =========================================================================

  /**
   * @brief Print a section header with optional underline
   *
   * Example: "Dependencies:" in cyan/bold
   */
  static void print_section(const std::string &title);

  /**
   * @brief Print a key-value pair with aligned columns
   *
   * Example: "  Location:     /path/to/cache"
   *
   * @param key The label (will be right-padded)
   * @param value The value
   * @param key_width Width for key column (default 14)
   * @param indent Number of spaces to indent (default 2)
   */
  static void print_kv(const std::string &key, const std::string &value,
                       int key_width = 14, int indent = 2);

  /**
   * @brief Print a key-value pair with colored value
   *
   * @param key The label
   * @param value The value
   * @param value_color Color for the value
   * @param key_width Width for key column
   * @param indent Number of spaces to indent
   */
  static void print_kv_colored(const std::string &key, const std::string &value,
                               fmt::color value_color,
                               int key_width = 14, int indent = 2);

  /**
   * @brief Print a list item with bullet or dash
   *
   * Example: "  - item text"
   *
   * @param text The item text
   * @param bullet The bullet character (default "-")
   * @param indent Number of spaces before bullet (default 2)
   */
  static void print_list_item(const std::string &text,
                              const std::string &bullet = "-",
                              int indent = 2);

  /**
   * @brief Print a dimmed/secondary text line
   *
   * Used for hints, help text, secondary info
   */
  static void print_dim(const std::string &message, int indent = 0);

  /**
   * @brief Print a horizontal rule/separator
   *
   * @param width Width of the rule (default 60)
   * @param ch Character to use (default '-')
   */
  static void print_rule(int width = 60, char ch = '-');

  /**
   * @brief Print emphasized/highlighted text
   */
  static void print_emphasis(const std::string &message);

  /**
   * @brief Print a note with "note:" prefix
   */
  static void print_note(const std::string &message);

  /**
   * @brief Print a hint with "hint:" prefix
   */
  static void print_hint(const std::string &message);

  /**
   * @brief Print help text with proper indentation
   *
   * @param help_lines Vector of help strings
   * @param indent Indentation for each line
   */
  static void print_help_lines(const std::vector<std::string> &help_lines,
                               int indent = 2);

  /**
   * @brief Print a table row with aligned columns
   *
   * @param columns Vector of column values
   * @param widths Vector of column widths
   * @param indent Indentation before row
   */
  static void print_table_row(const std::vector<std::string> &columns,
                              const std::vector<int> &widths,
                              int indent = 0);

  /**
   * @brief Print a table header with separator
   */
  static void print_table_header(const std::vector<std::string> &columns,
                                 const std::vector<int> &widths,
                                 int indent = 0);

  /**
   * @brief Print blank line
   */
  static void print_blank();

  // =========================================================================
  // Help formatting utilities
  // =========================================================================

  /**
   * @brief Print command help header
   *
   * Example: "cforge build - Build the project"
   *
   * @param cmd Command name (e.g., "build")
   * @param description Brief description
   */
  static void print_cmd_header(const std::string &cmd,
                               const std::string &description);

  /**
   * @brief Print usage line
   *
   * Example: "Usage: cforge build [options]"
   */
  static void print_usage(const std::string &usage);

  /**
   * @brief Print a command-line option with colored flags
   *
   * Example: "  -c, --config <cfg>    Build configuration"
   *
   * @param flags The flag(s) like "-c, --config <cfg>"
   * @param description Description of the option
   * @param flag_width Width for flags column (default 24)
   */
  static void print_option(const std::string &flags,
                           const std::string &description,
                           int flag_width = 24);

  /**
   * @brief Print a positional argument
   *
   * @param name Argument name
   * @param description Description
   * @param name_width Width for name column (default 18)
   */
  static void print_arg(const std::string &name,
                        const std::string &description,
                        int name_width = 18);

  /**
   * @brief Print an example command
   *
   * Example: "  $ cforge build -c Release"
   */
  static void print_example(const std::string &example,
                            const std::string &description = "");

  /**
   * @brief Print a subcommand entry
   *
   * @param name Subcommand name (will be colored)
   * @param description Description
   * @param name_width Width for name column
   */
  static void print_subcommand(const std::string &name,
                               const std::string &description,
                               int name_width = 14);

  /**
   * @brief Print a help section header
   *
   * Example: "OPTIONS" in bold gold color
   *
   * @param title Section title (e.g., "OPTIONS", "SUBCOMMANDS", "EXAMPLES")
   */
  static void print_help_section(const std::string &title);

  /**
   * @brief Print a configuration example block
   *
   * @param lines Vector of config lines to display
   */
  static void print_config_block(const std::vector<std::string> &lines);

  /**
   * @brief Print a footer hint/note for help
   *
   * @param message Hint message
   */
  static void print_help_footer(const std::string &message);

  // =========================================================================
  // Error/Diagnostic formatting utilities
  // =========================================================================

  /**
   * @brief Print an error header with code
   *
   * Example: "error[E0104]: failed recompaction: Permission denied"
   *
   * @param code Error code (e.g., "E0104")
   * @param message Error message
   */
  static void print_error_header(const std::string &code,
                                 const std::string &message);

  /**
   * @brief Print a warning header with code
   *
   * @param code Warning code (e.g., "W0100")
   * @param message Warning message
   */
  static void print_warning_header(const std::string &code,
                                   const std::string &message);

  /**
   * @brief Print a file location line for diagnostics
   *
   * Example: " --> src/main.cpp:10:5"
   *
   * @param file_path Path to the file
   * @param line Line number (0 to skip)
   * @param column Column number (0 to skip)
   */
  static void print_location(const std::string &file_path, int line = 0,
                             int column = 0);

  /**
   * @brief Print a code snippet line with gutter
   *
   * @param line_number Line number (0 for empty gutter)
   * @param content Line content
   * @param gutter_width Width of the gutter
   */
  static void print_code_line(int line_number, const std::string &content,
                              int gutter_width = 4);

  /**
   * @brief Print an error pointer line with carets
   *
   * @param column_start Starting column for the pointer
   * @param length Length of the pointer (default 1)
   * @param gutter_width Width of the gutter
   */
  static void print_error_pointer(int column_start, int length = 1,
                                  int gutter_width = 4);

  /**
   * @brief Print a diagnostic note line
   *
   * @param message Note message
   */
  static void print_diag_note(const std::string &message);

  /**
   * @brief Print a diagnostic help line
   *
   * @param message Help message
   */
  static void print_diag_help(const std::string &message);

  /**
   * @brief Print a diagnostic fix suggestion
   *
   * @param description Fix description
   * @param replacement Optional replacement text
   */
  static void print_diag_fix(const std::string &description,
                             const std::string &replacement = "");

  /**
   * @brief Print error summary line
   *
   * Example: "   |  3 compiler errors"
   *
   * @param count Number of errors/warnings
   * @param type Type description (e.g., "compiler error")
   * @param is_error True for error color, false for warning color
   */
  static void print_error_count(int count, const std::string &type,
                                bool is_error = true);

  /**
   * @brief Print a gutter separator line
   */
  static void print_gutter_line();

private:
  static log_verbosity s_verbosity;

  // Status width for right-alignment (CARGO uses 12)
  static constexpr int STATUS_WIDTH = 12;

  /**
   * @brief Internal helper to print formatted status line
   */
  static void print_status_line(const std::string &status,
                                const std::string &message,
                                fmt::color status_color, bool is_bold = true,
                                FILE *stream = stdout);
};

} // namespace cforge
#endif

#endif // CFORGE_LOG_HPP
