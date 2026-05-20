/**
 * @file dependency_hash.cpp
 * @brief Implementation of dependency hashing for incremental builds
 */

#include "core/dependency_hash.hpp"

#include "core/types.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace cforge {

// Section names used to live in their own cforge.hash file. They now live
// inside cforge.lock under `[buildcache]` and `[buildcache.dependency.<name>]`
// so the lockfile class's own sections (`[metadata]`, `[dependency.<name>]`)
// are left untouched.
namespace {

constexpr cforge_cstring_t kBuildcacheSection   = "buildcache";
constexpr cforge_cstring_t kBuildcacheDepPrefix = "buildcache.dependency.";

// True if a section name belongs to this class (and therefore should be
// rewritten on save; sections we don't own are preserved verbatim).
bool is_buildcache_section(const std::string &name) {
  return name == kBuildcacheSection || name.rfind(kBuildcacheDepPrefix, 0) == 0;
}

// Read the entire file, returning a vector of (section_header_line, body_lines)
// so we can rewrite only our sections and keep everything else byte-for-byte.
// A section starts at a `[...]` line and continues until the next one.
// Leading lines before any section (comments/blank/metadata header) are
// returned in the first entry with an empty section_header.
struct raw_section {
  std::string header;              // e.g. "[metadata]" — empty for preamble
  std::string section_name;        // e.g. "metadata"  — empty for preamble
  std::vector<std::string> lines;  // including blank lines / comments
};

std::vector<raw_section> read_sections(const std::filesystem::path &p) {
  std::vector<raw_section> out;
  std::ifstream in(p);
  if (!in.is_open()) {
    return out;
  }
  raw_section current;
  std::string line;
  while (std::getline(in, line)) {
    std::string trimmed = line;
    // strip trailing CR for Windows-line-ending tolerance
    while (!trimmed.empty() && (trimmed.back() == '\r' || trimmed.back() == '\n')) {
      trimmed.pop_back();
    }
    std::string t = trimmed;
    // trim leading whitespace for the section check only
    cforge_size_t s = t.find_first_not_of(" \t");
    if (s != std::string::npos) {
      t = t.substr(s);
    }
    if (!t.empty() && t.front() == '[' && t.back() == ']') {
      if (!current.header.empty() || !current.lines.empty()) {
        out.push_back(std::move(current));
        current = raw_section{};
      }
      current.header       = trimmed;
      current.section_name = t.substr(1, t.size() - 2);
      continue;
    }
    current.lines.push_back(trimmed);
  }
  if (!current.header.empty() || !current.lines.empty()) {
    out.push_back(std::move(current));
  }
  return out;
}

}  // namespace

bool dependency_hash::load(const std::filesystem::path &project_dir) {
  std::filesystem::path lock_file = project_dir / HASH_FILE;

  // One-time migration: if an old cforge.hash exists, read it once and let
  // the next save() consolidate the data into cforge.lock. After save the
  // legacy file is removed by save().
  std::filesystem::path legacy = project_dir / "cforge.hash";
  std::filesystem::path source = std::filesystem::exists(lock_file)
                                   ? lock_file
                                   : (std::filesystem::exists(legacy) ? legacy : lock_file);
  if (!std::filesystem::exists(source)) {
    return false;
  }
  auto sections = read_sections(source);

  // Legacy cforge.hash used [config] and [dependency.X] section names that
  // collide with cforge.lock's lockfile entries. Translate them on the fly
  // when reading the legacy file so load() returns consistent data.
  const bool legacy_mode = (source == legacy);
  if (legacy_mode) {
    for (auto &sec : sections) {
      if (sec.section_name == "config") {
        sec.section_name = kBuildcacheSection;
      } else if (sec.section_name.rfind("dependency.", 0) == 0) {
        sec.section_name = kBuildcacheDepPrefix
                         + sec.section_name.substr(std::strlen("dependency."));
      }
    }
  }
  hashes.clear();
  versions.clear();

  for (const auto &sec : sections) {
    if (!is_buildcache_section(sec.section_name)) {
      continue;
    }

    std::string dep_name;
    if (sec.section_name.rfind(kBuildcacheDepPrefix, 0) == 0) {
      dep_name = sec.section_name.substr(std::strlen(kBuildcacheDepPrefix));
    }

    for (const auto &raw_line : sec.lines) {
      std::string line = trim(raw_line);
      if (line.empty() || line[0] == '#') {
        continue;
      }
      cforge_size_t eq_pos = line.find('=');
      if (eq_pos == std::string::npos) {
        continue;
      }
      std::string key   = trim(line.substr(0, eq_pos));
      std::string value = trim(line.substr(eq_pos + 1));
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }
      if (dep_name.empty()) {
        // top-level [buildcache] section: config & workspace hashes
        if (key == "config_hash" || key == "hash") {
          hashes["cforge.toml"] = value;
        } else if (key == "workspace_hash") {
          hashes["cforge.workspace.toml"] = value;
        }
      } else {
        // per-dependency [buildcache.dependency.<name>] section
        if (key == "hash") {
          hashes[dep_name] = value;
        } else if (key == "version") {
          versions[dep_name] = value;
        }
      }
    }
  }
  return true;
}

bool dependency_hash::save(const std::filesystem::path &project_dir) const {
  std::filesystem::path lock_file = project_dir / HASH_FILE;

  // Migration: if a legacy cforge.hash is still on disk, remove it now that
  // the data lives in cforge.lock. We don't need to read it again — load()
  // already pulled the contents into `hashes`/`versions` before save().
  std::error_code ec;
  std::filesystem::path legacy = project_dir / "cforge.hash";
  if (std::filesystem::exists(legacy)) {
    std::filesystem::remove(legacy, ec);
  }

  // Read everything currently in cforge.lock and keep sections that belong
  // to the lockfile class (or anyone else) verbatim. We only rewrite our
  // own [buildcache.*] sections at the end.
  auto sections = read_sections(lock_file);
  std::vector<raw_section> preserved;
  preserved.reserve(sections.size());
  for (auto &s : sections) {
    if (!is_buildcache_section(s.section_name)) {
      preserved.push_back(std::move(s));
    }
  }

  std::ofstream file(lock_file, std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }

  bool wrote_anything_yet = false;
  // If the original file had no preamble at all, emit our own header so the
  // file remains self-documenting after a fresh save.
  if (preserved.empty()) {
    file << "# cforge.lock - DO NOT EDIT MANUALLY\n"
         << "# Tracks dependency versions AND build-cache state for "
            "incremental rebuilds.\n\n";
  }

  for (const auto &sec : preserved) {
    if (!sec.header.empty()) {
      file << sec.header << "\n";
    }
    for (const auto &l : sec.lines) {
      file << l << "\n";
    }
    wrote_anything_yet = true;
  }

  if (wrote_anything_yet
      && (!preserved.empty()
          && (preserved.back().lines.empty() || !preserved.back().lines.back().empty()))) {
    file << "\n";
  }

  // Top-level buildcache: cforge.toml / workspace hashes.
  bool emitted_buildcache_header = false;
  auto emit_header               = [&]() {
    if (!emitted_buildcache_header) {
      file << "[" << kBuildcacheSection << "]\n";
      file << "# Auto-generated. Tracks change detection for incremental "
                            "builds.\n";
      file << "generated = \"" << get_timestamp() << "\"\n";
      emitted_buildcache_header = true;
    }
  };
  for (const auto &[name, hash] : hashes) {
    if (name == "cforge.toml") {
      emit_header();
      file << "config_hash = \"" << hash << "\"\n";
    } else if (name == "cforge.workspace.toml") {
      emit_header();
      file << "workspace_hash = \"" << hash << "\"\n";
    }
  }
  if (emitted_buildcache_header) {
    file << "\n";
  }

  // Per-dependency [buildcache.dependency.X].
  for (const auto &[name, hash] : hashes) {
    if (name == "cforge.toml" || name == "cforge.workspace.toml") {
      continue;
    }
    file << "[" << kBuildcacheDepPrefix << name << "]\n";
    file << "hash = \"" << hash << "\"\n";
    auto v = versions.find(name);
    if (v != versions.end() && !v->second.empty()) {
      file << "version = \"" << v->second << "\"\n";
    }
    file << "\n";
  }

  return true;
}

std::string dependency_hash::get_hash(const std::string &name) const {
  auto it = hashes.find(name);
  return it != hashes.end() ? it->second : "";
}

void dependency_hash::set_hash(const std::string &name, const std::string &hash) {
  hashes[name] = hash;
}

std::string dependency_hash::get_version(const std::string &name) const {
  auto it = versions.find(name);
  return it != versions.end() ? it->second : "";
}

void dependency_hash::set_version(const std::string &name, const std::string &version) {
  versions[name] = version;
}

cforge_ulong_t dependency_hash::fnv1a_hash(const std::string &str) {
  return fnv1a_hash(str.data(), str.size());
}

uint64_t dependency_hash::fnv1a_hash(const void *data, cforge_size_t size) {
  cforge_ulong_t hash        = FNV_OFFSET_BASIS;
  const cforge_byte_t *bytes = static_cast<const uint8_t *>(data);

  for (cforge_size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= FNV_PRIME;
  }

  return hash;
}

std::string dependency_hash::hash_to_string(cforge_ulong_t hash) {
  std::stringstream ss;
  ss << std::hex << std::setw(16) << std::setfill('0') << hash;
  return ss.str();
}

std::string dependency_hash::calculate_directory_hash(const std::filesystem::path &dir_path) {
  if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
    return "";
  }

  // Sort entries for consistent hashing
  std::vector<std::filesystem::path> entries;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(dir_path)) {
    entries.emplace_back(entry.path());
  }
  std::sort(entries.begin(), entries.end());

  // Calculate hash for each file
  cforge_ulong_t combined_hash = FNV_OFFSET_BASIS;
  for (const auto &entry : entries) {
    if (std::filesystem::is_regular_file(entry)) {
      // Add file path relative to dir_path
      std::string rel_path  = std::filesystem::relative(entry, dir_path).string();
      combined_hash        ^= fnv1a_hash(rel_path);

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

}  // namespace cforge
