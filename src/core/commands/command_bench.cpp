/**
 * @file command_bench.cpp
 * @brief Implementation of the bench command for running benchmarks
 */

#include "cforge/log.hpp"
#include "core/benchmark_framework.hpp"
#include "core/benchmark_runner.hpp"
#include "core/command.h"
#include "core/commands.hpp"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
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

/**
 * @brief Print benchmark results summary
 */
void print_benchmark_summary(const cforge::benchmark_summary &summary) {
  fmt::print("\n");
  cforge::logger::print_header("Benchmark Summary");
  fmt::print("\n");

  if (summary.results.empty()) {
    cforge::logger::print_plain("  No benchmark results collected");
    return;
  }

  // Print results table
  fmt::print("  {:<40} {:>15} {:>15} {:>12}\n",
             "Benchmark", "Time", "CPU", "Iterations");
  fmt::print("  {:-<40} {:-<15} {:-<15} {:-<12}\n", "", "", "", "");

  for (const auto &result : summary.results) {
    if (result.success) {
      fmt::print("  {:<40} {:>15} {:>15} {:>12}\n",
                 result.name,
                 cforge::format_bench_time(result.time_ns),
                 cforge::format_bench_time(result.cpu_time_ns),
                 result.iterations);
    } else {
      fmt::print("  {:<40} ",
                 fmt::format(fg(fmt::color::red), "{}", result.name));
      fmt::print("{}\n",
                 fmt::format(fg(fmt::color::red), "FAILED: {}", result.error_message));
    }
  }

  fmt::print("\n");
  fmt::print("  Ran {} benchmark(s) in {:.2f}s\n",
             summary.total,
             summary.total_duration.count() / 1000.0);

  if (summary.failed > 0) {
    fmt::print(fg(fmt::color::red), "  {} failed\n", summary.failed);
  }
  if (summary.successful > 0) {
    fmt::print(fg(fmt::color::green), "  {} passed\n", summary.successful);
  }
}

/**
 * @brief Print help for benchmark command
 */
void print_bench_help() {
  cforge::logger::print_plain("cforge bench - Run benchmarks");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Usage: cforge bench [options] [benchmark-name]");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Options:");
  cforge::logger::print_plain("  -c, --config <cfg>   Build configuration (default: Release)");
  cforge::logger::print_plain("  --no-build           Skip building before running");
  cforge::logger::print_plain("  --filter <pattern>   Run only benchmarks matching pattern");
  cforge::logger::print_plain("  --json               Output in JSON format");
  cforge::logger::print_plain("  --csv                Output in CSV format");
  cforge::logger::print_plain("  -v, --verbose        Show verbose output");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Examples:");
  cforge::logger::print_plain("  cforge bench                      Run all benchmarks");
  cforge::logger::print_plain("  cforge bench --filter 'BM_Sort'   Run only Sort benchmarks");
  cforge::logger::print_plain("  cforge bench --no-build           Run without rebuilding");
  cforge::logger::print_plain("  cforge bench --json > results.json");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Configuration (cforge.toml):");
  cforge::logger::print_plain("  [benchmark]");
  cforge::logger::print_plain("  directory = \"bench\"        # Benchmark source directory");
  cforge::logger::print_plain("  framework = \"google\"       # google, nanobench, catch2");
  cforge::logger::print_plain("  auto_link_project = true   # Link project library");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Supported Frameworks:");
  cforge::logger::print_plain("  - Google Benchmark (default)");
  cforge::logger::print_plain("  - nanobench");
  cforge::logger::print_plain("  - Catch2 BENCHMARK");
  cforge::logger::print_plain("");
  cforge::logger::print_plain("Notes:");
  cforge::logger::print_plain("  - Benchmarks run in Release mode by default for accurate timing");
  cforge::logger::print_plain("  - Create a bench/ directory with benchmark source files");
  cforge::logger::print_plain("  - Files with 'bench' or 'perf' in the name are auto-discovered");
}

} // anonymous namespace

/**
 * @brief Handle the 'bench' command for running benchmarks
 */
cforge_int_t cforge_cmd_bench(const cforge_context_t *ctx) {
  fs::path project_dir = ctx->working_dir;

  // Parse arguments
  cforge::benchmark_run_options options;
  options.build_config = "Release"; // Benchmarks should run in Release by default
  std::string specific_bench;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];

    if (arg == "-h" || arg == "--help") {
      print_bench_help();
      return 0;
    } else if (arg == "-c" || arg == "--config") {
      if (i + 1 < ctx->args.arg_count) {
        options.build_config = ctx->args.args[++i];
      }
    } else if (arg == "-v" || arg == "--verbose") {
      options.verbose = true;
    } else if (arg == "--no-build") {
      options.no_build = true;
    } else if (arg == "--filter" && i + 1 < ctx->args.arg_count) {
      options.filter = ctx->args.args[++i];
    } else if (arg == "--json") {
      options.json_output = true;
    } else if (arg == "--csv") {
      options.csv_output = true;
    } else if (arg[0] != '-' && arg != "bench" && arg != "benchmark") {
      specific_bench = arg;
    }
  }

  // Use specific bench name as filter if provided
  if (!specific_bench.empty() && options.filter.empty()) {
    options.filter = specific_bench;
  }

  // Check for cforge.toml
  fs::path config_file = project_dir / "cforge.toml";
  if (!fs::exists(config_file)) {
    cforge::logger::print_error("No cforge.toml found in current directory");
    return 1;
  }

  // Load project config
  cforge::toml_reader reader;
  if (!reader.load(config_file.string())) {
    cforge::logger::print_error("Failed to load cforge.toml");
    return 1;
  }

  // Check for benchmark directory
  std::string bench_dir_str = reader.get_string("benchmark.directory", "bench");
  fs::path bench_dir = project_dir / bench_dir_str;

  if (!fs::exists(bench_dir)) {
    cforge::logger::print_warning("Benchmark directory not found: " + bench_dir.string());
    cforge::logger::print_plain("");
    cforge::logger::print_plain("To add benchmarks:");
    cforge::logger::print_plain("  1. Create a " + bench_dir_str + "/ directory");
    cforge::logger::print_plain("  2. Add benchmark source files (e.g., bench_main.cpp)");
    cforge::logger::print_plain("  3. Use Google Benchmark, nanobench, or Catch2 BENCHMARK");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Example bench/bench_main.cpp with Google Benchmark:");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("  #include <benchmark/benchmark.h>");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("  static void BM_Example(benchmark::State& state) {");
    cforge::logger::print_plain("    for (auto _ : state) {");
    cforge::logger::print_plain("      // Code to benchmark");
    cforge::logger::print_plain("    }");
    cforge::logger::print_plain("  }");
    cforge::logger::print_plain("  BENCHMARK(BM_Example);");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("  BENCHMARK_MAIN();");
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Configure in cforge.toml:");
    cforge::logger::print_plain("  [benchmark]");
    cforge::logger::print_plain("  directory = \"bench\"");
    cforge::logger::print_plain("  framework = \"google\"");
    return 0;
  }

  // Create and run benchmark runner
  cforge::benchmark_runner runner(project_dir, reader);

  if (!runner.load_config()) {
    cforge::logger::print_error("Failed to load benchmark configuration");
    return 1;
  }

  // Discover benchmark targets
  auto targets = runner.discover_targets();

  if (targets.empty()) {
    cforge::logger::print_warning("No benchmark targets found in " + bench_dir.string());
    cforge::logger::print_plain("");
    cforge::logger::print_plain("Benchmark files should:");
    cforge::logger::print_plain("  - Have 'bench' or 'perf' in the filename");
    cforge::logger::print_plain("  - Include a benchmark framework header");
    cforge::logger::print_plain("  - Contain BENCHMARK() macros or equivalent");
    return 0;
  }

  cforge::logger::print_header("Running Benchmarks");
  fmt::print("\n");

  for (const auto &target : targets) {
    cforge::logger::print_action("Found",
                                 target.name + " (" +
                                 cforge::benchmark_framework_to_string(target.framework) +
                                 ")");
  }
  fmt::print("\n");

  // Run benchmarks
  auto summary = runner.run_benchmarks(options);

  // Print summary
  print_benchmark_summary(summary);

  // Return appropriate exit code
  return summary.failed > 0 ? 1 : 0;
}
