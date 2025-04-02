/**
 * @file file_system.h
 * @brief File system manipulation utilities
 */

#ifndef CFORGE_FILE_SYSTEM_H
#define CFORGE_FILE_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Path representation
 */
typedef struct {
  cforge_string_t data;
  cforge_size_t length;
} cforge_path_t;

/**
 * @brief Error codes for file system operations
 */
typedef enum {
  CFORGE_FS_SUCCESS = 0,
  CFORGE_FS_NOT_FOUND,
  CFORGE_FS_ACCESS_DENIED,
  CFORGE_FS_IO_ERROR,
  CFORGE_FS_INVALID_PATH,
  CFORGE_FS_UNKNOWN_ERROR
} cforge_fs_error_t;

/**
 * @brief Initialize a path
 */
cforge_fs_error_t cforge_path_init(cforge_path_t *path,
                                   cforge_cstring_t path_str);

/**
 * @brief Free a path
 */
void cforge_path_free(cforge_path_t *path);

/**
 * @brief Join two paths
 */
cforge_fs_error_t cforge_path_join(const cforge_path_t *base,
                                   cforge_cstring_t component,
                                   cforge_path_t *result);

/**
 * @brief Check if a path exists
 */
bool cforge_path_exists(const cforge_path_t *path);

/**
 * @brief Check if a path is a directory
 */
bool cforge_path_is_directory(const cforge_path_t *path);

/**
 * @brief Check if a path is a file
 */
bool cforge_path_is_file(const cforge_path_t *path);

/**
 * @brief Create a directory
 */
cforge_fs_error_t cforge_create_directory(const cforge_path_t *path,
                                          bool recursive);

/**
 * @brief Remove a file
 */
cforge_fs_error_t cforge_remove_file(const cforge_path_t *path);

/**
 * @brief Remove a directory
 */
cforge_fs_error_t cforge_remove_directory(const cforge_path_t *path,
                                          bool recursive);

/**
 * @brief Read the contents of a file
 */
cforge_fs_error_t cforge_read_file(const cforge_path_t *path,
                                   cforge_string_t *buffer,
                                   cforge_size_t *size);

/**
 * @brief Write content to a file
 */
cforge_fs_error_t cforge_write_file(const cforge_path_t *path,
                                    cforge_cstring_t content,
                                    cforge_size_t size);

/**
 * @brief Get a string representation of a file system error
 */
cforge_cstring_t cforge_fs_error_str(cforge_fs_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* CFORGE_FILE_SYSTEM_H */