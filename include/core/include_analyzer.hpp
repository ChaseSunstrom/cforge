/**
 * @file include_analyzer.hpp
 * @brief Include graph analysis for circular dependency detection
 */

#pragma once

#include "types.h"

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Represents a circular dependency chain
 */
struct circular_chain {
  std::vector<std::string> files; ///< Files in the chain (last = first to show cycle)
  std::string root;               ///< The file where the cycle starts
};

/**
 * @brief Result of include analysis
 */
struct include_analysis_result {
  std::vector<circular_chain> chains; ///< All circular dependency chains found
  std::map<std::string, std::vector<std::string>>
      include_graph; ///< Full include graph
  cforge_int_t total_files_analyzed = 0;
  bool has_cycles = false;
};

/**
 * @brief Analyzer for detecting circular include dependencies
 */
class include_analyzer {
public:
  /**
   * @brief Constructor
   * @param project_dir The project directory to analyze
   * @param include_paths Additional include paths to search
   */
  include_analyzer(const std::filesystem::path &project_dir,
                   const std::vector<std::filesystem::path> &include_paths = {});

  /**
   * @brief Analyze the project for circular includes
   * @param include_deps Whether to also analyze dependency headers
   * @return Analysis result with all circular chains found
   */
  include_analysis_result analyze(bool include_deps = false);

  /**
   * @brief Add additional include paths to search
   */
  void add_include_path(const std::filesystem::path &path);

  /**
   * @brief Set file extensions to analyze
   * @param extensions Vector of extensions (e.g., {".hpp", ".h", ".cpp"})
   */
  void set_extensions(const std::vector<std::string> &extensions);

  /**
   * @brief Set directories to exclude from analysis
   */
  void set_excluded_dirs(const std::vector<std::string> &dirs);

private:
  std::filesystem::path project_dir_;
  std::vector<std::filesystem::path> include_paths_;
  std::vector<std::string> extensions_ = {".hpp", ".h", ".cpp", ".c", ".cc",
                                          ".cxx", ".hxx"};
  std::vector<std::string> excluded_dirs_ = {"build", "vendor", "deps",
                                             "third_party", "external", "node_modules"};

  /**
   * @brief Parse a source file for #include directives
   * @param file_path Path to the file
   * @return Vector of included file paths (relative or resolved)
   */
  std::vector<std::string> parse_includes(const std::filesystem::path &file_path);

  /**
   * @brief Resolve an include path to an actual file
   * @param include_path The path from the #include directive
   * @param from_file The file containing the include
   * @return Resolved path or empty if not found
   */
  std::filesystem::path resolve_include(const std::string &include_path,
                                         const std::filesystem::path &from_file);

  /**
   * @brief Build the full include graph
   * @param include_deps Whether to include dependency files
   * @return Map of file -> included files
   */
  std::map<std::string, std::vector<std::string>>
  build_include_graph(bool include_deps);

  /**
   * @brief Find all cycles in the include graph using DFS
   * @param graph The include graph
   * @return Vector of all circular chains
   */
  std::vector<circular_chain>
  find_cycles(const std::map<std::string, std::vector<std::string>> &graph);

  /**
   * @brief DFS helper for cycle detection
   */
  void dfs_find_cycles(
      const std::string &node,
      const std::map<std::string, std::vector<std::string>> &graph,
      std::set<std::string> &visited, std::set<std::string> &rec_stack,
      std::vector<std::string> &path, std::vector<circular_chain> &cycles);

  /**
   * @brief Check if a path is within excluded directories
   */
  bool is_excluded(const std::filesystem::path &path) const;

  /**
   * @brief Check if a file has an analyzed extension
   */
  bool has_valid_extension(const std::filesystem::path &path) const;

  /**
   * @brief Normalize path for consistent comparison
   */
  std::string normalize_path(const std::filesystem::path &path) const;
};

/**
 * @brief Format circular chains as a tree-like display
 * @param chains The circular chains to format
 * @return Formatted string for display
 */
std::string format_circular_chains(const std::vector<circular_chain> &chains);

/**
 * @brief Format circular chains as JSON
 * @param chains The circular chains to format
 * @return JSON string
 */
std::string format_circular_chains_json(const std::vector<circular_chain> &chains);

} // namespace cforge
