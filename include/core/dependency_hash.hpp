#pragma once

#include "types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>

namespace cforge {

/**
 * @brief Class for tracking dependency states through hashing
 *
 * Stores build cache information in cforge.hash (TOML format) to enable
 * incremental builds by detecting when dependencies or config have changed.
 */
class dependency_hash {
public:
  dependency_hash() = default;
  ~dependency_hash() = default;

  /**
   * @brief Load dependency hashes from file
   * @param project_dir Project or workspace directory path
   * @return true if successful, false if file doesn't exist or read fails
   */
  bool load(const std::filesystem::path &project_dir);

  /**
   * @brief Save dependency hashes to file
   * @param project_dir Project or workspace directory path
   * @return true if successful
   */
  bool save(const std::filesystem::path &project_dir) const;

  /**
   * @brief Get hash for a dependency or config file
   * @param name Dependency or config name
   * @return Hash string if found, empty string if not found
   */
  std::string get_hash(const std::string &name) const;

  /**
   * @brief Set hash for a dependency or config file
   * @param name Dependency or config name
   * @param hash Hash value
   */
  void set_hash(const std::string &name, const std::string &hash);

  /**
   * @brief Get version for a dependency
   * @param name Dependency name
   * @return Version string if found, empty string if not found
   */
  std::string get_version(const std::string &name) const;

  /**
   * @brief Set version for a dependency
   * @param name Dependency name
   * @param version Version value (tag, branch, or commit)
   */
  void set_version(const std::string &name, const std::string &version);

  /**
   * @brief Calculate hash for a file's content
   * @param content File content
   * @return Hash string based on file content
   */
  std::string calculate_file_content_hash(const std::string &content) const {
    return hash_to_string(fnv1a_hash(content));
  }

  /**
   * @brief Calculate hash for a directory
   * @param dir_path Directory path
   * @return Hash string based on directory contents
   */
  static std::string
  calculate_directory_hash(const std::filesystem::path &dir_path);

  /**
   * @brief Check if hash file exists
   * @param project_dir Project or workspace directory
   * @return true if cforge.hash exists
   */
  static bool exists(const std::filesystem::path &project_dir) {
    return std::filesystem::exists(project_dir / HASH_FILE);
  }

  /**
   * @brief Clear all cached hashes
   */
  void clear() {
    hashes.clear();
    versions.clear();
  }

private:
  std::unordered_map<std::string, std::string> hashes;
  std::unordered_map<std::string, std::string> versions;
  static constexpr const char *HASH_FILE = "cforge.hash";

  // FNV-1a hash constants
  static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
  static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

  /**
   * @brief Calculate FNV-1a hash for a string
   * @param str Input string
   * @return 64-bit hash value
   */
  static uint64_t fnv1a_hash(const std::string &str);

  /**
   * @brief Calculate FNV-1a hash for binary data
   * @param data Pointer to data
   * @param size Size of data in bytes
   * @return 64-bit hash value
   */
  static uint64_t fnv1a_hash(const void *data, cforge_size_t size);

  /**
   * @brief Convert 64-bit hash to hex string
   * @param hash 64-bit hash value
   * @return Hex string representation
   */
  static std::string hash_to_string(uint64_t hash);

  /**
   * @brief Get current timestamp in ISO 8601 format
   */
  static std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
  }

  /**
   * @brief Trim whitespace from string
   */
  static std::string trim(const std::string &str) {
    cforge_size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
      return "";
    cforge_size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
  }
};

} // namespace cforge
