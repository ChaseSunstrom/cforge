/**
 * @file http_client.cpp
 * @brief HTTP client implementation
 */

#include "core/http_client.hpp"
#include "core/types.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

// Use WinHTTP only on MSVC (native Windows), curl everywhere else (Unix, MinGW)
#if defined(_WIN32) && defined(_MSC_VER)
#define CFORGE_USE_WINHTTP 1
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#define CFORGE_USE_CURL 1
#include <cstdlib>
#ifdef _WIN32
// MinGW on Windows
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#endif

namespace cforge {

// ============================================================================
// URL parsing
// ============================================================================

std::optional<parsed_url> parsed_url::parse(const std::string &url) {
  // Simple URL parser: scheme://host[:port]/path[?query]
  std::regex url_regex(R"(^(https?)://([^/:]+)(?::(\d+))?(/[^?]*)?(\?.*)?$)");
  std::smatch match;

  if (!std::regex_match(url, match, url_regex)) {
    return std::nullopt;
  }

  parsed_url result;
  result.scheme = match[1];
  result.host = match[2];
  result.port = match[3].matched ? std::stoi(match[3]) : (result.scheme == "https" ? 443 : 80);
  result.path = match[4].matched ? std::string(match[4]) : "/";
  result.query = match[5].matched ? std::string(match[5]).substr(1) : "";

  return result;
}

std::string parsed_url::to_string() const {
  std::stringstream ss;
  ss << scheme << "://" << host;
  if ((scheme == "https" && port != 443) || (scheme == "http" && port != 80)) {
    ss << ":" << port;
  }
  ss << path;
  if (!query.empty()) {
    ss << "?" << query;
  }
  return ss.str();
}

// ============================================================================
// http_client implementation
// ============================================================================

http_client::http_client() = default;
http_client::~http_client() = default;

#ifdef CFORGE_USE_WINHTTP

// Windows MSVC implementation using WinHTTP

bool http_client::is_available() {
  return true;  // WinHTTP is always available on Windows
}

std::optional<http_response> http_client::perform_request(
    const std::string &method,
    const std::string &url,
    const std::vector<char> &body,
    const http_request_options &options) {

  auto parsed = parsed_url::parse(url);
  if (!parsed) {
    last_error_ = "Invalid URL: " + url;
    return std::nullopt;
  }

  // Convert strings to wide strings for WinHTTP
  auto to_wstring = [](const std::string &s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    ws.resize(len - 1);  // Remove null terminator
    return ws;
  };

  // Initialize WinHTTP
  HINTERNET session = WinHttpOpen(L"cforge/1.0",
                                  WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    last_error_ = "Failed to initialize WinHTTP";
    return std::nullopt;
  }

  // Set timeouts
  int timeout_ms = options.timeout_seconds * 1000;
  WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  // Connect to server
  std::wstring host_w = to_wstring(parsed->host);
  HINTERNET connect = WinHttpConnect(session, host_w.c_str(),
                                     static_cast<INTERNET_PORT>(parsed->port), 0);
  if (!connect) {
    last_error_ = "Failed to connect to " + parsed->host;
    WinHttpCloseHandle(session);
    return std::nullopt;
  }

  // Create request
  std::wstring path_w = to_wstring(parsed->path + (parsed->query.empty() ? "" : "?" + parsed->query));
  std::wstring method_w = to_wstring(method);

  DWORD flags = (parsed->scheme == "https") ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(connect, method_w.c_str(), path_w.c_str(),
                                         nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!request) {
    last_error_ = "Failed to create request";
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return std::nullopt;
  }

  // Add headers
  std::wstring headers;
  for (const auto &[key, value] : options.headers) {
    headers += to_wstring(key + ": " + value + "\r\n");
  }
  if (!options.api_key.empty()) {
    headers += to_wstring("Authorization: Bearer " + options.api_key + "\r\n");
  }

  if (!headers.empty()) {
    WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD);
  }

  // Send request
  BOOL success = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
                                    static_cast<DWORD>(body.size()),
                                    static_cast<DWORD>(body.size()), 0);
  if (!success) {
    last_error_ = "Failed to send request";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return std::nullopt;
  }

  // Receive response
  success = WinHttpReceiveResponse(request, nullptr);
  if (!success) {
    last_error_ = "Failed to receive response";
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return std::nullopt;
  }

  // Get status code
  DWORD status_code = 0;
  DWORD size = sizeof(status_code);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX);

  http_response response;
  response.status_code = static_cast<int>(status_code);

  // Read response body
  std::vector<char> buffer(8192);
  DWORD bytes_read = 0;
  while (WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read)) {
    if (bytes_read == 0) break;
    response.body.insert(response.body.end(), buffer.begin(), buffer.begin() + bytes_read);

    if (options.progress_callback) {
      if (!options.progress_callback(response.body.size(), 0)) {
        last_error_ = "Download cancelled";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return std::nullopt;
      }
    }
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);

  return response;
}

#else  // CFORGE_USE_CURL

// Unix/macOS/MinGW implementation using system curl command

bool http_client::is_available() {
  // Check if curl is installed
#ifdef _WIN32
  // On MinGW, check for curl.exe
  return system("where curl > nul 2>&1") == 0;
#else
  return system("which curl > /dev/null 2>&1") == 0;
#endif
}

std::optional<http_response> http_client::perform_request(
    const std::string &method,
    const std::string &url,
    const std::vector<char> &body,
    const http_request_options &options) {

  if (!is_available()) {
    last_error_ = "curl not found. Install curl to enable remote cache.";
    return std::nullopt;
  }

  // Build curl command
  std::stringstream cmd;
  cmd << "curl -s -S -w '\\n%{http_code}' -X " << method;

  // Add timeout
  cmd << " --connect-timeout " << options.timeout_seconds;
  cmd << " --max-time " << (options.timeout_seconds * 2);

  // Add headers
  for (const auto &[key, value] : options.headers) {
    cmd << " -H '" << key << ": " << value << "'";
  }
  if (!options.api_key.empty()) {
    cmd << " -H 'Authorization: Bearer " << options.api_key << "'";
  }

  // Handle request body
  std::string temp_file;
  if (!body.empty()) {
#ifdef _WIN32
    // MinGW on Windows - use temp directory
    char temp_path[MAX_PATH];
    GetTempPathA(MAX_PATH, temp_path);
    temp_file = std::string(temp_path) + "cforge_http_body_" + std::to_string(getpid());
#else
    temp_file = "/tmp/cforge_http_body_" + std::to_string(getpid());
#endif
    std::ofstream out(temp_file, std::ios::binary);
    out.write(body.data(), body.size());
    out.close();
    cmd << " --data-binary @\"" << temp_file << "\"";
  }

  // Follow redirects
  if (options.follow_redirects) {
    cmd << " -L";
  }

  cmd << " '" << url << "' 2>&1";

  // Execute curl
  FILE *pipe = popen(cmd.str().c_str(), "r");
  if (!pipe) {
    last_error_ = "Failed to execute curl";
    if (!temp_file.empty()) std::remove(temp_file.c_str());
    return std::nullopt;
  }

  // Read output
  std::vector<char> output;
  char buffer[4096];
  while (size_t n = fread(buffer, 1, sizeof(buffer), pipe)) {
    output.insert(output.end(), buffer, buffer + n);
  }
  int exit_code = pclose(pipe);

  // Clean up temp file
  if (!temp_file.empty()) {
    std::remove(temp_file.c_str());
  }

  if (exit_code != 0) {
    last_error_ = "curl failed with exit code " + std::to_string(exit_code);
    return std::nullopt;
  }

  // Parse output: body + newline + status_code
  std::string output_str(output.begin(), output.end());
  auto last_newline = output_str.rfind('\n');
  if (last_newline == std::string::npos) {
    last_error_ = "Invalid curl output";
    return std::nullopt;
  }

  http_response response;
  try {
    response.status_code = std::stoi(output_str.substr(last_newline + 1));
  } catch (...) {
    last_error_ = "Failed to parse status code";
    return std::nullopt;
  }

  response.body.assign(output.begin(), output.begin() + last_newline);
  return response;
}

#endif

std::optional<http_response> http_client::get(const std::string &url,
                                               const http_request_options &options) {
  return perform_request("GET", url, {}, options);
}

std::optional<http_response> http_client::head(const std::string &url,
                                                const http_request_options &options) {
  return perform_request("HEAD", url, {}, options);
}

std::optional<http_response> http_client::put(const std::string &url,
                                               const std::vector<char> &body,
                                               const std::string &content_type,
                                               const http_request_options &options) {
  auto opts = options;
  opts.headers["Content-Type"] = content_type;
  return perform_request("PUT", url, body, opts);
}

bool http_client::download_file(const std::string &url,
                                 const std::filesystem::path &dest_path,
                                 const http_request_options &options) {
  auto response = get(url, options);
  if (!response || !response->ok()) {
    return false;
  }

  // Create parent directories
  std::filesystem::create_directories(dest_path.parent_path());

  // Write to file
  std::ofstream out(dest_path, std::ios::binary);
  if (!out) {
    last_error_ = "Failed to create file: " + dest_path.string();
    return false;
  }

  out.write(response->body.data(), response->body.size());
  return true;
}

bool http_client::upload_file(const std::string &url,
                               const std::filesystem::path &source_path,
                               const http_request_options &options) {
  // Read file
  std::ifstream in(source_path, std::ios::binary);
  if (!in) {
    last_error_ = "Failed to read file: " + source_path.string();
    return false;
  }

  std::vector<char> body((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());

  auto response = put(url, body, "application/octet-stream", options);
  return response && response->ok();
}

} // namespace cforge
