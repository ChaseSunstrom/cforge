/**
 * @file command_bench.cpp
 * @brief Implementation of the bench command for running benchmarks
 */

#include "cforge/log.hpp"
#include "core/benchmark_framework.hpp"
#include "core/benchmark_runner.hpp"
#include "core/command.h"
#include "core/command_registry.hpp"
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
  cforge::logger::print_blank();
  cforge::logger::print_header("Benchmark Summary");
  cforge::logger::print_blank();

  if (summary.results.empty()) {
    cforge::logger::print_plain("  No benchmark results collected");
    return;
  }

  // Print results table
  std::vector<int> widths = {40, 15, 15, 12};
  cforge::logger::print_table_header({"Benchmark", "Time", "CPU", "Iterations"}, widths, 2);

  for (const auto &result : summary.results) {
    if (result.success) {
      cforge::logger::print_table_row({
          result.name,
          cforge::format_bench_time(result.time_ns),
          cforge::format_bench_time(result.cpu_time_ns),
          std::to_string(result.iterations)
      }, widths, 2);
    } else {
      cforge::logger::print_error("  " + result.name + " FAILED: " + result.error_message);
    }
  }

  cforge::logger::print_blank();
  cforge::logger::print_plain("  Ran " + std::to_string(summary.total) + " benchmark(s) in " +
                              fmt::format("{:.2f}s", summary.total_duration.count() / 1000.0));

  if (summary.failed > 0) {
    cforge::logger::print_error("  " + std::to_string(summary.failed) + " failed");
  }
  if (summary.successful > 0) {
    cforge::logger::print_success(std::to_string(summary.successful) + " passed");
  }
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
      // Use command registry for consistent help output
      cforge::command_registry::instance().print_command_help("bench");
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
    cforge::logger::print_blank();

    cforge::logger::print_help_section("TO ADD BENCHMARKS");
    cforge::logger::print_list_item("Create a " + bench_dir_str + "/ directory", "1.", 4);
    cforge::logger::print_list_item(
        "Drop in any .cpp/.c file with BENCH() macros — that's it.", "2.", 4);
    cforge::logger::print_list_item(
        "Or use Google Benchmark / nanobench / Catch2 — auto-detected.", "3.", 4);
    cforge::logger::print_blank();

    cforge::logger::print_help_section("EXAMPLE (built-in, no dependencies)");
    cforge::logger::print_dim(bench_dir_str + "/bench_example.cpp:", 4);
    cforge::logger::print_config_block({
      "#include \"bench_framework.h\"",
      "",
      "BENCH(VectorPush) {",
      "    std::vector<int> v;",
      "    for (int i = 0; i < 1000; ++i) v.push_back(i);",
      "    cf_clobber_();",
      "}",
      "",
      "// No main() needed — cforge generates one."
    });
    cforge::logger::print_blank();

    cforge::logger::print_help_section("CONFIGURATION (optional)");
    cforge::logger::print_config_block({
      "[benchmark]",
      "directory = \"bench\"",
      "framework = \"auto\"   # or builtin / google / nanobench / catch2"
    });
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
    cforge::logger::print_blank();
    cforge::logger::print_help_section("BENCHMARK FILES SHOULD");
    cforge::logger::print_list_item("Have 'bench' or 'perf' in the filename", "-", 4);
    cforge::logger::print_list_item("Include a benchmark framework header", "-", 4);
    cforge::logger::print_list_item("Contain BENCHMARK() macros or equivalent", "-", 4);
    return 0;
  }

  cforge::logger::print_header("Running Benchmarks");
  cforge::logger::print_blank();

  for (const auto &target : targets) {
    cforge::logger::print_action("Found",
                                 target.name + " (" +
                                 cforge::benchmark_framework_to_string(target.framework) +
                                 ")");
  }
  cforge::logger::print_blank();

  // Run benchmarks
  auto summary = runner.run_benchmarks(options);

  // Print summary
  print_benchmark_summary(summary);

  // Return appropriate exit code
  return summary.failed > 0 ? 1 : 0;
}
