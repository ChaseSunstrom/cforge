/**
 * @file errors.hpp
 * @brief Structured error codes and error handling for cforge
 *
 * Provides a consistent error handling system with:
 * - Typed error codes
 * - Helpful error messages with context
 * - Suggestions for fixing common issues
 */

#ifndef CFORGE_ERRORS_HPP
#define CFORGE_ERRORS_HPP

#include "core/types.h"

#include <optional>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Error categories
 */
enum class error_category {
  GENERAL,
  CONFIG,
  DEPENDENCY,
  BUILD,
  CACHE,
  NETWORK,
  IO,
};

/**
 * @brief Structured error codes
 */
enum class error_code {
  // Success (0)
  SUCCESS = 0,

  // General errors (1-99)
  UNKNOWN_ERROR = 1,
  INVALID_ARGUMENT = 2,
  NOT_IMPLEMENTED = 3,
  OPERATION_CANCELLED = 4,

  // Configuration errors (100-199)
  CONFIG_NOT_FOUND = 100,
  CONFIG_PARSE_ERROR = 101,
  CONFIG_INVALID_VALUE = 102,
  CONFIG_MISSING_FIELD = 103,
  CONFIG_INVALID_TOML = 104,
  WORKSPACE_NOT_FOUND = 110,
  WORKSPACE_INVALID = 111,

  // Dependency errors (200-299)
  DEP_NOT_FOUND = 200,
  DEP_VERSION_NOT_FOUND = 201,
  DEP_VERSION_CONFLICT = 202,
  DEP_FETCH_FAILED = 203,
  DEP_BUILD_FAILED = 204,
  DEP_INVALID_SPEC = 205,
  DEP_CIRCULAR = 206,
  REGISTRY_UPDATE_FAILED = 210,
  REGISTRY_OFFLINE = 211,

  // Build errors (300-399)
  BUILD_CMAKE_NOT_FOUND = 300,
  BUILD_CMAKE_CONFIG_FAILED = 301,
  BUILD_CMAKE_BUILD_FAILED = 302,
  BUILD_COMPILE_ERROR = 303,
  BUILD_LINK_ERROR = 304,
  BUILD_GENERATOR_NOT_FOUND = 305,
  BUILD_TARGET_NOT_FOUND = 306,
  BUILD_BINARY_NOT_FOUND = 307,

  // Cache errors (400-499)
  CACHE_NOT_FOUND = 400,
  CACHE_INVALID = 401,
  CACHE_WRITE_FAILED = 402,
  CACHE_READ_FAILED = 403,
  CACHE_REMOTE_UNAVAILABLE = 410,
  CACHE_REMOTE_AUTH_FAILED = 411,
  CACHE_REMOTE_UPLOAD_FAILED = 412,

  // Network errors (500-599)
  NETWORK_UNAVAILABLE = 500,
  NETWORK_TIMEOUT = 501,
  NETWORK_SSL_ERROR = 502,
  NETWORK_HTTP_ERROR = 503,

  // IO errors (600-699)
  IO_FILE_NOT_FOUND = 600,
  IO_PERMISSION_DENIED = 601,
  IO_DISK_FULL = 602,
  IO_READ_ERROR = 603,
  IO_WRITE_ERROR = 604,
  IO_PATH_TOO_LONG = 605,
};

/**
 * @brief Get the category for an error code
 */
error_category get_error_category(error_code code);

/**
 * @brief Get a human-readable name for an error code
 */
std::string error_code_name(error_code code);

/**
 * @brief Structured error with context and suggestions
 */
struct cforge_error {
  error_code code;
  std::string message;           // Main error message
  std::string context;           // Additional context (file path, line, etc.)
  std::vector<std::string> help; // Suggestions for fixing the issue

  /**
   * @brief Create a simple error
   */
  static cforge_error make(error_code code, const std::string &message);

  /**
   * @brief Create an error with context
   */
  static cforge_error make(error_code code, const std::string &message,
                           const std::string &context);

  /**
   * @brief Add a help suggestion
   */
  cforge_error &with_help(const std::string &suggestion);

  /**
   * @brief Add multiple help suggestions
   */
  cforge_error &with_help(const std::vector<std::string> &suggestions);

  /**
   * @brief Print the error to stderr
   */
  void print() const;

  /**
   * @brief Format the error as a string
   */
  std::string format() const;

  /**
   * @brief Check if this is an actual error (not success)
   */
  bool is_error() const { return code != error_code::SUCCESS; }

  /**
   * @brief Implicit conversion to int for exit codes
   */
  operator int() const { return static_cast<int>(code) > 0 ? 1 : 0; }
};

/**
 * @brief Result type that can hold either a value or an error
 */
template <typename T>
class result {
public:
  result(const T &value) : value_(value), has_value_(true) {}
  result(T &&value) : value_(std::move(value)), has_value_(true) {}
  result(const cforge_error &error) : error_(error), has_value_(false) {}
  result(cforge_error &&error) : error_(std::move(error)), has_value_(false) {}

  bool ok() const { return has_value_; }
  bool is_error() const { return !has_value_; }

  const T &value() const { return value_; }
  T &value() { return value_; }

  const cforge_error &error() const { return error_; }
  cforge_error &error() { return error_; }

  // Convenience accessors
  const T *operator->() const { return &value_; }
  T *operator->() { return &value_; }
  const T &operator*() const { return value_; }
  T &operator*() { return value_; }

private:
  T value_;
  cforge_error error_;
  bool has_value_;
};

/**
 * @brief Specialization for void result (just success/error)
 */
template <>
class result<void> {
public:
  result() : has_value_(true) {}
  result(const cforge_error &error) : error_(error), has_value_(false) {}

  bool ok() const { return has_value_; }
  bool is_error() const { return !has_value_; }

  const cforge_error &error() const { return error_; }
  cforge_error &error() { return error_; }

private:
  cforge_error error_;
  bool has_value_;
};

// ============================================================================
// Common error helpers
// ============================================================================

/**
 * @brief Create "package not found" error with suggestions
 * @param package_name The package that wasn't found
 * @param suggestions Similar package names
 */
cforge_error package_not_found_error(const std::string &package_name,
                                     const std::vector<std::string> &suggestions = {});

/**
 * @brief Create "config not found" error
 * @param path Expected config file path
 */
cforge_error config_not_found_error(const std::string &path);

/**
 * @brief Create "build failed" error with compiler output
 * @param target Build target
 * @param output Compiler/linker output
 */
cforge_error build_failed_error(const std::string &target,
                                const std::string &output = "");

/**
 * @brief Create "command not found" error with suggestions
 * @param command The command that wasn't found
 * @param suggestions Similar command names
 */
cforge_error command_not_found_error(const std::string &command,
                                     const std::vector<std::string> &suggestions = {});

} // namespace cforge

#endif // CFORGE_ERRORS_HPP
