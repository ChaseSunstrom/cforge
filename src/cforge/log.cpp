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
  // Skip empty messages to avoid blank warning lines
  if (message.empty()) {
    return;
  }
  // Warnings always show unless quiet
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  print_status_line("warning", message, fmt::color::yellow, true, stderr);
}

void logger::print_error(const std::string &message) {
  // Skip empty messages to avoid blank error lines
  if (message.empty()) {
    return;
  }
  // Errors always show
  print_status_line("error", message, fmt::color::red, true, stderr);
}

void logger::print_verbose(const std::string &message) {
  // Skip empty messages
  if (message.empty()) {
    return;
  }

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


// Build progress display (Rust-style)


void logger::compiling_file(const std::string &file, cforge_double_t duration_secs) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;

  std::string action = "Compiling";
  std::string display_file = file;

  // Check if this is a link action (indicated by "[link] " prefix)
  if (file.rfind("[link] ", 0) == 0) {
    action = "Linking";
    display_file = file.substr(7);  // Remove "[link] " prefix
  }

  std::string message = display_file;
  if (duration_secs >= 0) {
    if (duration_secs >= 10.0) {
      // Highlight slow files
      message = fmt::format("{} ({:.1f}s) <- slow", display_file, duration_secs);
    } else {
      message = fmt::format("{} ({:.1f}s)", display_file, duration_secs);
    }
  }
  print_status_line(action, message, fmt::color::green);
}

void logger::progress_bar(cforge_int_t current, cforge_int_t total, bool in_place) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  if (total <= 0)
    return;

  const cforge_int_t bar_width = 20;
  cforge_double_t progress = static_cast<cforge_double_t>(current) / static_cast<cforge_double_t>(total);
  cforge_int_t filled = static_cast<cforge_int_t>(progress * bar_width);

  // Build the bar with Unicode block characters
  std::string bar;
  for (cforge_int_t i = 0; i < bar_width; ++i) {
    if (i < filled) {
      bar += "\xe2\x96\x88"; // Full block
    } else {
      bar += "\xe2\x96\x91"; // Light shade
    }
  }

  cforge_int_t percent = static_cast<cforge_int_t>(progress * 100);

  if (in_place) {
    // Use carriage return to update in place
    fmt::print("\r");
    fmt::print(fg(fmt::color::cyan), "{:>{}}", "", STATUS_WIDTH);
    fmt::print(" [{}] {:3d}% ({}/{})", bar, percent, current, total);
    std::cout << std::flush;
  } else {
    // Print on new line
    fmt::print(fg(fmt::color::cyan), "{:>{}}", "", STATUS_WIDTH);
    fmt::print(" [{}] {:3d}% ({}/{})\n", bar, percent, current, total);
  }
}

void logger::clear_line() {
  // Move to beginning of line and clear it
  fmt::print("\r\033[K");
  std::cout << std::flush;
}

void logger::print_timing_summary(
    cforge_double_t total_duration,
    const std::vector<std::pair<std::string, cforge_double_t>> &slowest_files) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;

  fmt::print("\n");
  print_status_line("Finished", fmt::format("in {:.2f}s", total_duration),
                    fmt::color::green);

  if (!slowest_files.empty()) {
    fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold,
               "{:>{}}", "Slowest", STATUS_WIDTH);
    fmt::print(" files:\n");
    for (const auto &[file, duration] : slowest_files) {
      fmt::print("{:>{}}", "", STATUS_WIDTH);
      if (duration >= 10.0) {
        fmt::print(fg(fmt::color::yellow), " {:>6.1f}s  {}\n", duration, file);
      } else {
        fmt::print(" {:>6.1f}s  {}\n", duration, file);
      }
    }
  }
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
