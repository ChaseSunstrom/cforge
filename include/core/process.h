/**
 * @file process.h
 * @brief Process and command execution utilities
 */

 #ifndef CFORGE_PROCESS_H
 #define CFORGE_PROCESS_H
 
 #include <stdbool.h>
 #include <stddef.h>
 #include "file_system.h"
 #include "types.h"
 
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /**
  * @brief Redirect types for process I/O
  */
 typedef enum {
     CFORGE_REDIRECT_NONE,    /**< No redirection */
     CFORGE_REDIRECT_PIPE,    /**< Redirect to pipe */
     CFORGE_REDIRECT_NULL     /**< Redirect to /dev/null or NUL */
 } cforge_redirect_t;
 
 /**
  * @brief Process execution status
  */
 typedef enum {
     CFORGE_PROC_SUCCESS = 0,       /**< Process succeeded */
     CFORGE_PROC_RUNNING,           /**< Process is still running */
     CFORGE_PROC_ERROR_START,       /**< Error starting process */
     CFORGE_PROC_ERROR_WAIT,        /**< Error waiting for process */
     CFORGE_PROC_ERROR_TIMEOUT,     /**< Process timed out */
     CFORGE_PROC_ERROR_SIGNAL,      /**< Process terminated by signal */
     CFORGE_PROC_ERROR_NONZERO      /**< Process exited with non-zero code */
 } cforge_process_status_t;
 
 /**
  * @brief Process object
  */
 typedef struct {
     cforge_pointer_t handle;               /**< Platform-specific process handle */
     cforge_int_t exit_code;              /**< Process exit code */
     cforge_process_status_t status; /**< Process status */
     cforge_pointer_t stdout_handle;        /**< Handle for reading stdout */
     cforge_pointer_t stderr_handle;        /**< Handle for reading stderr */
     cforge_string_t command;              /**< Command that was executed */
     cforge_string_t* args;                /**< Arguments that were passed */
     cforge_string_t working_dir;          /**< Working directory */
     cforge_size_t arg_count;           /**< Number of arguments */
 } cforge_process_t;
 
 /**
  * @brief Process output
  */
 typedef struct {
     cforge_string_t stdout_data;          /**< Standard output data */
     cforge_string_t stderr_data;          /**< Standard error data */
     cforge_size_t stdout_size;         /**< Size of stdout data */
     cforge_size_t stderr_size;         /**< Size of stderr data */
     cforge_int_t exit_code;              /**< Process exit code */
     cforge_process_status_t status; /**< Process status */
 } cforge_process_output_t;
 
 /**
  * @brief Initialize a new process
  */
 cforge_process_status_t cforge_process_init(cforge_process_t* process, 
                                          cforge_cstring_t command,
                                          cforge_string_t const args[]);
 
 /**
  * @brief Free resources associated with a process
  */
 void cforge_process_free(cforge_process_t* process);
 
 /**
  * @brief Start a process
  */
 cforge_process_status_t cforge_process_start(cforge_process_t* process,
                                           cforge_cstring_t working_dir,
                                           cforge_redirect_t stdout_redirect,
                                           cforge_redirect_t stderr_redirect);
 
 /**
  * @brief Wait for a process to complete
  */
 cforge_process_status_t cforge_process_wait(cforge_process_t* process, 
                                          cforge_uint_t timeout_ms);
 
 /**
  * @brief Terminate a running process
  */
 cforge_process_status_t cforge_process_terminate(cforge_process_t* process);
 
 /**
  * @brief Read from a process's stdout
  */
 cforge_process_status_t cforge_process_read_stdout(cforge_process_t* process,
                                                cforge_string_t buffer,
                                                cforge_size_t size,
                                                cforge_size_t* bytes_read);
 
 /**
  * @brief Read from a process's stderr
  */
 cforge_process_status_t cforge_process_read_stderr(cforge_process_t* process,
                                                cforge_string_t buffer,
                                                cforge_size_t size,
                                                cforge_size_t* bytes_read);
 
 /**
  * @brief Run a command and capture its output
  */
 cforge_process_status_t cforge_run_command(cforge_cstring_t command,
                                         cforge_string_t const args[],
                                         cforge_cstring_t working_dir,
                                         cforge_uint_t timeout_ms,
                                         cforge_process_output_t* output);
 
 /**
  * @brief Free resources associated with process output
  */
 void cforge_process_output_free(cforge_process_output_t* output);
 
 /**
  * @brief Check if a command is available in the system PATH
  */
 bool cforge_command_exists(cforge_cstring_t command);
 
 /**
  * @brief Get a string representation of a process status
  */
 cforge_cstring_t cforge_process_status_str(cforge_process_status_t status);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* CFORGE_PROCESS_H */