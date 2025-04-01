/**
 * @file file_system.c
 * @brief Implementation of file system manipulation utilities
 */

 #include <stdlib.h>
 #include <string.h>
 #include <stdio.h>

 #include "core/file_system.h"
 
 #ifdef _WIN32
     #include <windows.h>
     #include <direct.h>
     #define PATH_SEPARATOR '\\'
 #else
     #include <unistd.h>
     #include <sys/stat.h>
     #include <dirent.h>
     #include <errno.h>
     #include <limits.h>
     #define PATH_SEPARATOR '/'
 #endif

 #ifdef __cplusplus
    extern "C" {
    #endif
 
 cforge_fs_error_t cforge_path_init(cforge_path_t* path, cforge_cstring_t path_str) {
     if (!path || !path_str) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     cforge_size_t len = strlen(path_str);
     path->data = (cforge_string_t)malloc(len + 1);
     if (!path->data) {
         return CFORGE_FS_UNKNOWN_ERROR;
     }
     
     memcpy(path->data, path_str, len + 1);
     path->length = len;
     
     // Normalize path separators to platform-specific ones
     for (cforge_size_t i = 0; i < len; i++) {
         if (path->data[i] == '/' || path->data[i] == '\\') {
             path->data[i] = PATH_SEPARATOR;
         }
     }
     
     return CFORGE_FS_SUCCESS;
 }
 
 void cforge_path_free(cforge_path_t* path) {
     if (path && path->data) {
         free(path->data);
         path->data = NULL;
         path->length = 0;
     }
 }
 
 cforge_fs_error_t cforge_path_join(const cforge_path_t* base, cforge_cstring_t component, 
                                  cforge_path_t* result) {
     if (!base || !component || !result) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     cforge_size_t base_len = base->length;
     cforge_size_t comp_len = strlen(component);
     
     // Calculate the required size with potential separator
     cforge_size_t needs_separator = (base_len > 0 && base->data[base_len-1] != PATH_SEPARATOR &&
                              comp_len > 0 && component[0] != PATH_SEPARATOR) ? 1 : 0;
     
     // Trim separator from component if base ends with one
     cforge_size_t comp_offset = (base_len > 0 && base->data[base_len-1] == PATH_SEPARATOR &&
                           comp_len > 0 && component[0] == PATH_SEPARATOR) ? 1 : 0;
     
     // Allocate memory for result
     cforge_size_t result_len = base_len + comp_len + needs_separator - comp_offset;
     result->data = (cforge_string_t)malloc(result_len + 1);
     if (!result->data) {
         return CFORGE_FS_UNKNOWN_ERROR;
     }
     
     // Copy base path
     memcpy(result->data, base->data, base_len);
     
     // Add separator if needed
     if (needs_separator) {
         result->data[base_len] = PATH_SEPARATOR;
     }
     
     // Copy component
     memcpy(result->data + base_len + needs_separator, 
            component + comp_offset, 
            comp_len - comp_offset + 1);  // +1 to include null terminator
     
     result->length = result_len;
     
     return CFORGE_FS_SUCCESS;
 }
 
 bool cforge_path_exists(const cforge_path_t* path) {
     if (!path || !path->data) {
         return false;
     }
     
 #ifdef _WIN32
     return GetFileAttributesA(path->data) != INVALID_FILE_ATTRIBUTES;
 #else
     struct stat st;
     return stat(path->data, &st) == 0;
 #endif
 }
 
 bool cforge_path_is_directory(const cforge_path_t* path) {
     if (!path || !path->data) {
         return false;
     }
     
 #ifdef _WIN32
     DWORD attr = GetFileAttributesA(path->data);
     if (attr == INVALID_FILE_ATTRIBUTES) {
         return false;
     }
     return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
 #else
     struct stat st;
     if (stat(path->data, &st) != 0) {
         return false;
     }
     return S_ISDIR(st.st_mode);
 #endif
 }
 
 bool cforge_path_is_file(const cforge_path_t* path) {
     if (!path || !path->data) {
         return false;
     }
     
     if (!cforge_path_exists(path)) {
         return false;
     }
     
     return !cforge_path_is_directory(path);
 }
 
 cforge_fs_error_t cforge_create_directory(const cforge_path_t* path, bool recursive) {
     if (!path || !path->data) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     // If the directory already exists, return success
     if (cforge_path_is_directory(path)) {
         return CFORGE_FS_SUCCESS;
     }
     
     if (!recursive) {
         // Simple case: try to create the directory
 #ifdef _WIN32
         if (_mkdir(path->data) != 0) {
             return CFORGE_FS_IO_ERROR;
         }
 #else
         if (mkdir(path->data, 0755) != 0) {
             return CFORGE_FS_IO_ERROR;
         }
 #endif
         return CFORGE_FS_SUCCESS;
     }
     
     // Recursive case
     cforge_string_t path_copy = strdup(path->data);
     if (!path_copy) {
         return CFORGE_FS_UNKNOWN_ERROR;
     }
     
     // Create directories one by one
     for (cforge_string_t p = path_copy + 1; *p; p++) {
         if (*p == PATH_SEPARATOR) {
             *p = '\0';  // Temporarily terminate the string
             
             cforge_path_t tmp_path;
             if (cforge_path_init(&tmp_path, path_copy) != CFORGE_FS_SUCCESS) {
                 free(path_copy);
                 return CFORGE_FS_UNKNOWN_ERROR;
             }
             
             if (!cforge_path_exists(&tmp_path) || !cforge_path_is_directory(&tmp_path)) {
 #ifdef _WIN32
                 if (_mkdir(path_copy) != 0 && errno != EEXIST) {
                     cforge_path_free(&tmp_path);
                     free(path_copy);
                     return CFORGE_FS_IO_ERROR;
                 }
 #else
                 if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                     cforge_path_free(&tmp_path);
                     free(path_copy);
                     return CFORGE_FS_IO_ERROR;
                 }
 #endif
             }
             
             cforge_path_free(&tmp_path);
             *p = PATH_SEPARATOR;  // Restore the path separator
         }
     }
     
     // Finally create the target directory
 #ifdef _WIN32
     if (_mkdir(path_copy) != 0 && errno != EEXIST) {
         free(path_copy);
         return CFORGE_FS_IO_ERROR;
     }
 #else
     if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
         free(path_copy);
         return CFORGE_FS_IO_ERROR;
     }
 #endif
     
     free(path_copy);
     return CFORGE_FS_SUCCESS;
 }
 
 cforge_fs_error_t cforge_remove_file(const cforge_path_t* path) {
     if (!path || !path->data) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     if (!cforge_path_exists(path)) {
         return CFORGE_FS_NOT_FOUND;
     }
     
     if (cforge_path_is_directory(path)) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     if (remove(path->data) != 0) {
         return CFORGE_FS_IO_ERROR;
     }
     
     return CFORGE_FS_SUCCESS;
 }
 
 // Helper function for recursive directory removal
 static cforge_fs_error_t remove_dir_recursive(cforge_cstring_t dir_path) {
 #ifdef _WIN32
     WIN32_FIND_DATAA find_data;
     HANDLE find_handle;
     char path[MAX_PATH];
     
     // Create a search pattern
     snprintf(path, MAX_PATH, "%s\\*", dir_path);
     find_handle = FindFirstFileA(path, &find_data);
     
     if (find_handle == INVALID_HANDLE_VALUE) {
         return CFORGE_FS_IO_ERROR;
     }
     
     do {
         // Skip "." and ".."
         if (strcmp(find_data.cFileName, ".") == 0 || 
             strcmp(find_data.cFileName, "..") == 0) {
             continue;
         }
         
         // Build the full path
         snprintf(path, MAX_PATH, "%s\\%s", dir_path, find_data.cFileName);
         
         if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
             // Recursively remove subdirectory
             cforge_fs_error_t err = remove_dir_recursive(path);
             if (err != CFORGE_FS_SUCCESS) {
                 FindClose(find_handle);
                 return err;
             }
         } else {
             // Remove file
             if (!DeleteFileA(path)) {
                 FindClose(find_handle);
                 return CFORGE_FS_IO_ERROR;
             }
         }
     } while (FindNextFileA(find_handle, &find_data));
     
     FindClose(find_handle);
     
     // Finally remove the directory itself
     if (!RemoveDirectoryA(dir_path)) {
         return CFORGE_FS_IO_ERROR;
     }
     
     return CFORGE_FS_SUCCESS;
 #else
     DIR* dir = opendir(dir_path);
     if (!dir) {
         return CFORGE_FS_IO_ERROR;
     }
     
     struct dirent* entry;
     char path[PATH_MAX];
     
     while ((entry = readdir(dir)) != NULL) {
         // Skip "." and ".."
         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
             continue;
         }
         
         // Build the full path
         snprintf(path, PATH_MAX, "%s/%s", dir_path, entry->d_name);
         
         struct stat st;
         if (stat(path, &st) == -1) {
             closedir(dir);
             return CFORGE_FS_IO_ERROR;
         }
         
         if (S_ISDIR(st.st_mode)) {
             // Recursively remove subdirectory
             cforge_fs_error_t err = remove_dir_recursive(path);
             if (err != CFORGE_FS_SUCCESS) {
                 closedir(dir);
                 return err;
             }
         } else {
             // Remove file
             if (unlink(path) != 0) {
                 closedir(dir);
                 return CFORGE_FS_IO_ERROR;
             }
         }
     }
     
     closedir(dir);
     
     // Finally remove the directory itself
     if (rmdir(dir_path) != 0) {
         return CFORGE_FS_IO_ERROR;
     }
     
     return CFORGE_FS_SUCCESS;
 #endif
 }
 
 cforge_fs_error_t cforge_remove_directory(const cforge_path_t* path, bool recursive) {
     if (!path || !path->data) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     if (!cforge_path_exists(path)) {
         return CFORGE_FS_NOT_FOUND;
     }
     
     if (!cforge_path_is_directory(path)) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     if (recursive) {
         return remove_dir_recursive(path->data);
     } else {
         // Non-recursive directory removal
 #ifdef _WIN32
         if (!RemoveDirectoryA(path->data)) {
             return CFORGE_FS_IO_ERROR;
         }
 #else
         if (rmdir(path->data) != 0) {
             return CFORGE_FS_IO_ERROR;
         }
 #endif
         return CFORGE_FS_SUCCESS;
     }
 }
 
 cforge_fs_error_t cforge_read_file(const cforge_path_t* path, cforge_string_t* buffer, cforge_size_t* size) {
     if (!path || !path->data || !buffer || !size) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     FILE* file = fopen(path->data, "rb");
     if (!file) {
         return CFORGE_FS_NOT_FOUND;
     }
     
     // Get file size
     fseek(file, 0, SEEK_END);
     long file_size = ftell(file);
     fseek(file, 0, SEEK_SET);
     
     if (file_size < 0) {
         fclose(file);
         return CFORGE_FS_IO_ERROR;
     }
     
     // Allocate buffer
     *buffer = (cforge_string_t)malloc(file_size + 1);
     if (!*buffer) {
         fclose(file);
         return CFORGE_FS_UNKNOWN_ERROR;
     }
     
     // Read file content
     cforge_size_t read_size = fread(*buffer, 1, file_size, file);
     if (read_size != (cforge_size_t)file_size) {
         free(*buffer);
         fclose(file);
         return CFORGE_FS_IO_ERROR;
     }
     
     // Null-terminate the buffer
     (*buffer)[file_size] = '\0';
     *size = file_size;
     
     fclose(file);
     return CFORGE_FS_SUCCESS;
 }
 
 cforge_fs_error_t cforge_write_file(const cforge_path_t* path, cforge_cstring_t content, cforge_size_t size) {
     if (!path || !path->data || !content) {
         return CFORGE_FS_INVALID_PATH;
     }
     
     FILE* file = fopen(path->data, "wb");
     if (!file) {
         return CFORGE_FS_IO_ERROR;
     }
     
     // Write content
     cforge_size_t write_size = fwrite(content, 1, size, file);
     if (write_size != size) {
         fclose(file);
         return CFORGE_FS_IO_ERROR;
     }
     
     fclose(file);
     return CFORGE_FS_SUCCESS;
 }
 
 cforge_cstring_t cforge_fs_error_str(cforge_fs_error_t error) {
     switch (error) {
         case CFORGE_FS_SUCCESS:
             return "Success";
         case CFORGE_FS_NOT_FOUND:
             return "Path not found";
         case CFORGE_FS_ACCESS_DENIED:
             return "Access denied";
         case CFORGE_FS_IO_ERROR:
             return "I/O error";
         case CFORGE_FS_INVALID_PATH:
             return "Invalid path";
         case CFORGE_FS_UNKNOWN_ERROR:
         default:
             return "Unknown error";
     }
 }

 #ifdef __cplusplus
    }
    #endif