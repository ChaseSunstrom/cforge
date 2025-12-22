/**
 * @file build_progress.cpp
 * @brief Implementation of build progress tracking and display
 */

#include "core/build_progress.hpp"
#include "core/types.h"
#include <algorithm>
#include <fmt/core.h>
#include <iostream>
#include <regex>

namespace cforge {

void build_progress::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_file_.clear();
  current_step_ = 0;
  total_steps_ = 0;
  has_progress_ = false;
  timings_.clear();
}

bool build_progress::parse_line(const std::string &line) {
  // Try each parser in order
  if (parse_ninja_progress(line)) {
    return true;
  }
  if (parse_make_progress(line)) {
    return true;
  }
  if (parse_msbuild_progress(line)) {
    return true;
  }
  return false;
}

bool build_progress::parse_ninja_progress(const std::string &line) {
  // Ninja format: [1/15] Building CXX object src/main.cpp.obj
  // Also matches: [1/15] Linking CXX executable bin/app.exe
  static std::regex ninja_regex(R"(\[(\d+)/(\d+)\]\s+(.+))");

  std::smatch match;
  if (std::regex_search(line, match, ninja_regex)) {
    std::lock_guard<std::mutex> lock(mutex_);

    cforge_int_t new_step = std::stoi(match[1].str());
    cforge_int_t new_total = std::stoi(match[2].str());

    // If we moved to a new step, finish the previous file
    if (has_progress_ && new_step > current_step_ && !current_file_.empty()) {
      file_timing timing;
      timing.filename = current_file_;
      timing.start_time = current_file_start_;
      timing.end_time = std::chrono::steady_clock::now();
      timing.duration_seconds =
          std::chrono::duration<double>(timing.end_time - timing.start_time)
              .count();
      timings_.emplace_back(timing);
    }

    current_step_ = new_step;
    total_steps_ = new_total;
    has_progress_ = true;

    // Extract the action and file
    std::string action = match[3].str();
    current_file_ = extract_filename(action);
    current_file_start_ = std::chrono::steady_clock::now();

    return true;
  }
  return false;
}

bool build_progress::parse_make_progress(const std::string &line) {
  // Make format: [ 10%] Building CXX object CMakeFiles/app.dir/src/main.cpp.o
  // Also: [100%] Linking CXX executable bin/app
  static std::regex make_regex(R"(\[\s*(\d+)%\]\s+(.+))");

  std::smatch match;
  if (std::regex_search(line, match, make_regex)) {
    std::lock_guard<std::mutex> lock(mutex_);

    cforge_int_t percentage = std::stoi(match[1].str());

    // Estimate step from percentage
    cforge_int_t new_step = percentage;
    cforge_int_t new_total = 100;

    // Finish previous file if step changed
    if (has_progress_ && new_step > current_step_ && !current_file_.empty()) {
      file_timing timing;
      timing.filename = current_file_;
      timing.start_time = current_file_start_;
      timing.end_time = std::chrono::steady_clock::now();
      timing.duration_seconds =
          std::chrono::duration<double>(timing.end_time - timing.start_time)
              .count();
      timings_.emplace_back(timing);
    }

    current_step_ = new_step;
    total_steps_ = new_total;
    has_progress_ = true;

    std::string action = match[2].str();
    current_file_ = extract_filename(action);
    current_file_start_ = std::chrono::steady_clock::now();

    return true;
  }
  return false;
}

bool build_progress::parse_msbuild_progress(const std::string &line) {
  // MSBuild format varies, common patterns:
  // ClCompile: main.cpp
  // cl /c ... main.cpp
  // Compiling main.cpp
  static std::regex msbuild_regex(
      R"((?:ClCompile|Compiling|cl\s.*/c).*?([^\\/\s]+\.(?:cpp|c|cc|cxx)))",
      std::regex::icase);

  std::smatch match;
  if (std::regex_search(line, match, msbuild_regex)) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Finish previous file
    if (!current_file_.empty()) {
      file_timing timing;
      timing.filename = current_file_;
      timing.start_time = current_file_start_;
      timing.end_time = std::chrono::steady_clock::now();
      timing.duration_seconds =
          std::chrono::duration<double>(timing.end_time - timing.start_time)
              .count();
      timings_.emplace_back(timing);
    }

    current_step_++;
    has_progress_ = true;
    current_file_ = match[1].str();
    current_file_start_ = std::chrono::steady_clock::now();

    return true;
  }
  return false;
}

std::string build_progress::extract_filename(const std::string &line) {
  // Try to extract a meaningful filename from the build action

  // Pattern: Building CXX object path/to/file.cpp.obj
  static std::regex obj_regex(
      R"(Building\s+(?:CXX|C)\s+object\s+.*?([^\\/]+\.(?:cpp|c|cc|cxx)))",
      std::regex::icase);
  std::smatch match;
  if (std::regex_search(line, match, obj_regex)) {
    return match[1].str();
  }

  // Pattern: Linking CXX executable path/to/app.exe
  static std::regex link_regex(
      R"(Linking\s+(?:CXX|C)\s+(?:executable|static library|shared library)\s+(.+))",
      std::regex::icase);
  if (std::regex_search(line, match, link_regex)) {
    std::string path = match[1].str();
    // Extract just the filename
    cforge_size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
      return "[link] " + path.substr(last_slash + 1);
    }
    return "[link] " + path;
  }

  // Pattern: CMakeFiles/target.dir/path/file.cpp.obj
  static std::regex cmake_obj_regex(
      R"(CMakeFiles/[^/]+\.dir/.*?([^\\/]+\.(?:cpp|c|cc|cxx))\.o)",
      std::regex::icase);
  if (std::regex_search(line, match, cmake_obj_regex)) {
    return match[1].str();
  }

  // Just return the whole action if we can't parse it
  return line;
}

std::string build_progress::get_current_file() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_file_;
}

cforge_double_t build_progress::get_progress() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (total_steps_ == 0)
    return 0.0;
  return static_cast<cforge_double_t>(current_step_) / static_cast<cforge_double_t>(total_steps_);
}

cforge_int_t build_progress::get_current_step() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_step_;
}

cforge_int_t build_progress::get_total_steps() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return total_steps_;
}

bool build_progress::has_progress() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return has_progress_;
}

const std::vector<file_timing> &build_progress::get_timings() const {
  return timings_;
}

std::vector<file_timing> build_progress::get_slowest_files(cforge_size_t count) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<file_timing> sorted = timings_;
  std::sort(sorted.begin(), sorted.end(),
            [](const file_timing &a, const file_timing &b) {
              return a.duration_seconds > b.duration_seconds;
            });

  if (sorted.size() > count) {
    sorted.resize(count);
  }
  return sorted;
}

void build_progress::file_started(const std::string &filename) {
  std::lock_guard<std::mutex> lock(mutex_);
  current_file_ = filename;
  current_file_start_ = std::chrono::steady_clock::now();
}

void build_progress::file_finished(const std::string &filename) {
  std::lock_guard<std::mutex> lock(mutex_);

  file_timing timing;
  timing.filename = filename;
  timing.start_time = current_file_start_;
  timing.end_time = std::chrono::steady_clock::now();
  timing.duration_seconds =
      std::chrono::duration<double>(timing.end_time - timing.start_time)
          .count();
  timings_.emplace_back(timing);
}

void display_progress_bar(cforge_int_t current, cforge_int_t total, cforge_int_t width,
                          bool show_percentage) {
  if (total <= 0)
    return;

  cforge_double_t progress = static_cast<cforge_double_t>(current) / static_cast<cforge_double_t>(total);
  cforge_int_t filled = static_cast<cforge_int_t>(progress * width);

  std::string bar;
  bar.reserve(width);

  for (cforge_int_t i = 0; i < width; ++i) {
    if (i < filled) {
      bar += "\xe2\x96\x88"; // Unicode full block (UTF-8)
    } else {
      bar += "\xe2\x96\x91"; // Unicode light shade (UTF-8)
    }
  }

  std::string line = fmt::format("   [{}]", bar);

  if (show_percentage) {
    cforge_int_t percent = static_cast<cforge_int_t>(progress * 100);
    line += fmt::format(" {:3d}% ({}/{})", percent, current, total);
  }

  // Use carriage return to update in place
  std::cout << "\r" << line << std::flush;
}

void clear_progress_line() {
  // Move to beginning of line and clear it
  std::cout << "\r\033[K" << std::flush;
}

} // namespace cforge
