/**
 * @file log.hpp
 * @brief Logging utilities for CForge
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
/**
 * @brief Sets the global verbosity level for logging
 * @param level The verbosity level to set
 */
void cforge_set_verbosity_impl(cforge_log_verbosity_t level);

/**
 * @brief Gets the current verbosity level
 * @return The current verbosity level
 */
cforge_log_verbosity_t cforge_get_verbosity(void);

/**
 * @brief Prints a header message
 * @param message The message to print
 */
void cforge_print_header(cforge_cstring_t message);

/**
 * @brief Prints a status message
 * @param message The message to print
 */
void cforge_print_status(cforge_cstring_t message);

/**
 * @brief Prints a success message
 * @param message The message to print
 */
void cforge_print_success(cforge_cstring_t message);

/**
 * @brief Prints a warning message
 * @param message The message to print
 */
void cforge_print_warning(cforge_cstring_t message);

/**
 * @brief Prints an error message
 * @param message The message to print
 */
void cforge_print_error(cforge_cstring_t message);

/**
 * @brief Prints a step message with action and target
 * @param action The action being performed
 * @param target The target of the action
 */
void cforge_print_step(cforge_cstring_t action, cforge_cstring_t target);

/**
 * @brief Prints a verbose message (only shown in verbose mode)
 * @param message The message to print
 */
void cforge_print_verbose(cforge_cstring_t message);

#ifdef __cplusplus
} // extern "C"

#define FMT_HEADER_ONLY
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
 * @brief Static class providing logging functionality
 */
class logger {
public:
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

  /**
   * @brief Prints a header message
   * @param message The message to print
   */
  static void print_header(const std::string &message);

  /**
   * @brief Prints a status message
   * @param message The message to print
   */
  static void print_status(const std::string &message);

  /**
   * @brief Prints a success message
   * @param message The message to print
   */
  static void print_success(const std::string &message);

  /**
   * @brief Prints a warning message
   * @param message The message to print
   */
  static void print_warning(const std::string &message);

  /**
   * @brief Prints an error message
   * @param message The message to print
   */
  static void print_error(const std::string &message);

  /**
   * @brief Prints a step message with action and target
   * @param action The action being performed
   * @param target The target of the action
   */
  static void print_step(const std::string &action, const std::string &target);

  /**
   * @brief Prints a verbose message (only shown in verbose mode)
   * @param message The message to print
   */
  static void print_verbose(const std::string &message);

  /**
   * @brief Prints a list of messages
   * @param messages The list of messages to print
   */
  static void print_lines(const std::vector<std::string> &messages);

private:
  static log_verbosity s_verbosity;
};
} // namespace cforge
#endif

#endif // CFORGE_LOG_HPP