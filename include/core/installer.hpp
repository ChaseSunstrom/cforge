/**
 * @file installer.h
 * @brief Installation utilities for cforge
 */

#ifndef CFORGE_INSTALLER_H
#define CFORGE_INSTALLER_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/toml_reader.hpp"
#include "core/types.h"

namespace cforge {

/**
 * @brief Class for handling cforge installation and updates
 */
class installer {
public:
  /**
   * @brief Constructor
   */
  installer() = default;

  /**
   * @brief Get the current version of cforge
   * @return Current version string
   */
  std::string get_current_version() const;

  /**
   * @brief Install cforge to a specified path
   * @param install_path Path to install cforge to. If empty, uses default path.
   * @param add_to_path Whether to add the installation bin directory to PATH
   * @return True if installation was successful
   */
  bool install(const std::string &install_path = "", bool add_to_path = false);

  /**
   * @brief Update cforge to the latest version
   * @return True if update was successful
   */
  bool update();

  /**
   * @brief Install a cforge project to a specified path
   * @param project_path Path to the project to install
   * @param install_path Path to install the project to. If empty, uses default
   * path.
   * @param add_to_path Whether to add the installation bin directory to PATH
   * @param project_name_override Optional override for the project name when
   * installing
   * @param build_config Build configuration for the project
   * @param env_var Environment variable to set for the project
   * @return True if installation was successful
   */
  bool install_project(const std::string &project_path,
                       const std::string &install_path = "",
                       bool add_to_path = false,
                       const std::string &project_name_override = "",
                       const std::string &build_config = "",
                       const std::string &env_var = "");

  /**
   * @brief Get the default installation path
   * @return Default installation path
   */
  std::string get_default_install_path() const;

  /**
   * @brief Check if cforge is installed
   * @return True if cforge is installed
   */
  bool is_installed() const;

  /**
   * @brief Get the path where cforge is installed
   * @return Path to cforge installation
   */
  std::string get_install_location() const;

  /**
   * @brief Set a custom environment variable to the given path
   * @param var_name Name of the environment variable to set
   * @param value Path to assign to the env var
   * @return True if succeeded
   */
  bool update_env_var(const std::string &var_name,
                      const std::filesystem::path &value) const;

private:
  /**
   * @brief Copy files from source to destination
   * @param source Source path
   * @param dest Destination path
   * @param exclude_patterns Patterns to exclude from copying
   * @return True if copy was successful
   */
  bool copy_files(const std::filesystem::path &source,
                  const std::filesystem::path &dest,
                  const std::vector<std::string> &exclude_patterns = {});

  /**
   * @brief Get platform-specific path for installation
   * @return Platform-specific install path
   */
  std::string get_platform_specific_path() const;

  /**
   * @brief Check if path is writable
   * @param path Path to check
   * @return True if path is writable
   */
  bool is_path_writeable(const std::filesystem::path &path) const;

  /**
   * @brief Create executable links or shortcuts
   * @param bin_path Path where binaries are installed
   * @return True if links were created successfully
   */
  bool create_executable_links(const std::filesystem::path &bin_path) const;

  /**
   * @brief Update PATH environment variable if needed
   * @param bin_path Path to add to PATH
   * @return True if PATH was updated successfully
   */
  bool update_path_env(const std::filesystem::path &bin_path) const;

  /**
   * @brief Read project configuration from TOML file
   * @param project_path Path to the project
   * @return toml_reader with loaded project data, or nullptr if failed
   */
  std::unique_ptr<toml_reader>
  read_project_config(const std::string &project_path) const;
};

} // namespace cforge

#endif // CFORGE_INSTALLER_H