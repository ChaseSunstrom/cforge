/**
 * @file benchmark_runner.cpp
 * @brief Implementation of benchmark runner and framework adapters
 */

#include "core/benchmark_runner.hpp"
#include "core/process_utils.hpp"
#include "cforge/log.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace cforge {

// ============================================================================
// Google Benchmark Adapter
// ============================================================================

class google_benchmark_adapter : public i_benchmark_framework_adapter {
public:
  benchmark_framework get_framework() const override {
    return benchmark_framework::GoogleBench;
  }

  bool detect_from_source(const std::string &source_content) const override {
    return source_content.find("#include <benchmark/benchmark.h>") != std::string::npos ||
           source_content.find("#include \"benchmark/benchmark.h\"") != std::string::npos ||
           source_content.find("BENCHMARK(") != std::string::npos ||
           source_content.find("BENCHMARK_DEFINE_F") != std::string::npos ||
           source_content.find("BENCHMARK_REGISTER_F") != std::string::npos;
  }

  std::string generate_cmake_setup(const benchmark_config::FrameworkConfig &config) const override {
    std::string version = config.version.empty() ? "v1.8.3" : config.version;

    std::ostringstream cmake;
    cmake << "# Google Benchmark\n";
    cmake << "include(FetchContent)\n";
    cmake << "FetchContent_Declare(\n";
    cmake << "  benchmark\n";
    cmake << "  GIT_REPOSITORY https://github.com/google/benchmark.git\n";
    cmake << "  GIT_TAG        " << version << "\n";
    cmake << "  GIT_SHALLOW    TRUE\n";
    cmake << ")\n";
    cmake << "set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL \"\" FORCE)\n";
    cmake << "set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL \"\" FORCE)\n";
    cmake << "FetchContent_MakeAvailable(benchmark)\n\n";

    return cmake.str();
  }

  std::string get_cmake_target() const override {
    return "benchmark::benchmark benchmark::benchmark_main";
  }

  std::vector<benchmark_result> parse_output(const std::string &output) const override {
    std::vector<benchmark_result> results;

    // Google Benchmark output format:
    // BM_Name                     123 ns        123 ns     1234567
    // BM_Name/1024                456 ns        456 ns      234567

    std::regex bench_regex(
        R"(^(BM_\w+(?:/\d+)?(?:/\w+)?)\s+(\d+(?:\.\d+)?)\s+(ns|us|ms|s)\s+(\d+(?:\.\d+)?)\s+(ns|us|ms|s)\s+(\d+))");

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
      std::smatch match;
      if (std::regex_search(line, match, bench_regex)) {
        benchmark_result result;
        result.name = match[1].str();
        result.time_ns = std::stod(match[2].str());
        result.time_unit = match[3].str();
        result.cpu_time_ns = std::stod(match[4].str());
        result.iterations = std::stoll(match[6].str());

        // Convert to nanoseconds
        double multiplier = 1.0;
        if (result.time_unit == "us") multiplier = 1000;
        else if (result.time_unit == "ms") multiplier = 1000000;
        else if (result.time_unit == "s") multiplier = 1000000000;

        result.time_ns *= multiplier;
        result.cpu_time_ns *= multiplier;
        result.time_unit = "ns";
        result.success = true;

        results.push_back(result);
      }
    }

    return results;
  }

  std::vector<std::string> get_filter_args(const std::string &filter) const override {
    return {"--benchmark_filter=" + filter};
  }

  std::vector<std::string> get_json_args() const override {
    return {"--benchmark_format=json"};
  }
};

// ============================================================================
// Nanobench Adapter
// ============================================================================

class nanobench_adapter : public i_benchmark_framework_adapter {
public:
  benchmark_framework get_framework() const override {
    return benchmark_framework::Nanobench;
  }

  bool detect_from_source(const std::string &source_content) const override {
    return source_content.find("#include <nanobench.h>") != std::string::npos ||
           source_content.find("#define ANKERL_NANOBENCH_IMPLEMENT") != std::string::npos ||
           source_content.find("ankerl::nanobench") != std::string::npos;
  }

  std::string generate_cmake_setup(const benchmark_config::FrameworkConfig &config) const override {
    std::string version = config.version.empty() ? "v4.3.11" : config.version;

    std::ostringstream cmake;
    cmake << "# nanobench (header-only)\n";
    cmake << "include(FetchContent)\n";
    cmake << "FetchContent_Declare(\n";
    cmake << "  nanobench\n";
    cmake << "  GIT_REPOSITORY https://github.com/martinus/nanobench.git\n";
    cmake << "  GIT_TAG        " << version << "\n";
    cmake << "  GIT_SHALLOW    TRUE\n";
    cmake << ")\n";
    cmake << "FetchContent_MakeAvailable(nanobench)\n\n";

    return cmake.str();
  }

  std::string get_cmake_target() const override {
    return "nanobench";
  }

  std::vector<benchmark_result> parse_output(const std::string &output) const override {
    std::vector<benchmark_result> results;
    // Nanobench has various output formats, parse the default table format
    // |               ns/op |                op/s |    err% |          ins/op |

    std::regex bench_regex(
        R"(\|\s*([0-9,.]+)\s+(ns|us|ms|s)/op\s*\|\s*([0-9,.]+)\s*op/s\s*\|\s*([0-9.]+)%\s*\|.*\|\s*`(.+)`\s*)");

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
      std::smatch match;
      if (std::regex_search(line, match, bench_regex)) {
        benchmark_result result;
        result.name = match[5].str();

        std::string time_str = match[1].str();
        time_str.erase(std::remove(time_str.begin(), time_str.end(), ','), time_str.end());
        result.time_ns = std::stod(time_str);

        std::string unit = match[2].str();
        if (unit == "us") result.time_ns *= 1000;
        else if (unit == "ms") result.time_ns *= 1000000;
        else if (unit == "s") result.time_ns *= 1000000000;

        result.success = true;
        results.push_back(result);
      }
    }

    return results;
  }

  std::vector<std::string> get_filter_args(const std::string &) const override {
    return {}; // Nanobench doesn't have built-in filtering
  }

  std::vector<std::string> get_json_args() const override {
    return {}; // Nanobench uses different output methods
  }
};

// ============================================================================
// Catch2 Benchmark Adapter
// ============================================================================

class catch2_benchmark_adapter : public i_benchmark_framework_adapter {
public:
  benchmark_framework get_framework() const override {
    return benchmark_framework::Catch2Bench;
  }

  bool detect_from_source(const std::string &source_content) const override {
    return (source_content.find("#include <catch2/") != std::string::npos ||
            source_content.find("#include \"catch2/") != std::string::npos) &&
           source_content.find("BENCHMARK") != std::string::npos;
  }

  std::string generate_cmake_setup(const benchmark_config::FrameworkConfig &config) const override {
    std::string version = config.version.empty() ? "v3.5.2" : config.version;

    std::ostringstream cmake;
    cmake << "# Catch2 (with benchmark support)\n";
    cmake << "include(FetchContent)\n";
    cmake << "FetchContent_Declare(\n";
    cmake << "  Catch2\n";
    cmake << "  GIT_REPOSITORY https://github.com/catchorg/Catch2.git\n";
    cmake << "  GIT_TAG        " << version << "\n";
    cmake << "  GIT_SHALLOW    TRUE\n";
    cmake << ")\n";
    cmake << "FetchContent_MakeAvailable(Catch2)\n\n";

    return cmake.str();
  }

  std::string get_cmake_target() const override {
    return "Catch2::Catch2WithMain";
  }

  std::vector<benchmark_result> parse_output(const std::string &output) const override {
    std::vector<benchmark_result> results;
    // Catch2 benchmark output format varies, parse basic format

    std::regex bench_regex(R"(benchmark name\s+samples.+mean.+)");
    // More complex parsing would be needed for full Catch2 benchmark output

    return results;
  }

  std::vector<std::string> get_filter_args(const std::string &filter) const override {
    return {"-n", filter};
  }

  std::vector<std::string> get_json_args() const override {
    return {"--reporter", "json"};
  }
};

// ============================================================================
// Factory Function
// ============================================================================

std::unique_ptr<i_benchmark_framework_adapter> create_benchmark_adapter(benchmark_framework fw) {
  switch (fw) {
  case benchmark_framework::GoogleBench:
    return std::make_unique<google_benchmark_adapter>();
  case benchmark_framework::Nanobench:
    return std::make_unique<nanobench_adapter>();
  case benchmark_framework::Catch2Bench:
    return std::make_unique<catch2_benchmark_adapter>();
  default:
    // Default to Google Benchmark
    return std::make_unique<google_benchmark_adapter>();
  }
}

// ============================================================================
// Benchmark Runner Implementation
// ============================================================================

benchmark_runner::benchmark_runner(const fs::path &project_dir,
                                   const toml_reader &config)
    : m_project_dir(project_dir), m_project_config(config) {}

benchmark_runner::~benchmark_runner() = default;

bool benchmark_runner::load_config() {
  // Load benchmark directory
  m_bench_config.directory =
      m_project_dir /
      m_project_config.get_string("benchmark.directory", "bench");

  // Load framework
  std::string fw_str = m_project_config.get_string("benchmark.framework", "auto");
  m_bench_config.default_framework = string_to_benchmark_framework(fw_str);

  // Load build type
  m_bench_config.default_build_type =
      m_project_config.get_string("benchmark.build_type", "Release");

  // Load auto-link setting
  m_bench_config.auto_link_project =
      m_project_config.get_bool("benchmark.auto_link_project", true);

  return true;
}

std::vector<benchmark_target> benchmark_runner::discover_targets() {
  std::vector<benchmark_target> targets;

  // Auto-discover from benchmark directory
  auto auto_targets = auto_discover_targets();
  targets.insert(targets.end(), auto_targets.begin(), auto_targets.end());

  // Load explicit targets from [[benchmark.targets]]
  auto explicit_targets = load_explicit_targets();
  targets.insert(targets.end(), explicit_targets.begin(), explicit_targets.end());

  return targets;
}

std::vector<benchmark_target> benchmark_runner::auto_discover_targets() {
  std::vector<benchmark_target> targets;

  if (!fs::exists(m_bench_config.directory)) {
    return targets;
  }

  // Look for benchmark source files
  std::vector<fs::path> bench_sources;
  for (const auto &entry : fs::recursive_directory_iterator(m_bench_config.directory)) {
    if (!entry.is_regular_file()) continue;

    std::string ext = entry.path().extension().string();
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
      bench_sources.push_back(entry.path());
    }
  }

  if (bench_sources.empty()) {
    return targets;
  }

  // Group files by directory to create targets (similar to test runner)
  std::map<fs::path, std::vector<fs::path>> files_by_dir;
  for (const auto &file : bench_sources) {
    fs::path rel = fs::relative(file.parent_path(), m_bench_config.directory);
    files_by_dir[rel].push_back(file);
  }

  // Create targets based on directory structure
  if (files_by_dir.size() == 1 && files_by_dir.begin()->first == ".") {
    // Flat structure - single target for all benchmarks
    benchmark_target target;
    target.name = "benchmarks";
    for (const auto &src : bench_sources) {
      target.sources.push_back(src.string());
      target.source_files.push_back(src);
    }

    // Detect framework from first file
    target.framework = detect_framework(bench_sources[0]);
    targets.push_back(target);
  } else {
    // Multiple directories - one target per directory
    for (const auto &[dir, files] : files_by_dir) {
      benchmark_target target;

      // Create name from directory
      if (dir == ".") {
        target.name = "bench_root";
      } else {
        target.name = "bench_" + dir.string();
        // Replace path separators with underscores
        std::replace(target.name.begin(), target.name.end(), '/', '_');
        std::replace(target.name.begin(), target.name.end(), '\\', '_');
      }

      for (const auto &src : files) {
        target.sources.push_back(src.string());
        target.source_files.push_back(src);
      }

      // Detect framework from first file
      target.framework = detect_framework(files.front());
      targets.push_back(target);
    }
  }

  return targets;
}

std::vector<benchmark_target> benchmark_runner::load_explicit_targets() {
  std::vector<benchmark_target> targets;

  // Check for [[benchmark.targets]] array
  // TODO: Implement explicit target loading from TOML array of tables

  return targets;
}

benchmark_framework benchmark_runner::detect_framework(const fs::path &source_file) {
  if (!fs::exists(source_file)) {
    return m_bench_config.default_framework;
  }

  std::ifstream file(source_file);
  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());

  // Try each adapter
  auto adapters = {
      create_benchmark_adapter(benchmark_framework::GoogleBench),
      create_benchmark_adapter(benchmark_framework::Nanobench),
      create_benchmark_adapter(benchmark_framework::Catch2Bench)
  };

  for (auto &adapter : adapters) {
    if (adapter->detect_from_source(content)) {
      return adapter->get_framework();
    }
  }

  // Default to Google Benchmark if auto-detection fails
  return benchmark_framework::GoogleBench;
}

i_benchmark_framework_adapter *benchmark_runner::get_adapter(benchmark_framework fw) {
  if (fw == benchmark_framework::Auto) {
    fw = benchmark_framework::GoogleBench;
  }

  auto it = m_adapters.find(fw);
  if (it == m_adapters.end()) {
    m_adapters[fw] = create_benchmark_adapter(fw);
  }
  return m_adapters[fw].get();
}

bool benchmark_runner::generate_benchmark_cmake(const benchmark_target &target) {
  // Use .cforge directory to keep generated files hidden
  fs::path gen_dir = m_project_dir / ".cforge" / "bench" / target.name;
  fs::create_directories(gen_dir);

  fs::path cmake_file = gen_dir / "CMakeLists.txt";
  std::ofstream out(cmake_file);

  if (!out) {
    m_error = "Failed to create CMakeLists.txt for benchmark: " + target.name;
    return false;
  }

  auto adapter = get_adapter(target.framework);

  out << "cmake_minimum_required(VERSION 3.15)\n";
  out << "project(" << target.name << "_benchmark CXX)\n\n";

  // Set C++ standard from project config
  std::string cpp_std = m_project_config.get_string("project.cpp_standard", "17");
  out << "set(CMAKE_CXX_STANDARD " << cpp_std << ")\n";
  out << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

  // Optimization for benchmarks
  out << "if(NOT CMAKE_BUILD_TYPE)\n";
  out << "  set(CMAKE_BUILD_TYPE Release)\n";
  out << "endif()\n\n";

  // Framework setup
  benchmark_config::FrameworkConfig fw_config;
  out << adapter->generate_cmake_setup(fw_config);

  // Create executable
  out << "add_executable(" << target.name << "\n";
  for (const auto &src : target.source_files) {
    out << "  \"" << src.string() << "\"\n";
  }
  out << ")\n\n";

  // Include directories
  out << "target_include_directories(" << target.name << " PRIVATE\n";
  out << "  \"" << (m_project_dir / "include").string() << "\"\n";
  out << "  \"" << (m_project_dir / "src").string() << "\"\n";
  for (const auto &inc : target.includes) {
    out << "  \"" << inc << "\"\n";
  }
  out << ")\n\n";

  // Link benchmark framework
  out << "target_link_libraries(" << target.name << " PRIVATE\n";
  out << "  " << adapter->get_cmake_target() << "\n";
  for (const auto &dep : target.dependencies) {
    out << "  " << dep << "\n";
  }
  out << ")\n\n";

  // Defines
  if (!target.defines.empty()) {
    out << "target_compile_definitions(" << target.name << " PRIVATE\n";
    for (const auto &def : target.defines) {
      out << "  " << def << "\n";
    }
    out << ")\n";
  }

  out.close();
  return true;
}

bool benchmark_runner::configure_cmake(const benchmark_target &target,
                                        const std::string &build_config) {
  // Source is in .cforge, build output goes to .cforge as well
  fs::path gen_dir = m_project_dir / ".cforge" / "bench" / target.name;
  fs::path build_dir = gen_dir / "build";

  std::vector<std::string> cmake_args = {
      "cmake",
      "-S", gen_dir.string(),
      "-B", build_dir.string(),
      "-DCMAKE_BUILD_TYPE=" + build_config
  };

  // Add generator if on Windows
#ifdef _WIN32
  // Try Ninja first, fall back to VS
  cmake_args.push_back("-G");
  cmake_args.push_back("Ninja");
#endif

  std::string cmake_cmd = cmake_args[0];
  std::vector<std::string> args(cmake_args.begin() + 1, cmake_args.end());

  auto result = execute_process(cmake_cmd, args, m_project_dir.string(),
                               nullptr, nullptr);

  return result.exit_code == 0;
}

bool benchmark_runner::build_target(const benchmark_target &target,
                                     const std::string &build_config) {
  fs::path build_dir = m_project_dir / ".cforge" / "bench" / target.name / "build";

  std::vector<std::string> args = {
      "--build", build_dir.string(),
      "--config", build_config
  };

  auto result = execute_process("cmake", args, m_project_dir.string(),
                               nullptr, nullptr);

  return result.exit_code == 0;
}

bool benchmark_runner::build_benchmarks(const std::string &config, bool verbose) {
  auto targets = discover_targets();

  if (targets.empty()) {
    m_error = "No benchmark targets found";
    return false;
  }

  for (auto &target : targets) {
    logger::print_action("Generating", "CMake for " + target.name);

    if (!generate_benchmark_cmake(target)) {
      return false;
    }

    logger::print_action("Configuring", target.name);
    if (!configure_cmake(target, config)) {
      m_error = "Failed to configure benchmark: " + target.name;
      return false;
    }

    logger::print_action("Building", target.name);
    if (!build_target(target, config)) {
      m_error = "Failed to build benchmark: " + target.name;
      return false;
    }
  }

  return true;
}

fs::path benchmark_runner::find_benchmark_executable(const benchmark_target &target,
                                                      const std::string &build_config) {
  fs::path build_dir = m_project_dir / ".cforge" / "bench" / target.name / "build";

  // Check various possible locations
  std::vector<fs::path> search_paths = {
      build_dir / build_config / (target.name + ".exe"),
      build_dir / build_config / target.name,
      build_dir / (target.name + ".exe"),
      build_dir / target.name,
      build_dir / "bin" / build_config / (target.name + ".exe"),
      build_dir / "bin" / build_config / target.name,
      build_dir / "bin" / (target.name + ".exe"),
      build_dir / "bin" / target.name
  };

  for (const auto &path : search_paths) {
    if (fs::exists(path)) {
      return path;
    }
  }

  return {};
}

std::vector<benchmark_result> benchmark_runner::run_target(
    const benchmark_target &target,
    const benchmark_run_options &options) {

  fs::path exe = find_benchmark_executable(target, options.build_config);
  if (exe.empty()) {
    benchmark_result error_result;
    error_result.name = target.name;
    error_result.success = false;
    error_result.error_message = "Executable not found";
    return {error_result};
  }

  auto adapter = get_adapter(target.framework);

  std::vector<std::string> args;
  if (!options.filter.empty()) {
    auto filter_args = adapter->get_filter_args(options.filter);
    args.insert(args.end(), filter_args.begin(), filter_args.end());
  }
  if (options.json_output) {
    auto json_args = adapter->get_json_args();
    args.insert(args.end(), json_args.begin(), json_args.end());
  }

  std::string output;
  auto result = execute_process(exe.string(), args, m_project_dir.string(),
                               [&output](const std::string &line) {
                                 output += line + "\n";
                                 fmt::print("{}\n", line);
                               },
                               [](const std::string &line) {
                                 fmt::print(fg(fmt::color::red), "{}\n", line);
                               });

  if (result.exit_code != 0) {
    benchmark_result error_result;
    error_result.name = target.name;
    error_result.success = false;
    error_result.error_message = "Benchmark exited with code " +
                                  std::to_string(result.exit_code);
    return {error_result};
  }

  return adapter->parse_output(output);
}

benchmark_summary benchmark_runner::run_benchmarks(const benchmark_run_options &options) {
  benchmark_summary summary;
  auto start = std::chrono::steady_clock::now();

  // Build if needed
  if (!options.no_build) {
    if (!build_benchmarks(options.build_config, options.verbose)) {
      return summary;
    }
  }

  auto targets = discover_targets();

  for (const auto &target : targets) {
    if (!target.enabled) continue;

    logger::print_action("Running", target.name);

    auto results = run_target(target, options);
    for (auto &result : results) {
      if (result.success) {
        summary.successful++;
      } else {
        summary.failed++;
      }
      summary.total++;
      m_results.push_back(result);
      summary.results.push_back(result);
    }
  }

  auto end = std::chrono::steady_clock::now();
  summary.total_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  return summary;
}

bool benchmark_runner::should_auto_link_project() const {
  return m_bench_config.auto_link_project;
}

std::string benchmark_runner::get_project_link_target() const {
  std::string project_name = m_project_config.get_string("project.name", "");
  std::string binary_type = m_project_config.get_string("project.binary_type", "executable");

  if (binary_type == "static_library" || binary_type == "shared_library") {
    return project_name;
  }
  return "";
}

} // namespace cforge
