/**
 * @file log.cpp
 * @brief Implementation of logging utilities
 */

#include <iostream>

#include "cforge/log.hpp"
#include "core/types.h"

#ifdef __cplusplus
namespace cforge {
log_verbosity logger::s_verbosity = log_verbosity::VERBOSITY_NORMAL;

void logger::set_verbosity(log_verbosity level) { s_verbosity = level; }

log_verbosity logger::get_verbosity() { return s_verbosity; }

void logger::print_header(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;

  fmt::print("┌───────────────────────────────────────────────────┐\n"
             "│ {:<53} │\n"
             "└───────────────────────────────────────────────────┘\n\n",
             message);
}

void logger::print_status(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::blue), "→ {}\n", message);
}

void logger::print_success(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::green), "✓ {}\n", message);
}

void logger::print_warning(const std::string &message) {
  fmt::print(fg(fmt::color::yellow), "⚠ {}\n", message);
}

void logger::print_error(const std::string &message) {
  fmt::print(stderr, fg(fmt::color::red), "✗ {}\n", message);
}

void logger::print_step(const std::string &action, const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("  • {} {}\n", action, target);
}

void logger::print_verbose(const std::string &message) {
  // Only print in verbose mode
  if (s_verbosity != log_verbosity::VERBOSITY_VERBOSE)
    return;
  fmt::print(fg(fmt::color::gray), "  {} {}\n", "▶", message);
}
} // namespace cforge

// C wrapper implementations
extern "C" {
void cforge_set_verbosity_impl(cforge_log_verbosity_t level) {
  cforge::log_verbosity cpp_level;
  switch (level) {
  case CFORGE_VERBOSITY_QUIET:
    cpp_level = cforge::log_verbosity::VERBOSITY_QUIET;
    break;
  case CFORGE_VERBOSITY_VERBOSE:
    cpp_level = cforge::log_verbosity::VERBOSITY_VERBOSE;
    break;
  default:
    cpp_level = cforge::log_verbosity::VERBOSITY_NORMAL;
    break;
  }
  cforge::logger::set_verbosity(cpp_level);
}

cforge_log_verbosity_t cforge_get_verbosity(void) {
  auto verb = cforge::logger::get_verbosity();
  switch (verb) {
  case cforge::log_verbosity::VERBOSITY_QUIET:
    return CFORGE_VERBOSITY_QUIET;
  case cforge::log_verbosity::VERBOSITY_VERBOSE:
    return CFORGE_VERBOSITY_VERBOSE;
  default:
    return CFORGE_VERBOSITY_NORMAL;
  }
}

void cforge_print_header(cforge_cstring_t message) {
  cforge::logger::print_header(message);
}

void cforge_print_status(cforge_cstring_t message) {
  cforge::logger::print_status(message);
}

void cforge_print_success(cforge_cstring_t message) {
  cforge::logger::print_success(message);
}

void cforge_print_warning(cforge_cstring_t message) {
  cforge::logger::print_warning(message);
}

void cforge_print_error(cforge_cstring_t message) {
  cforge::logger::print_error(message);
}

void cforge_print_step(cforge_cstring_t action, cforge_cstring_t target) {
  cforge::logger::print_step(action, target);
}

void cforge_print_verbose(cforge_cstring_t message) {
  cforge::logger::print_verbose(message);
}
} // extern "C"
#endif // __cplusplus