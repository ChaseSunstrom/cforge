/**
 * @file log.cpp
 * @brief Implementation of logging utilities
 */

#include <iostream>
#include <vector>

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

  // Use UTF-8 raw string literal for box drawing
  fmt::print(u8R"(┌───────────────────────────────────────────────────┐
│ {:<49} │
└───────────────────────────────────────────────────┘

)" , message);
}

void logger::print_status(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  // Prefix with caret and INFO status
  fmt::print(fg(fmt::color::white), "> ");
  fmt::print(fg(fmt::color::white), "{} ", message);
  fmt::print(fg(fmt::color::blue), "[INFO]\n");
}

void logger::print_success(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  // Prefix with caret and OK status
  fmt::print(fg(fmt::color::white), "> ");
  fmt::print(fg(fmt::color::white), "{} ", message);
  fmt::print(fg(fmt::color::green), "[OK]\n");
}

void logger::print_plain(const std::string &message) {
  // Prefix with caret
  fmt::print(fg(fmt::color::white), "> {}\n", message);
}

void logger::print_warning(const std::string &message) {
  // Prefix with caret and WARNING status
  fmt::print(fg(fmt::color::white), "> ");
  fmt::print(fg(fmt::color::white), "{} ", message);
  fmt::print(fg(fmt::color::yellow), "[WARNING]\n");
}

void logger::print_error(const std::string &message) {
  // Prefix with caret and FAILURE status
  fmt::print(stderr, fg(fmt::color::white), "> ");
  fmt::print(stderr, fg(fmt::color::white), "{} ", message);
  fmt::print(stderr, fg(fmt::color::red), "[FAILURE]\n");
}

void logger::print_step(const std::string &action, const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  // Prefix with caret and STEP status
  fmt::print(fg(fmt::color::white), "> ");
  fmt::print(fg(fmt::color::white), "{} {} ", action, target);
  fmt::print(fg(fmt::color::blue), "[STEP]\n");
}

void logger::print_verbose(const std::string &message) {
  // Always print error messages regardless of verbosity level
  if (message.find("error") != std::string::npos ||
      message.find("Error") != std::string::npos ||
      message.find("ERROR") != std::string::npos) {
    // Prefix with caret and FAILURE status for error in verbose
    fmt::print(stderr, fg(fmt::color::white), "> ");
    fmt::print(stderr, fg(fmt::color::white), "{} ", message);
    fmt::print(stderr, fg(fmt::color::red), "[FAILURE]\n");
    return;
  }

  // Only print non-error messages in verbose mode
  if (s_verbosity != log_verbosity::VERBOSITY_VERBOSE)
    return;
  // Prefix with caret and VERBOSE status
  fmt::print(fg(fmt::color::white), "> ");
  fmt::print(fg(fmt::color::white), "{} ", message);
  fmt::print(fg(fmt::color::gray), "[VERBOSE]\n");
}

void logger::print_lines(const std::vector<std::string> &messages) {
  for (const auto &message : messages) {
    print_status(message);
  }
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