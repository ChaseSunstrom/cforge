/**
 * @file process_utils.hpp
 * @brief Utilities for running processes with stdout/stderr capture
 */

#pragma once

#include "cforge/log.hpp"
#include "core/error_format.hpp"
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace cforge {

struct process_result {
  int exit_code;
  std::string stdout_output;
  std::string stderr_output;
  bool success;
};

/**
 * @brief Execute a command and capture stdout and stderr
 *
 * @param command The command to execute
 * @param args Command arguments
 * @param working_dir The working directory (empty for current dir)
 * @param stdout_callback Optional callback for real-time stdout processing
 * @param stderr_callback Optional callback for real-time stderr processing
 * @param timeout_seconds Timeout in seconds (0 for no timeout)
 * @return process_result containing exit code and captured output
 */
process_result execute_process(
    const std::string &command, const std::vector<std::string> &args = {},
    const std::string &working_dir = "",
    std::function<void(const std::string &)> stdout_callback = nullptr,
    std::function<void(const std::string &)> stderr_callback = nullptr,
    int timeout_seconds = 5);

/**
 * @brief Convert a string to lowercase
 *
 * @param str String to convert
 * @return std::string Lowercase string
 */
std::string string_to_lower(const std::string &str);

/**
 * @brief Execute a process with captured output and format for cforge logging
 *
 * @param command The command to run
 * @param args Command arguments
 * @param working_dir Working directory (empty for current)
 * @param command_name Name to display in logs (e.g., "CMake", "Git")
 * @param verbose Whether to show all output or just errors
 * @param timeout_seconds Timeout in seconds (0 for no timeout)
 * @return true if command succeeded, false otherwise
 */
bool execute_tool(const std::string &command,
                  const std::vector<std::string> &args = {},
                  const std::string &working_dir = "",
                  const std::string &command_name = "", bool verbose = false,
                  int timeout_seconds = 60);

/**
 * @brief Check if a command is available in the PATH
 *
 * @param command The command to check
 * @param timeout_seconds Timeout in seconds (default: 5)
 * @return true if command is available, false otherwise
 */
bool is_command_available(const std::string &command, int timeout_seconds = 5);

/**
 * @brief Ensure all generator names are uppercased
 *
 * @param generators Vector of generator names to process
 * @return std::vector<std::string> Vector with uppercased generator names
 */
std::vector<std::string>
uppercase_generators(const std::vector<std::string> &generators);

/**
 * @brief Join a vector of strings into a single string with a delimiter
 *
 * @param strings Vector of strings to join
 * @param delimiter Delimiter to use between strings
 * @return std::string Joined string
 */
std::string join_strings(const std::vector<std::string> &strings,
                                const std::string &delimiter);

// Global flag to suppress build warnings
extern bool g_suppress_warnings;

} // namespace cforge