/**
 * @file command_doctor.cpp
 * @brief Implementation of the doctor command for environment diagnostics
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/process_utils.hpp"

#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <regex>

namespace cforge {

static std::string extract_version(const std::string &output) {
  // Try to find version number in various formats
  static std::regex version_regex(R"((\d+\.\d+(?:\.\d+)?))");
  std::smatch match;
  if (std::regex_search(output, match, version_regex)) {
    return match[1].str();
  }
  return "";
}

static std::string get_tool_version(const std::string &command,
                                     const std::vector<std::string> &args) {
  process_result result = execute_process(command, args, "", nullptr, nullptr, 10);
  if (result.success) {
    std::string output = result.stdout_output + result.stderr_output;
    return extract_version(output);
  }
  return "";
}

static void print_check_result(const std::string &name, bool success,
                                const std::string &version = "",
                                const std::string &help = "") {
  if (success) {
    fmt::print(fg(fmt::color::green), "  \xe2\x9c\x93 "); // checkmark
    fmt::print("{}", name);
    if (!version.empty()) {
      fmt::print(" {}", version);
    }
    fmt::print("\n");
  } else {
    fmt::print(fg(fmt::color::red), "  \xe2\x9c\x97 "); // X
    fmt::print("{} not found\n", name);
    if (!help.empty()) {
      fmt::print("    = help: {}\n", help);
    }
  }
}

} // namespace cforge

cforge_int_t cforge_cmd_doctor(const cforge_context_t *ctx) {
  using namespace cforge;

  bool verbose = false;

  // Parse arguments
  for (int i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];
    if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-h" || arg == "--help") {
      fmt::print("Usage: cforge doctor [options]\n\n");
      fmt::print("Diagnose environment issues and verify toolchain setup\n\n");
      fmt::print("Options:\n");
      fmt::print("  -v, --verbose    Show detailed information\n");
      fmt::print("  -h, --help       Show this help message\n");
      return 0;
    }
  }

  fmt::print("\nChecking environment...\n\n");

  int passed = 0;
  int warnings = 0;

  // Check CMake
  bool cmake_ok = is_command_available("cmake", 5);
  std::string cmake_ver = cmake_ok ? get_tool_version("cmake", {"--version"}) : "";
  print_check_result("CMake", cmake_ok, cmake_ver,
                     "install from https://cmake.org/download/");
  if (cmake_ok)
    passed++;
  else
    warnings++;

  // Check Ninja
  bool ninja_ok = is_command_available("ninja", 5);
  std::string ninja_ver = ninja_ok ? get_tool_version("ninja", {"--version"}) : "";
  print_check_result("Ninja", ninja_ok, ninja_ver,
                     "install with 'choco install ninja' or 'apt install ninja-build'");
  if (ninja_ok)
    passed++;
  else
    warnings++;

  // Check for C++ compiler
  bool compiler_ok = false;
  std::string compiler_name;
  std::string compiler_ver;

#ifdef _WIN32
  // Check for MSVC or MinGW
  if (is_command_available("cl", 5)) {
    compiler_ok = true;
    compiler_name = "MSVC";
  } else if (is_command_available("g++", 5)) {
    compiler_ok = true;
    compiler_name = "g++ (MinGW)";
    compiler_ver = get_tool_version("g++", {"--version"});
  } else if (is_command_available("clang++", 5)) {
    compiler_ok = true;
    compiler_name = "clang++";
    compiler_ver = get_tool_version("clang++", {"--version"});
  }
#else
  if (is_command_available("g++", 5)) {
    compiler_ok = true;
    compiler_name = "g++";
    compiler_ver = get_tool_version("g++", {"--version"});
  } else if (is_command_available("clang++", 5)) {
    compiler_ok = true;
    compiler_name = "clang++";
    compiler_ver = get_tool_version("clang++", {"--version"});
  }
#endif

  print_check_result(compiler_ok ? compiler_name : "C++ Compiler", compiler_ok,
                     compiler_ver, "install a C++ compiler (g++, clang++, or MSVC)");
  if (compiler_ok)
    passed++;
  else
    warnings++;

  // Check ccache/sccache
  bool cache_ok = is_command_available("ccache", 5) ||
                  is_command_available("sccache", 5);
  std::string cache_name = is_command_available("ccache", 5) ? "ccache" : "sccache";
  std::string cache_ver =
      cache_ok ? get_tool_version(cache_name, {"--version"}) : "";
  print_check_result(cache_ok ? cache_name : "ccache/sccache", cache_ok, cache_ver,
                     "install with 'choco install ccache' or 'apt install ccache'");
  if (cache_ok)
    passed++;
  else
    warnings++;

  // Check vcpkg
  bool vcpkg_ok = false;
  std::string vcpkg_path;
  const char *vcpkg_root = std::getenv("VCPKG_ROOT");
  if (vcpkg_root) {
    std::filesystem::path vcpkg_exe =
        std::filesystem::path(vcpkg_root) / "vcpkg";
#ifdef _WIN32
    vcpkg_exe += ".exe";
#endif
    if (std::filesystem::exists(vcpkg_exe)) {
      vcpkg_ok = true;
      vcpkg_path = vcpkg_root;
    }
  }
  print_check_result("vcpkg", vcpkg_ok, vcpkg_ok ? vcpkg_path : "",
                     "set VCPKG_ROOT environment variable");
  if (vcpkg_ok)
    passed++;
  else
    warnings++;

  // Check Git
  bool git_ok = is_command_available("git", 5);
  std::string git_ver = git_ok ? get_tool_version("git", {"--version"}) : "";
  print_check_result("Git", git_ok, git_ver,
                     "install from https://git-scm.com/downloads");
  if (git_ok)
    passed++;
  else
    warnings++;

  // Check clang-format
  bool clang_format_ok = is_command_available("clang-format", 5);
  std::string clang_format_ver =
      clang_format_ok ? get_tool_version("clang-format", {"--version"}) : "";
  print_check_result("clang-format", clang_format_ok, clang_format_ver,
                     "install LLVM or use 'cforge install clang-format'");
  if (clang_format_ok)
    passed++;
  else
    warnings++;

  // Check clang-tidy
  bool clang_tidy_ok = is_command_available("clang-tidy", 5);
  std::string clang_tidy_ver =
      clang_tidy_ok ? get_tool_version("clang-tidy", {"--version"}) : "";
  print_check_result("clang-tidy", clang_tidy_ok, clang_tidy_ver,
                     "install LLVM or use 'cforge install clang-tidy'");
  if (clang_tidy_ok)
    passed++;
  else
    warnings++;

  // Summary
  fmt::print("\n");
  if (warnings == 0) {
    fmt::print(fg(fmt::color::green), "Summary: ");
    fmt::print("{} checks passed\n", passed);
  } else {
    fmt::print(fg(fmt::color::yellow), "Summary: ");
    fmt::print("{} passed, {} warnings\n", passed, warnings);
  }

  // Show additional info in verbose mode
  if (verbose) {
    fmt::print("\nDetailed tool information available with verbose output.\n");
  }

  return 0;
}
