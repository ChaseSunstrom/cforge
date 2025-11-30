/**
 * @file command_test.cpp
 * @brief Implementation of the 'test' command to run project tests
 */

#include "cforge/log.hpp"
#include "core/build_utils.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/script_runner.hpp"
#include "core/toml_reader.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace cforge;

// Note: get_build_dir_for_config() is now in build_utils.hpp

/**
 * @brief Find the test executable in the build directory
 */
[[maybe_unused]] static std::filesystem::path
find_test_executable(const std::filesystem::path &build_dir,
                     const std::string &project_name, const std::string &config,
                     const std::string &test_executable_name = "") {
  // Determine base executable name
  std::string executable_base;
  if (!test_executable_name.empty()) {
    executable_base = test_executable_name;
  } else {
    // Format: project_name_config_tests (e.g., myproject_debug_tests)
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(),
                   config_lower.begin(), ::tolower);
    executable_base = project_name + "_" + config_lower + "_tests";
  }

  // Try the standard naming convention first
  std::string executable_name = executable_base;

// Add extension for Windows
#ifdef _WIN32
  executable_name += ".exe";
#endif

  // Common test executable locations
  std::vector<std::filesystem::path> search_paths = {
      build_dir / "bin" / executable_name,
      build_dir / "tests" / "bin" / executable_name,
      build_dir / "tests" / executable_name, build_dir / executable_name};

  // Check for the executable with the naming convention
  for (const auto &path : search_paths) {
    if (std::filesystem::exists(path)) {
      logger::print_action("Found", "test executable: " + path.string());
      return path;
    }
  }

  // If not found, try alternative naming conventions
  std::vector<std::string> alt_names = {
      project_name + "_tests", // Standard name without config
      "test_" + project_name,  // Alternative prefix
      project_name + "_test"   // Alternative suffix
  };

  for (const auto &alt_base : alt_names) {
    std::string alt_name = alt_base;

// Add extension for Windows
#ifdef _WIN32
    alt_name += ".exe";
#endif

    for (const auto &base_path :
         {build_dir / "bin", build_dir / "tests" / "bin", build_dir / "tests",
          build_dir}) {
      std::filesystem::path alt_path = base_path / alt_name;
      if (std::filesystem::exists(alt_path)) {
        logger::print_action("Found", "alternative test executable: " +
                                          alt_path.string());
        return alt_path;
      }
    }
  }

  // If still not found, do a recursive search for any executable that might be
  // a test
  logger::print_action("Searching", "recursively for test executable");

  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(build_dir)) {
    if (entry.is_regular_file()) {
      std::string filename = entry.path().filename().string();

      // Look for files that have "test" in their name
      std::string lower_filename = filename;
      std::transform(lower_filename.begin(), lower_filename.end(),
                     lower_filename.begin(), ::tolower);

      if (lower_filename.find("test") != std::string::npos) {
#ifdef _WIN32
        if (entry.path().extension() == ".exe") {
#else
        if ((entry.status().permissions() &
             std::filesystem::perms::owner_exec) !=
            std::filesystem::perms::none) {
#endif
          logger::print_action("Found",
                               "test executable: " + entry.path().string());
          return entry.path();
        }
      }
    }
  }

  return {};
}

/**
 * @brief Run CTest in the build directory
 */
[[maybe_unused]] static bool run_ctest(const std::filesystem::path &build_dir, bool verbose,
                      int jobs) {
  std::string command = "ctest";
  std::vector<std::string> args;

  if (verbose)
    args.push_back("-V");
  if (jobs > 0) {
    args.push_back("-j");
    args.push_back(std::to_string(jobs));
  }

  logger::testing("CTest");
  auto result =
      execute_tool(command, args, build_dir.string(), "CTest", verbose);

  return result;
}

/**
 * @brief Run tests directly with the test executable
 */
static bool run_test_executable(const std::filesystem::path &test_executable,
                                const std::vector<std::string> &args,
                                bool verbose) {
  std::string command = test_executable.string();
  std::vector<std::string> test_args = args;

  auto working_dir = test_executable.parent_path();
  if (working_dir.empty()) {
    working_dir = std::filesystem::current_path();
  }

  logger::testing(test_executable.filename().string());
  auto result =
      execute_tool(command, test_args, working_dir.string(), "Test", verbose);

  return result;
}

/**
 * @brief Handle the 'test' command
 */
cforge_int_t cforge_cmd_test(const cforge_context_t *ctx) {
  namespace fs = std::filesystem;
  // Load project configuration
  fs::path project_dir = fs::absolute(ctx->working_dir);
  toml_reader cfg;
  if (!cfg.load((project_dir / CFORGE_FILE).string())) {
    logger::print_error("Failed to load " CFORGE_FILE);
    return 1;
  }
  // Get project name
  std::string project_name = cfg.get_string("project.name", "");
  if (project_name.empty()) {
    logger::print_error("project.name must be set in " CFORGE_FILE);
    return 1;
  }
  // Determine test directory
  std::string test_dir = cfg.get_string("test.directory", "tests");
  fs::path tests_src = project_dir / test_dir;
  logger::print_action("Using", "test directory: " + tests_src.string());
  // Create test directory if missing
  if (!fs::exists(tests_src)) {
    logger::print_action("Creating", "test directory: " + tests_src.string());
    try {
      fs::create_directories(tests_src);
    } catch (const std::exception &e) {
      logger::print_error("Failed to create test directory: " +
                          std::string(e.what()));
      return 1;
    }
  }

  // Generate unified C/C++ test framework header
  fs::path header_src = tests_src / "test_framework.h";
  if (!fs::exists(header_src)) {
    logger::print_action("Generating",
                         "test framework header: " + header_src.string());
    std::ofstream header_file(header_src);
    header_file << R"TFH(
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

// ANSI colors
#define COLOR_RED   "\x1b[31m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_CYAN  "\x1b[36m"
#define COLOR_RESET "\x1b[0m"

/// Assertion macro: returns 1 on failure, 0 on success
#define test_assert(expr)                           \
    do {                                           \
        if (!(expr)) {                             \
            fprintf(stderr, COLOR_RED              \
                "Assertion failed: %s at %s:%d\n" \
                COLOR_RESET,                      \
                #expr, __FILE__, __LINE__);       \
            return 1;                             \
        }                                          \
        return 0;                                  \
    } while (0)
#define cf_assert(expr) test_assert(expr)

#ifdef __cplusplus
extern "C" {
#endif
// TEST macro: supports TEST(name) or TEST(Category, name)
#define TEST1(name)             int name()
#define TEST2(cat,name)         int cat##_##name()
// pick correct TEST variant based on argument count
#define OVERLOAD_CHOOSER(_1,_2,NAME,...) NAME
#define EXPAND(x)               x
#define APPLY(macro, ...)       EXPAND(macro(__VA_ARGS__))
// public TEST entrypoint: forces MSVC to re-scan and expand TEST1/TEST2
#define TEST(...)               APPLY(OVERLOAD_CHOOSER(__VA_ARGS__, TEST2, TEST1), __VA_ARGS__)
#ifdef __cplusplus
}
#endif

#endif // TEST_FRAMEWORK_H
)TFH";
    header_file.close();
  }

  // Generate test_main.cpp by scanning TEST(Category,Name)
  fs::path main_src = tests_src / "test_main.cpp";
  // Always regenerate test_main.cpp
  logger::print_action("Generating",
                       "test_main.cpp via scan: " + main_src.string());
  std::ofstream main_file(main_src);
  // Includes for test runner
  main_file << "#include \"test_framework.h\"\n";
  main_file << "#include <stdio.h>\n";
  main_file << "#include <string>\n";
  main_file << "#include <vector>\n";
  main_file << "#include <algorithm>\n\n";
  // ANSI color codes for test output
  // Collect tests
  std::vector<std::pair<std::string, std::string>> tests_list;
  // Match TEST(name) or TEST(Category, name)
  std::regex re(
      R"REG(^\s*TEST\(\s*([A-Za-z_]\w*)(?:\s*,\s*([A-Za-z_]\w*))?\s*\))REG");
  for (auto &p : std::filesystem::recursive_directory_iterator(tests_src)) {
    if (!p.is_regular_file())
      continue;
    auto ext = p.path().extension().string();
    if (ext != ".cpp" && ext != ".c" && ext != ".h" && ext != ".hpp" &&
        ext != ".hxx" && ext != ".cxx" && ext != ".c++" && ext != ".cc")
      continue;
    std::ifstream f(p.path());
    std::string line;
    while (std::getline(f, line)) {
      std::smatch m;
      if (std::regex_search(line, m, re)) {
        std::string cat = m[1].str();
        std::string name;
        if (m.size() >= 3 && m[2].matched) {
          name = m[2].str();
        } else {
          name = m[1].str();
          cat.clear();
        }
        tests_list.emplace_back(cat, name);
      }
    }
  }
  // Extern declarations
  for (auto &t : tests_list) {
    std::string fn = t.first.empty() ? t.second : t.first + "_" + t.second;
    main_file << "int " << fn << "();\n";
  }
  main_file << "\nstruct test_entry { const char* full; int (*fn)(); };\n";
  main_file << "static test_entry tests[] = {\n";
  for (auto &t : tests_list) {
    std::string full = t.first.empty() ? t.second : t.first + "." + t.second;
    std::string fn = t.first.empty() ? t.second : t.first + "_" + t.second;
    main_file << "  {\"" << full << "\", " << fn << "},\n";
  }
  main_file << "};\n\n";
  // Main function with filtering flags
  main_file << "int main(int argc, char** argv) {\n";
  main_file << "  std::string category;\n";
  main_file << "  std::vector<std::string> test_filters;\n";
  main_file << "  // Positional args: first is category (optional), rest are "
               "test names\n";
  main_file << "  for (int i = 1; i < argc; ++i) {\n";
  main_file << "    if (i == 1) category = argv[i]; else "
               "test_filters.push_back(argv[i]);\n";
  main_file << "  }\n";
  main_file << "  int failures = 0;\n";
  main_file << "  size_t run_count = 0;\n";
  main_file << "  for (auto& tc : tests) {\n";
  main_file << "    std::string full(tc.full);\n";
  main_file << "    std::string cat, name; auto pos = full.find('.');\n";
  main_file << "    if (pos == std::string::npos) { name = full; } else { cat "
               "= full.substr(0,pos); name = full.substr(pos+1); }\n";
  main_file << "    if (!category.empty() && cat != category) continue;\n";
  main_file
      << "    if (!test_filters.empty() && std::find(test_filters.begin(), "
         "test_filters.end(), name) == test_filters.end()) continue;\n";
  main_file << "    ++run_count;\n";
  main_file
      << "    printf(COLOR_CYAN \"[RUN] %s\" COLOR_RESET \"\\n\", tc.full);\n";
  main_file << "    int res = tc.fn();\n";
  main_file << "    if (res) { printf(COLOR_RED \"[FAIL] %s\" COLOR_RESET "
               "\"\\n\", tc.full); ++failures; } else { printf(COLOR_GREEN "
               "\"[PASS] %s\" COLOR_RESET \"\\n\", tc.full); }\n";
  main_file << "  }\n";
  main_file
      << "  printf(\"Ran %zu tests: %d failures\\n\", run_count, failures);\n";
  main_file << "  return failures;\n";
  main_file << "}\n";
  main_file.close();

  // Prepare test build output directory under build_dir/test/executable
  std::string base_build = cfg.get_string("build.build_dir", "build");
  fs::path output_tests = project_dir / base_build / "test";
  fs::create_directories(output_tests);

  // Write CMakeLists.txt for tests
  fs::path cmake_tests = tests_src / "CMakeLists.txt";
  if (!fs::exists(cmake_tests)) {
    logger::print_action("Generating",
                         "CMakeLists.txt for tests: " + cmake_tests.string());
    std::ofstream cmake_file(cmake_tests);
    cmake_file << "cmake_minimum_required(VERSION 3.15)\n";
    cmake_file << "project(" << project_name << "_tests C CXX)\n";
    cmake_file << "set(CMAKE_CXX_STANDARD 17)\n";
    cmake_file << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    cmake_file
        << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}\")\n";
    cmake_file << "file(GLOB_RECURSE TEST_SRCS\n";
    cmake_file << "    \"${CMAKE_CURRENT_SOURCE_DIR}/*.c\"\n";
    cmake_file << "    \"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp\"\n";
    cmake_file << ")\n";
    cmake_file << "add_executable(${PROJECT_NAME} ${TEST_SRCS})\n";
    cmake_file << "target_include_directories(${PROJECT_NAME} PUBLIC "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}\")\n";

    // Propagate workspace project include and link dependencies
    {
      auto proj_deps = cfg.get_table_keys("dependencies.project");
      for (const auto &dep : proj_deps) {
        cmake_file << "target_include_directories(${PROJECT_NAME} PUBLIC "
                      "\"${CMAKE_CURRENT_SOURCE_DIR}/../"
                   << dep << "/include\")\n";
        cmake_file << "target_link_libraries(${PROJECT_NAME} PUBLIC " << dep
                   << ")\n";
      }
    }
    // Propagate vcpkg dependencies
    {
      auto vcpkg_deps = cfg.get_table_keys("dependencies.vcpkg");
      for (const auto &dep : vcpkg_deps) {
        cmake_file << "find_package(" << dep << " CONFIG REQUIRED)\n";
        cmake_file << "target_link_libraries(${PROJECT_NAME} PUBLIC " << dep
                   << "::" << dep << ")\n";
      }
    }
    // Propagate Git dependencies via FetchContent
    {
      auto git_deps = cfg.get_table_keys("dependencies.git");
      std::string deps_dir = cfg.get_string("dependencies.directory", "deps");
      if (!git_deps.empty()) {
        cmake_file << "include(FetchContent)\n";
        cmake_file << "set(FETCHCONTENT_GIT_PROTOCOL \"https\")\n";
        for (const auto &dep : git_deps) {
          std::string url =
              cfg.get_string("dependencies.git." + dep + ".url", "");
          if (!url.empty()) {
            cmake_file << "FetchContent_Declare(" << dep << "\n";
            cmake_file << "    GIT_REPOSITORY " << url << "\n";
            cmake_file << "    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../"
                       << deps_dir << "/" << dep << "\n";
            cmake_file << ")\n";
            cmake_file << "FetchContent_MakeAvailable(" << dep << ")\n";
          }
        }
      }
    }
    cmake_file.close();
  }

  // Configure tests via CMake to place binaries in output_tests
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
  logger::print_action("Configuring", "tests with CMake");
  std::vector<std::string> cmake_args = {"-S", tests_src.string(), "-B",
                                         output_tests.string()};
  // Determine build configuration (allow -c/--config)
  std::string build_config;
  if (ctx->args.config && std::strlen(ctx->args.config) > 0) {
    build_config = ctx->args.config;
    logger::print_action("Using", "build configuration: " + build_config);
  } else {
    build_config = cfg.get_string("build.build_type", "Debug");
  }
  cmake_args.push_back(std::string("-DCMAKE_BUILD_TYPE=") + build_config);
  if (!execute_tool("cmake", cmake_args, "", "CTest Configure", verbose)) {
    logger::print_error("Failed to configure tests");
    return 1;
  }
  logger::building("tests");
  // Build tests via CMake with specific configuration
  std::vector<std::string> build_args = {"--build", output_tests.string(),
                                         "--config", build_config};
  if (!execute_tool("cmake", build_args, "", "CTest Build", verbose)) {
    logger::print_error("Failed to build tests");
    return 1;
  }
  logger::finished("test");

  // Determine test executable name and path (respect multi-config)
  std::string exe_name = project_name + "_tests";
#ifdef _WIN32
  exe_name += ".exe";
#endif
  fs::path test_exec;
  // 1) try config subdirectory
  fs::path config_exec = output_tests / build_config / exe_name;
  if (fs::exists(config_exec)) {
    test_exec = config_exec;
    logger::print_action("Found", "test executable for config '" +
                                      build_config +
                                      "': " + test_exec.string());
  } else {
    // 2) try top-level
    fs::path direct_exec = output_tests / exe_name;
    if (fs::exists(direct_exec)) {
      test_exec = direct_exec;
      logger::print_action("Found", "test executable at top-level: " +
                                        test_exec.string());
    } else {
      // 3) recursive fallback
      logger::print_action("Searching",
                           "recursively for test executable under " +
                               output_tests.string());
      for (auto &entry : fs::recursive_directory_iterator(output_tests)) {
        if (!entry.is_regular_file())
          continue;
        if (entry.path().filename() == exe_name) {
          test_exec = entry.path();
          logger::print_action("Found",
                               "test executable: " + test_exec.string());
          break;
        }
      }
    }
  }
  if (test_exec.empty() || !fs::exists(test_exec)) {
    logger::print_error("Test executable not found for config '" +
                        build_config + "'");
    return 1;
  }
  logger::running(test_exec.string());
  // Collect test executable arguments: skip CForge flags and split at `--`
  std::vector<std::string> test_args;
  bool found_dd = false;
  for (int i = 0; i < ctx->args.arg_count; /*incremented in loop*/) {
    std::string arg = ctx->args.args[i];
    if (arg == "--") {
      found_dd = true;
      ++i;
      continue;
    }
    if (!found_dd) {
      if (arg == "-c" || arg == "--config") {
        // skip flag and its value
        i += 2;
        continue;
      }
      if (arg == "-v" || arg == "--verbose" || arg == "-q" ||
          arg == "--quiet") {
        ++i;
        continue;
      }
      // any other argument before -- is a category/test
      test_args.push_back(arg);
      ++i;
      continue;
    }
    // after --, forward everything
    test_args.push_back(arg);
    ++i;
  }
  if (!test_args.empty()) {
    std::ostringstream oss;
    for (size_t idx = 0; idx < test_args.size(); ++idx) {
      if (idx > 0)
        oss << ' ';
      oss << test_args[idx];
    }
    logger::print_action("Passing",
                         "arguments to test executable: " + oss.str());
  }
  // Always show test program output
  if (!run_test_executable(test_exec, test_args, true)) {
    return 1;
  }
  logger::finished("test");
  return 0;
}