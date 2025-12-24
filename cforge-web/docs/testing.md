---
id: testing
title: Testing & Benchmarking
---

## Testing

CForge integrates with CTest and supports multiple testing frameworks:

```toml
[test]
enabled = true
framework = "catch2"  # catch2, gtest, doctest, boost
directory = "tests"
timeout = 30  # seconds per test
```

### Test Configuration

```toml
[[tests.executables]]
name = "math_tests"
sources = ["tests/math_test.cpp"]
includes = ["include", "tests/common"]
links = ["my_project"]
labels = ["unit", "math"]
```

### Running Tests

```bash
# Run all tests
cforge test

# Run tests with a specific label
cforge test --label unit

# Run tests matching a pattern
cforge test --filter math

# Run in specific configuration
cforge test -c Release

# Initialize test directory with sample test
cforge test --init

# Discover tests and update config
cforge test --discover

# Generate test reports
cforge test --report xml

# Verbose output
cforge test -v
```

### Example Test File

Using Catch2 (`tests/math_test.cpp`):

```cpp
#include <catch2/catch_test_macros.hpp>
#include "my_project.h"

TEST_CASE("Addition works correctly", "[math]") {
    REQUIRE(my_project::add(2, 3) == 5);
    REQUIRE(my_project::add(-1, 1) == 0);
}

TEST_CASE("Multiplication works correctly", "[math]") {
    REQUIRE(my_project::multiply(2, 3) == 6);
    REQUIRE(my_project::multiply(0, 5) == 0);
}
```

---

## Benchmarking

CForge provides integrated benchmarking support with multiple frameworks.

### Configuration

```toml
[benchmark]
directory = "bench"              # Benchmark source directory
framework = "google"             # google, nanobench, catch2
auto_link_project = true         # Automatically link project library
```

### Supported Frameworks

| Framework | Description |
|-----------|-------------|
| **Google Benchmark** | Industry-standard microbenchmarking library |
| **nanobench** | Header-only, easy to integrate |
| **Catch2 BENCHMARK** | Use Catch2's built-in benchmarking macros |

### Running Benchmarks

```bash
# Run all benchmarks
cforge bench

# Run matching benchmarks only
cforge bench --filter 'BM_Sort'

# Skip build step
cforge bench --no-build

# Output formats
cforge bench --json > results.json
cforge bench --csv > results.csv

# Specific configuration (Release is default)
cforge bench -c Release

# Verbose output
cforge bench -v
```

### Example Benchmark (Google Benchmark)

Create `bench/bench_main.cpp`:

```cpp
#include <benchmark/benchmark.h>
#include <vector>
#include <algorithm>

static void BM_VectorPush(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<int> v;
        for (int i = 0; i < state.range(0); ++i) {
            v.push_back(i);
        }
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_VectorPush)->Range(8, 8<<10);

static void BM_VectorReserve(benchmark::State& state) {
    for (auto _ : state) {
        std::vector<int> v;
        v.reserve(state.range(0));
        for (int i = 0; i < state.range(0); ++i) {
            v.push_back(i);
        }
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(BM_VectorReserve)->Range(8, 8<<10);

BENCHMARK_MAIN();
```

### Example Benchmark (nanobench)

```cpp
#define ANKERL_NANOBENCH_IMPLEMENT
#include <nanobench.h>
#include <vector>

int main() {
    ankerl::nanobench::Bench().run("vector push_back", [&] {
        std::vector<int> v;
        for (int i = 0; i < 1000; ++i) {
            v.push_back(i);
        }
        ankerl::nanobench::doNotOptimizeAway(v);
    });
}
```

### Example Benchmark (Catch2)

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <vector>

TEST_CASE("Vector benchmarks", "[benchmark]") {
    BENCHMARK("push_back 1000 elements") {
        std::vector<int> v;
        for (int i = 0; i < 1000; ++i) {
            v.push_back(i);
        }
        return v;
    };
}
```

### Auto-Discovery

CForge automatically discovers benchmark files that:
- Are located in the `bench/` directory (or configured directory)
- Have 'bench' or 'perf' in the filename
- Include a recognized benchmark framework header

### Output Format

Benchmark results are displayed in a table format:

```
┌──────────────────────────────────────────────────────────────────┐
│                      Benchmark Summary                            │
├──────────────────────────────────────────────────────────────────┤
│ Benchmark                          Time           CPU   Iterations │
├──────────────────────────────────────────────────────────────────┤
│ BM_VectorPush/8                   45 ns         45 ns   15555556  │
│ BM_VectorPush/64                 312 ns        312 ns    2240000  │
│ BM_VectorReserve/8                28 ns         28 ns   25000000  │
│ BM_VectorReserve/64              198 ns        198 ns    3555556  │
└──────────────────────────────────────────────────────────────────┘

Ran 4 benchmark(s) in 2.34s
4 passed
```

### Tips

- **Use Release mode**: Benchmarks run in Release mode by default for accurate timing
- **Warm-up**: Frameworks handle warm-up iterations automatically
- **Consistency**: Run benchmarks on an idle system for consistent results
- **Compare**: Use `--json` output to compare results across runs