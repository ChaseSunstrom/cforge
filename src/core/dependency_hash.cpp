/**
 * @file dependency_hash.cpp
 * @brief Implementation of dependency hashing for incremental builds
 */

#include "core/dependency_hash.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace cforge {

bool dependency_hash::load(const std::filesystem::path &project_dir) {
  std::filesystem::path hash_file = project_dir / HASH_FILE;
  if (!std::filesystem::exists(hash_file)) {
    return false;
  }

  std::ifstream file(hash_file);
  if (!file.is_open()) {
    return false;
  }

  hashes.clear();
  versions.clear();

  std::string line;
  std::string current_section;
  std::string current_dep;

  while (std::getline(file, line)) {
    // Skip empty lines and comments
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    // Check for section header [section] or [dependency.name]
    if (line[0] == '[' && line.back() == ']') {
      current_section = line.substr(1, line.length() - 2);

      // Check if it's a dependency section
      if (current_section.find("dependency.") == 0) {
        current_dep = current_section.substr(11); // Remove "dependency."
      } else {
        current_dep.clear();
      }
      continue;
    }

    // Parse key = "value" pairs
    size_t eq_pos = line.find('=');
    if (eq_pos != std::string::npos) {
      std::string key = trim(line.substr(0, eq_pos));
      std::string value = trim(line.substr(eq_pos + 1));

      // Remove quotes if present
      if (value.length() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.length() - 2);
      }

      // Handle config section (for cforge.toml hash, etc.)
      if (current_section == "config") {
        if (key == "hash") {
          hashes["cforge.toml"] = value;
        } else if (key == "workspace_hash") {
          hashes["cforge.workspace.toml"] = value;
        }
      }
      // Handle dependency sections
      else if (!current_dep.empty()) {
        if (key == "hash") {
          hashes[current_dep] = value;
        } else if (key == "version") {
          versions[current_dep] = value;
        }
      }
    }
  }

  return true;
}

bool dependency_hash::save(const std::filesystem::path &project_dir) const {
  std::filesystem::path hash_file = project_dir / HASH_FILE;
  std::ofstream file(hash_file);
  if (!file.is_open()) {
    return false;
  }

  // Write header
  file << "# cforge.hash - Build cache file\n";
  file << "# This file is auto-generated to track dependency state for "
          "incremental builds.\n";
  file << "# Do not commit to version control.\n\n";

  // Write metadata
  file << "[metadata]\n";
  file << "generated = \"" << get_timestamp() << "\"\n\n";

  // Write config hashes (cforge.toml, workspace config, etc.)
  bool has_config = false;
  for (const auto &[name, hash] : hashes) {
    if (name == "cforge.toml" || name == "cforge.workspace.toml") {
      if (!has_config) {
        file << "[config]\n";
        has_config = true;
      }
      if (name == "cforge.toml") {
        file << "hash = \"" << hash << "\"\n";
      } else if (name == "cforge.workspace.toml") {
        file << "workspace_hash = \"" << hash << "\"\n";
      }
    }
  }
  if (has_config) {
    file << "\n";
  }

  // Write dependency hashes
  for (const auto &[name, hash] : hashes) {
    // Skip config entries (already written above)
    if (name == "cforge.toml" || name == "cforge.workspace.toml") {
      continue;
    }

    file << "[dependency." << name << "]\n";
    file << "hash = \"" << hash << "\"\n";

    // Include version if available
    auto version_it = versions.find(name);
    if (version_it != versions.end() && !version_it->second.empty()) {
      file << "version = \"" << version_it->second << "\"\n";
    }

    file << "\n";
  }

  return true;
}

std::string dependency_hash::get_hash(const std::string &name) const {
  auto it = hashes.find(name);
  return it != hashes.end() ? it->second : "";
}

void dependency_hash::set_hash(const std::string &name,
                               const std::string &hash) {
  hashes[name] = hash;
}

std::string dependency_hash::get_version(const std::string &name) const {
  auto it = versions.find(name);
  return it != versions.end() ? it->second : "";
}

void dependency_hash::set_version(const std::string &name,
                                  const std::string &version) {
  versions[name] = version;
}

uint64_t dependency_hash::fnv1a_hash(const std::string &str) {
  return fnv1a_hash(str.data(), str.size());
}

uint64_t dependency_hash::fnv1a_hash(const void *data, size_t size) {
  uint64_t hash = FNV_OFFSET_BASIS;
  const uint8_t *bytes = static_cast<const uint8_t *>(data);

  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= FNV_PRIME;
  }

  return hash;
}

std::string dependency_hash::hash_to_string(uint64_t hash) {
  std::stringstream ss;
  ss << std::hex << std::setw(16) << std::setfill('0') << hash;
  return ss.str();
}

std::string dependency_hash::calculate_directory_hash(
    const std::filesystem::path &dir_path) {
  if (!std::filesystem::exists(dir_path) ||
      !std::filesystem::is_directory(dir_path)) {
    return "";
  }

  // Sort entries for consistent hashing
  std::vector<std::filesystem::path> entries;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(dir_path)) {
    entries.push_back(entry.path());
  }
  std::sort(entries.begin(), entries.end());

  // Calculate hash for each file
  uint64_t combined_hash = FNV_OFFSET_BASIS;
  for (const auto &entry : entries) {
    if (std::filesystem::is_regular_file(entry)) {
      // Add file path relative to dir_path
      std::string rel_path =
          std::filesystem::relative(entry, dir_path).string();
      combined_hash ^= fnv1a_hash(rel_path);

      // Add file contents
      std::ifstream file(entry, std::ios::binary);
      if (file) {
        char buffer[4096];
        while (file.read(buffer, sizeof(buffer))) {
          combined_hash ^= fnv1a_hash(buffer, file.gcount());
        }
        if (file.gcount() > 0) {
          combined_hash ^= fnv1a_hash(buffer, file.gcount());
        }
      }
    }
  }

  return hash_to_string(combined_hash);
}

} // namespace cforge
