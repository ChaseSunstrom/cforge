/**
 * @file log.hpp
 * @brief Cargo-style logging utilities for cforge
 *
 * Output format matches Rust's Cargo:
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
 * All output follows Cargo's format:
 *   {status:>12} {message}
 *
 * Where status is a colored action word like "Compiling", "Building", etc.
 */
class logger {
public:
  // ============================================================
  // Configuration
  // ============================================================

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

  // ============================================================
  // Cargo-style status messages (right-aligned status word)
  // ============================================================

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
   * Common actions: "Checking", "Fetching", "Updating", "Running"
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

  // ============================================================
  // Specific action helpers (Cargo-style)
  // ============================================================

  /**
   * @brief Print "Compiling {target}"
   */
  static void compiling(const std::string &target);

  /**
   * @brief Print "Building {target}"
   */
  static void building(const std::string &target);

  /**
   * @brief Print "Running {command}"
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

  // ============================================================
  // Build progress display (Rust-style)
  // ============================================================

  /**
   * @brief Print "Compiling {file}" with optional timing
   * @param file The file being compiled
   * @param duration_secs Optional duration in seconds (for completed files)
   */
  static void compiling_file(const std::string &file,
                             double duration_secs = -1.0);

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
      double total_duration,
      const std::vector<std::pair<std::string, double>> &slowest_files);

  // ============================================================
  // Legacy compatibility (maps to new style)
  // ============================================================

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

private:
  static log_verbosity s_verbosity;

  // Status width for right-alignment (Cargo uses 12)
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
