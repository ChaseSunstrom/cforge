/**
 * @file config_resolver.cpp
 * @brief Implementation of configuration resolution and merging
 */

#include "core/config_resolver.hpp"
#include "cforge/log.hpp"
#include "core/types.h"

#include <algorithm>
#include <cctype>

namespace cforge {

// Helper to convert string to lowercase
static std::string to_lower(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

Platform get_current_platform() {
#if defined(_WIN32) || defined(_WIN64)
  return Platform::Windows;
#elif defined(__APPLE__) && defined(__MACH__)
  return Platform::MacOS;
#elif defined(__linux__)
  return Platform::Linux;
#else
  return Platform::Unknown;
#endif
}

std::string platform_to_string(Platform platform) {
  switch (platform) {
  case Platform::Windows:
    return "windows";
  case Platform::Linux:
    return "linux";
  case Platform::MacOS:
    return "macos";
  default:
    return "unknown";
  }
}

Platform string_to_platform(const std::string &str) {
  std::string lower = to_lower(str);
  if (lower == "windows" || lower == "win32" || lower == "win64") {
    return Platform::Windows;
  } else if (lower == "linux") {
    return Platform::Linux;
  } else if (lower == "macos" || lower == "darwin" || lower == "apple" || lower == "osx") {
    return Platform::MacOS;
  }
  return Platform::Unknown;
}

Compiler detect_compiler() {
#if defined(_MSC_VER) && !defined(__clang__)
  return Compiler::MSVC;
#elif defined(__MINGW32__) || defined(__MINGW64__)
  return Compiler::MinGW;
#elif defined(__clang__)
  #if defined(__apple_build_version__)
    return Compiler::AppleClang;
  #else
    return Compiler::Clang;
  #endif
#elif defined(__GNUC__)
  return Compiler::GCC;
#else
  return Compiler::Unknown;
#endif
}

std::string compiler_to_string(Compiler compiler) {
  switch (compiler) {
  case Compiler::MSVC:
    return "msvc";
  case Compiler::GCC:
    return "gcc";
  case Compiler::Clang:
    return "clang";
  case Compiler::AppleClang:
    return "apple_clang";
  case Compiler::MinGW:
    return "mingw";
  default:
    return "unknown";
  }
}

Compiler string_to_compiler(const std::string &str) {
  std::string lower = to_lower(str);
  if (lower == "msvc" || lower == "cl" || lower == "visual studio") {
    return Compiler::MSVC;
  } else if (lower == "gcc" || lower == "gnu") {
    return Compiler::GCC;
  } else if (lower == "clang" || lower == "llvm") {
    return Compiler::Clang;
  } else if (lower == "apple_clang" || lower == "appleclang" || lower == "apple-clang") {
    return Compiler::AppleClang;
  } else if (lower == "mingw" || lower == "mingw32" || lower == "mingw64") {
    return Compiler::MinGW;
  }
  return Compiler::Unknown;
}

bool matches_current_platform(const std::vector<std::string> &platforms) {
  if (platforms.empty()) {
    return true; // No restriction means all platforms
  }

  Platform current = get_current_platform();
  for (const auto &p : platforms) {
    if (string_to_platform(p) == current) {
      return true;
    }
  }
  return false;
}

// config_resolver implementation

config_resolver::config_resolver(const toml_reader &config)
    : config_(config), platform_(get_current_platform()),
      compiler_(detect_compiler()) {}

void config_resolver::set_platform(Platform platform) {
  platform_ = platform;
}

void config_resolver::set_compiler(Compiler compiler) {
  compiler_ = compiler;
}

void config_resolver::merge_arrays(std::vector<std::string> &target,
                                   const std::vector<std::string> &source) const {
  for (const auto &item : source) {
    // Only add if not already present
    if (std::find(target.begin(), target.end(), item) == target.end()) {
      target.push_back(item);
    }
  }
}

std::vector<std::string>
config_resolver::get_section_array(const std::string &key) const {
  return config_.get_string_array(key);
}

std::vector<std::string>
config_resolver::resolve_defines(const std::string &build_config) const {
  std::vector<std::string> result;
  std::string platform_str = platform_to_string(platform_);
  std::string compiler_str = compiler_to_string(compiler_);
  std::string config_lower = to_lower(build_config);

  // 1. Base defines
  merge_arrays(result, get_section_array("build.defines"));

  // 2. Platform-specific defines
  merge_arrays(result, get_section_array("platform." + platform_str + ".defines"));

  // 3. Compiler-specific defines
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".defines"));

  // 4. Platform+Compiler nested defines
  merge_arrays(result, get_section_array("platform." + platform_str +
                                         ".compiler." + compiler_str + ".defines"));

  // 5. Build config defines (support both singular and plural naming)
  if (!build_config.empty()) {
    // Try singular first (preferred)
    auto config_defines = get_section_array("build.config." + config_lower + ".defines");
    if (config_defines.empty()) {
      // Fall back to plural (deprecated)
      config_defines = get_section_array("build.configs." + config_lower + ".defines");
    }
    merge_arrays(result, config_defines);
  }

  return result;
}

std::vector<std::string>
config_resolver::resolve_flags(const std::string &build_config) const {
  std::vector<std::string> result;
  std::string platform_str = platform_to_string(platform_);
  std::string compiler_str = compiler_to_string(compiler_);
  std::string config_lower = to_lower(build_config);

  // 1. Base flags
  merge_arrays(result, get_section_array("build.flags"));

  // 2. Platform-specific flags
  merge_arrays(result, get_section_array("platform." + platform_str + ".flags"));

  // 3. Compiler-specific flags
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".flags"));

  // 4. Platform+Compiler nested flags
  merge_arrays(result, get_section_array("platform." + platform_str +
                                         ".compiler." + compiler_str + ".flags"));

  // 5. Build config flags
  if (!build_config.empty()) {
    auto config_flags = get_section_array("build.config." + config_lower + ".flags");
    if (config_flags.empty()) {
      config_flags = get_section_array("build.configs." + config_lower + ".flags");
    }
    merge_arrays(result, config_flags);
  }

  return result;
}

std::vector<std::string> config_resolver::resolve_links() const {
  std::vector<std::string> result;
  std::string platform_str = platform_to_string(platform_);
  std::string compiler_str = compiler_to_string(compiler_);

  // 1. Base links
  merge_arrays(result, get_section_array("build.links"));

  // 2. Platform-specific links
  merge_arrays(result, get_section_array("platform." + platform_str + ".links"));

  // 3. Compiler-specific links
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".links"));

  // 4. Platform+Compiler nested links
  merge_arrays(result, get_section_array("platform." + platform_str +
                                         ".compiler." + compiler_str + ".links"));

  return result;
}

std::vector<std::string> config_resolver::resolve_frameworks() const {
  std::vector<std::string> result;

  // Frameworks are macOS only
  if (platform_ != Platform::MacOS) {
    return result;
  }

  std::string compiler_str = compiler_to_string(compiler_);

  // 1. Platform-specific frameworks
  merge_arrays(result, get_section_array("platform.macos.frameworks"));

  // 2. Compiler-specific frameworks (rare but possible)
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".frameworks"));

  // 3. Platform+Compiler nested frameworks
  merge_arrays(result, get_section_array("platform.macos.compiler." +
                                         compiler_str + ".frameworks"));

  return result;
}

std::vector<std::string>
config_resolver::resolve_cmake_args(const std::string &build_config) const {
  std::vector<std::string> result;
  std::string config_lower = to_lower(build_config);

  // CMake args are typically only at build config level
  if (!build_config.empty()) {
    auto config_args = get_section_array("build.config." + config_lower + ".cmake_args");
    if (config_args.empty()) {
      config_args = get_section_array("build.configs." + config_lower + ".cmake_args");
    }
    merge_arrays(result, config_args);
  }

  return result;
}

resolved_config config_resolver::resolve(const std::string &build_config) const {
  resolved_config cfg;
  cfg.defines = resolve_defines(build_config);
  cfg.flags = resolve_flags(build_config);
  cfg.links = resolve_links();
  cfg.frameworks = resolve_frameworks();
  cfg.cmake_args = resolve_cmake_args(build_config);
  return cfg;
}

bool config_resolver::has_section(const std::string &section_prefix) const {
  // Check base section
  if (config_.has_key(section_prefix)) {
    return true;
  }

  // Check platform-specific section
  std::string platform_str = platform_to_string(platform_);
  if (config_.has_key("platform." + platform_str + "." + section_prefix)) {
    return true;
  }

  return false;
}

} // namespace cforge
