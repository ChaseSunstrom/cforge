/**
 * @file process_utils.cpp
 * @brief Implementation of process utilities with stdout/stderr capture
 */

#include "core/process_utils.hpp"
#include "core/error_format.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>

#include <fmt/color.h>
#include <fmt/core.h>

namespace cforge {

// Global flag to suppress build warnings
bool g_suppress_warnings = false;

#ifdef _WIN32
// Windows-specific implementation
process_result
execute_process(const std::string &command,
                const std::vector<std::string> &args,
                const std::string &working_dir,
                std::function<void(const std::string &)> stdout_callback,
                std::function<void(const std::string &)> stderr_callback,
                int timeout_seconds) {
  process_result result;
  result.exit_code = -1;
  result.success = false;

  // Build command line string
  std::string cmd_line = command;
  for (const auto &arg : args) {
    // Add quotes if there are spaces
    if (arg.find(' ') != std::string::npos) {
      cmd_line += " \"" + arg + "\"";
    } else {
      cmd_line += " " + arg;
    }
  }

  // Log command being executed in verbose mode
  logger::print_verbose("Executing command: " + cmd_line);

  if (!working_dir.empty()) {
    logger::print_verbose("Working directory: " + working_dir);
  }

  // Set up security attributes for pipe
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;

  // Create pipes for stdout and stderr
  HANDLE stdout_read, stdout_write;
  HANDLE stderr_read, stderr_write;

  if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0) ||
      !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) {
    logger::print_error("Failed to create stdout pipe");
    return result;
  }

  if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0) ||
      !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(stdout_read);
    CloseHandle(stdout_write);
    logger::print_error("Failed to create stderr pipe");
    return result;
  }

  // Set up process startup info
  STARTUPINFOA si;
  ZeroMemory(&si, sizeof(STARTUPINFOA));
  si.cb = sizeof(STARTUPINFOA);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = stdout_write;
  si.hStdError = stderr_write;

  // Set up process info
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

  // Create the process
  BOOL success = CreateProcessA(
      NULL,                                // No module name (use command line)
      const_cast<LPSTR>(cmd_line.c_str()), // Command line
      NULL,                                // Process handle not inheritable
      NULL,                                // Thread handle not inheritable
      TRUE,                                // Set handle inheritance to TRUE
      CREATE_NO_WINDOW,                    // Do not create console window
      NULL,                                // Use parent's environment block
      working_dir.empty() ? NULL : working_dir.c_str(), // Working directory
      &si, // Pointer to STARTUPINFO structure
      &pi  // Pointer to PROCESS_INFORMATION structure
  );

  // Close pipe handles to ensure child process gets EOF
  CloseHandle(stdout_write);
  CloseHandle(stderr_write);

  if (!success) {
    DWORD error_code = GetLastError();
    CloseHandle(stdout_read);
    CloseHandle(stderr_read);
    result.stderr_output =
        "Failed to create process: " + std::to_string(error_code);
    logger::print_error(result.stderr_output);
    return result;
  }

  // Read output from the child process's pipes
  const int BUFFER_SIZE = 4096;
  std::array<char, BUFFER_SIZE> buffer;
  DWORD bytes_read;
  std::stringstream stdout_stream, stderr_stream;

  // Non-blocking reads with timeout support
  DWORD stdout_avail = 0, stderr_avail = 0;
  auto start_time = std::chrono::steady_clock::now();
  [[maybe_unused]] bool timed_out = false;
  auto last_activity_time = start_time;

  // Status indicator for long-running commands
  bool show_status = timeout_seconds > 10;
  auto last_status_time = start_time;

  while (true) {
    auto current_time = std::chrono::steady_clock::now();
    auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                               current_time - start_time)
                               .count();
    auto since_last_activity = std::chrono::duration_cast<std::chrono::seconds>(
                                   current_time - last_activity_time)
                                   .count();

    // Check if we've exceeded the timeout
    if (timeout_seconds > 0 && elapsed_seconds > timeout_seconds) {
      logger::print_warning("Process timed out after " +
                            std::to_string(timeout_seconds) + " seconds");
      // Terminate the process
      TerminateProcess(pi.hProcess, 1);
      timed_out = true;
      result.exit_code = -1;
      result.success = false;
      result.stderr_output = "Process timed out after " +
                             std::to_string(timeout_seconds) + " seconds";
      break;
    }

    // Show status indicator for long-running commands with no output
    if (show_status && since_last_activity > 5) {
      auto since_last_status = std::chrono::duration_cast<std::chrono::seconds>(
                                   current_time - last_status_time)
                                   .count();

      if (since_last_status > 5) {
        logger::running(command + " (still running...)");
        last_status_time = current_time;
      }
    }

    // Check if process is still running
    DWORD exit_code;
    if (GetExitCodeProcess(pi.hProcess, &exit_code) &&
        exit_code != STILL_ACTIVE) {
      result.exit_code = static_cast<int>(exit_code);
    }

    [[maybe_unused]] bool had_activity = false;

    // Check for stdout data
    BOOL stdout_success =
        PeekNamedPipe(stdout_read, NULL, 0, NULL, &stdout_avail, NULL);
    if (stdout_success && stdout_avail > 0) {
      if (ReadFile(stdout_read, buffer.data(), BUFFER_SIZE - 1, &bytes_read,
                   NULL) &&
          bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::string output_chunk(buffer.data(), bytes_read);
        stdout_stream << output_chunk;
        if (stdout_callback) {
          stdout_callback(output_chunk);
        }
        had_activity = true;
        last_activity_time = current_time;
      }
    }

    // Check for stderr data
    BOOL stderr_success =
        PeekNamedPipe(stderr_read, NULL, 0, NULL, &stderr_avail, NULL);
    if (stderr_success && stderr_avail > 0) {
      if (ReadFile(stderr_read, buffer.data(), BUFFER_SIZE - 1, &bytes_read,
                   NULL) &&
          bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::string error_chunk(buffer.data(), bytes_read);
        stderr_stream << error_chunk;
        if (stderr_callback) {
          stderr_callback(error_chunk);
        }
        had_activity = true;
        last_activity_time = current_time;
      }
    }

    // If process has exited and no more data to read, or if the pipes are
    // broken, break
    if (result.exit_code != -1) {
      // Process has exited
      if ((stdout_success && stdout_avail == 0) &&
          (stderr_success && stderr_avail == 0)) {
        // No more data to read
        break;
      }

      // Check if pipes are broken (which can happen after process exits)
      if (!stdout_success || !stderr_success) {
        // One of the pipes is broken, likely means process is fully done
        break;
      }

      // If it's been more than 2 seconds since we've seen any data and process
      // is done, assume we're finished even if PeekNamedPipe might be stuck
      if (since_last_activity > 2) {
        logger::print_verbose(
            "No activity for 2s after process exit, assuming complete");
        break;
      }
    }

    // Avoid busy-waiting
    Sleep(10);
  }

  // Get the final output
  result.stdout_output = stdout_stream.str();
  result.stderr_output = stderr_stream.str();
  result.success = (result.exit_code == 0);

  // Clean up handles
  CloseHandle(stdout_read);
  CloseHandle(stderr_read);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return result;
}

#else
// Unix-specific implementation
process_result
execute_process(const std::string &command,
                const std::vector<std::string> &args,
                const std::string &working_dir,
                std::function<void(const std::string &)> stdout_callback,
                std::function<void(const std::string &)> stderr_callback,
                int timeout_seconds) {
  process_result result;
  result.exit_code = -1;
  result.success = false;

  // Create pipes for stdout and stderr
  int stdout_pipe[2];
  int stderr_pipe[2];

  if (pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
    return result;
  }

  // Fork the process
  pid_t pid = fork();

  if (pid < 0) {
    // Fork failed
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[0]);
    close(stderr_pipe[1]);
    return result;
  } else if (pid == 0) {
    // Child process

    // Change working directory if specified
    if (!working_dir.empty()) {
      if (chdir(working_dir.c_str()) == -1) {
        exit(EXIT_FAILURE);
      }
    }

    // Redirect stdout to pipe
    close(stdout_pipe[0]);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdout_pipe[1]);

    // Redirect stderr to pipe
    close(stderr_pipe[0]);
    dup2(stderr_pipe[1], STDERR_FILENO);
    close(stderr_pipe[1]);

    // Prepare arguments for execvp
    std::vector<char *> c_args;
    c_args.push_back(const_cast<char *>(command.c_str()));
    for (const auto &arg : args) {
      c_args.push_back(const_cast<char *>(arg.c_str()));
    }
    c_args.push_back(nullptr);

    // Execute command
    execvp(command.c_str(), c_args.data());

    // If execvp returns, there was an error
    exit(EXIT_FAILURE);
  } else {
    // Parent process

    // Close write ends of pipes
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Set up non-blocking reads
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);

    // Read from pipes
    const int BUFFER_SIZE = 4096;
    std::array<char, BUFFER_SIZE> buffer;
    std::stringstream stdout_stream, stderr_stream;
    bool child_running = true;

    while (child_running) {
      // Check child process status
      int status;
      pid_t wait_result = waitpid(pid, &status, WNOHANG);

      if (wait_result == -1) {
        // Error
        break;
      } else if (wait_result > 0) {
        // Child process has terminated
        if (WIFEXITED(status)) {
          result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
          result.exit_code = 128 + WTERMSIG(status);
        }
        child_running = false;
      }

      // Read stdout
      ssize_t bytes_read;
      while ((bytes_read =
                  read(stdout_pipe[0], buffer.data(), BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        std::string output_chunk(buffer.data(), bytes_read);
        stdout_stream << output_chunk;
        if (stdout_callback) {
          stdout_callback(output_chunk);
        }
      }

      // Read stderr
      while ((bytes_read =
                  read(stderr_pipe[0], buffer.data(), BUFFER_SIZE - 1)) > 0) {
        buffer[bytes_read] = '\0';
        std::string error_chunk(buffer.data(), bytes_read);
        stderr_stream << error_chunk;
        if (stderr_callback) {
          stderr_callback(error_chunk);
        }
      }

      // Avoid busy-waiting
      usleep(10000); // 10ms
    }

    // Read any remaining data from pipes
    ssize_t bytes_read;
    while ((bytes_read = read(stdout_pipe[0], buffer.data(), BUFFER_SIZE - 1)) >
           0) {
      buffer[bytes_read] = '\0';
      std::string output_chunk(buffer.data(), bytes_read);
      stdout_stream << output_chunk;
      if (stdout_callback) {
        stdout_callback(output_chunk);
      }
    }

    while ((bytes_read = read(stderr_pipe[0], buffer.data(), BUFFER_SIZE - 1)) >
           0) {
      buffer[bytes_read] = '\0';
      std::string error_chunk(buffer.data(), bytes_read);
      stderr_stream << error_chunk;
      if (stderr_callback) {
        stderr_callback(error_chunk);
      }
    }

    // Close read ends of pipes
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);

    // Get final output
    result.stdout_output = stdout_stream.str();
    result.stderr_output = stderr_stream.str();
    result.success = (result.exit_code == 0);
  }

  return result;
}
#endif

std::string string_to_lower(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

// Common implementation for both platforms
bool execute_tool(const std::string &command,
                  const std::vector<std::string> &args,
                  const std::string &working_dir, const std::string &tool_name,
                  bool verbose, int timeout_seconds) {
  std::string tool_name_lower = string_to_lower(tool_name);

  // Check if the command is for a build tool that should use the error
  // formatter
  bool is_build_tool = false;
  if (tool_name_lower == "cmake" || tool_name_lower == "cpack" ||
      tool_name_lower == "ctest" || tool_name_lower == "make" ||
      tool_name_lower == "ninja" || tool_name_lower == "cl" ||
      tool_name_lower == "gcc" || tool_name_lower == "clang" ||
      tool_name_lower == "g++" || tool_name_lower == "clang++" ||
      tool_name_lower == "ar" || tool_name_lower == "ld" ||
      tool_name_lower.find("visual studio") != std::string::npos ||
      // Also check for common compiler commands in the command string
      command.find("cmake") != std::string::npos ||
      command.find("cpack") !=
          std::string::npos || // Also check command for cpack
      command.find("ctest") != std::string::npos ||
      command.find("cl.exe") != std::string::npos ||
      command.find("link.exe") != std::string::npos) {
    is_build_tool = true;
  }

  // Create a formatted command string for logging
  std::string cmd_str = command;
  for (const auto &arg : args) {
    if (arg.find(' ') != std::string::npos) {
      cmd_str += " \"" + arg + "\"";
    } else {
      cmd_str += " " + arg;
    }
  }

  // Show which tool we're running but only if verbose is true or no
  // command_name provided
  std::string tool_name_to_use = tool_name.empty() ? command : tool_name;

  if (verbose) {
    logger::print_action("Running", cmd_str);
  }

  // Collect output
  std::stringstream stdout_collect, stderr_collect;

  // Process stdout in real-time if in verbose mode
  std::function<void(const std::string &)> stdout_callback = nullptr;
  if (verbose) {
    stdout_callback = [&tool_name](const std::string &chunk) {
      std::string line;
      std::istringstream ss(chunk);
      while (std::getline(ss, line)) {
        if (!line.empty()) {
          logger::print_action("Output", line);
        }
      }
    };
  }

  // Process stderr in real-time
  std::function<void(const std::string &)> stderr_callback = nullptr;
  if (verbose) {
    stderr_callback = [&tool_name](const std::string &chunk) {
      std::string line;
      std::istringstream ss(chunk);
      while (std::getline(ss, line)) {
        if (!line.empty()) {
          logger::print_action("Output", line);
        }
      }
    };
  }

  // Execute the process with timeout
  process_result result =
      execute_process(command, args, working_dir, stdout_callback,
                      stderr_callback, timeout_seconds);

  // Always show output/errors for failed commands regardless of verbose mode
  if (!result.success) {
    // Log for debugging, but don't print directly to users
    logger::print_verbose("Command failed with exit code: " +
                          std::to_string(result.exit_code));
  } else if (verbose) {
    // Only show for successful commands if verbose mode is on
    if (!result.stdout_output.empty()) {
      logger::print_verbose("Tool output:\n" + result.stdout_output);
    }
    if (!result.stderr_output.empty()) {
      logger::print_verbose("Tool errors:\n" + result.stderr_output);
    }
  }

  // Always show warnings for build tools in non-verbose mode unless suppressed
  else if (is_build_tool && result.success && !g_suppress_warnings) {
    // Parse stderr for warnings
    std::istringstream warn_stream(result.stderr_output);
    std::string warn_line;
    while (std::getline(warn_stream, warn_line)) {
      if (!warn_line.empty() &&
          (warn_line.find("warning") != std::string::npos ||
           warn_line.find("Warning") != std::string::npos)) {
        logger::print_warning(warn_line);
      }
    }
    // Parse stdout for warnings
    std::istringstream warn_stream_out(result.stdout_output);
    while (std::getline(warn_stream_out, warn_line)) {
      if (!warn_line.empty() &&
          (warn_line.find("warning") != std::string::npos ||
           warn_line.find("Warning") != std::string::npos)) {
        logger::print_warning(warn_line);
      }
    }
  }

  // Always try to format and display errors using the Rust-style formatter
  if (!result.success) {
    // For both stdout and stderr, try to format errors using our nice formatter
    bool found_errors = false;

    if (!result.stderr_output.empty()) {
      // Format with our error formatter to make errors more readable
      std::string formatted_errors = format_build_errors(result.stderr_output);
      if (!formatted_errors.empty()) {
        found_errors = true;
        fmt::print("{}", formatted_errors);
      }
    }

    // Also check for errors in stdout (some tools like CMake output errors
    // there)
    if (!result.stdout_output.empty() &&
        (result.stdout_output.find("error") != std::string::npos ||
         result.stdout_output.find("Error") != std::string::npos ||
         result.stdout_output.find("ERROR") != std::string::npos)) {

      std::string formatted_stdout_errors =
          format_build_errors(result.stdout_output);
      if (!formatted_stdout_errors.empty()) {
        found_errors = true;
        fmt::print("{}", formatted_stdout_errors);
      }
    }

    // If our formatter didn't produce anything useful, fall back to simple
    // error display
    if (!found_errors) {
      // For non-build tools or if formatter failed, extract relevant error
      // messages
      bool printed_error_header = false;

      if (!result.stderr_output.empty()) {
        std::istringstream error_stream(result.stderr_output);
        std::string line;

        while (std::getline(error_stream, line)) {
          // Skip empty lines or common information messages
          if (line.empty() || line.find("--") == 0 ||
              line.find("MSBuild") == 0) {
            continue;
          }

          // Focus on lines with "error" in them
          if (line.find("error") != std::string::npos ||
              line.find("Error") != std::string::npos ||
              line.find("ERROR") != std::string::npos) {

            if (!printed_error_header) {
              // Use color for the error header
              fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
                         "→ Command failed:\n");
              printed_error_header = true;
            }

            // Print the actual error line with color
            fmt::print(fg(fmt::color::light_pink), "    {}\n", line);
          }
        }
      }

      if (!result.stdout_output.empty() && !printed_error_header) {
        std::istringstream output_stream(result.stdout_output);
        std::string line;

        while (std::getline(output_stream, line)) {
          if (line.find("error") != std::string::npos ||
              line.find("Error") != std::string::npos ||
              line.find("ERROR") != std::string::npos) {

            if (!printed_error_header) {
              // Use color for the error header
              fmt::print(fg(fmt::color::crimson) | fmt::emphasis::bold,
                         "→ Command failed:\n");
              printed_error_header = true;
            }

            // Print the actual error line with color
            fmt::print(fg(fmt::color::light_pink), "    {}\n", line);
          }
        }
      }
    }
  }

  return result.exit_code == 0;
}

// Add a utility function to check if a command is available in the PATH
bool is_command_available(const std::string &command, int timeout_seconds) {
  std::vector<std::string> args;

  // Adjust default timeout based on command
  // Commands like CMake and Ninja may take longer on some Windows systems
  if (timeout_seconds <= 0) {
    if (command == "cmake" || command == "ninja") {
#ifdef _WIN32
      timeout_seconds = 6; // Shorter timeout for CMake and Ninja on Windows
#else
      timeout_seconds = 5; // Default timeout on other platforms
#endif
    } else {
      timeout_seconds = 3; // Shorter default timeout for most commands
    }
  }

  // Use appropriate flags for different commands
  // Some tools don't respond well to --version, so we use command-specific
  // flags
  if (command == "git") {
    args = {"--version"};
  } else if (command == "cmake") {
    args = {"--version"};

    // On Windows, try an alternative approach to detect CMake
#ifdef _WIN32
    // First, check common CMake installation paths
    std::vector<std::string> cmake_paths = {
        "C:\\Program Files\\CMake\\bin\\cmake.exe",
        "C:\\Program Files (x86)\\CMake\\bin\\cmake.exe"};

    for (const auto &path : cmake_paths) {
      if (std::filesystem::exists(path)) {
        logger::print_verbose("Found CMake at: " + path);
        return true;
      }
    }

    // Try just the version flag as it's simple
    logger::print_verbose("Checking availability of CMake with timeout: " +
                          std::to_string(timeout_seconds) + "s");
#endif
  } else if (command == "ninja") {
    args = {"--version"};
  } else if (command == "make") {
    args = {"--version"};
  } else if (command == "mingw32-make") {
    args = {"--version"};
  } else if (command == "nmake") {
    args = {"/?"}; // nmake doesn't support --version
  } else if (command == "gcc" || command == "g++") {
    args = {"-v"};
  } else if (command == "clang" || command == "clang++") {
    args = {"-v"};
  } else if (command == "msbuild") {
    args = {"/version"}; // MSBuild uses /version
  } else if (command == "dotnet") {
    args = {"--info"}; // dotnet uses --info
  } else if (command == "npm") {
    args = {"--version"};
  } else if (command == "python" || command == "python3") {
    args = {"--version"};
  } else {
    // Default to --version for most commands
    args = {"--version"};
  }

  try {
    // Use a short timeout since this is just a check
    logger::print_verbose("Checking availability of command: " + command +
                          " (timeout: " + std::to_string(timeout_seconds) +
                          "s)");
    process_result result =
        execute_process(command, args, "", nullptr, nullptr, timeout_seconds);

    if (result.success) {
      logger::print_verbose("Command '" + command + "' is available (" +
                            result.stdout_output.substr(0, 50) +
                            (result.stdout_output.length() > 50 ? "..." : "") +
                            ")");
      return true;
    } else {
      if (result.exit_code == -1) {
        logger::print_verbose("Command '" + command +
                              "' failed: Process execution error");
      } else {
        logger::print_verbose(
            "Command '" + command +
            "' failed with exit code: " + std::to_string(result.exit_code));
      }

      if (!result.stderr_output.empty()) {
        logger::print_verbose(
            "Error output: " + result.stderr_output.substr(0, 100) +
            (result.stderr_output.length() > 100 ? "..." : ""));
      }

      return false;
    }
  } catch (const std::exception &ex) {
    logger::print_verbose("Exception checking command '" + command +
                          "': " + ex.what());
    return false;
  } catch (...) {
    logger::print_verbose("Unknown exception checking command: " + command);
    return false;
  }
}

} // namespace cforge