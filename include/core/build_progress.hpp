/**
 * @file build_progress.hpp
 * @brief Build progress tracking and display utilities
 */

#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Structure to track timing for a single file compilation
 */
struct file_timing {
  std::string filename;
  std::chrono::steady_clock::time_point start_time;
  std::chrono::steady_clock::time_point end_time;
  double duration_seconds = 0.0;
};

/**
 * @brief Class to track and display build progress
 */
class build_progress {
public:
  build_progress() = default;

  /**
   * @brief Reset progress tracking for a new build
   */
  void reset();

  /**
   * @brief Parse a line of build output and update progress
   * @param line The line of output to parse
   * @return true if the line contained progress information
   */
  bool parse_line(const std::string &line);

  /**
   * @brief Get the current file being compiled
   */
  std::string get_current_file() const;

  /**
   * @brief Get the current progress as a fraction (0.0 to 1.0)
   */
  double get_progress() const;

  /**
   * @brief Get current step number
   */
  int get_current_step() const;

  /**
   * @brief Get total steps
   */
  int get_total_steps() const;

  /**
   * @brief Check if progress information is available
   */
  bool has_progress() const;

  /**
   * @brief Get all file timings
   */
  const std::vector<file_timing> &get_timings() const;

  /**
   * @brief Get the slowest files (sorted by duration, descending)
   * @param count Maximum number of files to return
   */
  std::vector<file_timing> get_slowest_files(size_t count = 5) const;

  /**
   * @brief Called when a file starts compiling
   */
  void file_started(const std::string &filename);

  /**
   * @brief Called when a file finishes compiling
   */
  void file_finished(const std::string &filename);

private:
  mutable std::mutex mutex_;
  std::string current_file_;
  int current_step_ = 0;
  int total_steps_ = 0;
  bool has_progress_ = false;

  std::vector<file_timing> timings_;
  std::chrono::steady_clock::time_point current_file_start_;

  /**
   * @brief Parse Ninja-style progress: [1/15] Building CXX object ...
   */
  bool parse_ninja_progress(const std::string &line);

  /**
   * @brief Parse Make-style progress: [ 10%] Building CXX object ...
   */
  bool parse_make_progress(const std::string &line);

  /**
   * @brief Parse MSBuild/ClCompile progress
   */
  bool parse_msbuild_progress(const std::string &line);

  /**
   * @brief Extract filename from a build output line
   */
  std::string extract_filename(const std::string &line);
};

/**
 * @brief Display a progress bar to the terminal
 * @param current Current step
 * @param total Total steps
 * @param width Width of the progress bar in characters
 * @param show_percentage Whether to show percentage
 */
void display_progress_bar(int current, int total, int width = 20,
                          bool show_percentage = true);

/**
 * @brief Clear the current progress bar line
 */
void clear_progress_line();

} // namespace cforge
