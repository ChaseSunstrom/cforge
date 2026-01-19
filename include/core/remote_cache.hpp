/**
 * @file remote_cache.hpp
 * @brief Remote binary cache client
 *
 * Provides HTTP-based access to a remote cache server for sharing
 * pre-built dependency artifacts across machines and CI pipelines.
 */

#ifndef CFORGE_REMOTE_CACHE_HPP
#define CFORGE_REMOTE_CACHE_HPP

#include "core/cache.hpp"
#include "core/http_client.hpp"
#include "core/types.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace cforge {

/**
 * @brief Remote cache configuration
 */
struct remote_cache_config {
  std::string url;           // Base URL (e.g., "https://cache.example.com/cforge")
  bool enabled = false;      // Whether remote cache is enabled
  bool push_enabled = false; // Whether to upload built packages
  std::string api_key;       // Optional authentication key
  int timeout_seconds = 60;  // Request timeout

  /**
   * @brief Load remote cache config from global config file
   * @return Loaded config, or default (disabled) config
   */
  static remote_cache_config load_from_global_config();

  /**
   * @brief Check if the configuration is valid
   */
  bool is_valid() const { return enabled && !url.empty(); }
};

/**
 * @brief Statistics for remote cache operations
 */
struct remote_cache_stats {
  cforge_size_t total_packages;
  cforge_size_t total_size_bytes;
  cforge_size_t downloads;
  cforge_size_t uploads;
};

/**
 * @brief Remote cache client
 *
 * Communicates with a remote cache server using a simple HTTP protocol:
 * - GET  /cache/{cache_key}.tar.gz  - Download package
 * - PUT  /cache/{cache_key}.tar.gz  - Upload package
 * - HEAD /cache/{cache_key}.tar.gz  - Check if package exists
 * - GET  /stats                     - Get server statistics
 */
class remote_cache {
public:
  /**
   * @brief Constructor
   * @param config Remote cache configuration
   */
  explicit remote_cache(const remote_cache_config &config);

  /**
   * @brief Check if a package exists on the remote cache
   * @param key Cache key to check
   * @return true if package exists on remote
   */
  bool has(const cache_key &key) const;

  /**
   * @brief Download a package from remote cache
   * @param key Cache key to download
   * @param dest Destination directory for extracted package
   * @param progress Progress callback (downloaded_bytes, total_bytes)
   * @return true if download and extraction succeeded
   */
  bool fetch(const cache_key &key,
             const std::filesystem::path &dest,
             std::function<void(cforge_size_t, cforge_size_t)> progress = nullptr);

  /**
   * @brief Upload a package to remote cache
   * @param key Cache key for the package
   * @param source Source directory to compress and upload
   * @param progress Progress callback (uploaded_bytes, total_bytes)
   * @return true if compression and upload succeeded
   */
  bool push(const cache_key &key,
            const std::filesystem::path &source,
            std::function<void(cforge_size_t, cforge_size_t)> progress = nullptr);

  /**
   * @brief Get remote cache statistics
   * @return Statistics, or nullopt if unavailable
   */
  std::optional<remote_cache_stats> stats() const;

  /**
   * @brief Test connection to remote cache server
   * @return true if server is reachable
   */
  bool test_connection() const;

  /**
   * @brief Check if remote cache is enabled and available
   */
  bool is_available() const;

  /**
   * @brief Check if push (upload) is enabled
   */
  bool can_push() const { return config_.push_enabled && is_available(); }

  /**
   * @brief Get the last error message
   */
  std::string last_error() const { return last_error_; }

  /**
   * @brief Get the remote cache URL
   */
  std::string url() const { return config_.url; }

private:
  remote_cache_config config_;
  mutable http_client http_;
  mutable std::string last_error_;

  /**
   * @brief Get the full URL for a cache key
   */
  std::string get_package_url(const cache_key &key) const;

  /**
   * @brief Create a tar.gz archive of a directory
   * @param source Source directory
   * @param dest Destination .tar.gz file
   * @return true if successful
   */
  static bool create_archive(const std::filesystem::path &source,
                             const std::filesystem::path &dest);

  /**
   * @brief Extract a tar.gz archive to a directory
   * @param source Source .tar.gz file
   * @param dest Destination directory
   * @return true if successful
   */
  static bool extract_archive(const std::filesystem::path &source,
                              const std::filesystem::path &dest);
};

/**
 * @brief Combined local + remote cache manager
 *
 * Provides a unified interface that checks local cache first,
 * then falls back to remote cache, with automatic population
 * of local cache from remote hits.
 */
class unified_cache {
public:
  /**
   * @brief Constructor
   * @param local_cache Local cache instance
   * @param remote_config Remote cache configuration (can be disabled)
   */
  unified_cache(package_cache &local_cache,
                const remote_cache_config &remote_config = {});

  /**
   * @brief Check if a package is available (local or remote)
   * @param key Cache key to check
   * @return true if package is available
   */
  bool has(const cache_key &key) const;

  /**
   * @brief Get a cached package, fetching from remote if needed
   * @param key Cache key to get
   * @param dest Destination directory to restore to
   * @return true if package was restored (from local or remote)
   */
  bool get(const cache_key &key, const std::filesystem::path &dest);

  /**
   * @brief Store a package in cache (local, and optionally remote)
   * @param key Cache key
   * @param build_output Directory containing built artifacts
   * @return true if stored successfully
   */
  bool store(const cache_key &key, const std::filesystem::path &build_output);

  /**
   * @brief Check if remote cache is enabled
   */
  bool has_remote() const { return remote_.is_available(); }

  /**
   * @brief Get combined cache statistics
   */
  cache_stats stats() const;

private:
  package_cache &local_;
  remote_cache remote_;
};

} // namespace cforge

#endif // CFORGE_REMOTE_CACHE_HPP
