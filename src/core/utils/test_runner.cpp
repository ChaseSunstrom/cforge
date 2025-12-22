/**
 * @file test_runner.cpp
 * @brief Implementation of the test runner for cforge's testing system
 */

#include "core/test_runner.hpp"
#include "cforge/log.hpp"
#include "core/types.h"
#include "core/process_utils.hpp"
#include "core/test_adapters.hpp"
#include "core/workspace.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace cforge {

namespace fs = std::filesystem;

// Helper to convert path to CMake format (forward slashes)
static std::string to_cmake_path(const fs::path &path) {
  std::string str = path.string();
  std::replace(str.begin(), str.end(), '\\', '/');
  return str;
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

test_runner::test_runner(const fs::path &project_dir, const toml_reader &config)
    : m_project_dir(project_dir), m_project_config(config) {}

test_runner::~test_runner() = default;

// ============================================================================
// Configuration Loading
// ============================================================================

bool test_runner::load_config() {
  // Load [test] section defaults
  m_test_config.directory = m_project_config.get_string("test.directory", "tests");
  m_test_config.default_timeout =
      static_cast<cforge_int_t>(m_project_config.get_int("test.timeout", 300));
  m_test_config.jobs =
      static_cast<cforge_int_t>(m_project_config.get_int("test.jobs", 0));
  m_test_config.auto_link_project =
      m_project_config.get_bool("test.auto_link_project", true);
  m_test_config.discovery_mode =
      m_project_config.get_string("test.discovery", "both");

  // Parse output style
  std::string output_style = m_project_config.get_string("test.output_style", "cargo");
  m_test_config.cargo_style_output = (output_style != "native");

  // Parse default framework
  std::string fw_str = m_project_config.get_string("test.framework", "auto");
  m_test_config.default_framework = string_to_test_framework(fw_str);

  // Load framework-specific configs
  load_framework_config(test_framework::GTest, "test.gtest");
  load_framework_config(test_framework::Catch2, "test.catch2");
  load_framework_config(test_framework::Doctest, "test.doctest");
  load_framework_config(test_framework::BoostTest, "test.boost");

  return true;
}

void test_runner::load_framework_config(test_framework fw, const std::string &section) {
  if (!m_project_config.has_key(section)) {
    return;
  }

  test_config::FrameworkConfig fc;
  fc.fetch = m_project_config.get_bool(section + ".fetch", true);
  fc.version = m_project_config.get_string(section + ".version", "");

  // Load additional options from the section
  auto opts = m_project_config.get_string_map(section);
  for (const auto &[key, val] : opts) {
    if (key != "fetch" && key != "version") {
      fc.options[key] = val;
    }
  }

  m_test_config.framework_configs[fw] = fc;
}

// ============================================================================
// Target Discovery
// ============================================================================

std::vector<test_target> test_runner::discover_targets() {
  std::vector<test_target> targets;

  // Load explicit targets first (always)
  if (m_test_config.discovery_mode == "explicit" ||
      m_test_config.discovery_mode == "both") {
    auto explicit_targets = load_explicit_targets();
    targets.insert(targets.end(), explicit_targets.begin(), explicit_targets.end());
  }

  // Auto-discover if requested
  if (m_test_config.discovery_mode == "auto" ||
      m_test_config.discovery_mode == "both") {
    auto auto_targets = auto_discover_targets();

    // Merge auto-discovered targets, avoiding duplicates
    for (auto &target : auto_targets) {
      bool exists = false;
      for (const auto &existing : targets) {
        if (existing.name == target.name) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        targets.push_back(std::move(target));
      }
    }
  }

  // Store in config
  m_test_config.targets = targets;
  return targets;
}

std::vector<test_target> test_runner::load_explicit_targets() {
  std::vector<test_target> targets;

  auto target_tables = m_project_config.get_table_array("test.targets");
  for (const auto &table : target_tables) {
    test_target target;
    target.name = table.get_string("name", "");
    if (target.name.empty()) {
      logger::print_warning("[[test.targets]] entry missing 'name', skipping");
      continue;
    }

    target.sources = table.get_string_array("sources");
    target.dependencies = table.get_string_array("dependencies");
    target.defines = table.get_string_array("defines");
    target.includes = table.get_string_array("includes");
    target.timeout_seconds =
        static_cast<cforge_int_t>(table.get_int("timeout", m_test_config.default_timeout));
    target.enabled = table.get_bool("enabled", true);

    // Parse framework
    std::string fw_str = table.get_string("framework", "auto");
    target.framework = string_to_test_framework(fw_str);

    // Expand source globs
    target.source_files = expand_globs(target.sources, m_project_dir);

    // Detect framework from sources if auto
    if (target.framework == test_framework::Auto && !target.source_files.empty()) {
      target.framework = detect_framework(target.source_files.front());
    }

    if (!target.source_files.empty()) {
      targets.push_back(std::move(target));
    } else {
      logger::print_warning("Test target '" + target.name + "' has no source files");
    }
  }

  return targets;
}

std::vector<test_target> test_runner::auto_discover_targets() {
  std::vector<test_target> targets;

  fs::path test_dir = m_project_dir / m_test_config.directory;
  if (!fs::exists(test_dir) || !fs::is_directory(test_dir)) {
    return targets;
  }

  // Look for test source files
  std::vector<fs::path> test_files;
  for (const auto &entry : fs::recursive_directory_iterator(test_dir)) {
    if (!entry.is_regular_file()) continue;

    auto ext = entry.path().extension().string();
    if (ext == ".cpp" || ext == ".cxx" || ext == ".cc") {
      test_files.push_back(entry.path());
    }
  }

  if (test_files.empty()) {
    return targets;
  }

  // Group files by directory to create targets
  std::map<fs::path, std::vector<fs::path>> files_by_dir;
  for (const auto &file : test_files) {
    fs::path rel = fs::relative(file.parent_path(), test_dir);
    files_by_dir[rel].push_back(file);
  }

  // Create one target per directory (or single target for flat structure)
  if (files_by_dir.size() == 1 && files_by_dir.begin()->first == ".") {
    // Flat structure - single target
    test_target target;
    target.name = "tests";
    target.source_files = test_files;
    target.timeout_seconds = m_test_config.default_timeout;

    // Detect framework from first file
    target.framework = detect_framework(test_files.front());

    targets.push_back(std::move(target));
  } else {
    // Multiple directories - one target per directory
    for (const auto &[dir, files] : files_by_dir) {
      test_target target;
      target.name = "test_" + dir.string();
      // Replace path separators with underscores
      std::replace(target.name.begin(), target.name.end(), '/', '_');
      std::replace(target.name.begin(), target.name.end(), '\\', '_');

      target.source_files = files;
      target.timeout_seconds = m_test_config.default_timeout;
      target.framework = detect_framework(files.front());

      targets.push_back(std::move(target));
    }
  }

  return targets;
}

std::vector<fs::path> test_runner::expand_globs(
    const std::vector<std::string> &patterns, const fs::path &base_dir) {
  std::vector<fs::path> result;

  for (const auto &pattern : patterns) {
    // Simple glob expansion - handle ** and *
    fs::path pattern_path = base_dir / pattern;
    fs::path search_dir = pattern_path.parent_path();
    std::string filename_pattern = pattern_path.filename().string();

    // Convert glob to regex
    std::string regex_pattern = filename_pattern;
    // Escape regex special chars except * and ?
    std::string escaped;
    for (char c : regex_pattern) {
      if (c == '*') {
        escaped += ".*";
      } else if (c == '?') {
        escaped += ".";
      } else if (std::string("^$.|()[]{}+\\").find(c) != std::string::npos) {
        escaped += '\\';
        escaped += c;
      } else {
        escaped += c;
      }
    }

    std::regex file_regex(escaped, std::regex::icase);

    // Handle ** in directory path
    bool recursive = pattern.find("**") != std::string::npos;

    if (!fs::exists(search_dir)) {
      continue;
    }

    auto iterate = [&](auto &iterator) {
      for (const auto &entry : iterator) {
        if (!entry.is_regular_file()) continue;

        std::string filename = entry.path().filename().string();
        if (std::regex_match(filename, file_regex)) {
          result.push_back(entry.path());
        }
      }
    };

    if (recursive) {
      fs::recursive_directory_iterator iter(search_dir);
      iterate(iter);
    } else {
      fs::directory_iterator iter(search_dir);
      iterate(iter);
    }
  }

  return result;
}

// ============================================================================
// Framework Detection
// ============================================================================

test_framework test_runner::detect_framework(const fs::path &source_file) {
  std::ifstream file(source_file);
  if (!file) {
    return m_test_config.default_framework;
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  std::string content = ss.str();

  // Try each adapter
  std::vector<test_framework> frameworks = {
    test_framework::GTest,
    test_framework::Catch2,
    test_framework::Doctest,
    test_framework::BoostTest,
    test_framework::Builtin
  };

  for (auto fw : frameworks) {
    auto adapter = get_adapter(fw);
    if (adapter && adapter->detect_from_source(content)) {
      return fw;
    }
  }

  return m_test_config.default_framework;
}

i_test_framework_adapter *test_runner::get_adapter(test_framework fw) {
  if (fw == test_framework::Auto) {
    fw = test_framework::Builtin;
  }

  auto it = m_adapters.find(fw);
  if (it != m_adapters.end()) {
    return it->second.get();
  }

  auto adapter = create_adapter(fw);
  auto *ptr = adapter.get();
  m_adapters[fw] = std::move(adapter);
  return ptr;
}

// ============================================================================
// CMake Generation
// ============================================================================

bool test_runner::generate_test_cmake(const test_target &target) {
  fs::path build_dir = m_project_dir / "build" / "tests" / target.name;
  fs::create_directories(build_dir);

  fs::path cmake_file = build_dir / "CMakeLists.txt";
  std::ofstream out(cmake_file);
  if (!out) {
    m_error = "FAILED to create " + cmake_file.string();
    return false;
  }

  // Get adapter for framework
  auto *adapter = get_adapter(target.framework);
  if (!adapter) {
    m_error = "No adapter for framework: " + test_framework_to_string(target.framework);
    return false;
  }

  // Get framework config
  test_config::FrameworkConfig fw_config;
  auto it = m_test_config.framework_configs.find(target.framework);
  if (it != m_test_config.framework_configs.end()) {
    fw_config = it->second;
  }

  out << "cmake_minimum_required(VERSION 3.15)\n"
      << "project(" << target.name << "_test CXX)\n\n";

  // Set C++ standard from project config
  std::string cxx_std = m_project_config.get_string("project.cpp_standard", "17");
  out << "set(CMAKE_CXX_STANDARD " << cxx_std << ")\n"
      << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n\n";

  // Configure project dependencies via FetchContent (reads from cforge.toml)
  configure_index_dependencies_fetchcontent_phase1(m_project_dir, m_project_config, out);

  // Add framework setup
  std::string fw_setup = adapter->generate_cmake_setup(fw_config);
  if (!fw_setup.empty()) {
    out << fw_setup << "\n";
  }

  // Source files
  out << "# Test sources\n"
      << "set(TEST_SOURCES\n";
  for (const auto &src : target.source_files) {
    out << "  \"" << to_cmake_path(src) << "\"\n";
  }
  out << ")\n\n";

  // Create executable
  out << "add_executable(${PROJECT_NAME} ${TEST_SOURCES})\n\n";

  // Include directories
  out << "target_include_directories(${PROJECT_NAME} PRIVATE\n"
      << "  \"" << to_cmake_path(m_project_dir / "include") << "\"\n"
      << "  \"" << to_cmake_path(m_project_dir / "src") << "\"\n"
      << "  \"" << to_cmake_path(m_project_dir / m_test_config.directory) << "\"\n";
  for (const auto &inc : target.includes) {
    out << "  \"" << to_cmake_path(m_project_dir / inc) << "\"\n";
  }
  out << ")\n\n";

  // Link project dependencies (reads from cforge.toml)
  configure_index_dependencies_fetchcontent_phase2(m_project_dir, m_project_config, out);

  // Link framework and additional targets
  out << "target_link_libraries(${PROJECT_NAME} PRIVATE\n";

  // Framework target
  std::string cmake_target = adapter->get_cmake_target();
  if (!cmake_target.empty()) {
    out << "  " << cmake_target << "\n";
  }

  // Auto-link project if applicable
  if (should_auto_link_project()) {
    std::string project_target = get_project_link_target();
    if (!project_target.empty()) {
      out << "  " << project_target << "\n";
    }
  }

  // User dependencies
  for (const auto &dep : target.dependencies) {
    out << "  " << dep << "\n";
  }
  out << ")\n\n";

  // Defines
  if (!target.defines.empty()) {
    out << "target_compile_definitions(${PROJECT_NAME} PRIVATE\n";
    for (const auto &def : target.defines) {
      out << "  " << def << "\n";
    }
    out << ")\n\n";
  }

  // Add CTest integration for GTest
  if (target.framework == test_framework::GTest) {
    out << "# CTest integration\n"
        << "include(GoogleTest)\n"
        << "gtest_discover_tests(${PROJECT_NAME})\n";
  } else if (target.framework == test_framework::Catch2) {
    out << "# CTest integration\n"
        << "include(Catch)\n"
        << "catch_discover_tests(${PROJECT_NAME})\n";
  }

  return true;
}

bool test_runner::should_auto_link_project() const {
  if (!m_test_config.auto_link_project) {
    return false;
  }

  std::string type = m_project_config.get_string("project.binary_type", "executable");
  return (type == "library" || type == "static_library" ||
          type == "shared_library" || type == "static" || type == "shared");
}

std::string test_runner::get_project_link_target() const {
  std::string name = m_project_config.get_string("project.name", "");
  if (name.empty()) {
    return "";
  }
  return name;
}

// ============================================================================
// Building
// ============================================================================

bool test_runner::configure_cmake(const test_target &target,
                                  const std::string &build_config) {
  fs::path build_dir = m_project_dir / "build" / "tests" / target.name;

  std::vector<std::string> args = {
    "-S", to_cmake_path(build_dir),
    "-B", to_cmake_path(build_dir),
    "-DCMAKE_BUILD_TYPE=" + build_config
  };

#ifdef _WIN32
  // Use same generator as main project if specified
  std::string generator = m_project_config.get_string("build.generator", "");
  if (!generator.empty()) {
    args.push_back("-G");
    args.push_back(generator);
  }
#endif

  // Use execute_process directly for cleaner error handling
  auto result = execute_process("cmake", args, m_project_dir.string(), nullptr, nullptr, 120);
  if (!result.success) {
    // Store error for later reporting
    m_error = "CMake configuration failed";
    if (!result.stderr_output.empty()) {
      // Just show the key error, not the full output
      logger::print_error("FAILED to configure " + target.name);
    }
  }
  return result.success;
}

bool test_runner::build_target(const test_target &target,
                               const std::string &build_config) {
  fs::path build_dir = m_project_dir / "build" / "tests" / target.name;

  std::vector<std::string> args = {
    "--build", to_cmake_path(build_dir),
    "--config", build_config
  };

  // Use execute_process directly for cleaner error handling
  auto result = execute_process("cmake", args, m_project_dir.string(), nullptr, nullptr, 300);
  if (!result.success) {
    m_error = "Build failed";
    if (!result.stderr_output.empty()) {
      logger::print_error("FAILED to build " + target.name);
    }
  }
  return result.success;
}

bool test_runner::build_tests(const std::string &config, [[maybe_unused]] bool verbose) {
  // Discover targets if not already done
  if (m_test_config.targets.empty()) {
    discover_targets();
  }

  bool all_success = true;
  for (const auto &target : m_test_config.targets) {
    if (!target.enabled) continue;

    logger::print_action("Building", "test target: " + target.name);

    // Generate CMakeLists.txt
    if (!generate_test_cmake(target)) {
      logger::print_error("FAILED to generate CMake for " + target.name);
      all_success = false;
      continue;
    }

    // Configure (error printed inside)
    if (!configure_cmake(target, config)) {
      all_success = false;
      continue;
    }

    // Build (error printed inside)
    if (!build_target(target, config)) {
      all_success = false;
      continue;
    }
  }

  return all_success;
}

// ============================================================================
// Test Execution
// ============================================================================

fs::path test_runner::find_test_executable(const test_target &target,
                                           const std::string &build_config) {
  fs::path build_dir = m_project_dir / "build" / "tests" / target.name;

  // Check various possible locations
  std::vector<fs::path> candidates = {
    build_dir / (target.name + "_test.exe"),
    build_dir / (target.name + "_test"),
    build_dir / build_config / (target.name + "_test.exe"),
    build_dir / build_config / (target.name + "_test"),
    build_dir / "Debug" / (target.name + "_test.exe"),
    build_dir / "Release" / (target.name + "_test.exe")
  };

  for (const auto &path : candidates) {
    if (fs::exists(path)) {
      return path;
    }
  }

  return {};
}

std::vector<test_result> test_runner::run_target(const test_target &target,
                                                const test_run_options &options) {
  std::vector<test_result> results;

  fs::path exe = find_test_executable(target, options.build_config);
  if (exe.empty()) {
    test_result error_result;
    error_result.name = target.name;
    error_result.status = test_status::FAILED;
    error_result.failure_message = "Test executable not found";
    results.push_back(error_result);
    return results;
  }

  // Get adapter
  auto *adapter = get_adapter(target.framework);
  if (!adapter) {
    test_result error_result;
    error_result.name = target.name;
    error_result.status = test_status::FAILED;
    error_result.failure_message = "No adapter for framework";
    results.push_back(error_result);
    return results;
  }

  // Build command arguments
  std::vector<std::string> args;

  if (!options.filter.empty()) {
    auto filter_args = adapter->get_filter_args(options.filter);
    args.insert(args.end(), filter_args.begin(), filter_args.end());
  }

  if (options.native_output || options.verbose) {
    auto verbose_args = adapter->get_verbose_args();
    args.insert(args.end(), verbose_args.begin(), verbose_args.end());
  }

  // Execute test
  cforge_int_t timeout = options.timeout_override > 0 ? options.timeout_override : target.timeout_seconds;

  auto proc_result = execute_process(exe.string(), args,
                                      m_project_dir.string(),
                                      nullptr, nullptr, timeout);

  // Parse output
  std::string combined_output = proc_result.stdout_output + proc_result.stderr_output;

  if (options.native_output) {
    // Just print native output, create summary result
    std::cout << combined_output << std::endl;

    test_result summary;
    summary.name = target.name;
    summary.status = proc_result.success ? test_status::PASSED : test_status::FAILED;
    results.push_back(summary);
  } else {
    // Parse output into individual results
    results = adapter->parse_output(combined_output);

    // If parsing returned nothing, create a summary result
    if (results.empty()) {
      test_result summary;
      summary.name = target.name;
      summary.status = proc_result.success ? test_status::PASSED : test_status::FAILED;
      if (!proc_result.success) {
        summary.failure_message = "Test execution failed";
        summary.notes.push_back("Exit code: " + std::to_string(proc_result.exit_code));
      }
      results.push_back(summary);
    }
  }

  return results;
}

test_summary test_runner::run_tests(const test_run_options &options) {
  test_summary summary;
  m_results.clear();

  auto start_time = std::chrono::steady_clock::now();

  // Build if needed
  if (!options.no_build) {
    if (!build_tests(options.build_config, options.verbose)) {
      summary.failed = 1;
      summary.total = 1;
      return summary;
    }
  }

  // Run each target
  for (const auto &target : m_test_config.targets) {
    if (!target.enabled) continue;

    auto results = run_target(target, options);
    m_results.insert(m_results.end(), results.begin(), results.end());
  }

  // Calculate summary
  for (const auto &result : m_results) {
    summary.total++;
    switch (result.status) {
      case test_status::PASSED:
        summary.passed++;
        break;
      case test_status::FAILED:
        summary.failed++;
        summary.failed_tests.push_back(result.name);
        break;
      case test_status::SKIPPED:
        summary.skipped++;
        break;
      case test_status::TIMEOUT:
        summary.timeout++;
        summary.failed_tests.push_back(result.name + " (timeout)");
        break;
      default:
        break;
    }
    summary.total_duration += result.duration;
  }

  auto end_time = std::chrono::steady_clock::now();
  summary.total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  return summary;
}

std::vector<std::string> test_runner::list_tests() {
  std::vector<std::string> all_tests;

  // Make sure targets are discovered
  if (m_test_config.targets.empty()) {
    discover_targets();
  }

  // For each target, try to get test list
  for (const auto &target : m_test_config.targets) {
    if (!target.enabled) continue;

    fs::path exe = find_test_executable(target, "Debug");
    if (exe.empty()) {
      // Not built yet - just list target name
      all_tests.push_back("[" + target.name + "] (not built)");
      continue;
    }

    auto *adapter = get_adapter(target.framework);
    if (!adapter) continue;

    auto list_args = adapter->get_list_args();
    auto proc_result = execute_process(exe.string(), list_args,
                                        m_project_dir.string(),
                                        nullptr, nullptr, 30);

    if (proc_result.success) {
      auto tests = adapter->parse_test_list(proc_result.stdout_output);
      for (const auto &test : tests) {
        all_tests.push_back(target.name + "::" + test);
      }
    } else {
      all_tests.push_back("[" + target.name + "] (list failed)");
    }
  }

  return all_tests;
}

} // namespace cforge
