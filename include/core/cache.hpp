/**
 * @file cache.hpp
 * @brief Binary package cache for cforge dependencies
 *
 * Provides local and remote caching of built dependency artifacts to
 * dramatically reduce build times by avoiding redundant compilation.
 */

#ifndef CFORGE_CACHE_HPP
#define CFORGE_CACHE_HPP

#include "core/config_resolver.hpp"
#include "core/dependency_hash.hpp"
#include "core/types.h"

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Cache key uniquely identifying a built package artifact
 *
 * The cache key encodes all factors that affect the binary output:
 * package identity, target environment, and build configuration.
 */
struct cache_key {
  std::string package;       // Package name (e.g., "fmt")
  std::string version;       // Package version (e.g., "11.1.4")
  std::string platform;      // Target platform: windows, linux, macos
  std::string compiler;      // Compiler: msvc, gcc, clang, apple_clang, mingw
  std::string compiler_ver;  // Compiler version (e.g., "19.40", "13.2")
  std::string config;        // Build config: Debug, Release, RelWithDebInfo
  std::string arch;          // Architecture: x64, x86, arm64
  int cpp_standard;          // C++ standard: 11, 14, 17, 20, 23
  std::string options_hash;  // FNV-1a hash of CMake options/defines

  /**
   * @brief Generate the full cache key string
   * @return Key in format: "pkg-ver-platform-compiler-ver-config-arch-cppXX-hash"
   */
  std::string to_string() const;

  /**
   * @brief Parse a cache key string back into components
   * @param key_str The cache key string
   * @return Parsed cache_key, or nullopt if invalid
   */
  static std::optional<cache_key> from_string(const std::string &key_str);

  /**
   * @brief Get the cache directory name for this key
   * @return Directory name (same as to_string())
   */
  std::string directory_name() const { return to_string(); }

  bool operator==(const cache_key &other) const;
  bool operator<(const cache_key &other) const;
};

/**
 * @brief Information about a cached package entry
 */
struct cache_entry {
  cache_key key;
  std::filesystem::path path;
  std::chrono::system_clock::time_point created;
  std::chrono::system_clock::time_point last_accessed;
  cforge_size_t size_bytes;
  bool valid;  // Manifest and files verified
};

/**
 * @brief Cache statistics
 */
struct cache_stats {
  cforge_size_t total_entries;
  cforge_size_t total_size_bytes;
  cforge_size_t cache_hits;
  cforge_size_t cache_misses;
  cforge_size_t remote_hits;
  cforge_size_t remote_misses;

  double hit_rate() const {
    auto total = cache_hits + cache_misses;
    return total > 0 ? static_cast<double>(cache_hits) / total : 0.0;
  }
};

/**
 * @brief Manifest for a cached package
 *
 * Stored as manifest.toml in each cache entry directory.
 */
struct cache_manifest {
  // Metadata
  std::string package;
  std::string version;
  std::string cache_key;
  std::string created;  // ISO 8601 timestamp

  // Build environment
  std::string platform;
  std::string compiler;
  std::string compiler_version;
  std::string config;
  std::string arch;
  int cpp_standard;

  // Files with checksums
  struct file_entry {
    std::string path;       // Relative path within cache entry
    cforge_size_t size;
    std::string checksum;   // FNV-1a hash
  };
  std::vector<file_entry> files;

  /**
   * @brief Load manifest from file
   */
  static std::optional<cache_manifest>
  load(const std::filesystem::path &manifest_path);

  /**
   * @brief Save manifest to file
   */
  bool save(const std::filesystem::path &manifest_path) const;

  /**
   * @brief Verify all files exist and have correct checksums
   */
  bool verify(const std::filesystem::path &cache_entry_dir) const;
};

/**
 * @brief Build environment information for generating cache keys
 */
struct build_environment {
  platform platform;
  compiler compiler;
  std::string compiler_version;
  std::string arch;
  int cpp_standard;

  /**
   * @brief Detect the current build environment
   */
  static build_environment detect();

  /**
   * @brief Get compiler version string (e.g., "19.40" for MSVC)
   */
  static std::string get_compiler_version();

  /**
   * @brief Get architecture string (e.g., "x64", "arm64")
   */
  static std::string get_arch();
};

/**
 * @brief Local package cache manager
 *
 * Manages the local cache directory structure:
 * ~/.cforge/cache/
 * ├── packages/
 * │   ├── fmt/
 * │   │   └── 11.1.4-windows-msvc19.40-release-x64-cpp17-a3f8c2b1/
 * │   │       ├── manifest.toml
 * │   │       ├── include/
 * │   │       ├── lib/
 * │   │       └── cmake/
 * │   └── ...
 * └── stats.toml
 */
class package_cache {
public:
  /**
   * @brief Constructor
   * @param cache_dir Cache directory (default: ~/.cforge/cache)
   */
  explicit package_cache(
      const std::filesystem::path &cache_dir = get_default_cache_dir());

  /**
   * @brief Check if a package is in the cache
   * @param key Cache key to look up
   * @return true if package is cached and valid
   */
  bool has(const cache_key &key) const;

  /**
   * @brief Get the path to a cached package
   * @param key Cache key to look up
   * @return Path to cache entry directory, or nullopt if not cached
   */
  std::optional<std::filesystem::path> get(const cache_key &key) const;

  /**
   * @brief Store a built package in the cache
   * @param key Cache key for the package
   * @param build_output Directory containing built artifacts (include/, lib/, etc.)
   * @return true if successfully cached
   */
  bool store(const cache_key &key, const std::filesystem::path &build_output);

  /**
   * @brief Restore a cached package to a destination directory
   * @param key Cache key to restore
   * @param dest Destination directory
   * @return true if successfully restored
   */
  bool restore(const cache_key &key, const std::filesystem::path &dest);

  /**
   * @brief Remove a specific package from the cache
   * @param key Cache key to remove
   * @return true if removed (or didn't exist)
   */
  bool remove(const cache_key &key);

  /**
   * @brief Remove all versions of a package from the cache
   * @param package_name Package name
   * @return Number of entries removed
   */
  cforge_size_t remove_package(const std::string &package_name);

  /**
   * @brief Remove old cache entries to stay within size limit
   * @param max_size_mb Maximum cache size in megabytes
   * @return Number of entries removed
   */
  cforge_size_t prune(cforge_size_t max_size_mb = 5000);

  /**
   * @brief Clear all cache entries
   * @return Number of entries removed
   */
  cforge_size_t clear();

  /**
   * @brief List all cached packages
   * @return Vector of cache entries
   */
  std::vector<cache_entry> list() const;

  /**
   * @brief List cached versions of a specific package
   * @param package_name Package name
   * @return Vector of cache entries for that package
   */
  std::vector<cache_entry> list_package(const std::string &package_name) const;

  /**
   * @brief Get cache statistics
   * @return Cache statistics
   */
  cache_stats stats() const;

  /**
   * @brief Record a cache hit (for statistics)
   */
  void record_hit();

  /**
   * @brief Record a cache miss (for statistics)
   */
  void record_miss();

  /**
   * @brief Record a remote cache hit (for statistics)
   */
  void record_remote_hit();

  /**
   * @brief Record a remote cache miss (for statistics)
   */
  void record_remote_miss();

  /**
   * @brief Get the cache directory path
   */
  std::filesystem::path cache_dir() const { return cache_dir_; }

  /**
   * @brief Get the default cache directory
   * @return ~/.cforge/cache on Unix, %LOCALAPPDATA%/cforge/cache on Windows
   */
  static std::filesystem::path get_default_cache_dir();

private:
  std::filesystem::path cache_dir_;
  std::filesystem::path packages_dir_;
  std::filesystem::path stats_file_;
  mutable cache_stats stats_;

  /**
   * @brief Get the path for a cache entry
   */
  std::filesystem::path get_entry_path(const cache_key &key) const;

  /**
   * @brief Load statistics from disk
   */
  void load_stats() const;

  /**
   * @brief Save statistics to disk
   */
  void save_stats() const;

  /**
   * @brief Calculate total cache size
   */
  cforge_size_t calculate_total_size() const;

  /**
   * @brief Copy directory recursively
   */
  static bool copy_directory(const std::filesystem::path &src,
                             const std::filesystem::path &dst);

  /**
   * @brief Generate manifest for a directory
   */
  static cache_manifest generate_manifest(const cache_key &key,
                                          const std::filesystem::path &dir);
};

/**
 * @brief Generate a cache key for a dependency
 * @param package_name Package name
 * @param version Package version
 * @param env Build environment
 * @param config Build configuration (Debug/Release)
 * @param cmake_options CMake options map
 * @return Generated cache key
 */
cache_key generate_cache_key(const std::string &package_name,
                             const std::string &version,
                             const build_environment &env,
                             const std::string &config,
                             const std::map<std::string, std::string> &cmake_options = {});

/**
 * @brief Hash a map of CMake options to a short string
 * @param options CMake options map
 * @return 8-character hex hash
 */
std::string hash_cmake_options(const std::map<std::string, std::string> &options);

} // namespace cforge

#endif // CFORGE_CACHE_HPP
