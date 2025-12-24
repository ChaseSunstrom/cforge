/**
 * @file benchmark_framework.hpp
 * @brief Core data structures and interfaces for cforge's benchmarking system
 */

#pragma once

#include "types.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Supported benchmark frameworks
 */
enum class benchmark_framework {
  Auto,           // Auto-detect from source
  GoogleBench,    // Google Benchmark
  Nanobench,      // nanobench (header-only)
  Catch2Bench,    // Catch2's BENCHMARK macro
  Nonius,         // Nonius benchmark
  Celero          // Celero benchmark
};

/**
 * @brief Individual benchmark result
 */
struct benchmark_result {
  std::string name;           // Benchmark name (e.g., "BM_VectorPush")
  std::string suite;          // Benchmark suite/group

  // Timing metrics
  double time_ns = 0;         // Real time per iteration (nanoseconds)
  double cpu_time_ns = 0;     // CPU time per iteration (nanoseconds)
  int64_t iterations = 0;     // Number of iterations run

  // Additional metrics
  double bytes_per_second = 0;
  double items_per_second = 0;

  // Statistics
  double min_time_ns = 0;
  double max_time_ns = 0;
  double mean_time_ns = 0;
  double median_time_ns = 0;
  double stddev_ns = 0;

  // Status
  bool success = true;
  std::string error_message;

  // Time unit for display
  std::string time_unit = "ns";
};

/**
 * @brief Benchmark target configuration (from cforge.toml)
 */
struct benchmark_target {
  std::string name;
  std::vector<std::string> sources;       // Source files
  benchmark_framework framework = benchmark_framework::Auto;
  std::vector<std::string> dependencies;  // Link dependencies
  std::vector<std::string> defines;
  std::vector<std::string> includes;
  bool enabled = true;

  // Computed paths
  std::filesystem::path executable_path;
  std::vector<std::filesystem::path> source_files;
};

/**
 * @brief Global benchmark configuration (from [benchmark] section)
 */
struct benchmark_config {
  std::filesystem::path directory{"bench"};
  benchmark_framework default_framework = benchmark_framework::Auto;
  std::string default_build_type = "Release";
  bool auto_link_project = true;

  // Framework-specific settings
  struct FrameworkConfig {
    bool fetch = true;
    std::string version;
    std::map<std::string, std::string> options;
  };
  std::map<benchmark_framework, FrameworkConfig> framework_configs;

  // Discovered/explicit targets
  std::vector<benchmark_target> targets;
};

/**
 * @brief Benchmark run summary statistics
 */
struct benchmark_summary {
  int total = 0;
  int successful = 0;
  int failed = 0;
  std::chrono::milliseconds total_duration{0};

  // Results for summary
  std::vector<benchmark_result> results;
};

/**
 * @brief Convert BenchmarkFramework enum to string
 */
inline std::string benchmark_framework_to_string(benchmark_framework fw) {
  switch (fw) {
  case benchmark_framework::Auto:
    return "auto";
  case benchmark_framework::GoogleBench:
    return "google";
  case benchmark_framework::Nanobench:
    return "nanobench";
  case benchmark_framework::Catch2Bench:
    return "catch2";
  case benchmark_framework::Nonius:
    return "nonius";
  case benchmark_framework::Celero:
    return "celero";
  default:
    return "unknown";
  }
}

/**
 * @brief Convert string to BenchmarkFramework enum
 */
inline benchmark_framework string_to_benchmark_framework(const std::string &str) {
  if (str == "auto")
    return benchmark_framework::Auto;
  if (str == "google" || str == "googlebenchmark" || str == "gbench")
    return benchmark_framework::GoogleBench;
  if (str == "nanobench" || str == "nano")
    return benchmark_framework::Nanobench;
  if (str == "catch2" || str == "catch")
    return benchmark_framework::Catch2Bench;
  if (str == "nonius")
    return benchmark_framework::Nonius;
  if (str == "celero")
    return benchmark_framework::Celero;
  return benchmark_framework::Auto;
}

/**
 * @brief Format time duration for display
 */
inline std::string format_bench_time(double ns) {
  if (ns < 1000) {
    return std::to_string(static_cast<int>(ns)) + " ns";
  } else if (ns < 1000000) {
    return std::to_string(static_cast<int>(ns / 1000)) + " us";
  } else if (ns < 1000000000) {
    return std::to_string(static_cast<int>(ns / 1000000)) + " ms";
  } else {
    return std::to_string(ns / 1000000000) + " s";
  }
}

/**
 * @brief Abstract interface for framework-specific benchmark operations
 */
class i_benchmark_framework_adapter {
public:
  virtual ~i_benchmark_framework_adapter() = default;

  /**
   * @brief Get the framework type this adapter handles
   */
  virtual benchmark_framework get_framework() const = 0;

  /**
   * @brief Detect if source file uses this framework
   * @param source_content The content of a source file
   * @return true if this framework is detected
   */
  virtual bool detect_from_source(const std::string &source_content) const = 0;

  /**
   * @brief Generate CMake code to fetch/configure the framework
   * @param config Framework-specific configuration
   * @return CMake code as string
   */
  virtual std::string
  generate_cmake_setup(const benchmark_config::FrameworkConfig &config) const = 0;

  /**
   * @brief Get the CMake target name to link against
   * @return Target name (e.g., "benchmark::benchmark")
   */
  virtual std::string get_cmake_target() const = 0;

  /**
   * @brief Parse framework output into BenchmarkResults
   * @param output Raw output from benchmark execution
   * @return Vector of benchmark results
   */
  virtual std::vector<benchmark_result>
  parse_output(const std::string &output) const = 0;

  /**
   * @brief Get command-line args for filtering benchmarks
   * @param filter The filter pattern
   * @return Vector of arguments
   */
  virtual std::vector<std::string>
  get_filter_args(const std::string &filter) const = 0;

  /**
   * @brief Get command-line args for JSON output
   * @return Vector of arguments
   */
  virtual std::vector<std::string> get_json_args() const = 0;
};

} // namespace cforge
