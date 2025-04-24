/**
 * @file workspace.hpp
 * @brief Workspace management utilities for cforge
 */

#pragma once

#include "core/toml_reader.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Utility function to split a comma-separated list of project names
 *
 * @param project_list String containing comma-separated project names
 * @return Vector of individual project names
 */
inline std::vector<std::string>
split_project_list(const std::string &project_list) {
  std::vector<std::string> result;
  std::string::size_type start = 0;
  std::string::size_type end = 0;

  while ((end = project_list.find(',', start)) != std::string::npos) {
    // Extract the substring and trim whitespace
    std::string project = project_list.substr(start, end - start);
    // Add to result if not empty
    if (!project.empty()) {
      result.push_back(project);
    }
    start = end + 1;
  }

  // Add the last part
  std::string last_project = project_list.substr(start);
  if (!last_project.empty()) {
    result.push_back(last_project);
  }

  return result;
}

/**
 * @brief Represents a project within a workspace
 */
struct workspace_project {
  std::string name;
  std::filesystem::path path;
  bool is_startup = false;
  std::vector<std::string> dependencies;
  bool is_startup_project = false;
};

/**
 * @brief Class for managing workspace configuration
 */
class workspace_config {
public:
  /**
   * @brief Constructor
   */
  workspace_config();

  /**
   * @brief Constructor
   *
   * @param workspace_file Path to the workspace configuration file
   */
  workspace_config(const std::string &workspace_file);

  /**
   * @brief Load a workspace configuration file
   *
   * @param workspace_file Path to the workspace configuration file
   * @return true if successful
   */
  bool load(const std::string &workspace_file);

  /**
   * @brief Save the workspace configuration to a file
   *
   * @param workspace_file Path to save the configuration to
   * @return true if successful
   */
  bool save(const std::string &workspace_file) const;

  /**
   * @brief Get the startup project
   *
   * @return Pointer to the startup project, or nullptr if none
   */
  const workspace_project *get_startup_project() const;

  /**
   * @brief Check if a project exists in the workspace
   *
   * @param name Project name
   * @return true if the project exists
   */
  bool has_project(const std::string &name) const;

  /**
   * @brief Add a dependency to a project
   *
   * @param project_name Project name
   * @param dependency Dependency name
   * @return true if successful
   */
  bool add_project_dependency(const std::string &project_name,
                              const std::string &dependency);

  /**
   * @brief Get all projects in the workspace
   *
   * @return Vector of workspace projects
   */
  const std::vector<workspace_project> &get_projects() const;

  /**
   * @brief Get all projects in the workspace (non-const version)
   *
   * @return Vector of workspace projects
   */
  std::vector<workspace_project> &get_projects();

  /**
   * @brief Get the build order for projects
   *
   * @return Vector of project names in build order
   */
  std::vector<std::string> get_build_order() const;

  /**
   * @brief Set the startup project
   *
   * @param project_name Name of the project to set as startup
   * @return true if successful
   */
  bool set_startup_project(const std::string &project_name);

  /**
   * @brief Set the name of the workspace
   *
   * @param name Name of the workspace
   */
  void set_name(const std::string &name);

  /**
   * @brief Set the description of the workspace
   *
   * @param description Description of the workspace
   */

  void set_description(const std::string &description);

  /**
   * @brief Get the name of the workspace
   *
   * @return Name of the workspace
   */
  const std::string &get_name() const;

  /**
   * @brief Get the description of the workspace
   *
   * @return Description of the workspace
   */
  const std::string &get_description() const;

private:
  std::string name_;
  std::string description_;
  std::vector<workspace_project> projects_;
};
class workspace {
public:
  bool load(const std::filesystem::path &workspace_path);
  bool is_loaded() const;

  std::string get_name() const;
  std::filesystem::path get_path() const;
  std::vector<workspace_project> get_projects() const;

  workspace_project get_startup_project() const;
  bool set_startup_project(const std::string &project_name);

  bool build_all(const std::string &config, int num_jobs, bool verbose) const;
  bool build_project(const std::string &project_name, const std::string &config,
                     int num_jobs, bool verbose,
                     const std::string &target = "") const;

  bool run_startup_project(const std::vector<std::string> &args,
                           const std::string &config, bool verbose) const;
  bool run_project(const std::string &project_name,
                   const std::vector<std::string> &args,
                   const std::string &config, bool verbose) const;

  static bool is_workspace_dir(const std::filesystem::path &dir);
  static bool create_workspace(const std::filesystem::path &workspace_path,
                               const std::string &workspace_name);

  // Helper method to get a project by name
  const workspace_project *get_project_by_name(const std::string &name) const {
    auto it = std::find_if(
        projects_.begin(), projects_.end(),
        [&name](const workspace_project &p) { return p.name == name; });
    if (it != projects_.end()) {
      return &(*it);
    }
    return nullptr;
  }

private:
  void load_projects();

  std::unique_ptr<toml_reader> config_;
  std::string workspace_name_;
  std::filesystem::path workspace_path_;
  std::vector<workspace_project> projects_;
  std::string startup_project_;
};

} // namespace cforge