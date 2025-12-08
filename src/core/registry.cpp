/**
 * @file registry.cpp
 * @brief Implementation of package registry for cforge-index
 */

#include "core/registry.hpp"
#include "cforge/log.hpp"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif


namespace cforge {

// Index repository URL

// Cache validity duration (24 hours)
static const int CACHE_VALIDITY_HOURS = 24;

std::string registry::get_index_url() { return INDEX_REPO_URL; }

std::filesystem::path registry::get_default_cache_dir() {
#ifdef _WIN32
  const char *appdata = std::getenv("LOCALAPPDATA");
  if (appdata) {
    return std::filesystem::path(appdata) / "cforge" / "registry";
  }
  const char *userprofile = std::getenv("USERPROFILE");
  if (userprofile) {
    return std::filesystem::path(userprofile) / ".cforge" / "registry";
  }
#else
  const char *home = std::getenv("HOME");
  if (home) {
    return std::filesystem::path(home) / ".cforge" / "registry";
  }
#endif
  return std::filesystem::current_path() / ".cforge" / "registry";
}

registry::registry(const std::filesystem::path &cache_dir)
    : cache_dir_(cache_dir), index_dir_(cache_dir / "cforge-index") {}

bool registry::needs_update() const {
  // Check if index exists
  if (!std::filesystem::exists(index_dir_ / "packages")) {
    return true;
  }

  // Check timestamp file
  std::filesystem::path timestamp_file = cache_dir_ / ".last_update";
  if (!std::filesystem::exists(timestamp_file)) {
    return true;
  }

  // Read timestamp
  std::ifstream ts_in(timestamp_file);
  if (!ts_in) {
    return true;
  }

  long long timestamp;
  ts_in >> timestamp;
  ts_in.close();

  // Check if older than CACHE_VALIDITY_HOURS
  auto now = std::chrono::system_clock::now();
  auto now_ts =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count();

  return (now_ts - timestamp) > (CACHE_VALIDITY_HOURS * 3600);
}

bool registry::update(bool force) {
  if (!force && !needs_update()) {
    logger::print_verbose("Registry cache is up to date");
    return true;
  }

  // Create cache directory
  std::filesystem::create_directories(cache_dir_);

  bool success = false;

  if (std::filesystem::exists(index_dir_)) {
    // Update existing clone
    logger::updating("package index");

    auto result = execute_process(
        "git", {"pull", "--quiet"}, index_dir_.string(),
        [](const std::string &) {},
        [](const std::string &line) { logger::print_error(line); }, 60);

    success = result.success;

    if (!success) {
      // Try fresh clone if pull fails
      logger::print_warning("Pull failed, trying fresh clone...");
      std::filesystem::remove_all(index_dir_);
    }
  }

  if (!std::filesystem::exists(index_dir_)) {
    // Fresh clone
    logger::fetching("package index from " INDEX_REPO_URL);

    auto result = execute_process(
        "git", {"clone", "--quiet", "--depth", "1", INDEX_REPO_URL, index_dir_.string()},
        "", [](const std::string &) {},
        [](const std::string &line) { logger::print_error(line); }, 120);

    success = result.success;
  }

  if (success) {
    // Update timestamp
    std::filesystem::path timestamp_file = cache_dir_ / ".last_update";
    std::ofstream ts_out(timestamp_file);
    if (ts_out) {
      auto now = std::chrono::system_clock::now();
      auto ts = std::chrono::duration_cast<std::chrono::seconds>(
                    now.time_since_epoch())
                    .count();
      ts_out << ts;
      ts_out.close();
    }

    logger::finished("package index updated");
  }

  return success;
}

std::vector<std::string> registry::search(const std::string &query,
                                          size_t limit) const {
  std::vector<std::string> results;
  std::filesystem::path packages_dir = index_dir_ / "packages";

  if (!std::filesystem::exists(packages_dir)) {
    return results;
  }

  std::string query_lower = query;
  std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(),
                 ::tolower);

  // Score-based search results
  std::vector<std::pair<std::string, int>> scored_results;

  // Iterate through all package directories
  for (const auto &letter_dir :
       std::filesystem::directory_iterator(packages_dir)) {
    if (!letter_dir.is_directory()) {
      continue;
    }

    for (const auto &pkg_file :
         std::filesystem::directory_iterator(letter_dir.path())) {
      if (pkg_file.path().extension() != ".toml") {
        continue;
      }

      std::string pkg_name = pkg_file.path().stem().string();
      std::string pkg_name_lower = pkg_name;
      std::transform(pkg_name_lower.begin(), pkg_name_lower.end(),
                     pkg_name_lower.begin(), ::tolower);

      int score = 0;

      // Exact match
      if (pkg_name_lower == query_lower) {
        score = 1000;
      }
      // Prefix match
      else if (pkg_name_lower.find(query_lower) == 0) {
        score = 500;
      }
      // Contains match
      else if (pkg_name_lower.find(query_lower) != std::string::npos) {
        score = 100;
      } else {
        // Check description and keywords
        auto pkg_info = load_package_file(pkg_name);
        if (pkg_info) {
          std::string desc_lower = pkg_info->description;
          std::transform(desc_lower.begin(), desc_lower.end(),
                         desc_lower.begin(), ::tolower);
          if (desc_lower.find(query_lower) != std::string::npos) {
            score = 50;
          }

          for (const auto &kw : pkg_info->keywords) {
            std::string kw_lower = kw;
            std::transform(kw_lower.begin(), kw_lower.end(), kw_lower.begin(),
                           ::tolower);
            if (kw_lower == query_lower) {
              score = std::max(score, 75);
            } else if (kw_lower.find(query_lower) != std::string::npos) {
              score = std::max(score, 25);
            }
          }
        }
      }

      if (score > 0) {
        scored_results.emplace_back(pkg_name, score);
      }
    }
  }

  // Sort by score (descending)
  std::sort(scored_results.begin(), scored_results.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });

  // Extract names up to limit
  for (size_t i = 0; i < std::min(limit, scored_results.size()); ++i) {
    results.push_back(scored_results[i].first);
  }

  return results;
}

std::optional<package_info> registry::get_package(const std::string &name) const {
  return load_package_file(name);
}

std::string registry::pattern_to_regex(const std::string &pattern) {
  // Convert pattern like "v{version}" to regex like "^v([0-9]+\\.[0-9]+\\.?[0-9]*)$"
  std::string regex_str = "^";
  size_t pos = 0;
  size_t version_pos = pattern.find("{version}");

  if (version_pos == std::string::npos) {
    // No {version} placeholder, treat whole pattern as version
    return "^([0-9]+(?:\\.[0-9]+)*)$";
  }

  // Escape everything before {version}
  for (size_t i = 0; i < version_pos; ++i) {
    char c = pattern[i];
    if (c == '.' || c == '*' || c == '+' || c == '?' || c == '^' ||
        c == '$' || c == '[' || c == ']' || c == '(' || c == ')' ||
        c == '{' || c == '}' || c == '|' || c == '\\') {
      regex_str += '\\';
    }
    regex_str += c;
  }

  // Add version capture group - matches semver-like versions
  regex_str += "([0-9]+(?:\\.[0-9]+)*)";

  // Escape everything after {version}
  for (size_t i = version_pos + 9; i < pattern.size(); ++i) {
    char c = pattern[i];
    if (c == '.' || c == '*' || c == '+' || c == '?' || c == '^' ||
        c == '$' || c == '[' || c == ']' || c == '(' || c == ')' ||
        c == '{' || c == '}' || c == '|' || c == '\\') {
      regex_str += '\\';
    }
    regex_str += c;
  }

  regex_str += "$";
  return regex_str;
}

std::string registry::version_to_tag(const std::string &version,
                                      const std::string &pattern) {
  std::string tag = pattern;
  size_t pos = tag.find("{version}");
  if (pos != std::string::npos) {
    tag.replace(pos, 9, version);
  }
  return tag;
}

std::vector<package_version>
registry::fetch_git_tags(const std::string &repo_url,
                          const tag_config &config) const {
  std::vector<package_version> versions;

  // Check in-memory cache first
  auto cache_it = version_cache_.find(repo_url);
  if (cache_it != version_cache_.end()) {
    return cache_it->second;
  }

  logger::print_verbose("Fetching tags from " + repo_url);

  // Run git ls-remote --tags
  std::string stdout_output;
  auto result = execute_process(
      "git", {"ls-remote", "--tags", "--refs", repo_url}, "",
      [&stdout_output](const std::string &line) { stdout_output += line + "\n"; },
      [](const std::string &) {}, 30);

  if (!result.success) {
    logger::print_verbose("Failed to fetch tags from " + repo_url);
    return versions;
  }

  // Parse the output - format: "SHA\trefs/tags/tagname"
  std::string regex_str = pattern_to_regex(config.pattern);
  std::regex version_regex;
  try {
    version_regex = std::regex(regex_str);
  } catch (const std::regex_error &e) {
    logger::print_verbose("Invalid tag pattern regex: " + regex_str);
    return versions;
  }

  std::istringstream iss(stdout_output);
  std::string line;

  while (std::getline(iss, line)) {
    // Find the tag name after refs/tags/
    size_t tag_pos = line.find("refs/tags/");
    if (tag_pos == std::string::npos) {
      continue;
    }

    std::string tag = line.substr(tag_pos + 10);

    // Trim whitespace
    tag.erase(tag.find_last_not_of(" \t\r\n") + 1);

    // Check exclusions
    bool excluded = false;
    for (const auto &excl : config.exclude) {
      if (tag.find(excl) != std::string::npos) {
        excluded = true;
        break;
      }
    }
    if (excluded) {
      continue;
    }

    // Extract version using regex
    std::smatch match;
    if (std::regex_match(tag, match, version_regex) && match.size() >= 2) {
      package_version ver;
      ver.version = match[1].str();
      ver.tag = tag;
      versions.push_back(ver);
    }
  }

  // Sort by version (newest first)
  std::sort(versions.begin(), versions.end(),
            [](const package_version &a, const package_version &b) {
              return compare_versions(a.version, b.version) > 0;
            });

  // Limit number of versions
  if (versions.size() > static_cast<size_t>(config.max_versions)) {
    versions.resize(config.max_versions);
  }

  // Cache the results
  version_cache_[repo_url] = versions;

  return versions;
}

std::vector<package_version>
registry::load_version_cache(const std::string &name) const {
  std::vector<package_version> versions;
  // Always use global cache directory for version caching (not project-local)
  std::filesystem::path cache_file = get_default_cache_dir() / "versions" / (name + ".cache");

  if (!std::filesystem::exists(cache_file)) {
    return versions;
  }

  // Check if cache is still valid (1 hour)
  auto mod_time = std::filesystem::last_write_time(cache_file);
  auto now = std::filesystem::file_time_type::clock::now();
  auto age = std::chrono::duration_cast<std::chrono::hours>(now - mod_time);
  if (age.count() > 1) {
    return versions;  // Cache expired
  }

  std::ifstream file(cache_file);
  if (!file) {
    return versions;
  }

  std::string line;
  while (std::getline(file, line)) {
    size_t tab_pos = line.find('\t');
    if (tab_pos != std::string::npos) {
      package_version ver;
      ver.version = line.substr(0, tab_pos);
      ver.tag = line.substr(tab_pos + 1);
      versions.push_back(ver);
    }
  }

  return versions;
}

void registry::save_version_cache(
    const std::string &name,
    const std::vector<package_version> &versions) const {
  // Always use global cache directory for version caching (not project-local)
  std::filesystem::path version_cache_dir = get_default_cache_dir() / "versions";
  std::filesystem::create_directories(version_cache_dir);

  std::filesystem::path cache_file = version_cache_dir / (name + ".cache");
  std::ofstream file(cache_file);
  if (!file) {
    return;
  }

  for (const auto &ver : versions) {
    file << ver.version << "\t" << ver.tag << "\n";
  }
}

std::optional<package_info>
registry::load_package_file(const std::string &name) const {
  // Determine the package file path
  if (name.empty()) {
    return std::nullopt;
  }

  char first_letter = std::tolower(name[0]);
  std::filesystem::path pkg_file =
      index_dir_ / "packages" / std::string(1, first_letter) / (name + ".toml");

  if (!std::filesystem::exists(pkg_file)) {
    return std::nullopt;
  }

  toml_reader reader;
  if (!reader.load(pkg_file.string())) {
    return std::nullopt;
  }

  package_info info;
  info.name = reader.get_string("package.name", name);
  info.description = reader.get_string("package.description", "");
  info.repository = reader.get_string("package.repository", "");
  info.homepage = reader.get_string("package.homepage", "");
  info.documentation = reader.get_string("package.documentation", "");
  info.license = reader.get_string("package.license", "");
  info.keywords = reader.get_string_array("package.keywords");
  info.categories = reader.get_string_array("package.categories");
  info.verified = reader.get_bool("package.verified", false);

  // Tag discovery configuration
  info.tags.pattern = reader.get_string("package.tag_pattern", "v{version}");
  info.tags.exclude = reader.get_string_array("package.tag_exclude");
  info.tags.max_versions = reader.get_int("package.max_versions", 50);
  info.auto_discover_versions = reader.get_bool("package.auto_versions", true);

  // Integration - support both old [integration] and new [cmake] sections
  info.integration.type = reader.get_string("integration.type",
                                            reader.get_string("cmake.type", "cmake"));
  info.integration.cmake_target = reader.get_string("integration.cmake_target",
                                                    reader.get_string("cmake.target", ""));
  info.integration.include_dir = reader.get_string("integration.include_dir",
                                                   reader.get_string("cmake.include_dir", ""));
  info.integration.single_header = reader.get_string("integration.single_header", "");
  info.integration.cmake_subdir = reader.get_string("integration.cmake_subdir", "");
  info.integration.header_only_option =
      reader.get_string("integration.header_only_option", "");

  // Check if header_only is set at cmake level
  if (reader.get_bool("cmake.header_only", false)) {
    info.integration.type = "header_only";
  }

  // Default features
  info.default_features = reader.get_string_array("package.features.default");
  if (info.default_features.empty()) {
    info.default_features = reader.get_string_array("features.default");
  }

  // Features
  for (const auto &feature_name : reader.get_table_keys("features")) {
    if (feature_name == "default" || feature_name == "groups") {
      continue;
    }

    package_feature feat;
    feat.name = feature_name;
    std::string prefix = "features." + feature_name;
    feat.cmake_option = reader.get_string(prefix + ".option", "");
    feat.description = reader.get_string(prefix + ".description", "");
    // Support both inline requires and nested requires.dependencies
    feat.required_deps = reader.get_string_array(prefix + ".requires");
    if (feat.required_deps.empty()) {
      feat.required_deps = reader.get_string_array(prefix + ".requires.dependencies");
    }

    info.features[feature_name] = feat;
  }

  // First try to load explicit [[versions]] from file
  std::ifstream file(pkg_file);
  if (file) {
    std::string line;
    package_version current_ver;
    bool in_version = false;
    bool has_explicit_versions = false;

    while (std::getline(file, line)) {
      // Trim whitespace
      size_t start = line.find_first_not_of(" \t");
      if (start == std::string::npos) {
        continue;
      }
      line = line.substr(start);

      if (line.find("[[versions]]") == 0) {
        has_explicit_versions = true;
        if (in_version && !current_ver.version.empty()) {
          info.versions.push_back(current_ver);
        }
        current_ver = package_version();
        in_version = true;
      } else if (in_version) {
        if (line[0] == '[' && line.find("[[") != 0) {
          // New section, save current version
          if (!current_ver.version.empty()) {
            info.versions.push_back(current_ver);
          }
          in_version = false;
        } else {
          // Parse version fields
          size_t eq_pos = line.find('=');
          if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);

            // Trim key and value
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));

            // Remove quotes from value
            if (value.size() >= 2 && value.front() == '"' &&
                value.back() == '"') {
              value = value.substr(1, value.size() - 2);
            }

            if (key == "version") {
              current_ver.version = value;
            } else if (key == "tag") {
              current_ver.tag = value;
            } else if (key == "min_cpp") {
              try {
                current_ver.min_cpp = std::stoi(value);
              } catch (...) {
              }
            } else if (key == "yanked") {
              current_ver.yanked = (value == "true");
            }
          }
        }
      }
    }

    // Don't forget the last version
    if (in_version && !current_ver.version.empty()) {
      info.versions.push_back(current_ver);
    }

    // If no explicit versions and auto-discover is enabled, fetch from Git
    if (info.versions.empty() && info.auto_discover_versions &&
        !info.repository.empty()) {
      // Try cache first
      info.versions = load_version_cache(name);

      if (info.versions.empty()) {
        // Fetch from Git
        info.versions = fetch_git_tags(info.repository, info.tags);

        // Cache the results
        if (!info.versions.empty()) {
          save_version_cache(name, info.versions);
        }
      }
    }
  }

  // Maintainers
  info.maintainer_owners = reader.get_string_array("maintainers.owners");
  info.maintainer_authors = reader.get_string_array("maintainers.authors");

  return info;
}

std::string registry::resolve_version(const std::string &name,
                                       const std::string &version_spec) const {
  auto pkg = get_package(name);
  if (!pkg || pkg->versions.empty()) {
    return "";
  }

  // Sort versions (highest first)
  std::vector<package_version> sorted_versions = pkg->versions;
  std::sort(sorted_versions.begin(), sorted_versions.end(),
            [](const package_version &a, const package_version &b) {
              return compare_versions(a.version, b.version) > 0;
            });

  // Find matching version
  for (const auto &ver : sorted_versions) {
    if (ver.yanked) {
      continue;
    }
    if (version_matches(ver.version, version_spec)) {
      return ver.version;
    }
  }

  return "";
}

bool registry::version_matches(const std::string &version,
                                const std::string &spec) {
  if (spec == "*" || spec.empty()) {
    return true;
  }

  // Check for wildcard
  size_t wildcard_pos = spec.find('*');
  if (wildcard_pos != std::string::npos) {
    // Get the prefix before the wildcard
    std::string prefix = spec.substr(0, wildcard_pos);

    // Remove trailing dot if present
    if (!prefix.empty() && prefix.back() == '.') {
      prefix.pop_back();
    }

    if (prefix.empty()) {
      return true;
    }

    // Parse both versions
    auto ver_parts = parse_version(version);
    auto spec_parts = parse_version(prefix);

    // Check that version has at least as many parts as spec
    if (ver_parts.size() < spec_parts.size()) {
      return false;
    }

    // Compare parts
    for (size_t i = 0; i < spec_parts.size(); ++i) {
      if (ver_parts[i] != spec_parts[i]) {
        return false;
      }
    }

    return true;
  }

  // Exact match
  return version == spec;
}

int registry::compare_versions(const std::string &v1, const std::string &v2) {
  auto parts1 = parse_version(v1);
  auto parts2 = parse_version(v2);

  size_t max_parts = std::max(parts1.size(), parts2.size());
  parts1.resize(max_parts, 0);
  parts2.resize(max_parts, 0);

  for (size_t i = 0; i < max_parts; ++i) {
    if (parts1[i] < parts2[i]) {
      return -1;
    }
    if (parts1[i] > parts2[i]) {
      return 1;
    }
  }

  return 0;
}

std::vector<int> registry::parse_version(const std::string &version) {
  std::vector<int> parts;
  std::stringstream ss(version);
  std::string part;

  while (std::getline(ss, part, '.')) {
    try {
      // Handle versions like "3.11.3-rc1" by stripping suffix
      size_t dash = part.find('-');
      if (dash != std::string::npos) {
        part = part.substr(0, dash);
      }
      parts.push_back(std::stoi(part));
    } catch (...) {
      parts.push_back(0);
    }
  }

  return parts;
}

std::optional<resolved_dependency>
registry::resolve_dependency(const dependency_spec &spec) const {
  resolved_dependency resolved;
  resolved.name = spec.name;
  resolved.source = spec.source;
  resolved.features = spec.features;
  resolved.header_only = spec.header_only;
  resolved.link = spec.link;

  switch (spec.source) {
  case dependency_source::INDEX: {
    auto pkg = get_package(spec.name);
    if (!pkg) {
      return std::nullopt;
    }

    // Resolve version
    std::string version = resolve_version(spec.name, spec.version);
    if (version.empty() && !pkg->versions.empty()) {
      // Use latest
      version = pkg->versions[0].version;
    }

    resolved.repository = pkg->repository;
    resolved.version = version;
    resolved.cmake_target = pkg->integration.cmake_target;
    resolved.include_dir = pkg->integration.include_dir;

    // Find tag for this version
    for (const auto &ver : pkg->versions) {
      if (ver.version == version) {
        resolved.tag = ver.tag;
        break;
      }
    }

    // Apply default features if not disabled
    if (spec.default_features) {
      for (const auto &feat : pkg->default_features) {
        if (std::find(resolved.features.begin(), resolved.features.end(),
                      feat) == resolved.features.end()) {
          resolved.features.push_back(feat);
        }
      }
    }

    // Resolve feature CMake options
    for (const auto &feat_name : resolved.features) {
      auto it = pkg->features.find(feat_name);
      if (it != pkg->features.end()) {
        if (!it->second.cmake_option.empty()) {
          resolved.cmake_options[it->second.cmake_option] = "ON";
        }
      }
    }

    break;
  }

  case dependency_source::GIT:
    resolved.repository = spec.git_url;
    resolved.tag = spec.git_tag;
    resolved.branch = spec.git_branch;
    resolved.commit = spec.git_commit;
    break;

  case dependency_source::VCPKG:
    resolved.vcpkg_name = spec.vcpkg_name.empty() ? spec.name : spec.vcpkg_name;
    resolved.version = spec.version;
    break;

  case dependency_source::SYSTEM:
    resolved.pkg_config_name = spec.name;
    break;

  case dependency_source::PROJECT:
    resolved.path = spec.path;
    break;
  }

  return resolved;
}

std::vector<std::string> registry::list_packages() const {
  std::vector<std::string> packages;
  std::filesystem::path packages_dir = index_dir_ / "packages";

  if (!std::filesystem::exists(packages_dir)) {
    return packages;
  }

  for (const auto &letter_dir :
       std::filesystem::directory_iterator(packages_dir)) {
    if (!letter_dir.is_directory()) {
      continue;
    }

    for (const auto &pkg_file :
         std::filesystem::directory_iterator(letter_dir.path())) {
      if (pkg_file.path().extension() == ".toml") {
        packages.push_back(pkg_file.path().stem().string());
      }
    }
  }

  std::sort(packages.begin(), packages.end());
  return packages;
}

dependency_source registry::parse_source(const std::string &source) {
  if (source == "git") {
    return dependency_source::GIT;
  }
  if (source == "vcpkg") {
    return dependency_source::VCPKG;
  }
  if (source == "system") {
    return dependency_source::SYSTEM;
  }
  if (source == "project") {
    return dependency_source::PROJECT;
  }
  return dependency_source::INDEX;
}

std::string registry::source_to_string(dependency_source source) {
  switch (source) {
  case dependency_source::INDEX:
    return "index";
  case dependency_source::GIT:
    return "git";
  case dependency_source::VCPKG:
    return "vcpkg";
  case dependency_source::SYSTEM:
    return "system";
  case dependency_source::PROJECT:
    return "project";
  }
  return "index";
}

std::vector<dependency_spec>
parse_dependencies(const std::filesystem::path &config_path) {
  std::vector<dependency_spec> deps;

  toml_reader reader;
  if (!reader.load(config_path.string())) {
    return deps;
  }

  // Parse [dependencies] section
  for (const auto &name : reader.get_table_keys("dependencies")) {
    // Skip directory key
    if (name == "directory") {
      continue;
    }

    dependency_spec spec;
    spec.name = name;

    // Check if it's a simple string or a table
    std::string simple_version =
        reader.get_string("dependencies." + name, "");
    if (!simple_version.empty()) {
      // Simple format: name = "version"
      spec.version = simple_version;
      spec.source = dependency_source::INDEX;
    } else {
      // Table format: name = { ... }
      std::string prefix = "dependencies." + name;

      // Determine source
      std::string source_str = reader.get_string(prefix + ".source", "");
      if (!source_str.empty()) {
        spec.source = registry::parse_source(source_str);
      } else {
        // Infer source from available fields
        if (reader.has_key(prefix + ".git")) {
          spec.source = dependency_source::GIT;
          spec.git_url = reader.get_string(prefix + ".git", "");
        } else if (reader.has_key(prefix + ".vcpkg")) {
          spec.source = dependency_source::VCPKG;
          spec.vcpkg_name = reader.get_string(prefix + ".vcpkg", "");
        } else if (reader.has_key(prefix + ".path")) {
          spec.source = dependency_source::PROJECT;
          spec.path = reader.get_string(prefix + ".path", "");
        } else if (reader.has_key(prefix + ".system") &&
                   reader.get_bool(prefix + ".system", false)) {
          spec.source = dependency_source::SYSTEM;
        } else {
          spec.source = dependency_source::INDEX;
        }
      }

      spec.version = reader.get_string(prefix + ".version", "*");
      spec.git_url = reader.get_string(prefix + ".git", spec.git_url);
      spec.git_tag = reader.get_string(prefix + ".tag", "");
      spec.git_branch = reader.get_string(prefix + ".branch", "");
      spec.git_commit = reader.get_string(prefix + ".commit", "");
      spec.vcpkg_name = reader.get_string(prefix + ".vcpkg", spec.vcpkg_name);
      spec.path = reader.get_string(prefix + ".path", spec.path);
      spec.header_only = reader.get_bool(prefix + ".header_only", false);
      spec.link = reader.get_bool(prefix + ".link", true);
      spec.default_features =
          reader.get_bool(prefix + ".default_features", true);
      spec.features = reader.get_string_array(prefix + ".features");
    }

    deps.push_back(spec);
  }

  return deps;
}

} // namespace cforge
