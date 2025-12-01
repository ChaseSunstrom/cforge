/**
 * @file log.cpp
 * @brief Cargo-style logging implementation
 *
 * Output format:
 *   {status:>12} {message}
 *
 * Examples:
 *      Compiling myproject v0.1.0
 *       Building target/debug/myproject
 *       Finished dev [unoptimized] target(s) in 1.23s
 *        Running `target/debug/myproject`
 *        warning: unused variable
 *          error: cannot find value
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


// Core formatting helper


void logger::print_status_line(const std::string &status,
                               const std::string &message,
                               fmt::color status_color, bool is_bold,
                               FILE *stream) {
  // Right-align status word to STATUS_WIDTH characters
  if (is_bold) {
    fmt::print(stream, fg(status_color) | fmt::emphasis::bold, "{:>{}}", status,
               STATUS_WIDTH);
  } else {
    fmt::print(stream, fg(status_color), "{:>{}}", status, STATUS_WIDTH);
  }
  fmt::print(stream, " {}\n", message);
}


// Main logging functions


void logger::print_action(const std::string &action,
                          const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line(action, message, fmt::color::green);
}

void logger::print_status(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  // Use a generic status indicator for info messages
  print_status_line("", message, fmt::color::cyan);
}

void logger::print_success(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Finished", message, fmt::color::green);
}

void logger::print_warning(const std::string &message) {
  // Warnings always show unless quiet
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("warning", message, fmt::color::yellow, true, stderr);
}

void logger::print_error(const std::string &message) {
  // Errors always show
  print_status_line("error", message, fmt::color::red, true, stderr);
}

void logger::print_verbose(const std::string &message) {
  // Check if this looks like an error (for backwards compat)
  if (message.find("error") != std::string::npos ||
      message.find("Error") != std::string::npos ||
      message.find("ERROR") != std::string::npos) {
    print_error(message);
    return;
  }

  if (s_verbosity != log_verbosity::VERBOSITY_VERBOSE)
    return;

  // Gray, non-bold for verbose output
  print_status_line("", message, fmt::color::gray, false);
}


// Cargo-style action helpers


void logger::compiling(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Compiling", target, fmt::color::green);
}

void logger::building(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Building", target, fmt::color::green);
}

void logger::running(const std::string &command) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Running", command, fmt::color::green);
}

void logger::finished(const std::string &config, const std::string &time) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  std::string msg = config + " target(s)";
  if (!time.empty()) {
    msg += " in " + time;
  }
  print_status_line("Finished", msg, fmt::color::green);
}

void logger::fetching(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Fetching", target, fmt::color::green);
}

void logger::updating(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Updating", target, fmt::color::green);
}

void logger::installing(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Installing", target, fmt::color::green);
}

void logger::removing(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Removing", target, fmt::color::green);
}

void logger::creating(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Creating", target, fmt::color::green);
}

void logger::created(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Created", target, fmt::color::green);
}

void logger::generated(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Generated", target, fmt::color::green);
}

void logger::configuring(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Configuring", target, fmt::color::green);
}

void logger::linking(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Linking", target, fmt::color::green);
}

void logger::testing(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Testing", target, fmt::color::green);
}

void logger::packaging(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Packaging", target, fmt::color::green);
}

void logger::cleaning(const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("Cleaning", target, fmt::color::green);
}


// Legacy compatibility


void logger::print_header(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  // Simple header without box drawing
  fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold, "{}\n", message);
}

void logger::print_step(const std::string &action, const std::string &target) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line(action, target, fmt::color::green);
}

void logger::print_plain(const std::string &message) {
  fmt::print("{}\n", message);
}

void logger::print_lines(const std::vector<std::string> &messages) {
  for (const auto &message : messages) {
    print_plain(message);
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
