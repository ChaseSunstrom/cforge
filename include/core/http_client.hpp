/**
 * @file http_client.hpp
 * @brief Simple HTTP client for remote cache operations
 *
 * Provides a platform-agnostic HTTP client using WinHTTP on Windows
 * and libcurl on Unix/macOS.
 */

#ifndef CFORGE_HTTP_CLIENT_HPP
#define CFORGE_HTTP_CLIENT_HPP

#include "core/types.h"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief HTTP response structure
 */
struct http_response {
  int status_code;
  std::string status_message;
  std::map<std::string, std::string> headers;
  std::vector<char> body;

  bool ok() const { return status_code >= 200 && status_code < 300; }
  std::string body_string() const { return std::string(body.begin(), body.end()); }
};

/**
 * @brief HTTP request options
 */
struct http_request_options {
  std::map<std::string, std::string> headers;
  int timeout_seconds = 30;
  bool follow_redirects = true;
  std::string api_key;  // Added to Authorization header if set

  // Progress callback: (downloaded, total) -> bool (return false to cancel)
  std::function<bool(cforge_size_t, cforge_size_t)> progress_callback;
};

/**
 * @brief Simple HTTP client
 *
 * Platform-specific implementation:
 * - Windows: WinHTTP
 * - Unix/macOS: libcurl (if available) or fallback to system curl command
 */
class http_client {
public:
  http_client();
  ~http_client();

  /**
   * @brief Perform a GET request
   * @param url URL to fetch
   * @param options Request options
   * @return HTTP response, or nullopt on error
   */
  std::optional<http_response> get(const std::string &url,
                                   const http_request_options &options = {});

  /**
   * @brief Perform a HEAD request (check if resource exists)
   * @param url URL to check
   * @param options Request options
   * @return HTTP response, or nullopt on error
   */
  std::optional<http_response> head(const std::string &url,
                                    const http_request_options &options = {});

  /**
   * @brief Perform a PUT request (upload data)
   * @param url URL to upload to
   * @param body Request body
   * @param content_type Content type header
   * @param options Request options
   * @return HTTP response, or nullopt on error
   */
  std::optional<http_response> put(const std::string &url,
                                   const std::vector<char> &body,
                                   const std::string &content_type,
                                   const http_request_options &options = {});

  /**
   * @brief Download a file to disk
   * @param url URL to download
   * @param dest_path Destination file path
   * @param options Request options
   * @return true if download succeeded
   */
  bool download_file(const std::string &url,
                     const std::filesystem::path &dest_path,
                     const http_request_options &options = {});

  /**
   * @brief Upload a file from disk
   * @param url URL to upload to
   * @param source_path Source file path
   * @param options Request options
   * @return true if upload succeeded
   */
  bool upload_file(const std::string &url,
                   const std::filesystem::path &source_path,
                   const http_request_options &options = {});

  /**
   * @brief Get the last error message
   */
  std::string last_error() const { return last_error_; }

  /**
   * @brief Check if HTTP support is available
   *
   * On some systems, libcurl may not be installed.
   */
  static bool is_available();

private:
  std::string last_error_;

  // Platform-specific implementation
  std::optional<http_response> perform_request(const std::string &method,
                                               const std::string &url,
                                               const std::vector<char> &body,
                                               const http_request_options &options);
};

/**
 * @brief URL parsing utilities
 */
struct parsed_url {
  std::string scheme;   // "https"
  std::string host;     // "cache.example.com"
  int port;             // 443
  std::string path;     // "/cache/pkg-1.0.0"
  std::string query;    // "foo=bar"

  static std::optional<parsed_url> parse(const std::string &url);
  std::string to_string() const;
};

} // namespace cforge

#endif // CFORGE_HTTP_CLIENT_HPP
