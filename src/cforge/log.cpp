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

void logger::running_timer(const std::string &command, double elapsed_secs) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;

  // Truncate command if too long
  std::string display_cmd = command;
  if (display_cmd.length() > 30) {
    display_cmd = display_cmd.substr(0, 27) + "...";
  }

  // Format elapsed time
  std::string time_str;
  if (elapsed_secs < 60.0) {
    time_str = fmt::format("{:.1f}s", elapsed_secs);
  } else {
    int mins = static_cast<int>(elapsed_secs) / 60;
    double secs = elapsed_secs - (mins * 60);
    time_str = fmt::format("{}m {:.0f}s", mins, secs);
  }

  // Clear line and print with timer (updates in place)
  fmt::print(stderr, "\r\033[K");
  fmt::print(stderr, fg(fmt::color::green) | fmt::emphasis::bold, "{:>{}}", "Running", STATUS_WIDTH);
  fmt::print(stderr, " {}", display_cmd);
  fmt::print(stderr, fg(fmt::color::dim_gray), " ({})", time_str);
  std::fflush(stderr);
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


// Static to track if we've printed the initial lines
static bool s_progress_initialized = false;

void logger::compiling_file(const std::string &file, cforge_int_t current,
                            cforge_int_t total, cforge_double_t duration_secs) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;

  std::string action = "Compiling";
  std::string display_file = file;

  // Check if this is a link action (indicated by "[link] " prefix)
  if (file.rfind("[link] ", 0) == 0) {
    action = "Linking";
    display_file = file.substr(7);  // Remove "[link] " prefix
  }

  // Truncate long filenames for cleaner display
  if (display_file.length() > 35) {
    display_file = display_file.substr(0, 32) + "...";
  }

  if (!s_progress_initialized) {
    // First time: just print the filename line, then create space for progress bar
    fmt::print(stderr, fg(fmt::color::green) | fmt::emphasis::bold, "{:>{}}", action, STATUS_WIDTH);
    fmt::print(stderr, " {}", display_file);
    if (current > 0 && total > 0) {
      fmt::print(stderr, fg(fmt::color::dim_gray), " [{}/{}]", current, total);
    }
    fmt::print(stderr, "\n");  // Newline after filename
    s_progress_initialized = true;
  } else {
    // Subsequent updates: move up, clear, reprint filename, move back down
    fmt::print(stderr, "\033[A\r\033[K");  // Move up, go to start, clear line
    fmt::print(stderr, fg(fmt::color::green) | fmt::emphasis::bold, "{:>{}}", action, STATUS_WIDTH);
    fmt::print(stderr, " {}", display_file);
    if (current > 0 && total > 0) {
      fmt::print(stderr, fg(fmt::color::dim_gray), " [{}/{}]", current, total);
    }
    if (duration_secs >= 0) {
      if (duration_secs >= 10.0) {
        fmt::print(stderr, fg(fmt::color::yellow), " ({:.1f}s slow)", duration_secs);
      } else if (duration_secs >= 1.0) {
        fmt::print(stderr, fg(fmt::color::gray), " ({:.1f}s)", duration_secs);
      }
    }
    fmt::print(stderr, "\n");  // Move back to progress bar line
  }
  std::fflush(stderr);
}

void logger::reset_progress_display() {
  s_progress_initialized = false;
}

void logger::progress_bar(cforge_int_t current, cforge_int_t total, bool in_place,
                          cforge_double_t elapsed_secs) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  if (total <= 0)
    return;

  const cforge_int_t bar_width = 30;
  cforge_double_t progress = static_cast<cforge_double_t>(current) / static_cast<cforge_double_t>(total);
  cforge_int_t filled = static_cast<cforge_int_t>(progress * bar_width);
  cforge_int_t percent = static_cast<cforge_int_t>(progress * 100);

  // Build ASCII progress bar
  std::string bar;
  for (cforge_int_t i = 0; i < bar_width; ++i) {
    if (i < filled) {
      bar += "=";
    } else if (i == filled && current < total) {
      bar += ">";
    } else {
      bar += " ";
    }
  }

  // Format elapsed time if provided
  std::string time_str;
  if (elapsed_secs >= 0) {
    if (elapsed_secs < 60.0) {
      time_str = fmt::format("{:.1f}s", elapsed_secs);
    } else {
      int mins = static_cast<int>(elapsed_secs) / 60;
      double secs = elapsed_secs - (mins * 60);
      time_str = fmt::format("{}m{:.0f}s", mins, secs);
    }
  }

  if (in_place) {
    // Progress bar on its own line below the compiling file line
    // Aligned to start at same position as "Compiling" (3 spaces indent)
    fmt::print(stderr, "\r\033[K");  // Clear current line
    fmt::print(stderr, "   ");  // 3 spaces to align with "Compiling"
    fmt::print(stderr, fg(fmt::color::cyan), "[{}]", bar);
    fmt::print(stderr, " {:3d}%", percent);
    if (!time_str.empty()) {
      fmt::print(stderr, fg(fmt::color::yellow), "  {}", time_str);
    }
    std::fflush(stderr);
  } else {
    // Full progress bar on new line
    fmt::print(stderr, "   ");  // 3 spaces to align with "Compiling"
    fmt::print(stderr, fg(fmt::color::cyan), "[{}]", bar);
    fmt::print(stderr, " {:3d}%", percent);
    if (!time_str.empty()) {
      fmt::print(stderr, "  {}", time_str);
    }
    fmt::print(stderr, "\n");
  }
}

void logger::clear_line() {
  // Move to beginning of line and clear it
  fmt::print(stderr, "\r\033[K");
  std::fflush(stderr);
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


// Enhanced formatting utilities


void logger::print_section(const std::string &title) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold, "{}\n", title);
}

void logger::print_kv(const std::string &key, const std::string &value,
                      int key_width, int indent) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  std::string key_fmt = key.empty() ? "" : key + ":";
  fmt::print("{:{}}{:<{}} {}\n", "", indent, key_fmt, key_width, value);
}

void logger::print_kv_colored(const std::string &key, const std::string &value,
                              fmt::color value_color,
                              int key_width, int indent) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  std::string key_fmt = key.empty() ? "" : key + ":";
  fmt::print("{:{}}{:<{}}", "", indent, key_fmt, key_width);
  fmt::print(fg(value_color), "{}\n", value);
}

void logger::print_list_item(const std::string &text,
                             const std::string &bullet,
                             int indent) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("{:{}}{} {}\n", "", indent, bullet, text);
}

void logger::print_dim(const std::string &message, int indent) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::gray), "{:{}}{}\n", "", indent, message);
}

void logger::print_rule(int width, char ch) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::gray), "{}\n", std::string(width, ch));
}

void logger::print_emphasis(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fmt::emphasis::bold, "{}\n", message);
}

void logger::print_note(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::steel_blue) | fmt::emphasis::bold, "{:>{}}", "note", STATUS_WIDTH);
  fmt::print(" {}\n", message);
}

void logger::print_hint(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::medium_sea_green) | fmt::emphasis::bold, "{:>{}}", "hint", STATUS_WIDTH);
  fmt::print(" {}\n", message);
}

void logger::print_help_lines(const std::vector<std::string> &help_lines,
                              int indent) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  for (const auto &line : help_lines) {
    fmt::print("{:{}}{}\n", "", indent, line);
  }
}

void logger::print_table_row(const std::vector<std::string> &columns,
                             const std::vector<int> &widths,
                             int indent) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("{:{}}", "", indent);
  for (size_t i = 0; i < columns.size(); ++i) {
    int width = (i < widths.size()) ? widths[i] : 12;
    fmt::print("{:<{}} ", columns[i], width);
  }
  fmt::print("\n");
}

void logger::print_table_header(const std::vector<std::string> &columns,
                                const std::vector<int> &widths,
                                int indent) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  // Print header row in bold
  fmt::print("{:{}}", "", indent);
  int total_width = 0;
  for (size_t i = 0; i < columns.size(); ++i) {
    int width = (i < widths.size()) ? widths[i] : 12;
    fmt::print(fmt::emphasis::bold, "{:<{}} ", columns[i], width);
    total_width += width + 1;
  }
  fmt::print("\n");
  // Print separator
  fmt::print("{:{}}", "", indent);
  fmt::print(fg(fmt::color::gray), "{}\n", std::string(total_width, '-'));
}

void logger::print_blank() {
  fmt::print("\n");
}


// Help formatting utilities


void logger::print_cmd_header(const std::string &cmd,
                              const std::string &description) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  // Clean header without separators, similar to cargo/git
  fmt::print("\n");
  fmt::print(fg(fmt::color::lime_green) | fmt::emphasis::bold, "cforge {}", cmd);
  fmt::print(fg(fmt::color::white), " - ");
  fmt::print("{}\n\n", description);
}

void logger::print_usage(const std::string &usage) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold, "USAGE:\n");
  fmt::print(fg(fmt::color::white), "    {}\n\n", usage);
}

void logger::print_option(const std::string &flags,
                          const std::string &description,
                          int flag_width) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("    ");
  fmt::print(fg(fmt::color::lime_green), "{:<{}}", flags, flag_width);
  fmt::print(fg(fmt::color::light_gray), "{}\n", description);
}

void logger::print_arg(const std::string &name,
                       const std::string &description,
                       int name_width) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("    ");
  fmt::print(fg(fmt::color::lime_green) | fmt::emphasis::bold, "{:<{}}", name, name_width);
  fmt::print(fg(fmt::color::light_gray), "{}\n", description);
}

void logger::print_example(const std::string &example,
                           const std::string &description) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("    ");
  fmt::print(fg(fmt::color::steel_blue), "$ ");
  fmt::print(fg(fmt::color::white), "{}", example);
  if (!description.empty()) {
    fmt::print(fg(fmt::color::gray), "  # {}", description);
  }
  fmt::print("\n");
}

void logger::print_subcommand(const std::string &name,
                              const std::string &description,
                              int name_width) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("    ");
  fmt::print(fg(fmt::color::lime_green) | fmt::emphasis::bold, "{:<{}}", name, name_width);
  fmt::print(fg(fmt::color::light_gray), "{}\n", description);
}

void logger::print_help_section(const std::string &title) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::cyan) | fmt::emphasis::bold, "{}\n", title);
}

void logger::print_config_block(const std::vector<std::string> &lines) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print(fg(fmt::color::steel_blue), "    {}\n", std::string(40, '-'));
  for (const auto &line : lines) {
    fmt::print(fg(fmt::color::light_slate_gray), "    {}\n", line);
  }
  fmt::print(fg(fmt::color::steel_blue), "    {}\n", std::string(40, '-'));
}

void logger::print_help_footer(const std::string &message) {
  if (s_verbosity == log_verbosity::VERBOSITY_QUIET)
    return;
  fmt::print("\n");
  fmt::print(fg(fmt::color::gray), "{}\n", message);
}


// Error/Diagnostic formatting utilities


void logger::print_error_header(const std::string &code,
                                const std::string &message) {
  fmt::print(stderr, fg(fmt::color::red) | fmt::emphasis::bold, "error");
  if (!code.empty()) {
    fmt::print(stderr, fg(fmt::color::red) | fmt::emphasis::bold, "[{}]", code);
  }
  fmt::print(stderr, fg(fmt::color::white) | fmt::emphasis::bold, ": {}\n", message);
}

void logger::print_warning_header(const std::string &code,
                                  const std::string &message) {
  fmt::print(stderr, fg(fmt::color::yellow) | fmt::emphasis::bold, "warning");
  if (!code.empty()) {
    fmt::print(stderr, fg(fmt::color::yellow) | fmt::emphasis::bold, "[{}]", code);
  }
  fmt::print(stderr, fg(fmt::color::white) | fmt::emphasis::bold, ": {}\n", message);
}

void logger::print_location(const std::string &file_path, int line, int column) {
  // Shorten long paths for display
  std::string display_path = file_path;
  if (display_path.length() > 60) {
    display_path = display_path.substr(0, 25) + "..." +
                   display_path.substr(display_path.length() - 32);
  }

  fmt::print(stderr, fg(fmt::color::steel_blue), "   --> ");
  fmt::print(stderr, "{}", display_path);
  if (line > 0) {
    fmt::print(stderr, ":{}", line);
    if (column > 0) {
      fmt::print(stderr, ":{}", column);
    }
  }
  fmt::print(stderr, "\n");
}

void logger::print_code_line(int line_number, const std::string &content,
                             int gutter_width) {
  if (line_number > 0) {
    fmt::print(stderr, fg(fmt::color::steel_blue), "{:>{}} | ", line_number,
               gutter_width);
  } else {
    fmt::print(stderr, fg(fmt::color::steel_blue), "{:>{}} | ", "",
               gutter_width);
  }
  fmt::print(stderr, "{}\n", content);
}

void logger::print_error_pointer(int column_start, int length, int gutter_width) {
  fmt::print(stderr, fg(fmt::color::steel_blue), "{:>{}} | ", "", gutter_width);
  if (column_start > 0) {
    fmt::print(stderr, "{:>{}}", "", column_start - 1);
  }
  fmt::print(stderr, fg(fmt::color::red) | fmt::emphasis::bold, "{}\n",
             std::string(length > 0 ? length : 1, '^'));
}

void logger::print_diag_note(const std::string &message) {
  fmt::print(stderr, fg(fmt::color::cyan) | fmt::emphasis::bold, "       note: ");
  fmt::print(stderr, fg(fmt::color::light_gray), "{}\n", message);
}

void logger::print_diag_help(const std::string &message) {
  fmt::print(stderr, fg(fmt::color::medium_sea_green) | fmt::emphasis::bold, "       help: ");
  fmt::print(stderr, fg(fmt::color::light_gray), "{}\n", message);
}

void logger::print_diag_fix(const std::string &description,
                            const std::string &replacement) {
  fmt::print(stderr, fg(fmt::color::magenta) | fmt::emphasis::bold, "        fix: ");
  fmt::print(stderr, fg(fmt::color::light_gray), "{}", description);
  if (!replacement.empty() && replacement.length() < 40) {
    fmt::print(stderr, fg(fmt::color::gray), " -> ");
    fmt::print(stderr, fg(fmt::color::lime_green), "`{}`", replacement);
  }
  fmt::print(stderr, "\n");
}

void logger::print_error_count(int count, const std::string &type, bool is_error) {
  fmt::print(stderr, fg(fmt::color::steel_blue), "   |  ");
  fmt::color color = is_error ? fmt::color::red : fmt::color::yellow;
  fmt::print(stderr, fg(color), "{} {}{}\n", count, type, count == 1 ? "" : "s");
}

void logger::print_gutter_line() {
  fmt::print(stderr, fg(fmt::color::steel_blue), "   |\n");
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
