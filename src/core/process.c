/**
 * @file process.c
 * @brief Implementation of process and command execution utilities
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/process.h"

// Platform-specific includes
#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// String representation of process status
cforge_cstring_t cforge_process_status_str(cforge_process_status_t status) {
  switch (status) {
  case CFORGE_PROC_SUCCESS:
    return "Success";
  case CFORGE_PROC_RUNNING:
    return "Running";
  case CFORGE_PROC_ERROR_START:
    return "Error starting process";
  case CFORGE_PROC_ERROR_WAIT:
    return "Error waiting for process";
  case CFORGE_PROC_ERROR_TIMEOUT:
    return "Process timed out";
  case CFORGE_PROC_ERROR_SIGNAL:
    return "Process terminated by signal";
  case CFORGE_PROC_ERROR_NONZERO:
    return "Process exited with non-zero code";
  default:
    return "Unknown status";
  }
}

// Initialize a process structure
cforge_process_status_t cforge_process_init(cforge_process_t *process,
                                            cforge_cstring_t command,
                                            cforge_string_t const args[]) {
  if (!process || !command) {
    return CFORGE_PROC_ERROR_START;
  }

  // Initialize structure to zeros
  memset(process, 0, sizeof(cforge_process_t));

  // Copy command
  process->command = strdup(command);
  if (!process->command) {
    return CFORGE_PROC_ERROR_START;
  }

  // Count arguments
  cforge_size_t arg_count = 0;
  if (args) {
    while (args[arg_count]) {
      arg_count++;
    }
  }

  // Allocate space for arguments
  process->args =
      (cforge_string_t *)malloc((arg_count + 1) * sizeof(cforge_string_t));
  if (!process->args) {
    free(process->command);
    process->command = NULL;
    return CFORGE_PROC_ERROR_START;
  }

  // Copy arguments
  for (cforge_size_t i = 0; i < arg_count; i++) {
    process->args[i] = strdup(args[i]);
    if (!process->args[i]) {
      // Clean up previously allocated memory
      for (cforge_size_t j = 0; j < i; j++) {
        free(process->args[j]);
      }
      free(process->args);
      free(process->command);
      process->args = NULL;
      process->command = NULL;
      return CFORGE_PROC_ERROR_START;
    }
  }
  process->args[arg_count] = NULL; // Null terminate argument list
  process->arg_count = arg_count;

  return CFORGE_PROC_SUCCESS;
}

// Free resources associated with a process
void cforge_process_free(cforge_process_t *process) {
  if (!process) {
    return;
  }

  // Free command string
  if (process->command) {
    free(process->command);
    process->command = NULL;
  }

  // Free arguments
  if (process->args) {
    for (cforge_size_t i = 0; i < process->arg_count; i++) {
      if (process->args[i]) {
        free(process->args[i]);
      }
    }
    free(process->args);
    process->args = NULL;
  }

  // Free working directory
  if (process->working_dir) {
    free(process->working_dir);
    process->working_dir = NULL;
  }

  // Platform-specific cleanup
#ifdef _WIN32
  if (process->handle) {
    CloseHandle((HANDLE)process->handle);
    process->handle = NULL;
  }
  if (process->stdout_handle) {
    CloseHandle((HANDLE)process->stdout_handle);
    process->stdout_handle = NULL;
  }
  if (process->stderr_handle) {
    CloseHandle((HANDLE)process->stderr_handle);
    process->stderr_handle = NULL;
  }
#else
  if (process->stdout_handle) {
    close((cforge_int_t)(intptr_t)process->stdout_handle);
    process->stdout_handle = NULL;
  }
  if (process->stderr_handle) {
    close((cforge_int_t)(intptr_t)process->stderr_handle);
    process->stderr_handle = NULL;
  }
#endif
}

// Windows implementation of process_start
#ifdef _WIN32
cforge_process_status_t
cforge_process_start(cforge_process_t *process, cforge_cstring_t working_dir,
                     cforge_redirect_t stdout_redirect,
                     cforge_redirect_t stderr_redirect) {
  if (!process || !process->command) {
    return CFORGE_PROC_ERROR_START;
  }

  // Save working directory
  if (working_dir) {
    process->working_dir = strdup(working_dir);
  }

  SECURITY_ATTRIBUTES sa = {0};
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;

  HANDLE stdout_read = NULL, stdout_write = NULL;
  HANDLE stderr_read = NULL, stderr_write = NULL;

  // Setup stdout redirection
  if (stdout_redirect == CFORGE_REDIRECT_PIPE) {
    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
      return CFORGE_PROC_ERROR_START;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);
  } else if (stdout_redirect == CFORGE_REDIRECT_NULL) {
    stdout_write =
        CreateFile("NUL", GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, NULL);
  }

  // Setup stderr redirection
  if (stderr_redirect == CFORGE_REDIRECT_PIPE) {
    if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
      if (stdout_read)
        CloseHandle(stdout_read);
      if (stdout_write)
        CloseHandle(stdout_write);
      return CFORGE_PROC_ERROR_START;
    }
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);
  } else if (stderr_redirect == CFORGE_REDIRECT_NULL) {
    stderr_write =
        CreateFile("NUL", GENERIC_WRITE, 0, &sa, OPEN_EXISTING, 0, NULL);
  }

  // Construct command line
  cforge_string_t command_line = NULL;
  cforge_size_t total_length =
      strlen(process->command) + 3; // Space for quotes and space

  for (cforge_size_t i = 0; i < process->arg_count; i++) {
    total_length += strlen(process->args[i]) + 3; // Space for quotes and space
  }

  command_line =
      (cforge_string_t)malloc(total_length + 1); // +1 for null terminator
  if (!command_line) {
    if (stdout_read)
      CloseHandle(stdout_read);
    if (stdout_write)
      CloseHandle(stdout_write);
    if (stderr_read)
      CloseHandle(stderr_read);
    if (stderr_write)
      CloseHandle(stderr_write);
    return CFORGE_PROC_ERROR_START;
  }

  // Build command line with proper quoting
  sprintf(command_line, "\"%s\"", process->command);

  for (cforge_size_t i = 0; i < process->arg_count; i++) {
    strcat(command_line, " \"");
    strcat(command_line, process->args[i]);
    strcat(command_line, "\"");
  }

  // Setup process information
  STARTUPINFO si = {0};
  PROCESS_INFORMATION pi = {0};

  si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = stdout_redirect != CFORGE_REDIRECT_NONE
                      ? stdout_write
                      : GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = stderr_redirect != CFORGE_REDIRECT_NONE
                     ? stderr_write
                     : GetStdHandle(STD_ERROR_HANDLE);

  // Create the process
  BOOL result =
      CreateProcess(NULL,         // Application name (NULL = use command line)
                    command_line, // Command line
                    NULL,         // Process security attributes
                    NULL,         // Thread security attributes
                    TRUE,         // Inherit handles
                    0,            // Creation flags
                    NULL,         // Environment
                    working_dir,  // Working directory
                    &si,          // Startup info
                    &pi           // Process information
      );

  // Free command line
  free(command_line);

  if (!result) {
    DWORD error = GetLastError();
    if (stdout_read)
      CloseHandle(stdout_read);
    if (stdout_write)
      CloseHandle(stdout_write);
    if (stderr_read)
      CloseHandle(stderr_read);
    if (stderr_write)
      CloseHandle(stderr_write);
    return CFORGE_PROC_ERROR_START;
  }

  // Store process information
  process->handle = (cforge_pointer_t)pi.hProcess;
  process->stdout_handle = (cforge_pointer_t)stdout_read;
  process->stderr_handle = (cforge_pointer_t)stderr_read;

  // Close unused handles
  if (stdout_write)
    CloseHandle(stdout_write);
  if (stderr_write)
    CloseHandle(stderr_write);
  CloseHandle(pi.hThread);

  process->status = CFORGE_PROC_RUNNING;
  return CFORGE_PROC_SUCCESS;
}
#else
// POSIX implementation of process_start
cforge_process_status_t
cforge_process_start(cforge_process_t *process, cforge_cstring_t working_dir,
                     cforge_redirect_t stdout_redirect,
                     cforge_redirect_t stderr_redirect) {
  if (!process || !process->command) {
    return CFORGE_PROC_ERROR_START;
  }

  // Save working directory
  if (working_dir) {
    process->working_dir = strdup(working_dir);
  }

  // Setup file descriptors for redirection
  cforge_int_t stdout_pipe[2] = {-1, -1};
  cforge_int_t stderr_pipe[2] = {-1, -1};

  // Setup stdout redirection
  if (stdout_redirect == CFORGE_REDIRECT_PIPE) {
    if (pipe(stdout_pipe) != 0) {
      return CFORGE_PROC_ERROR_START;
    }
    // Set non-blocking mode for the read end
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
  }

  // Setup stderr redirection
  if (stderr_redirect == CFORGE_REDIRECT_PIPE) {
    if (pipe(stderr_pipe) != 0) {
      if (stdout_pipe[0] != -1)
        close(stdout_pipe[0]);
      if (stdout_pipe[1] != -1)
        close(stdout_pipe[1]);
      return CFORGE_PROC_ERROR_START;
    }
    // Set non-blocking mode for the read end
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
  }

  // Fork the process
  pid_t pid = fork();

  if (pid < 0) {
    // Fork failed
    if (stdout_pipe[0] != -1)
      close(stdout_pipe[0]);
    if (stdout_pipe[1] != -1)
      close(stdout_pipe[1]);
    if (stderr_pipe[0] != -1)
      close(stderr_pipe[0]);
    if (stderr_pipe[1] != -1)
      close(stderr_pipe[1]);
    return CFORGE_PROC_ERROR_START;
  }

  if (pid == 0) {
    // Child process

    // Change working directory if specified
    if (working_dir) {
      if (chdir(working_dir) != 0) {
        _exit(EXIT_FAILURE);
      }
    }

    // Setup stdout redirection
    if (stdout_redirect == CFORGE_REDIRECT_PIPE) {
      dup2(stdout_pipe[1], STDOUT_FILENO);
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
    } else if (stdout_redirect == CFORGE_REDIRECT_NULL) {
      cforge_int_t null_fd = open("/dev/null", O_WRONLY);
      if (null_fd != -1) {
        dup2(null_fd, STDOUT_FILENO);
        close(null_fd);
      }
    }

    // Setup stderr redirection
    if (stderr_redirect == CFORGE_REDIRECT_PIPE) {
      dup2(stderr_pipe[1], STDERR_FILENO);
      close(stderr_pipe[0]);
      close(stderr_pipe[1]);
    } else if (stderr_redirect == CFORGE_REDIRECT_NULL) {
      cforge_int_t null_fd = open("/dev/null", O_WRONLY);
      if (null_fd != -1) {
        dup2(null_fd, STDERR_FILENO);
        close(null_fd);
      }
    }

    // Prepare arguments for execvp
    cforge_char_t **args = (cforge_char_t **)malloc((process->arg_count + 2) *
                                                    sizeof(cforge_char_t *));
    if (!args) {
      _exit(EXIT_FAILURE);
    }

    args[0] = process->command;

    for (cforge_size_t i = 0; i < process->arg_count; i++) {
      args[i + 1] = process->args[i];
    }

    args[process->arg_count + 1] = NULL;

    // Execute the command
    execvp(process->command, args);

    // If we get here, execvp failed
    free(args);
    _exit(EXIT_FAILURE);
  }

  // Parent process

  // Store process information
  process->handle = (cforge_pointer_t)(intptr_t)pid;

  // Close write ends of pipes
  if (stdout_pipe[1] != -1)
    close(stdout_pipe[1]);
  if (stderr_pipe[1] != -1)
    close(stderr_pipe[1]);

  // Store read ends of pipes
  if (stdout_redirect == CFORGE_REDIRECT_PIPE) {
    process->stdout_handle = (cforge_pointer_t)(intptr_t)stdout_pipe[0];
  }

  if (stderr_redirect == CFORGE_REDIRECT_PIPE) {
    process->stderr_handle = (cforge_pointer_t)(intptr_t)stderr_pipe[0];
  }

  process->status = CFORGE_PROC_RUNNING;
  return CFORGE_PROC_SUCCESS;
}
#endif

// Windows implementation of process_wait
#ifdef _WIN32
cforge_process_status_t cforge_process_wait(cforge_process_t *process,
                                            cforge_uint_t timeout_ms) {
  if (!process || !process->handle) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  DWORD result = WaitForSingleObject((HANDLE)process->handle, timeout_ms);

  if (result == WAIT_TIMEOUT) {
    return CFORGE_PROC_ERROR_TIMEOUT;
  }

  if (result != WAIT_OBJECT_0) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  // Get exit code
  DWORD exit_code = 0;
  if (!GetExitCodeProcess((HANDLE)process->handle, &exit_code)) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  process->exit_code = (cforge_int_t)exit_code;

  if (exit_code != 0) {
    process->status = CFORGE_PROC_ERROR_NONZERO;
  } else {
    process->status = CFORGE_PROC_SUCCESS;
  }

  return process->status;
}
#else
// POSIX implementation of process_wait
cforge_process_status_t cforge_process_wait(cforge_process_t *process,
                                            cforge_uint_t timeout_ms) {
  if (!process || !process->handle) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  pid_t pid = (pid_t)(intptr_t)process->handle;

  if (timeout_ms == 0) {
    // Wait indefinitely
    cforge_int_t status;
    pid_t wait_result = waitpid(pid, &status, 0);

    if (wait_result == -1) {
      return CFORGE_PROC_ERROR_WAIT;
    }

    if (WIFEXITED(status)) {
      process->exit_code = WEXITSTATUS(status);
      process->status = process->exit_code == 0 ? CFORGE_PROC_SUCCESS
                                                : CFORGE_PROC_ERROR_NONZERO;
    } else if (WIFSIGNALED(status)) {
      process->exit_code = WTERMSIG(status);
      process->status = CFORGE_PROC_ERROR_SIGNAL;
    } else {
      process->status = CFORGE_PROC_ERROR_WAIT;
    }
  } else {
    // Use polling for timeout
    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (1) {
      cforge_int_t status;
      pid_t wait_result = waitpid(pid, &status, WNOHANG);

      if (wait_result == pid) {
        // Process has exited
        if (WIFEXITED(status)) {
          process->exit_code = WEXITSTATUS(status);
          process->status = process->exit_code == 0 ? CFORGE_PROC_SUCCESS
                                                    : CFORGE_PROC_ERROR_NONZERO;
        } else if (WIFSIGNALED(status)) {
          process->exit_code = WTERMSIG(status);
          process->status = CFORGE_PROC_ERROR_SIGNAL;
        } else {
          process->status = CFORGE_PROC_ERROR_WAIT;
        }
        break;
      } else if (wait_result == 0) {
        // Process still running, check timeout
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        cforge_ulong_t elapsed_ms =
            (current_time.tv_sec - start_time.tv_sec) * 1000 +
            (current_time.tv_nsec - start_time.tv_nsec) / 1000000;

        if (elapsed_ms >= timeout_ms) {
          return CFORGE_PROC_ERROR_TIMEOUT;
        }

        // Sleep a bit to avoid busy waiting
        usleep(10000); // 10ms
      } else {
        // Error
        return CFORGE_PROC_ERROR_WAIT;
      }
    }
  }

  return process->status;
}
#endif

// Windows implementation of process_terminate
#ifdef _WIN32
cforge_process_status_t cforge_process_terminate(cforge_process_t *process) {
  if (!process || !process->handle) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  if (!TerminateProcess((HANDLE)process->handle, 1)) {
    return CFORGE_PROC_ERROR_SIGNAL;
  }

  process->status = CFORGE_PROC_ERROR_SIGNAL;
  return CFORGE_PROC_SUCCESS;
}
#else
// POSIX implementation of process_terminate
cforge_process_status_t cforge_process_terminate(cforge_process_t *process) {
  if (!process || !process->handle) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  pid_t pid = (pid_t)(intptr_t)process->handle;

  if (kill(pid, SIGTERM) != 0) {
    return CFORGE_PROC_ERROR_SIGNAL;
  }

  // Wait a moment for the process to terminate
  usleep(100000); // 100ms

  // Check if process is still running
  if (kill(pid, 0) == 0) {
    // Process is still running, force kill
    if (kill(pid, SIGKILL) != 0) {
      return CFORGE_PROC_ERROR_SIGNAL;
    }
  }

  process->status = CFORGE_PROC_ERROR_SIGNAL;
  return CFORGE_PROC_SUCCESS;
}
#endif

// Windows implementation of process_read_stdout
#ifdef _WIN32
cforge_process_status_t cforge_process_read_stdout(cforge_process_t *process,
                                                   cforge_string_t buffer,
                                                   cforge_size_t size,
                                                   cforge_size_t *bytes_read) {
  if (!process || !process->stdout_handle || !buffer || !bytes_read) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  DWORD read_bytes = 0;
  if (!ReadFile((HANDLE)process->stdout_handle, buffer, (DWORD)size,
                &read_bytes, NULL)) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  *bytes_read = (cforge_size_t)read_bytes;
  return CFORGE_PROC_SUCCESS;
}
#else
// POSIX implementation of process_read_stdout
cforge_process_status_t cforge_process_read_stdout(cforge_process_t *process,
                                                   cforge_string_t buffer,
                                                   cforge_size_t size,
                                                   cforge_size_t *bytes_read) {
  if (!process || !process->stdout_handle || !buffer || !bytes_read) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  cforge_int_t fd = (cforge_int_t)(intptr_t)process->stdout_handle;
  cforge_int_t read_bytes = read(fd, buffer, size);

  if (read_bytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // No data available
      *bytes_read = 0;
      return CFORGE_PROC_SUCCESS;
    }
    return CFORGE_PROC_ERROR_WAIT;
  }

  *bytes_read = (cforge_size_t)read_bytes;
  return CFORGE_PROC_SUCCESS;
}
#endif

// Windows implementation of process_read_stderr
#ifdef _WIN32
cforge_process_status_t cforge_process_read_stderr(cforge_process_t *process,
                                                   cforge_string_t buffer,
                                                   cforge_size_t size,
                                                   cforge_size_t *bytes_read) {
  if (!process || !process->stderr_handle || !buffer || !bytes_read) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  DWORD read_bytes = 0;
  if (!ReadFile((HANDLE)process->stderr_handle, buffer, (DWORD)size,
                &read_bytes, NULL)) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  *bytes_read = (cforge_size_t)read_bytes;
  return CFORGE_PROC_SUCCESS;
}
#else
// POSIX implementation of process_read_stderr
cforge_process_status_t cforge_process_read_stderr(cforge_process_t *process,
                                                   cforge_string_t buffer,
                                                   cforge_size_t size,
                                                   cforge_size_t *bytes_read) {
  if (!process || !process->stderr_handle || !buffer || !bytes_read) {
    return CFORGE_PROC_ERROR_WAIT;
  }

  cforge_int_t fd = (cforge_int_t)(intptr_t)process->stderr_handle;
  cforge_int_t read_bytes = read(fd, buffer, size);

  if (read_bytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // No data available
      *bytes_read = 0;
      return CFORGE_PROC_SUCCESS;
    }
    return CFORGE_PROC_ERROR_WAIT;
  }

  *bytes_read = (cforge_size_t)read_bytes;
  return CFORGE_PROC_SUCCESS;
}
#endif

// Free resources associated with process output
void cforge_process_output_free(cforge_process_output_t *output) {
  if (!output) {
    return;
  }

  if (output->stdout_data) {
    free(output->stdout_data);
    output->stdout_data = NULL;
  }

  if (output->stderr_data) {
    free(output->stderr_data);
    output->stderr_data = NULL;
  }

  output->stdout_size = 0;
  output->stderr_size = 0;
}

// Run a command and capture its output
cforge_process_status_t cforge_run_command(cforge_cstring_t command,
                                           cforge_string_t const args[],
                                           cforge_cstring_t working_dir,
                                           cforge_uint_t timeout_ms,
                                           cforge_process_output_t *output) {
  if (!command || !output) {
    return CFORGE_PROC_ERROR_START;
  }

  // Initialize output
  memset(output, 0, sizeof(cforge_process_output_t));

  // Initialize process
  cforge_process_t process;
  cforge_process_status_t status = cforge_process_init(&process, command, args);
  if (status != CFORGE_PROC_SUCCESS) {
    return status;
  }

  // Start process with redirection
  status = cforge_process_start(&process, working_dir, CFORGE_REDIRECT_PIPE,
                                CFORGE_REDIRECT_PIPE);
  if (status != CFORGE_PROC_SUCCESS) {
    cforge_process_free(&process);
    return status;
  }

// Allocate buffers for output
#define BUFFER_SIZE 4096
  cforge_char_t stdout_buffer[BUFFER_SIZE];
  cforge_char_t stderr_buffer[BUFFER_SIZE];

  // Dynamically growing output strings
  cforge_size_t stdout_capacity = BUFFER_SIZE;
  cforge_size_t stderr_capacity = BUFFER_SIZE;

  output->stdout_data = (cforge_string_t)malloc(stdout_capacity);
  output->stderr_data = (cforge_string_t)malloc(stderr_capacity);

  if (!output->stdout_data || !output->stderr_data) {
    if (output->stdout_data)
      free(output->stdout_data);
    if (output->stderr_data)
      free(output->stderr_data);
    output->stdout_data = NULL;
    output->stderr_data = NULL;
    cforge_process_terminate(&process);
    cforge_process_free(&process);
    return CFORGE_PROC_ERROR_START;
  }

  output->stdout_data[0] = '\0';
  output->stderr_data[0] = '\0';

  // Read from stdout and stderr until process is done
  cforge_process_status_t wait_status = CFORGE_PROC_RUNNING;
  cforge_uint_t wait_time = 0;
  const cforge_uint_t wait_interval = 10; // ms

  while (wait_status == CFORGE_PROC_RUNNING) {
    cforge_size_t bytes_read = 0;

    // Read from stdout
    status = cforge_process_read_stdout(&process, stdout_buffer,
                                        BUFFER_SIZE - 1, &bytes_read);
    if (status == CFORGE_PROC_SUCCESS && bytes_read > 0) {
      stdout_buffer[bytes_read] = '\0';

      // Ensure we have enough space
      if (output->stdout_size + bytes_read + 1 > stdout_capacity) {
        stdout_capacity *= 2;
        cforge_string_t new_buffer =
            (cforge_string_t)realloc(output->stdout_data, stdout_capacity);
        if (!new_buffer) {
          free(output->stdout_data);
          free(output->stderr_data);
          output->stdout_data = NULL;
          output->stderr_data = NULL;
          cforge_process_terminate(&process);
          cforge_process_free(&process);
          return CFORGE_PROC_ERROR_START;
        }
        output->stdout_data = new_buffer;
      }

      // Append to output
      strcat(output->stdout_data, stdout_buffer);
      output->stdout_size += bytes_read;
    }

    // Read from stderr
    bytes_read = 0;
    status = cforge_process_read_stderr(&process, stderr_buffer,
                                        BUFFER_SIZE - 1, &bytes_read);
    if (status == CFORGE_PROC_SUCCESS && bytes_read > 0) {
      stderr_buffer[bytes_read] = '\0';

      // Ensure we have enough space
      if (output->stderr_size + bytes_read + 1 > stderr_capacity) {
        stderr_capacity *= 2;
        cforge_string_t new_buffer =
            (cforge_string_t)realloc(output->stderr_data, stderr_capacity);
        if (!new_buffer) {
          free(output->stdout_data);
          free(output->stderr_data);
          output->stdout_data = NULL;
          output->stderr_data = NULL;
          cforge_process_terminate(&process);
          cforge_process_free(&process);
          return CFORGE_PROC_ERROR_START;
        }
        output->stderr_data = new_buffer;
      }

      // Append to output
      strcat(output->stderr_data, stderr_buffer);
      output->stderr_size += bytes_read;
    }

    // Check if process has finished (non-blocking)
    wait_status = cforge_process_wait(&process, 0);

    if (wait_status == CFORGE_PROC_RUNNING) {
// Sleep a bit to avoid busy waiting
#ifdef _WIN32
      Sleep(wait_interval);
#else
      usleep(wait_interval * 1000);
#endif

      wait_time += wait_interval;

      // Check for timeout
      if (timeout_ms > 0 && wait_time >= timeout_ms) {
        cforge_process_terminate(&process);
        wait_status = CFORGE_PROC_ERROR_TIMEOUT;
        break;
      }
    }
  }

  // Store exit code and status
  output->exit_code = process.exit_code;
  output->status = process.status;

  // Clean up process
  cforge_process_free(&process);

  return wait_status;
}

// Check if a command is available in the system PATH
bool cforge_command_exists(cforge_cstring_t command) {
  if (!command) {
    return false;
  }

  // Create process structure for testing
  cforge_process_t process;
  cforge_string_t const args[] = {NULL};

#ifdef _WIN32
  // On Windows, try 'where' command to find executables
  const cforge_char_t *where_cmd = "where";
  cforge_string_t where_args[] = {(cforge_string_t)command, NULL};

  if (cforge_process_init(&process, where_cmd, where_args) !=
      CFORGE_PROC_SUCCESS) {
    return false;
  }
#else
  // On Unix systems, try 'which' command
  const cforge_char_t *which_cmd = "which";
  cforge_string_t which_args[] = {command, NULL};

  if (cforge_process_init(&process, which_cmd, which_args) !=
      CFORGE_PROC_SUCCESS) {
    return false;
  }
#endif

  // Redirect output to null
  cforge_process_start(&process, NULL, CFORGE_REDIRECT_NULL,
                       CFORGE_REDIRECT_NULL);

  // Wait for completion
  cforge_process_status_t status =
      cforge_process_wait(&process, 5000); // 5 second timeout

  // Clean up
  cforge_process_free(&process);

  // Command exists if the process succeeded
  return (status == CFORGE_PROC_SUCCESS);
}

#ifdef __cplusplus
}
#endif