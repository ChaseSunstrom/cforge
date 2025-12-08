/**
 * @file registry.hpp
 * @brief Package registry for cforge-index integration
 */

#ifndef CFORGE_REGISTRY_HPP
#define CFORGE_REGISTRY_HPP

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Dependency source type
 */
enum class dependency_source {
  INDEX,   // From cforge-index registry (default)
  GIT,     // Direct git repository
  VCPKG,   // vcpkg package
  SYSTEM,  // System library
  PROJECT  // Local project/path
};

/**
 * @brief Feature definition from package index
 */
struct package_feature {
  std::string name;
  std::string cmake_option;
  std::string description;
  std::vector<std::string> required_deps;
  std::map<std::string, std::vector<std::string>>
      required_features; // dep -> features
};

/**
 * @brief Version entry from package index
 */
struct package_version {
  std::string version;
  std::string tag;
  int min_cpp = 11;
  std::string checksum;
  bool yanked = false;
  std::string yanked_reason;
};

/**
 * @brief Package integration info
 */
struct package_integration {
  std::string type; // "cmake", "header_only", "pkg-config"
  std::string cmake_target;
  std::string include_dir;
  std::string single_header;
  std::string cmake_subdir;
  std::string header_only_option;
  std::map<std::string, std::string> cmake_options;
};

/**
 * @brief Package definition from cforge-index
 */
struct package_info {
  std::string name;
  std::string description;
  std::string repository;
  std::string homepage;
  std::string documentation;
  std::string license;
  std::vector<std::string> keywords;
  std::vector<std::string> categories;
  bool verified = false;

  package_integration integration;
  std::map<std::string, package_feature> features;
  std::vector<std::string> default_features;
  std::vector<package_version> versions;

  std::vector<std::string> maintainer_owners;
  std::vector<std::string> maintainer_authors;
};

/**
 * @brief Resolved dependency with all information needed for build
 */
struct resolved_dependency {
  std::string name;
  dependency_source source;

  // For index/git sources
  std::string repository;
  std::string version;
  std::string tag;
  std::string branch;
  std::string commit;

  // For vcpkg
  std::string vcpkg_name;

  // For system
  std::string pkg_config_name;

  // For project
  std::string path;

  // Common options
  bool header_only = false;
  bool link = true;
  std::vector<std::string> features;
  std::map<std::string, std::string> cmake_options;

  // Resolved from registry
  std::string cmake_target;
  std::string include_dir;
};

/**
 * @brief Dependency specification from cforge.toml
 */
struct dependency_spec {
  std::string name;
  std::string version; // Can be "1.2.3", "1.2.*", "1.*", "*"
  dependency_source source = dependency_source::INDEX;

  // Source-specific fields
  std::string git_url;
  std::string git_tag;
  std::string git_branch;
  std::string git_commit;
  std::string vcpkg_name;
  std::string path;

  // Options
  bool header_only = false;
  bool link = true;
  bool default_features = true;
  std::vector<std::string> features;
};

/**
 * @brief Package registry for fetching and caching package information
 */
class registry {
public:
  /**
   * @brief Constructor
   * @param cache_dir Directory to cache the index (default: ~/.cforge/registry)
   */
  explicit registry(
      const std::filesystem::path &cache_dir = get_default_cache_dir());

  /**
   * @brief Update the local index cache from remote
   * @param force Force update even if recently updated
   * @return true if successful
   */
  bool update(bool force = false);

  /**
   * @brief Check if the index needs updating
   * @return true if update is recommended
   */
  bool needs_update() const;

  /**
   * @brief Search for packages matching a query
   * @param query Search query (matches name, description, keywords)
   * @param limit Maximum number of results
   * @return Vector of matching package names
   */
  std::vector<std::string> search(const std::string &query,
                                  size_t limit = 20) const;

  /**
   * @brief Get package information
   * @param name Package name
   * @return Package info if found
   */
  std::optional<package_info> get_package(const std::string &name) const;

  /**
   * @brief Resolve a version specification to an exact version
   * @param name Package name
   * @param version_spec Version specification (e.g., "1.2.*", "*")
   * @return Resolved version string, or empty if not found
   */
  std::string resolve_version(const std::string &name,
                              const std::string &version_spec) const;

  /**
   * @brief Resolve a dependency specification to a full resolved dependency
   * @param spec Dependency specification from cforge.toml
   * @return Resolved dependency with all build information
   */
  std::optional<resolved_dependency>
  resolve_dependency(const dependency_spec &spec) const;

  /**
   * @brief Get all available packages
   * @return Vector of all package names
   */
  std::vector<std::string> list_packages() const;

  /**
   * @brief Get the index repository URL
   * @return URL of the cforge-index repository
   */
  static std::string get_index_url();

  /**
   * @brief Get the default cache directory
   * @return Path to default cache directory
   */
  static std::filesystem::path get_default_cache_dir();

  /**
   * @brief Parse a dependency source string
   * @param source Source string ("index", "git", "vcpkg", "system", "project")
   * @return Corresponding dependency_source enum
   */
  static dependency_source parse_source(const std::string &source);

  /**
   * @brief Convert dependency source to string
   * @param source Dependency source enum
   * @return String representation
   */
  static std::string source_to_string(dependency_source source);

private:
  std::filesystem::path cache_dir_;
  std::filesystem::path index_dir_;

  /**
   * @brief Load a package definition from the index
   * @param name Package name
   * @return Package info if found
   */
  std::optional<package_info>
  load_package_file(const std::string &name) const;

  /**
   * @brief Check if a version matches a version specification
   * @param version Actual version (e.g., "1.2.3")
   * @param spec Version specification (e.g., "1.2.*")
   * @return true if version matches spec
   */
  static bool version_matches(const std::string &version,
                              const std::string &spec);

  /**
   * @brief Compare two semantic versions
   * @param v1 First version
   * @param v2 Second version
   * @return -1 if v1 < v2, 0 if equal, 1 if v1 > v2
   */
  static int compare_versions(const std::string &v1, const std::string &v2);

  /**
   * @brief Parse a semantic version string
   * @param version Version string
   * @return Vector of version components [major, minor, patch]
   */
  static std::vector<int> parse_version(const std::string &version);
};

/**
 * @brief Parse dependencies from cforge.toml
 * @param config_path Path to cforge.toml
 * @return Vector of dependency specifications
 */
std::vector<dependency_spec>
parse_dependencies(const std::filesystem::path &config_path);

/**
 * @brief Parse a single dependency entry from TOML
 * @param name Dependency name
 * @param value TOML value (string version or table)
 * @return Dependency specification
 */
dependency_spec parse_dependency_entry(const std::string &name,
                                        const std::string &table_prefix);

} // namespace cforge

#endif // CFORGE_REGISTRY_HPP
