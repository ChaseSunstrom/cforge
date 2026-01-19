/**
 * @file cache.cpp
 * @brief Implementation of binary package cache
 */

#include "core/cache.hpp"
#include "cforge/log.hpp"
#include "core/types.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace cforge {

// FNV-1a hash constants (same as dependency_hash)
static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
static constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;

static uint64_t fnv1a_hash(const std::string &str) {
  uint64_t hash = FNV_OFFSET_BASIS;
  for (char c : str) {
    hash ^= static_cast<uint8_t>(c);
    hash *= FNV_PRIME;
  }
  return hash;
}

static uint64_t fnv1a_hash_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return 0;

  uint64_t hash = FNV_OFFSET_BASIS;
  char buffer[4096];
  while (file.read(buffer, sizeof(buffer))) {
    for (std::streamsize i = 0; i < file.gcount(); ++i) {
      hash ^= static_cast<uint8_t>(buffer[i]);
      hash *= FNV_PRIME;
    }
  }
  for (std::streamsize i = 0; i < file.gcount(); ++i) {
    hash ^= static_cast<uint8_t>(buffer[i]);
    hash *= FNV_PRIME;
  }
  return hash;
}

static std::string hash_to_hex(uint64_t hash, int digits = 8) {
  std::stringstream ss;
  ss << std::hex << std::setw(digits) << std::setfill('0') << (hash & ((1ULL << (digits * 4)) - 1));
  return ss.str();
}

static std::string get_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
  return ss.str();
}

static std::string trim(const std::string &str) {
  auto start = str.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  auto end = str.find_last_not_of(" \t\r\n");
  return str.substr(start, end - start + 1);
}

static std::string to_lower(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

// ============================================================================
// cache_key implementation
// ============================================================================

std::string cache_key::to_string() const {
  std::stringstream ss;
  ss << package << "-" << version << "-"
     << platform << "-" << compiler << compiler_ver << "-"
     << to_lower(config) << "-" << arch << "-"
     << "cpp" << cpp_standard << "-"
     << options_hash;
  return ss.str();
}

std::optional<cache_key> cache_key::from_string(const std::string &key_str) {
  // Format: pkg-ver-platform-compilerVer-config-arch-cppXX-hash
  // Example: fmt-11.1.4-windows-msvc19.40-release-x64-cpp17-a3f8c2b1
  std::regex pattern(
      R"(^([^-]+)-([^-]+)-([^-]+)-([a-z_]+)([0-9.]+)-([^-]+)-([^-]+)-cpp([0-9]+)-([a-f0-9]+)$)");
  std::smatch match;

  if (!std::regex_match(key_str, match, pattern)) {
    return std::nullopt;
  }

  cache_key key;
  key.package = match[1];
  key.version = match[2];
  key.platform = match[3];
  key.compiler = match[4];
  key.compiler_ver = match[5];
  key.config = match[6];
  key.arch = match[7];
  key.cpp_standard = std::stoi(match[8]);
  key.options_hash = match[9];

  return key;
}

bool cache_key::operator==(const cache_key &other) const {
  return to_string() == other.to_string();
}

bool cache_key::operator<(const cache_key &other) const {
  return to_string() < other.to_string();
}

// ============================================================================
// build_environment implementation
// ============================================================================

build_environment build_environment::detect() {
  build_environment env;
  env.target_platform = get_current_platform();
  env.target_compiler = detect_compiler();
  env.compiler_version = get_compiler_version();
  env.arch = get_arch();
  env.cpp_standard = __cplusplus >= 202002L ? 20 :
                     __cplusplus >= 201703L ? 17 :
                     __cplusplus >= 201402L ? 14 :
                     __cplusplus >= 201103L ? 11 : 11;
  return env;
}

std::string build_environment::get_compiler_version() {
#if defined(_MSC_VER)
  // MSVC version: _MSC_VER / 100 . _MSC_VER % 100
  int major = _MSC_VER / 100;
  int minor = _MSC_VER % 100;
  return std::to_string(major) + "." + std::to_string(minor);
#elif defined(__clang__)
  return std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
  return std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
  return "unknown";
#endif
}

std::string build_environment::get_arch() {
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
  return "x64";
#elif defined(_M_IX86) || defined(__i386__)
  return "x86";
#elif defined(_M_ARM64) || defined(__aarch64__)
  return "arm64";
#elif defined(_M_ARM) || defined(__arm__)
  return "arm";
#else
  return "unknown";
#endif
}

// ============================================================================
// cache_manifest implementation
// ============================================================================

std::optional<cache_manifest> cache_manifest::load(const std::filesystem::path &manifest_path) {
  std::ifstream file(manifest_path);
  if (!file.is_open()) {
    return std::nullopt;
  }

  cache_manifest manifest;
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

    if (current_section == "metadata") {
      if (key == "package") manifest.package = value;
      else if (key == "version") manifest.version = value;
      else if (key == "cache_key") manifest.cache_key = value;
      else if (key == "created") manifest.created = value;
    } else if (current_section == "build") {
      if (key == "platform") manifest.platform = value;
      else if (key == "compiler") manifest.compiler = value;
      else if (key == "compiler_version") manifest.compiler_version = value;
      else if (key == "config") manifest.config = value;
      else if (key == "arch") manifest.arch = value;
      else if (key == "cpp_standard") manifest.cpp_standard = std::stoi(value);
    } else if (current_section == "files") {
      // Format: "path" = { size = 123, checksum = "abc" }
      // Simplified: just parse as path = size,checksum
      auto parts_start = value.find('{');
      if (parts_start != std::string::npos) {
        file_entry entry;
        entry.path = key;

        // Parse size and checksum from the inline table
        auto size_pos = value.find("size");
        auto checksum_pos = value.find("checksum");

        if (size_pos != std::string::npos) {
          auto eq = value.find('=', size_pos);
          auto comma = value.find(',', eq);
          if (comma == std::string::npos) comma = value.find('}', eq);
          std::string size_str = trim(value.substr(eq + 1, comma - eq - 1));
          entry.size = std::stoull(size_str);
        }

        if (checksum_pos != std::string::npos) {
          auto eq = value.find('=', checksum_pos);
          auto end = value.find('}', eq);
          std::string checksum_str = trim(value.substr(eq + 1, end - eq - 1));
          // Remove quotes
          if (checksum_str.front() == '"') checksum_str = checksum_str.substr(1);
          if (checksum_str.back() == '"') checksum_str.pop_back();
          entry.checksum = checksum_str;
        }

        manifest.files.push_back(entry);
      }
    }
  }

  return manifest;
}

bool cache_manifest::save(const std::filesystem::path &manifest_path) const {
  std::ofstream file(manifest_path);
  if (!file.is_open()) {
    return false;
  }

  file << "# Cache manifest for " << package << " " << version << "\n\n";

  file << "[metadata]\n";
  file << "package = \"" << package << "\"\n";
  file << "version = \"" << version << "\"\n";
  file << "cache_key = \"" << cache_key << "\"\n";
  file << "created = \"" << created << "\"\n\n";

  file << "[build]\n";
  file << "platform = \"" << platform << "\"\n";
  file << "compiler = \"" << compiler << "\"\n";
  file << "compiler_version = \"" << compiler_version << "\"\n";
  file << "config = \"" << config << "\"\n";
  file << "arch = \"" << arch << "\"\n";
  file << "cpp_standard = " << cpp_standard << "\n\n";

  file << "[files]\n";
  for (const auto &f : files) {
    file << "\"" << f.path << "\" = { size = " << f.size
         << ", checksum = \"" << f.checksum << "\" }\n";
  }

  return true;
}

bool cache_manifest::verify(const std::filesystem::path &cache_entry_dir) const {
  for (const auto &f : files) {
    std::filesystem::path file_path = cache_entry_dir / f.path;
    if (!std::filesystem::exists(file_path)) {
      return false;
    }
    if (std::filesystem::file_size(file_path) != f.size) {
      return false;
    }
    // Optional: verify checksum (expensive for large files)
    // uint64_t actual_hash = fnv1a_hash_file(file_path);
    // if (hash_to_hex(actual_hash, 16) != f.checksum) return false;
  }
  return true;
}

// ============================================================================
// package_cache implementation
// ============================================================================

std::filesystem::path package_cache::get_default_cache_dir() {
#ifdef _WIN32
  char path[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
    return std::filesystem::path(path) / "cforge" / "cache";
  }
  // Fallback to USERPROFILE
  const char *userprofile = std::getenv("USERPROFILE");
  if (userprofile) {
    return std::filesystem::path(userprofile) / ".cforge" / "cache";
  }
  return std::filesystem::path(".cforge") / "cache";
#else
  // Use XDG_DATA_HOME if set, otherwise ~/.local/share/cforge
  const char *xdg_data = std::getenv("XDG_DATA_HOME");
  if (xdg_data) {
    return std::filesystem::path(xdg_data) / "cforge" / "cache";
  }
  const char *home = std::getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    home = pw ? pw->pw_dir : ".";
  }
  return std::filesystem::path(home) / ".local" / "share" / "cforge" / "cache";
#endif
}

package_cache::package_cache(const std::filesystem::path &cache_dir)
    : cache_dir_(cache_dir),
      packages_dir_(cache_dir / "packages"),
      stats_file_(cache_dir / "stats.toml") {
  // Create directories if they don't exist
  std::filesystem::create_directories(packages_dir_);
  load_stats();
}

std::filesystem::path package_cache::get_entry_path(const cache_key &key) const {
  return packages_dir_ / key.package / key.directory_name();
}

bool package_cache::has(const cache_key &key) const {
  auto entry_path = get_entry_path(key);
  auto manifest_path = entry_path / "manifest.toml";

  if (!std::filesystem::exists(manifest_path)) {
    return false;
  }

  // Verify manifest
  auto manifest = cache_manifest::load(manifest_path);
  if (!manifest) {
    return false;
  }

  return manifest->verify(entry_path);
}

std::optional<std::filesystem::path> package_cache::get(const cache_key &key) const {
  if (has(key)) {
    return get_entry_path(key);
  }
  return std::nullopt;
}

bool package_cache::copy_directory(const std::filesystem::path &src,
                                   const std::filesystem::path &dst) {
  try {
    std::filesystem::create_directories(dst);
    for (const auto &entry : std::filesystem::recursive_directory_iterator(src)) {
      auto rel_path = std::filesystem::relative(entry.path(), src);
      auto dest_path = dst / rel_path;

      if (entry.is_directory()) {
        std::filesystem::create_directories(dest_path);
      } else if (entry.is_regular_file()) {
        std::filesystem::create_directories(dest_path.parent_path());
        std::filesystem::copy_file(entry.path(), dest_path,
                                   std::filesystem::copy_options::overwrite_existing);
      }
    }
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

cache_manifest package_cache::generate_manifest(const cache_key &key,
                                                const std::filesystem::path &dir) {
  cache_manifest manifest;
  manifest.package = key.package;
  manifest.version = key.version;
  manifest.cache_key = key.to_string();
  manifest.created = get_timestamp();
  manifest.platform = key.platform;
  manifest.compiler = key.compiler;
  manifest.compiler_version = key.compiler_ver;
  manifest.config = key.config;
  manifest.arch = key.arch;
  manifest.cpp_standard = key.cpp_standard;

  for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      cache_manifest::file_entry file;
      file.path = std::filesystem::relative(entry.path(), dir).string();
      file.size = entry.file_size();
      file.checksum = hash_to_hex(fnv1a_hash_file(entry.path()), 16);
      manifest.files.push_back(file);
    }
  }

  return manifest;
}

bool package_cache::store(const cache_key &key, const std::filesystem::path &build_output) {
  auto entry_path = get_entry_path(key);

  // Remove existing entry if present
  if (std::filesystem::exists(entry_path)) {
    std::filesystem::remove_all(entry_path);
  }

  // Copy build output to cache
  if (!copy_directory(build_output, entry_path)) {
    return false;
  }

  // Generate and save manifest
  auto manifest = generate_manifest(key, entry_path);
  if (!manifest.save(entry_path / "manifest.toml")) {
    std::filesystem::remove_all(entry_path);
    return false;
  }

  return true;
}

bool package_cache::restore(const cache_key &key, const std::filesystem::path &dest) {
  auto cached_path = get(key);
  if (!cached_path) {
    return false;
  }

  // Update last accessed time (touch manifest)
  auto manifest_path = *cached_path / "manifest.toml";
  std::filesystem::last_write_time(manifest_path, std::filesystem::file_time_type::clock::now());

  return copy_directory(*cached_path, dest);
}

bool package_cache::remove(const cache_key &key) {
  auto entry_path = get_entry_path(key);
  if (std::filesystem::exists(entry_path)) {
    std::filesystem::remove_all(entry_path);
  }
  return true;
}

cforge_size_t package_cache::remove_package(const std::string &package_name) {
  auto pkg_path = packages_dir_ / package_name;
  if (!std::filesystem::exists(pkg_path)) {
    return 0;
  }

  cforge_size_t count = 0;
  for (const auto &entry : std::filesystem::directory_iterator(pkg_path)) {
    if (entry.is_directory()) {
      std::filesystem::remove_all(entry.path());
      ++count;
    }
  }

  // Remove package directory if empty
  if (std::filesystem::is_empty(pkg_path)) {
    std::filesystem::remove(pkg_path);
  }

  return count;
}

cforge_size_t package_cache::prune(cforge_size_t max_size_mb) {
  cforge_size_t max_size_bytes = max_size_mb * 1024 * 1024;
  cforge_size_t current_size = calculate_total_size();

  if (current_size <= max_size_bytes) {
    return 0;
  }

  // Collect all entries with their last access times
  std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> entries;

  for (const auto &pkg_entry : std::filesystem::directory_iterator(packages_dir_)) {
    if (!pkg_entry.is_directory()) continue;
    for (const auto &ver_entry : std::filesystem::directory_iterator(pkg_entry.path())) {
      if (!ver_entry.is_directory()) continue;
      auto manifest_path = ver_entry.path() / "manifest.toml";
      if (std::filesystem::exists(manifest_path)) {
        entries.emplace_back(std::filesystem::last_write_time(manifest_path), ver_entry.path());
      }
    }
  }

  // Sort by access time (oldest first)
  std::sort(entries.begin(), entries.end());

  cforge_size_t removed = 0;
  for (const auto &[time, path] : entries) {
    if (current_size <= max_size_bytes) break;

    cforge_size_t entry_size = 0;
    for (const auto &f : std::filesystem::recursive_directory_iterator(path)) {
      if (f.is_regular_file()) {
        entry_size += f.file_size();
      }
    }

    std::filesystem::remove_all(path);
    current_size -= entry_size;
    ++removed;
  }

  return removed;
}

cforge_size_t package_cache::clear() {
  cforge_size_t count = 0;
  for (const auto &pkg_entry : std::filesystem::directory_iterator(packages_dir_)) {
    if (pkg_entry.is_directory()) {
      for (const auto &ver_entry : std::filesystem::directory_iterator(pkg_entry.path())) {
        if (ver_entry.is_directory()) {
          std::filesystem::remove_all(ver_entry.path());
          ++count;
        }
      }
    }
  }
  // Clean up empty package directories
  for (const auto &pkg_entry : std::filesystem::directory_iterator(packages_dir_)) {
    if (pkg_entry.is_directory() && std::filesystem::is_empty(pkg_entry.path())) {
      std::filesystem::remove(pkg_entry.path());
    }
  }
  return count;
}

std::vector<cache_entry> package_cache::list() const {
  std::vector<cache_entry> entries;

  if (!std::filesystem::exists(packages_dir_)) {
    return entries;
  }

  for (const auto &pkg_entry : std::filesystem::directory_iterator(packages_dir_)) {
    if (!pkg_entry.is_directory()) continue;
    for (const auto &ver_entry : std::filesystem::directory_iterator(pkg_entry.path())) {
      if (!ver_entry.is_directory()) continue;

      auto manifest_path = ver_entry.path() / "manifest.toml";
      auto manifest = cache_manifest::load(manifest_path);
      if (!manifest) continue;

      cache_entry entry;
      auto parsed_key = cache_key::from_string(manifest->cache_key);
      if (parsed_key) {
        entry.key = *parsed_key;
      }
      entry.path = ver_entry.path();

      // Get timestamps - use manifest time as created time approximation
      entry.last_accessed = std::chrono::system_clock::now(); // Approximate

      // Calculate size
      entry.size_bytes = 0;
      for (const auto &f : std::filesystem::recursive_directory_iterator(ver_entry.path())) {
        if (f.is_regular_file()) {
          entry.size_bytes += f.file_size();
        }
      }

      entry.valid = manifest->verify(ver_entry.path());
      entries.push_back(entry);
    }
  }

  return entries;
}

std::vector<cache_entry> package_cache::list_package(const std::string &package_name) const {
  std::vector<cache_entry> entries;
  auto pkg_path = packages_dir_ / package_name;

  if (!std::filesystem::exists(pkg_path)) {
    return entries;
  }

  auto all_entries = list();
  for (const auto &entry : all_entries) {
    if (entry.key.package == package_name) {
      entries.push_back(entry);
    }
  }

  return entries;
}

cforge_size_t package_cache::calculate_total_size() const {
  cforge_size_t total = 0;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(packages_dir_)) {
    if (entry.is_regular_file()) {
      total += entry.file_size();
    }
  }
  return total;
}

void package_cache::load_stats() const {
  stats_ = cache_stats{};

  std::ifstream file(stats_file_);
  if (!file.is_open()) return;

  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == '[') continue;

    auto eq_pos = line.find('=');
    if (eq_pos == std::string::npos) continue;

    std::string key = trim(line.substr(0, eq_pos));
    std::string value = trim(line.substr(eq_pos + 1));

    if (key == "cache_hits") stats_.cache_hits = std::stoull(value);
    else if (key == "cache_misses") stats_.cache_misses = std::stoull(value);
    else if (key == "remote_hits") stats_.remote_hits = std::stoull(value);
    else if (key == "remote_misses") stats_.remote_misses = std::stoull(value);
  }
}

void package_cache::save_stats() const {
  std::ofstream file(stats_file_);
  if (!file.is_open()) return;

  file << "# Cache statistics\n\n";
  file << "[stats]\n";
  file << "cache_hits = " << stats_.cache_hits << "\n";
  file << "cache_misses = " << stats_.cache_misses << "\n";
  file << "remote_hits = " << stats_.remote_hits << "\n";
  file << "remote_misses = " << stats_.remote_misses << "\n";
}

cache_stats package_cache::stats() const {
  load_stats();
  stats_.total_entries = list().size();
  stats_.total_size_bytes = calculate_total_size();
  return stats_;
}

void package_cache::record_hit() {
  load_stats();
  stats_.cache_hits++;
  save_stats();
}

void package_cache::record_miss() {
  load_stats();
  stats_.cache_misses++;
  save_stats();
}

void package_cache::record_remote_hit() {
  load_stats();
  stats_.remote_hits++;
  save_stats();
}

void package_cache::record_remote_miss() {
  load_stats();
  stats_.remote_misses++;
  save_stats();
}

// ============================================================================
// Helper functions
// ============================================================================

cache_key generate_cache_key(const std::string &package_name,
                             const std::string &version,
                             const build_environment &env,
                             const std::string &config,
                             const std::map<std::string, std::string> &cmake_options) {
  cache_key key;
  key.package = package_name;
  key.version = version;
  key.platform = platform_to_string(env.target_platform);
  key.compiler = compiler_to_string(env.target_compiler);
  key.compiler_ver = env.compiler_version;
  key.config = config;
  key.arch = env.arch;
  key.cpp_standard = env.cpp_standard;
  key.options_hash = hash_cmake_options(cmake_options);
  return key;
}

std::string hash_cmake_options(const std::map<std::string, std::string> &options) {
  if (options.empty()) {
    return "00000000";
  }

  std::stringstream ss;
  for (const auto &[key, value] : options) {
    ss << key << "=" << value << ";";
  }
  return hash_to_hex(fnv1a_hash(ss.str()), 8);
}

} // namespace cforge
