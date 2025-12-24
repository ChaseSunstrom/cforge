/**
 * @file benchmark_runner.hpp
 * @brief Benchmark runner class for cforge's benchmarking system
 */

#pragma once

#include "core/benchmark_framework.hpp"
#include "core/toml_reader.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>

namespace cforge {

/**
 * @brief Benchmark execution options
 */
struct benchmark_run_options {
  std::string build_config{"Release"};
  std::string filter;
  bool no_build = false;
  bool json_output = false;
  bool csv_output = false;
  bool verbose = false;
  int repetitions = 1;
};

/**
 * @brief Benchmark runner - orchestrates benchmark discovery, building, and execution
 */
class benchmark_runner {
public:
  /**
   * @brief Construct a benchmark runner
   * @param project_dir Path to the project root
   * @param config Project configuration
   */
  benchmark_runner(const std::filesystem::path &project_dir,
                   const toml_reader &config);

  ~benchmark_runner();

  /**
   * @brief Load benchmark configuration from cforge.toml
   * @return true if configuration was loaded successfully
   */
  bool load_config();

  /**
   * @brief Get the loaded benchmark configuration
   */
  const benchmark_config &get_config() const { return m_bench_config; }

  /**
   * @brief Discover benchmark targets (auto + explicit)
   * @return Vector of discovered benchmark targets
   */
  std::vector<benchmark_target> discover_targets();

  /**
   * @brief Detect framework from source file content
   * @param source_file Path to the source file
   * @return Detected framework type
   */
  benchmark_framework detect_framework(const std::filesystem::path &source_file);

  /**
   * @brief Build benchmark executables
   * @param config Build configuration (Debug/Release)
   * @param verbose Show verbose output
   * @return true if build succeeded
   */
  bool build_benchmarks(const std::string &config, bool verbose);

  /**
   * @brief Run benchmarks with given options
   * @param options Benchmark execution options
   * @return Benchmark summary with results
   */
  benchmark_summary run_benchmarks(const benchmark_run_options &options);

  /**
   * @brief Get all benchmark results from the last run
   */
  const std::vector<benchmark_result> &get_results() const { return m_results; }

  /**
   * @brief Get error message if any operation failed
   */
  const std::string &get_error() const { return m_error; }

private:
  std::filesystem::path m_project_dir;
  const toml_reader &m_project_config;
  benchmark_config m_bench_config;
  std::vector<benchmark_result> m_results;
  std::string m_error;

  // Framework adapters (lazily created)
  std::map<benchmark_framework, std::unique_ptr<i_benchmark_framework_adapter>> m_adapters;

  /**
   * @brief Get or create adapter for a framework
   */
  i_benchmark_framework_adapter *get_adapter(benchmark_framework fw);

  /**
   * @brief Generate CMakeLists.txt for benchmark targets
   * @param target The benchmark target to generate for
   * @return true if generation succeeded
   */
  bool generate_benchmark_cmake(const benchmark_target &target);

  /**
   * @brief Configure CMake for benchmarks
   * @param target The benchmark target
   * @param build_config Build configuration
   * @return true if configuration succeeded
   */
  bool configure_cmake(const benchmark_target &target, const std::string &build_config);

  /**
   * @brief Build a specific benchmark target
   * @param target The benchmark target
   * @param build_config Build configuration
   * @return true if build succeeded
   */
  bool build_target(const benchmark_target &target, const std::string &build_config);

  /**
   * @brief Find benchmark executable for a target
   * @param target The benchmark target
   * @param build_config Build configuration
   * @return Path to executable, or empty if not found
   */
  std::filesystem::path find_benchmark_executable(const benchmark_target &target,
                                                   const std::string &build_config);

  /**
   * @brief Run a single benchmark target
   * @param target The benchmark target
   * @param options Execution options
   * @return Vector of benchmark results
   */
  std::vector<benchmark_result> run_target(const benchmark_target &target,
                                           const benchmark_run_options &options);

  /**
   * @brief Auto-discover benchmarks from source files
   * @return Vector of discovered targets
   */
  std::vector<benchmark_target> auto_discover_targets();

  /**
   * @brief Load explicitly defined benchmark targets from config
   * @return Vector of explicit targets
   */
  std::vector<benchmark_target> load_explicit_targets();

  /**
   * @brief Check if project should be auto-linked to benchmarks
   */
  bool should_auto_link_project() const;

  /**
   * @brief Get project library name for linking
   */
  std::string get_project_link_target() const;
};

/**
 * @brief Factory function to create benchmark framework adapters
 */
std::unique_ptr<i_benchmark_framework_adapter> create_benchmark_adapter(benchmark_framework fw);

} // namespace cforge
