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

// C-compatible enum for verbosity
typedef enum {
  CFORGE_VERBOSITY_QUIET,
  CFORGE_VERBOSITY_NORMAL,
  CFORGE_VERBOSITY_VERBOSE
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

#ifdef __cplusplus
} // extern "C"

#define FMT_HEADER_ONLY
#include <fmt/color.h>
#include <fmt/core.h>
#include <string>

namespace cforge {
enum class log_verbosity {
  VERBOSITY_QUIET,
  VERBOSITY_NORMAL,
  VERBOSITY_VERBOSE
};

class logger {
public:
  static void set_verbosity(log_verbosity level);
  static log_verbosity get_verbosity();

  static void print_header(const std::string &message);
  static void print_status(const std::string &message);
  static void print_success(const std::string &message);
  static void print_warning(const std::string &message);
  static void print_error(const std::string &message);
  static void print_step(const std::string &action, const std::string &target);

private:
  static log_verbosity s_verbosity;
};
} // namespace cforge
#endif

#endif // CFORGE_LOG_HPP