/**
 * @file include_analyzer.cpp
 * @brief Implementation of include graph analysis for circular dependency detection
 */

#include "core/include_analyzer.hpp"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace cforge {

include_analyzer::include_analyzer(
    const std::filesystem::path &project_dir,
    const std::vector<std::filesystem::path> &include_paths)
    : project_dir_(project_dir), include_paths_(include_paths) {
  // Add project include directory if it exists
  std::filesystem::path include_dir = project_dir / "include";
  if (std::filesystem::exists(include_dir)) {
    include_paths_.push_back(include_dir);
  }
  // Add src directory as well
  std::filesystem::path src_dir = project_dir / "src";
  if (std::filesystem::exists(src_dir)) {
    include_paths_.push_back(src_dir);
  }
}

void include_analyzer::add_include_path(const std::filesystem::path &path) {
  include_paths_.push_back(path);
}

void include_analyzer::set_extensions(const std::vector<std::string> &extensions) {
  extensions_ = extensions;
}

void include_analyzer::set_excluded_dirs(const std::vector<std::string> &dirs) {
  excluded_dirs_ = dirs;
}

bool include_analyzer::is_excluded(const std::filesystem::path &path) const {
  std::string path_str = path.string();
  for (const auto &excluded : excluded_dirs_) {
    if (path_str.find(excluded) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool include_analyzer::has_valid_extension(const std::filesystem::path &path) const {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return std::find(extensions_.begin(), extensions_.end(), ext) != extensions_.end();
}

std::string include_analyzer::normalize_path(const std::filesystem::path &path) const {
  try {
    // Convert to relative path from project dir if possible
    if (path.is_absolute()) {
      auto rel = std::filesystem::relative(path, project_dir_);
      if (!rel.empty() && rel.string().find("..") != 0) {
        return rel.string();
      }
    }
    return path.string();
  } catch (...) {
    return path.string();
  }
}

std::vector<std::string>
include_analyzer::parse_includes(const std::filesystem::path &file_path) {
  std::vector<std::string> includes;

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return includes;
  }

  // Regex to match #include directives
  // Matches: #include "file.h" or #include <file.h>
  static std::regex include_regex(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");

  std::string line;
  while (std::getline(file, line)) {
    std::smatch match;
    if (std::regex_search(line, match, include_regex)) {
      includes.push_back(match[1].str());
    }
  }

  return includes;
}

std::filesystem::path
include_analyzer::resolve_include(const std::string &include_path,
                                   const std::filesystem::path &from_file) {
  // First, try relative to the file containing the include
  std::filesystem::path dir = from_file.parent_path();
  std::filesystem::path candidate = dir / include_path;
  if (std::filesystem::exists(candidate)) {
    return std::filesystem::canonical(candidate);
  }

  // Try project root
  candidate = project_dir_ / include_path;
  if (std::filesystem::exists(candidate)) {
    return std::filesystem::canonical(candidate);
  }

  // Try each include path
  for (const auto &inc_path : include_paths_) {
    candidate = inc_path / include_path;
    if (std::filesystem::exists(candidate)) {
      return std::filesystem::canonical(candidate);
    }
  }

  // Not found - return empty
  return {};
}

std::map<std::string, std::vector<std::string>>
include_analyzer::build_include_graph(bool include_deps) {
  std::map<std::string, std::vector<std::string>> graph;
  std::set<std::string> processed;

  // Queue of files to process
  std::vector<std::filesystem::path> to_process;

  // Find all source files in project
  try {
    for (auto &entry :
         std::filesystem::recursive_directory_iterator(project_dir_)) {
      if (entry.is_regular_file() && has_valid_extension(entry.path()) &&
          !is_excluded(entry.path())) {
        to_process.push_back(entry.path());
      }
    }
  } catch (...) {
    // Ignore filesystem errors
  }

  // Process each file
  while (!to_process.empty()) {
    std::filesystem::path current = to_process.back();
    to_process.pop_back();

    std::string norm_path = normalize_path(current);
    if (processed.count(norm_path)) {
      continue;
    }
    processed.insert(norm_path);

    // Parse includes
    std::vector<std::string> includes = parse_includes(current);
    std::vector<std::string> resolved_includes;

    for (const auto &inc : includes) {
      std::filesystem::path resolved = resolve_include(inc, current);
      if (!resolved.empty()) {
        std::string resolved_norm = normalize_path(resolved);

        // Check if this is a project file or a dependency
        bool is_project_file = false;
        try {
          auto rel = std::filesystem::relative(resolved, project_dir_);
          is_project_file =
              !rel.empty() && rel.string().find("..") != 0 && !is_excluded(resolved);
        } catch (...) {
        }

        if (is_project_file || include_deps) {
          resolved_includes.push_back(resolved_norm);

          // Add to processing queue if not yet processed
          if (!processed.count(resolved_norm) && is_project_file) {
            to_process.push_back(resolved);
          }
        }
      }
    }

    graph[norm_path] = resolved_includes;
  }

  return graph;
}

void include_analyzer::dfs_find_cycles(
    const std::string &node,
    const std::map<std::string, std::vector<std::string>> &graph,
    std::set<std::string> &visited, std::set<std::string> &rec_stack,
    std::vector<std::string> &path, std::vector<circular_chain> &cycles) {
  visited.insert(node);
  rec_stack.insert(node);
  path.push_back(node);

  auto it = graph.find(node);
  if (it != graph.end()) {
    for (const auto &neighbor : it->second) {
      if (!visited.count(neighbor)) {
        dfs_find_cycles(neighbor, graph, visited, rec_stack, path, cycles);
      } else if (rec_stack.count(neighbor)) {
        // Found a cycle!
        circular_chain chain;
        chain.root = neighbor;

        // Build the cycle path
        bool in_cycle = false;
        for (const auto &p : path) {
          if (p == neighbor) {
            in_cycle = true;
          }
          if (in_cycle) {
            chain.files.push_back(p);
          }
        }
        chain.files.push_back(neighbor); // Complete the cycle

        cycles.push_back(chain);
      }
    }
  }

  path.pop_back();
  rec_stack.erase(node);
}

std::vector<circular_chain> include_analyzer::find_cycles(
    const std::map<std::string, std::vector<std::string>> &graph) {
  std::vector<circular_chain> cycles;
  std::set<std::string> visited;
  std::set<std::string> rec_stack;
  std::vector<std::string> path;

  for (const auto &[node, _] : graph) {
    if (!visited.count(node)) {
      dfs_find_cycles(node, graph, visited, rec_stack, path, cycles);
    }
  }

  return cycles;
}

include_analysis_result include_analyzer::analyze(bool include_deps) {
  include_analysis_result result;

  // Build the include graph
  result.include_graph = build_include_graph(include_deps);
  result.total_files_analyzed = static_cast<int>(result.include_graph.size());

  // Find all cycles
  result.chains = find_cycles(result.include_graph);
  result.has_cycles = !result.chains.empty();

  return result;
}

std::string format_circular_chains(const std::vector<circular_chain> &chains) {
  if (chains.empty()) {
    return "No circular dependencies found.\n";
  }

  std::ostringstream ss;
  ss << "Circular Dependencies Found:\n\n";

  for (size_t i = 0; i < chains.size(); ++i) {
    const auto &chain = chains[i];

    // Print the chain as a tree
    for (size_t j = 0; j < chain.files.size(); ++j) {
      std::string prefix;
      if (j == 0) {
        prefix = "  ";
      } else {
        // Build tree prefix
        prefix = "  ";
        for (size_t k = 1; k < j; ++k) {
          prefix += "\xe2\x94\x82   "; // │
        }
        if (j == chain.files.size() - 1) {
          prefix += "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 "; // └──
        } else {
          prefix += "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 "; // ├──
        }
      }

      ss << prefix << chain.files[j];

      // Mark the circular reference
      if (j == chain.files.size() - 1) {
        ss << "  \xe2\x86\x90 CIRCULAR (back to root)"; // ←
      }
      ss << "\n";
    }
    ss << "\n";
  }

  ss << "Summary: " << chains.size() << " circular dependency chain"
     << (chains.size() == 1 ? "" : "s") << " detected\n";

  return ss.str();
}

std::string
format_circular_chains_json(const std::vector<circular_chain> &chains) {
  std::ostringstream ss;
  ss << "{\n";
  ss << "  \"circular_dependencies\": [\n";

  for (size_t i = 0; i < chains.size(); ++i) {
    const auto &chain = chains[i];
    ss << "    {\n";
    ss << "      \"root\": \"" << chain.root << "\",\n";
    ss << "      \"chain\": [";

    for (size_t j = 0; j < chain.files.size(); ++j) {
      ss << "\"" << chain.files[j] << "\"";
      if (j < chain.files.size() - 1) {
        ss << ", ";
      }
    }
    ss << "]\n";
    ss << "    }";
    if (i < chains.size() - 1) {
      ss << ",";
    }
    ss << "\n";
  }

  ss << "  ],\n";
  ss << "  \"total_chains\": " << chains.size() << "\n";
  ss << "}\n";

  return ss.str();
}

} // namespace cforge
