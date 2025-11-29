#ifndef CORE_WORKSPACE_UTILS_HPP
#define CORE_WORKSPACE_UTILS_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace cforge {

// List all project names (subdirectory names) in the workspace
std::vector<std::string> get_workspace_projects(
    const std::filesystem::path &workspace_dir);

// Topologically sort workspace projects by dependencies.project
std::vector<std::string> topo_sort_projects(
    const std::filesystem::path &workspace_dir,
    const std::vector<std::string> &projects);

// Note: is_in_workspace() is declared in workspace.hpp

} // namespace cforge

#endif // CORE_WORKSPACE_UTILS_HPP