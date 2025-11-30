/**
 * @file version.hpp
 * @brief Semantic version parsing and constraint matching
 *
 * Supports semver-style versions (1.2.3, v1.2.3, 1.2.3-beta)
 * and version constraints like:
 *   - Exact: "1.2.3" or "=1.2.3"
 *   - Range: ">=1.0.0,<2.0.0"
 *   - Caret: "^1.2.3" (compatible with 1.x.x)
 *   - Tilde: "~1.2.3" (compatible with 1.2.x)
 *   - Wildcard: "1.2.*" or "1.*"
 */

#pragma once

#include <algorithm>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include "core/process_utils.hpp"

namespace cforge {

/**
 * @brief Parsed semantic version
 */
struct semver {
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string prerelease; // e.g., "beta", "rc1"
  std::string build;      // Build metadata

  /**
   * @brief Parse a version string
   *
   * @param version_str Version string (e.g., "1.2.3", "v1.2.3-beta+build123")
   * @return Parsed version or nullopt if invalid
   */
  static std::optional<semver> parse(const std::string &version_str) {
    if (version_str.empty()) {
      return std::nullopt;
    }

    semver v;
    std::string str = version_str;

    // Remove leading 'v' if present
    if (str[0] == 'v' || str[0] == 'V') {
      str = str.substr(1);
    }

    // Extract build metadata (+...)
    size_t build_pos = str.find('+');
    if (build_pos != std::string::npos) {
      v.build = str.substr(build_pos + 1);
      str = str.substr(0, build_pos);
    }

    // Extract prerelease (-...)
    size_t pre_pos = str.find('-');
    if (pre_pos != std::string::npos) {
      v.prerelease = str.substr(pre_pos + 1);
      str = str.substr(0, pre_pos);
    }

    // Parse major.minor.patch
    std::vector<int> parts;
    std::stringstream ss(str);
    std::string part;

    while (std::getline(ss, part, '.')) {
      try {
        // Handle wildcards
        if (part == "*" || part == "x" || part == "X") {
          parts.push_back(-1); // -1 indicates wildcard
        } else {
          parts.push_back(std::stoi(part));
        }
      } catch (...) {
        return std::nullopt;
      }
    }

    if (parts.empty()) {
      return std::nullopt;
    }

    v.major = parts[0];
    v.minor = parts.size() > 1 ? parts[1] : 0;
    v.patch = parts.size() > 2 ? parts[2] : 0;

    return v;
  }

  /**
   * @brief Convert version to string
   */
  std::string to_string() const {
    std::string result =
        std::to_string(major) + "." + std::to_string(minor) + "." +
        std::to_string(patch);

    if (!prerelease.empty()) {
      result += "-" + prerelease;
    }

    if (!build.empty()) {
      result += "+" + build;
    }

    return result;
  }

  /**
   * @brief Compare two versions
   * @return -1 if this < other, 0 if equal, 1 if this > other
   */
  int compare(const semver &other) const {
    if (major != other.major)
      return major < other.major ? -1 : 1;
    if (minor != other.minor)
      return minor < other.minor ? -1 : 1;
    if (patch != other.patch)
      return patch < other.patch ? -1 : 1;

    // Prerelease versions have lower precedence
    if (prerelease.empty() && !other.prerelease.empty())
      return 1;
    if (!prerelease.empty() && other.prerelease.empty())
      return -1;
    if (prerelease != other.prerelease)
      return prerelease < other.prerelease ? -1 : 1;

    return 0;
  }

  bool operator<(const semver &other) const { return compare(other) < 0; }
  bool operator<=(const semver &other) const { return compare(other) <= 0; }
  bool operator>(const semver &other) const { return compare(other) > 0; }
  bool operator>=(const semver &other) const { return compare(other) >= 0; }
  bool operator==(const semver &other) const { return compare(other) == 0; }
  bool operator!=(const semver &other) const { return compare(other) != 0; }
};

/**
 * @brief Single version constraint
 */
struct version_constraint {
  enum class op_type {
    EQ,  // =, exact match
    NE,  // !=
    LT,  // <
    LE,  // <=
    GT,  // >
    GE,  // >=
    CARET, // ^, compatible (same major)
    TILDE  // ~, approximately (same major.minor)
  };

  op_type op = op_type::EQ;
  semver version;

  /**
   * @brief Check if a version satisfies this constraint
   */
  bool satisfies(const semver &v) const {
    switch (op) {
    case op_type::EQ:
      return v == version;
    case op_type::NE:
      return v != version;
    case op_type::LT:
      return v < version;
    case op_type::LE:
      return v <= version;
    case op_type::GT:
      return v > version;
    case op_type::GE:
      return v >= version;
    case op_type::CARET:
      // ^1.2.3 means >=1.2.3 and <2.0.0
      // ^0.2.3 means >=0.2.3 and <0.3.0 (special case for 0.x)
      if (v < version)
        return false;
      if (version.major == 0) {
        return v.major == 0 && v.minor == version.minor;
      }
      return v.major == version.major;
    case op_type::TILDE:
      // ~1.2.3 means >=1.2.3 and <1.3.0
      if (v < version)
        return false;
      return v.major == version.major && v.minor == version.minor;
    }
    return false;
  }
};

/**
 * @brief Version requirement (possibly multiple constraints)
 */
class version_requirement {
public:
  /**
   * @brief Parse a version requirement string
   *
   * Examples:
   *   "1.2.3"           -> exact version
   *   ">=1.0.0"         -> at least 1.0.0
   *   ">=1.0.0,<2.0.0"  -> range
   *   "^1.2.3"          -> compatible with 1.x.x
   *   "~1.2.3"          -> compatible with 1.2.x
   *   "*"               -> any version
   *
   * @param req_str Requirement string
   * @return Parsed requirement or nullopt if invalid
   */
  static std::optional<version_requirement> parse(const std::string &req_str) {
    version_requirement req;

    if (req_str.empty() || req_str == "*") {
      req.any_version_ = true;
      return req;
    }

    // Split by comma for multiple constraints
    std::vector<std::string> parts;
    std::stringstream ss(req_str);
    std::string part;

    while (std::getline(ss, part, ',')) {
      // Trim whitespace
      part.erase(0, part.find_first_not_of(" \t"));
      part.erase(part.find_last_not_of(" \t") + 1);

      if (!part.empty()) {
        parts.push_back(part);
      }
    }

    for (const auto &p : parts) {
      auto constraint = parse_constraint(p);
      if (!constraint) {
        return std::nullopt;
      }
      req.constraints_.push_back(*constraint);
    }

    return req;
  }

  /**
   * @brief Check if a version satisfies all constraints
   */
  bool satisfies(const semver &v) const {
    if (any_version_) {
      return true;
    }

    for (const auto &c : constraints_) {
      if (!c.satisfies(v)) {
        return false;
      }
    }

    return true;
  }

  /**
   * @brief Check if a version string satisfies the requirement
   */
  bool satisfies(const std::string &version_str) const {
    auto v = semver::parse(version_str);
    if (!v) {
      return false;
    }
    return satisfies(*v);
  }

  /**
   * @brief Get the constraints
   */
  const std::vector<version_constraint> &constraints() const {
    return constraints_;
  }

  /**
   * @brief Check if this accepts any version
   */
  bool accepts_any() const { return any_version_; }

private:
  std::vector<version_constraint> constraints_;
  bool any_version_ = false;

  static std::optional<version_constraint>
  parse_constraint(const std::string &str) {
    version_constraint c;
    std::string version_part;

    if (str.empty()) {
      return std::nullopt;
    }

    // Check for operator prefix
    if (str[0] == '^') {
      c.op = version_constraint::op_type::CARET;
      version_part = str.substr(1);
    } else if (str[0] == '~') {
      c.op = version_constraint::op_type::TILDE;
      version_part = str.substr(1);
    } else if (str.length() > 1 && str[0] == '>' && str[1] == '=') {
      c.op = version_constraint::op_type::GE;
      version_part = str.substr(2);
    } else if (str.length() > 1 && str[0] == '<' && str[1] == '=') {
      c.op = version_constraint::op_type::LE;
      version_part = str.substr(2);
    } else if (str.length() > 1 && str[0] == '!' && str[1] == '=') {
      c.op = version_constraint::op_type::NE;
      version_part = str.substr(2);
    } else if (str[0] == '>') {
      c.op = version_constraint::op_type::GT;
      version_part = str.substr(1);
    } else if (str[0] == '<') {
      c.op = version_constraint::op_type::LT;
      version_part = str.substr(1);
    } else if (str[0] == '=') {
      c.op = version_constraint::op_type::EQ;
      version_part = str.substr(1);
    } else {
      // No operator means exact match
      c.op = version_constraint::op_type::EQ;
      version_part = str;
    }

    // Trim whitespace from version part
    version_part.erase(0, version_part.find_first_not_of(" \t"));
    version_part.erase(version_part.find_last_not_of(" \t") + 1);

    auto v = semver::parse(version_part);
    if (!v) {
      return std::nullopt;
    }

    c.version = *v;
    return c;
  }
};

/**
 * @brief Find the best matching version from a list
 *
 * @param available Available versions
 * @param requirement Version requirement
 * @return Best matching version or nullopt if none match
 */
inline std::optional<std::string>
find_best_version(const std::vector<std::string> &available,
                  const version_requirement &requirement) {
  std::optional<semver> best;
  std::string best_str;

  for (const auto &v_str : available) {
    auto v = semver::parse(v_str);
    if (!v) {
      continue;
    }

    if (requirement.satisfies(*v)) {
      if (!best || *v > *best) {
        best = v;
        best_str = v_str;
      }
    }
  }

  if (best) {
    return best_str;
  }

  return std::nullopt;
}

/**
 * @brief Get available Git tags for a repository
 *
 * @param repo_dir Repository directory
 * @return Vector of tag names
 */
inline std::vector<std::string>
get_git_tags(const std::filesystem::path &repo_dir) {
  std::vector<std::string> tags;

  process_result result =
      execute_process("git", {"tag", "-l"}, repo_dir.string());

  if (result.success) {
    std::istringstream iss(result.stdout_output);
    std::string line;
    while (std::getline(iss, line)) {
      // Trim whitespace
      line.erase(0, line.find_first_not_of(" \t\r\n"));
      line.erase(line.find_last_not_of(" \t\r\n") + 1);
      if (!line.empty()) {
        tags.push_back(line);
      }
    }
  }

  return tags;
}

} // namespace cforge
