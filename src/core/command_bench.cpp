/**
 * @file command_bench.cpp
 * @brief Implementation of the bench command for running benchmarks
 */

#include "cforge/log.hpp"
#include "core/command.h"
#include "core/commands.hpp"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

namespace {

struct benchmark_result {
  std::string name;
  double iterations;
  double time_ns;
  double time_per_op_ns;
  std::string unit;
};

/**
 * @brief Find benchmark executables in build directory
 */
std::vector<fs::path> find_benchmark_executables(const fs::path& build_dir,
                                                  const std::string& config) {
  std::vector<fs::path> executables;

  // Common benchmark naming patterns
  std::vector<std::string> patterns = {
    "bench", "benchmark", "benchmarks", "_bench", "_benchmark"
  };

  // Search directories
  std::vector<fs::path> search_dirs = {
    build_dir / "bin" / config,
    build_dir / config,
    build_dir / "bin",
    build_dir
  };

  for (const auto& dir : search_dirs) {
    if (!fs::exists(dir)) continue;

    try {
      for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().stem().string();
        std::string ext = entry.path().extension().string();

        // Check if it's an executable
#ifdef _WIN32
        if (ext != ".exe") continue;
#else
        // On Unix, check if executable bit is set
        auto perms = fs::status(entry.path()).permissions();
        if ((perms & fs::perms::owner_exec) == fs::perms::none) continue;
#endif

        // Check if name matches benchmark pattern
        std::string lower_name = filename;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

        for (const auto& pattern : patterns) {
          if (lower_name.find(pattern) != std::string::npos) {
            executables.push_back(entry.path());
            break;
          }
        }
      }
    } catch (...) {
      // Ignore errors during directory iteration
    }
  }

  return executables;
}

/**
 * @brief Parse Google Benchmark output
 */
std::vector<benchmark_result> parse_google_benchmark_output(const std::string& output) {
  std::vector<benchmark_result> results;

  // Google Benchmark format: BM_Name/iterations    time ns    cpu ns    iterations
  std::regex bench_regex(R"(^(BM_\w+(?:/\d+)?)\s+(\d+(?:\.\d+)?)\s+(ns|us|ms|s))");

  std::istringstream iss(output);
  std::string line;
  while (std::getline(iss, line)) {
    std::smatch match;
    if (std::regex_search(line, match, bench_regex)) {
      benchmark_result result;
      result.name = match[1].str();
      result.time_ns = std::stod(match[2].str());
      result.unit = match[3].str();

      // Convert to nanoseconds
      if (result.unit == "us") result.time_ns *= 1000;
      else if (result.unit == "ms") result.time_ns *= 1000000;
      else if (result.unit == "s") result.time_ns *= 1000000000;

      results.push_back(result);
    }
  }

  return results;
}

/**
 * @brief Format time duration for display
 */
std::string format_duration(double ns) {
  if (ns < 1000) {
    return fmt::format("{:.2f} ns", ns);
  } else if (ns < 1000000) {
    return fmt::format("{:.2f} us", ns / 1000);
  } else if (ns < 1000000000) {
    return fmt::format("{:.2f} ms", ns / 1000000);
  } else {
    return fmt::format("{:.2f} s", ns / 1000000000);
  }
}

/**
 * @brief Run a simple benchmark on a function
 */
void run_simple_benchmark(const std::string& name,
                          std::function<void()> func,
                          int iterations = 1000) {
  using namespace cforge;

  // Warmup
  for (int i = 0; i < 10; i++) {
    func();
  }

  // Actual benchmark
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < iterations; i++) {
    func();
  }
  auto end = std::chrono::high_resolution_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  double ns_per_op = static_cast<double>(duration.count()) / iterations;

  fmt::print("  {:<40} {:>15} ({} iterations)\n",
             name, format_duration(ns_per_op), iterations);
}

} // anonymous namespace

/**
 * @brief Handle the 'bench' command for running benchmarks
 */
cforge_int_t cforge_cmd_bench(const cforge_context_t *ctx) {
  using namespace cforge;

  fs::path project_dir = ctx->working_dir;

  // Parse arguments
  std::string config = "Release";  // Benchmarks should run in Release by default
  bool verbose = false;
  bool build_first = true;
  std::string filter;
  std::string output_format;
  std::string specific_bench;

  for (int i = 1; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-c" || arg == "--config") {
      if (i + 1 < ctx->args.arg_count) {
        config = ctx->args.args[++i];
      }
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "--no-build") {
      build_first = false;
    } else if (arg == "--filter" && i + 1 < ctx->args.arg_count) {
      filter = ctx->args.args[++i];
    } else if (arg == "--json") {
      output_format = "json";
    } else if (arg == "--csv") {
      output_format = "csv";
    } else if (arg[0] != '-') {
      specific_bench = arg;
    }
  }

  // Check for cforge.toml
  fs::path config_file = project_dir / "cforge.toml";
  if (!fs::exists(config_file)) {
    logger::print_error("No cforge.toml found in current directory");
    return 1;
  }

  // Load project config
  toml_reader reader;
  reader.load(config_file.string());

  // Check for benchmark configuration
  std::string bench_dir = reader.get_string("benchmark.directory", "bench");
  std::string bench_target = reader.get_string("benchmark.target", "");

  // Build first if needed
  if (build_first) {
    logger::print_action("Building", "project in " + config + " mode for benchmarks");

    // Create a context for build
    cforge_context_t build_ctx = *ctx;
    cforge_command_args_t build_args_struct = {};
    build_args_struct.command = strdup("build");
    build_args_struct.config = strdup(config.c_str());
    build_args_struct.arg_count = 0;
    build_args_struct.args = nullptr;
    build_ctx.args = build_args_struct;

    int build_result = cforge_cmd_build(&build_ctx);

    // Clean up
    free(build_args_struct.command);
    free(build_args_struct.config);

    if (build_result != 0) {
      logger::print_error("Build failed. Cannot run benchmarks.");
      return 1;
    }
  }

  // Find benchmark executables
  fs::path build_dir = project_dir / "build";
  auto bench_executables = find_benchmark_executables(build_dir, config);

  // If specific bench target is configured, filter to only that
  if (!bench_target.empty()) {
    auto it = std::find_if(bench_executables.begin(), bench_executables.end(),
      [&bench_target](const fs::path& p) {
        return p.stem().string() == bench_target;
      });
    if (it != bench_executables.end()) {
      bench_executables = {*it};
    }
  }

  // If specific bench is specified on command line, filter to it
  if (!specific_bench.empty()) {
    auto it = std::find_if(bench_executables.begin(), bench_executables.end(),
      [&specific_bench](const fs::path& p) {
        return p.stem().string().find(specific_bench) != std::string::npos;
      });
    if (it != bench_executables.end()) {
      bench_executables = {*it};
    } else {
      logger::print_error("No benchmark matching '" + specific_bench + "' found");
      return 1;
    }
  }

  if (bench_executables.empty()) {
    logger::print_warning("No benchmark executables found");
    logger::print_plain("");
    logger::print_plain("To add benchmarks:");
    logger::print_plain("  1. Create a bench/ directory with benchmark source files");
    logger::print_plain("  2. Name the target with 'bench' or 'benchmark' in the name");
    logger::print_plain("  3. Or configure in cforge.toml:");
    logger::print_plain("     [benchmark]");
    logger::print_plain("     target = \"my_benchmarks\"");
    logger::print_plain("     directory = \"bench\"");
    return 0;
  }

  // Run benchmarks
  logger::print_header("Running benchmarks");
  fmt::print("\n");

  int total_benchmarks = 0;
  std::vector<benchmark_result> all_results;

  for (const auto& bench_exe : bench_executables) {
    logger::print_action("Running", bench_exe.filename().string());

    std::vector<std::string> args;
    if (!filter.empty()) {
      args.push_back("--benchmark_filter=" + filter);
    }
    if (output_format == "json") {
      args.push_back("--benchmark_format=json");
    } else if (output_format == "csv") {
      args.push_back("--benchmark_format=csv");
    }

    auto result = execute_process(bench_exe.string(), args, project_dir.string(),
      [&verbose, &all_results, &total_benchmarks](const std::string& line) {
        fmt::print("{}\n", line);

        // Try to parse benchmark results
        if (line.find("BM_") != std::string::npos ||
            line.find("Benchmark") != std::string::npos) {
          total_benchmarks++;
        }
      },
      [](const std::string& line) {
        fmt::print(fg(fmt::color::red), "{}\n", line);
      });

    if (result.exit_code != 0) {
      logger::print_warning("Benchmark " + bench_exe.filename().string() +
                           " exited with code " + std::to_string(result.exit_code));
    }

    fmt::print("\n");
  }

  // Summary
  logger::print_header("Benchmark Summary");
  fmt::print("  Ran {} benchmark executable(s)\n", bench_executables.size());
  if (total_benchmarks > 0) {
    fmt::print("  Completed {} benchmark(s)\n", total_benchmarks);
  }

  return 0;
}
