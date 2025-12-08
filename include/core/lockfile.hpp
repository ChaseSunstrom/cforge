/**
 * @file lockfile.hpp
 * @brief Lock file mechanism for reproducible builds
 *
 * The lock file (cforge.lock) tracks exact versions of all dependencies
 * to ensure reproducible builds across different machines and times.
 */

#pragma once

#include "cforge/log.hpp"
#include "core/constants.h"
#include "core/git_utils.hpp"
#include "core/toml_reader.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cforge {

// Lock file name
constexpr const char *LOCK_FILE = "cforge.lock";

/**
 * @brief Locked dependency information
 */
struct locked_dependency {
  std::string name;
  std::string source_type; // "git", "vcpkg", "system"
  std::string url;         // For git deps
  std::string version;     // Requested version/tag/branch
  std::string resolved;    // Actual resolved version (commit hash for git)
  std::string checksum;    // Optional integrity checksum
};

/**
 * @brief Lock file manager
 */
class lockfile {
public:
  lockfile() = default;

  /**
   * @brief Load lock file from disk
   *
   * @param project_dir Project directory containing cforge.lock
   * @return true if lock file was loaded successfully
   */
  bool load(const std::filesystem::path &project_dir) {
    lock_path_ = project_dir / LOCK_FILE;

    if (!std::filesystem::exists(lock_path_)) {
      return false;
    }

    std::ifstream file(lock_path_);
    if (!file.is_open()) {
      return false;
    }

    dependencies_.clear();
    std::string line;
    locked_dependency current;
    bool in_dependency = false;

    while (std::getline(file, line)) {
      // Skip empty lines and comments
      if (line.empty() || line[0] == '#') {
        continue;
      }

      // Check for section header
      if (line[0] == '[' && line.back() == ']') {
        // Save previous dependency if any
        if (in_dependency && !current.name.empty()) {
          dependencies_[current.name] = current;
        }

        // Parse new dependency name
        std::string section = line.substr(1, line.length() - 2);
        if (section.find("dependency.") == 0) {
          current = locked_dependency();
          current.name = section.substr(11); // Remove "dependency."
          in_dependency = true;
        } else {
          in_dependency = false;
        }
        continue;
      }

      // Parse key-value pairs within dependency section
      if (in_dependency) {
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
          std::string key = trim(line.substr(0, eq_pos));
          std::string value = trim(line.substr(eq_pos + 1));

          // Remove quotes if present
          if (value.length() >= 2 && value.front() == '"' &&
              value.back() == '"') {
            value = value.substr(1, value.length() - 2);
          }

          if (key == "source") {
            current.source_type = value;
          } else if (key == "url") {
            current.url = value;
          } else if (key == "version") {
            current.version = value;
          } else if (key == "resolved") {
            current.resolved = value;
          } else if (key == "checksum") {
            current.checksum = value;
          }
        }
      }
    }

    // Save last dependency
    if (in_dependency && !current.name.empty()) {
      dependencies_[current.name] = current;
    }

    return true;
  }

  /**
   * @brief Save lock file to disk
   *
   * @param project_dir Project directory
   * @return true if save was successful
   */
  bool save(const std::filesystem::path &project_dir) const {
    std::filesystem::path path = project_dir / LOCK_FILE;
    std::ofstream file(path);

    if (!file.is_open()) {
      logger::print_error("Failed to write lock file: " + path.string());
      return false;
    }

    // Write header
    file << "# cforge.lock - DO NOT EDIT MANUALLY\n";
    file << "# This file is auto-generated and tracks exact dependency "
            "versions\n";
    file << "# for reproducible builds.\n";
    file << "#\n";
    file << "# To update dependencies, run: cforge deps update\n";
    file << "# To force regeneration, delete this file and rebuild.\n\n";

    file << "[metadata]\n";
    file << "version = \"1\"\n";
    file << "generated = \"" << get_timestamp() << "\"\n\n";

    // Write dependencies
    for (const auto &[name, dep] : dependencies_) {
      file << "[dependency." << name << "]\n";
      file << "source = \"" << dep.source_type << "\"\n";

      if (!dep.url.empty()) {
        file << "url = \"" << dep.url << "\"\n";
      }

      if (!dep.version.empty()) {
        file << "version = \"" << dep.version << "\"\n";
      }

      file << "resolved = \"" << dep.resolved << "\"\n";

      if (!dep.checksum.empty()) {
        file << "checksum = \"" << dep.checksum << "\"\n";
      }

      file << "\n";
    }

    return true;
  }

  /**
   * @brief Check if a dependency is locked
   *
   * @param name Dependency name
   * @return true if dependency exists in lock file
   */
  bool has_dependency(const std::string &name) const {
    return dependencies_.find(name) != dependencies_.end();
  }

  /**
   * @brief Get locked dependency info
   *
   * @param name Dependency name
   * @return Locked dependency info, or nullopt if not found
   */
  std::optional<locked_dependency>
  get_dependency(const std::string &name) const {
    auto it = dependencies_.find(name);
    if (it != dependencies_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  /**
   * @brief Lock a Git dependency at its current state
   *
   * @param name Dependency name
   * @param url Repository URL
   * @param version Requested version (tag/branch)
   * @param repo_dir Local repository directory
   */
  void lock_git_dependency(const std::string &name, const std::string &url,
                           const std::string &version,
                           const std::filesystem::path &repo_dir) {
    locked_dependency dep;
    dep.name = name;
    dep.source_type = "git";
    dep.url = url;
    dep.version = version;

    // Get the actual commit hash
    std::string commit = git_get_head_commit(repo_dir, false);
    if (!commit.empty()) {
      dep.resolved = commit;
    } else {
      dep.resolved = version; // Fallback to requested version
    }

    dependencies_[name] = dep;
  }

  /**
   * @brief Lock a vcpkg dependency
   *
   * @param name Package name
   * @param version Package version
   */
  void lock_vcpkg_dependency(const std::string &name,
                             const std::string &version) {
    locked_dependency dep;
    dep.name = name;
    dep.source_type = "vcpkg";
    dep.version = version;
    dep.resolved = version;
    dependencies_[name] = dep;
  }

  /**
   * @brief Lock an index dependency (from cforge-index registry)
   *
   * @param name Package name
   * @param version Package version
   * @param repo_dir Local repository directory
   */
  void lock_index_dependency(const std::string &name,
                             const std::string &version,
                             const std::filesystem::path &repo_dir) {
    locked_dependency dep;
    dep.name = name;
    dep.source_type = "index";
    dep.version = version;

    // Get the actual commit hash
    std::string commit = git_get_head_commit(repo_dir, false);
    if (!commit.empty()) {
      dep.resolved = commit;
    } else {
      dep.resolved = version; // Fallback to requested version
    }

    dependencies_[name] = dep;
  }

  /**
   * @brief Remove a dependency from the lock file
   *
   * @param name Dependency name
   */
  void remove_dependency(const std::string &name) { dependencies_.erase(name); }

  /**
   * @brief Clear all locked dependencies
   */
  void clear() { dependencies_.clear(); }

  /**
   * @brief Get all locked dependencies
   */
  const std::map<std::string, locked_dependency> &get_all() const {
    return dependencies_;
  }

  /**
   * @brief Check if lock file exists
   *
   * @param project_dir Project directory
   * @return true if cforge.lock exists
   */
  static bool exists(const std::filesystem::path &project_dir) {
    return std::filesystem::exists(project_dir / LOCK_FILE);
  }

private:
  std::filesystem::path lock_path_;
  std::map<std::string, locked_dependency> dependencies_;

  static std::string trim(const std::string &str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
      return "";
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
  }

  static std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
    return ss.str();
  }
};

/**
 * @brief Update lock file from current project configuration
 *
 * Reads cforge.toml, resolves all dependencies to exact versions,
 * and writes cforge.lock.
 *
 * @param project_dir Project directory
 * @param deps_dir Dependencies directory
 * @param verbose Verbose output
 * @return true if lock file was updated successfully
 */
inline bool update_lockfile(const std::filesystem::path &project_dir,
                            const std::filesystem::path &deps_dir,
                            bool verbose = false) {
  toml_reader config;
  std::filesystem::path config_path = project_dir / CFORGE_FILE;

  if (!config.load(config_path.string())) {
    logger::print_error("Failed to load " + config_path.string());
    return false;
  }

  lockfile lock;

  // Lock Git dependencies
  if (config.has_key("dependencies.git")) {
    auto git_deps = config.get_table_keys("dependencies.git");

    for (const auto &dep : git_deps) {
      std::string url =
          config.get_string("dependencies.git." + dep + ".url", "");
      std::string tag =
          config.get_string("dependencies.git." + dep + ".tag", "");
      std::string branch =
          config.get_string("dependencies.git." + dep + ".branch", "");
      std::string commit =
          config.get_string("dependencies.git." + dep + ".commit", "");

      std::string version = commit;
      if (version.empty())
        version = tag;
      if (version.empty())
        version = branch;

      std::filesystem::path repo_dir = deps_dir / dep;

      if (std::filesystem::exists(repo_dir)) {
        lock.lock_git_dependency(dep, url, version, repo_dir);

        if (verbose) {
          auto locked = lock.get_dependency(dep);
          if (locked) {
            logger::print_verbose("Locked " + dep + " at " + locked->resolved);
          }
        }
      } else if (verbose) {
        logger::print_warning("Dependency " + dep +
                              " not found, skipping lock");
      }
    }
  }

  // Lock vcpkg dependencies
  if (config.has_key("dependencies.vcpkg")) {
    auto vcpkg_deps = config.get_string_array("dependencies.vcpkg");

    for (const auto &dep : vcpkg_deps) {
      // vcpkg deps might have version suffix like "fmt:x64-windows"
      std::string name = dep;
      size_t colon = dep.find(':');
      if (colon != std::string::npos) {
        name = dep.substr(0, colon);
      }
      lock.lock_vcpkg_dependency(name, dep);

      if (verbose) {
        logger::print_verbose("Locked vcpkg package: " + dep);
      }
    }
  }

  // Lock index dependencies (simple name = "version" format)
  // Skip if using FetchContent mode (CMake handles downloading, packages not in deps_dir)
  bool use_fetch_content = config.get_bool("dependencies.fetch_content", true);
  if (!use_fetch_content && config.has_key("dependencies")) {
    auto all_deps = config.get_table_keys("dependencies");

    for (const auto &dep : all_deps) {
      // Skip known special sections
      if (dep == "directory" || dep == "git" || dep == "vcpkg" ||
          dep == "subdirectory" || dep == "system" || dep == "project" ||
          dep == "fetch_content") {
        continue;
      }

      // Check if this is a simple version string (index dependency)
      std::string dep_key = "dependencies." + dep;

      // Skip if it's a table with source-specific keys
      if (config.has_key(dep_key + ".url") ||
          config.has_key(dep_key + ".vcpkg_name") ||
          config.has_key(dep_key + ".path") ||
          config.has_key(dep_key + ".system")) {
        continue;
      }

      // Get the version
      std::string version = config.get_string(dep_key, "");
      if (version.empty()) {
        continue;
      }

      std::filesystem::path repo_dir = deps_dir / dep;

      if (std::filesystem::exists(repo_dir)) {
        lock.lock_index_dependency(dep, version, repo_dir);

        if (verbose) {
          auto locked = lock.get_dependency(dep);
          if (locked) {
            logger::print_verbose("Locked " + dep + " at " + locked->resolved);
          }
        }
      } else if (verbose) {
        logger::print_warning("Index dependency " + dep +
                              " not found, skipping lock");
      }
    }
  }

  // Save lock file
  if (!lock.save(project_dir)) {
    return false;
  }

  logger::print_success("Updated " + std::string(LOCK_FILE));
  return true;
}

/**
 * @brief Verify dependencies match lock file
 *
 * @param project_dir Project directory
 * @param deps_dir Dependencies directory
 * @param verbose Verbose output
 * @return true if all dependencies match lock file
 */
inline bool verify_lockfile(const std::filesystem::path &project_dir,
                            const std::filesystem::path &deps_dir,
                            bool verbose = false) {
  lockfile lock;

  if (!lock.load(project_dir)) {
    if (verbose) {
      logger::print_warning("No lock file found");
    }
    return true; // No lock file is not an error
  }

  bool all_match = true;

  for (const auto &[name, dep] : lock.get_all()) {
    if (dep.source_type == "git") {
      std::filesystem::path repo_dir = deps_dir / name;

      if (!std::filesystem::exists(repo_dir)) {
        logger::print_warning("Locked dependency missing: " + name);
        all_match = false;
        continue;
      }

      std::string current_commit = git_get_head_commit(repo_dir, false);
      if (current_commit != dep.resolved) {
        logger::print_warning(name + " mismatch: expected " + dep.resolved +
                              ", got " + current_commit);
        all_match = false;
      } else if (verbose) {
        logger::print_verbose(name + " OK (" + dep.resolved.substr(0, 7) + ")");
      }
    }
  }

  return all_match;
}

} // namespace cforge
