#include "core/workspace_utils.hpp"
#include "core/constants.h"
#include "core/toml_reader.hpp"
#include <toml++/toml.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <map>
#include <set>
#include <stack>

namespace cforge {

std::vector<std::string>
uppercase_generators(const std::vector<std::string> &generators) {
  std::vector<std::string> result;
  for (const auto &gen : generators) {
    std::string upper_gen = gen;
    std::transform(upper_gen.begin(), upper_gen.end(), upper_gen.begin(),
                   ::toupper);
    result.push_back(upper_gen);
  }
  return result;
}

std::string join_strings(const std::vector<std::string> &strings,
                                const std::string &delimiter) {
  std::string result;
  bool first = true;

  for (const auto &str : strings) {
    if (!first) {
      result += delimiter;
    }
    result += str;
    first = false;
  }

  return result;
}


std::vector<std::string> get_workspace_projects(
    const std::filesystem::path &workspace_dir) {
  std::vector<std::string> projects;
  for (const auto &entry : std::filesystem::directory_iterator(workspace_dir)) {
    if (entry.is_directory() &&
        std::filesystem::exists(entry.path() / CFORGE_FILE)) {
      projects.push_back(entry.path().filename().string());
    }
  }
  return projects;
}

static bool dfs_visit(const std::string &proj,
                      const std::map<std::string, std::vector<std::string>> &deps,
                      std::set<std::string> &visiting,
                      std::set<std::string> &visited,
                      std::vector<std::string> &out) {
  if (visited.count(proj)) return true;
  if (visiting.count(proj)) return false; // cycle
  visiting.insert(proj);
  auto it = deps.find(proj);
  if (it != deps.end()) {
    for (const auto &dep : it->second) {
      if (!dfs_visit(dep, deps, visiting, visited, out)) return false;
    }
  }
  visiting.erase(proj);
  visited.insert(proj);
  out.push_back(proj);
  return true;
}

std::vector<std::string> topo_sort_projects(
    const std::filesystem::path &workspace_dir,
    const std::vector<std::string> &projects) {
  // Build dependency map
  std::map<std::string, std::vector<std::string>> deps_map;
  for (const auto &proj : projects) {
    std::filesystem::path ppath = workspace_dir / proj / CFORGE_FILE;
    toml_reader reader;
    if (reader.load(ppath.string()) && reader.has_key("dependencies.project")) {
      deps_map[proj] = reader.get_table_keys("dependencies.project");
    } else {
      deps_map[proj] = {};
    }
  }

  std::vector<std::string> sorted;
  std::set<std::string> visiting, visited;
  for (const auto &proj : projects) {
    if (!visited.count(proj)) {
      dfs_visit(proj, deps_map, visiting, visited, sorted);
    }
  }
  return sorted;
}

} // namespace cforge 