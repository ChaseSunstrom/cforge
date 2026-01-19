/**
 * @file remote_cache.cpp
 * @brief Remote cache client implementation
 */

#include "core/remote_cache.hpp"
#include "cforge/log.hpp"
#include "core/types.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace cforge {

// ============================================================================
// remote_cache_config implementation
// ============================================================================

static std::filesystem::path get_global_config_path() {
#ifdef _WIN32
  char path[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
    return std::filesystem::path(path) / "cforge" / "config.toml";
  }
  const char *userprofile = std::getenv("USERPROFILE");
  if (userprofile) {
    return std::filesystem::path(userprofile) / ".cforge" / "config.toml";
  }
  return ".cforge/config.toml";
#else
  // Use XDG_CONFIG_HOME for config files (XDG spec)
  const char *xdg_config = std::getenv("XDG_CONFIG_HOME");
  if (xdg_config) {
    return std::filesystem::path(xdg_config) / "cforge" / "config.toml";
  }
  const char *home = std::getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    home = pw ? pw->pw_dir : ".";
  }
  // Default to ~/.config/cforge for config (XDG compliant)
  return std::filesystem::path(home) / ".config" / "cforge" / "config.toml";
#endif
}

static std::string trim(const std::string &str) {
  auto start = str.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  auto end = str.find_last_not_of(" \t\r\n");
  return str.substr(start, end - start + 1);
}

remote_cache_config remote_cache_config::load_from_global_config() {
  remote_cache_config config;
  auto config_path = get_global_config_path();

  if (!std::filesystem::exists(config_path)) {
    return config;  // Return default (disabled)
  }

  std::ifstream file(config_path);
  if (!file.is_open()) {
    return config;
  }

  std::string line;
  std::string current_section;

  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;

    // Section header
    if (line[0] == '[' && line.back() == ']') {
      current_section = line.substr(1, line.length() - 2);
      continue;
    }

    // Key-value pair
    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) continue;

    std::string key = trim(line.substr(0, eq_pos));
    std::string value = trim(line.substr(eq_pos + 1));

    // Remove quotes
    if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.length() - 2);
    }

    if (current_section == "cache.remote" || current_section == "cache") {
      if (key == "enabled" && current_section == "cache.remote") {
        config.enabled = (value == "true" || value == "1");
      } else if (key == "url") {
        config.url = value;
      } else if (key == "push") {
        config.push_enabled = (value == "true" || value == "1");
      } else if (key == "api_key") {
        config.api_key = value;
      } else if (key == "timeout") {
        try {
          config.timeout_seconds = std::stoi(value);
        } catch (...) {}
      }
    }
  }

  return config;
}

// ============================================================================
// remote_cache implementation
// ============================================================================

remote_cache::remote_cache(const remote_cache_config &config)
    : config_(config) {}

std::string remote_cache::get_package_url(const cache_key &key) const {
  std::string url = config_.url;
  if (!url.empty() && url.back() != '/') {
    url += '/';
  }
  return url + "cache/" + key.to_string() + ".tar.gz";
}

bool remote_cache::is_available() const {
  return config_.is_valid() && http_client::is_available();
}

bool remote_cache::test_connection() const {
  if (!is_available()) {
    return false;
  }

  http_request_options opts;
  opts.timeout_seconds = 5;
  opts.api_key = config_.api_key;

  // Try to reach the stats endpoint
  std::string stats_url = config_.url;
  if (!stats_url.empty() && stats_url.back() != '/') {
    stats_url += '/';
  }
  stats_url += "stats";

  auto response = http_.get(stats_url, opts);
  return response && response->ok();
}

bool remote_cache::has(const cache_key &key) const {
  if (!is_available()) {
    return false;
  }

  http_request_options opts;
  opts.timeout_seconds = config_.timeout_seconds;
  opts.api_key = config_.api_key;

  auto response = http_.head(get_package_url(key), opts);
  return response && response->ok();
}

bool remote_cache::create_archive(const std::filesystem::path &source,
                                  const std::filesystem::path &dest) {
  // Use system tar command for portability
  std::stringstream cmd;

#ifdef _WIN32
  // On Windows, try to use tar if available (Windows 10+)
  cmd << "tar -czf \"" << dest.string() << "\" -C \""
      << source.parent_path().string() << "\" \""
      << source.filename().string() << "\"";
#else
  cmd << "tar -czf '" << dest.string() << "' -C '"
      << source.parent_path().string() << "' '"
      << source.filename().string() << "'";
#endif

  return system(cmd.str().c_str()) == 0;
}

bool remote_cache::extract_archive(const std::filesystem::path &source,
                                   const std::filesystem::path &dest) {
  // Create destination directory
  std::filesystem::create_directories(dest);

  std::stringstream cmd;

#ifdef _WIN32
  cmd << "tar -xzf \"" << source.string() << "\" -C \"" << dest.string() << "\"";
#else
  cmd << "tar -xzf '" << source.string() << "' -C '" << dest.string() << "'";
#endif

  return system(cmd.str().c_str()) == 0;
}

bool remote_cache::fetch(const cache_key &key,
                         const std::filesystem::path &dest,
                         std::function<void(cforge_size_t, cforge_size_t)> progress) {
  if (!is_available()) {
    last_error_ = "Remote cache not available";
    return false;
  }

  // Create temp file for download
  auto temp_file = std::filesystem::temp_directory_path() /
                   ("cforge_cache_" + key.to_string() + ".tar.gz");

  http_request_options opts;
  opts.timeout_seconds = config_.timeout_seconds;
  opts.api_key = config_.api_key;

  if (progress) {
    opts.progress_callback = [&progress](cforge_size_t downloaded, cforge_size_t total) {
      progress(downloaded, total);
      return true;  // Continue download
    };
  }

  // Download archive
  if (!http_.download_file(get_package_url(key), temp_file, opts)) {
    last_error_ = "Failed to download: " + http_.last_error();
    return false;
  }

  // Extract archive
  if (!extract_archive(temp_file, dest)) {
    last_error_ = "Failed to extract archive";
    std::filesystem::remove(temp_file);
    return false;
  }

  // Clean up
  std::filesystem::remove(temp_file);
  return true;
}

bool remote_cache::push(const cache_key &key,
                        const std::filesystem::path &source,
                        std::function<void(cforge_size_t, cforge_size_t)> progress) {
  if (!can_push()) {
    last_error_ = "Remote cache push not enabled";
    return false;
  }

  // Create temp file for archive
  auto temp_file = std::filesystem::temp_directory_path() /
                   ("cforge_cache_" + key.to_string() + ".tar.gz");

  // Create archive
  if (!create_archive(source, temp_file)) {
    last_error_ = "Failed to create archive";
    return false;
  }

  http_request_options opts;
  opts.timeout_seconds = config_.timeout_seconds * 2;  // More time for uploads
  opts.api_key = config_.api_key;

  if (progress) {
    opts.progress_callback = [&progress](cforge_size_t uploaded, cforge_size_t total) {
      progress(uploaded, total);
      return true;
    };
  }

  // Upload archive
  bool success = http_.upload_file(get_package_url(key), temp_file, opts);

  // Clean up
  std::filesystem::remove(temp_file);

  if (!success) {
    last_error_ = "Failed to upload: " + http_.last_error();
  }

  return success;
}

std::optional<remote_cache_stats> remote_cache::stats() const {
  if (!is_available()) {
    return std::nullopt;
  }

  std::string stats_url = config_.url;
  if (!stats_url.empty() && stats_url.back() != '/') {
    stats_url += '/';
  }
  stats_url += "stats";

  http_request_options opts;
  opts.timeout_seconds = config_.timeout_seconds;
  opts.api_key = config_.api_key;

  auto response = http_.get(stats_url, opts);
  if (!response || !response->ok()) {
    return std::nullopt;
  }

  // Parse simple JSON response
  // Expected format: {"total_packages": 123, "total_size": 456789, ...}
  remote_cache_stats result{};
  std::string body = response->body_string();

  auto parse_value = [&body](const std::string &key) -> cforge_size_t {
    auto pos = body.find("\"" + key + "\"");
    if (pos == std::string::npos) return 0;
    pos = body.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) pos++;
    try {
      return std::stoull(body.substr(pos));
    } catch (...) {
      return 0;
    }
  };

  result.total_packages = parse_value("total_packages");
  result.total_size_bytes = parse_value("total_size");
  result.downloads = parse_value("downloads");
  result.uploads = parse_value("uploads");

  return result;
}

// ============================================================================
// unified_cache implementation
// ============================================================================

unified_cache::unified_cache(package_cache &local_cache,
                             const remote_cache_config &remote_config)
    : local_(local_cache), remote_(remote_config) {}

bool unified_cache::has(const cache_key &key) const {
  // Check local first
  if (local_.has(key)) {
    return true;
  }

  // Then check remote
  if (remote_.is_available() && remote_.has(key)) {
    return true;
  }

  return false;
}

bool unified_cache::get(const cache_key &key, const std::filesystem::path &dest) {
  // Try local cache first
  if (local_.has(key)) {
    local_.record_hit();
    return local_.restore(key, dest);
  }

  local_.record_miss();

  // Try remote cache
  if (remote_.is_available()) {
    if (remote_.has(key)) {
      local_.record_remote_hit();

      // Download to local cache first
      auto local_path = local_.cache_dir() / "packages" / key.package / key.directory_name();
      if (remote_.fetch(key, local_path)) {
        // Generate manifest for the downloaded package
        // (The manifest should be included in the archive)
        return local_.restore(key, dest);
      }
    } else {
      local_.record_remote_miss();
    }
  }

  return false;
}

bool unified_cache::store(const cache_key &key, const std::filesystem::path &build_output) {
  // Store in local cache
  if (!local_.store(key, build_output)) {
    return false;
  }

  // Optionally push to remote
  if (remote_.can_push()) {
    auto local_path = local_.cache_dir() / "packages" / key.package / key.directory_name();
    remote_.push(key, local_path);  // Best effort, don't fail if push fails
  }

  return true;
}

cache_stats unified_cache::stats() const {
  return local_.stats();
}

} // namespace cforge
