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

platform get_current_platform() {
#if defined(_WIN32) || defined(_WIN64)
  return platform::WINDOWS;
#elif defined(__APPLE__) && defined(__MACH__)
  return platform::MACOS;
#elif defined(__linux__)
  return platform::LINUX;
#else
  return platform::UNKNOWN;
#endif
}

std::string platform_to_string(platform platform) {
  switch (platform) {
  case platform::WINDOWS:
    return "windows";
  case platform::LINUX:
    return "linux";
  case platform::MACOS:
    return "macos";
  default:
    return "unknown";
  }
}

platform string_to_platform(const std::string &str) {
  std::string lower = to_lower(str);
  if (lower == "windows" || lower == "win32" || lower == "win64") {
    return platform::WINDOWS;
  } else if (lower == "linux") {
    return platform::LINUX;
  } else if (lower == "macos" || lower == "darwin" || lower == "apple" || lower == "osx") {
    return platform::MACOS;
  }
  return platform::UNKNOWN;
}

compiler detect_compiler() {
#if defined(_MSC_VER) && !defined(__clang__)
  return compiler::MSVC;
#elif defined(__MINGW32__) || defined(__MINGW64__)
  return compiler::MINGW;
#elif defined(__clang__)
  #if defined(__apple_build_version__)
    return compiler::APPLE_CLANG;
  #else
    return compiler::CLANG;
  #endif
#elif defined(__GNUC__)
  return compiler::GCC;
#else
  return compiler::UNKNOWN;
#endif
}

std::string compiler_to_string(compiler compiler) {
  switch (compiler) {
  case compiler::MSVC:
    return "msvc";
  case compiler::GCC:
    return "gcc";
  case compiler::CLANG:
    return "clang";
  case compiler::APPLE_CLANG:
    return "apple_clang";
  case compiler::MINGW:
    return "mingw";
  default:
    return "unknown";
  }
}

compiler string_to_compiler(const std::string &str) {
  std::string lower = to_lower(str);
  if (lower == "msvc" || lower == "cl" || lower == "visual studio") {
    return compiler::MSVC;
  } else if (lower == "gcc" || lower == "gnu") {
    return compiler::GCC;
  } else if (lower == "clang" || lower == "llvm") {
    return compiler::CLANG;
  } else if (lower == "apple_clang" || lower == "appleclang" || lower == "apple-clang") {
    return compiler::APPLE_CLANG;
  } else if (lower == "mingw" || lower == "mingw32" || lower == "mingw64") {
    return compiler::MINGW;
  }
  return compiler::UNKNOWN;
}

bool matches_current_platform(const std::vector<std::string> &platforms) {
  if (platforms.empty()) {
    return true; // No restriction means all platforms
  }

  platform current = get_current_platform();
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

void config_resolver::set_platform(platform platform) {
  platform_ = platform;
}

void config_resolver::set_compiler(compiler compiler) {
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

  // 2. platform-specific defines
  merge_arrays(result, get_section_array("platform." + platform_str + ".defines"));

  // 3. compiler-specific defines
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".defines"));

  // 4. platform+compiler nested defines
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

  // 2. platform-specific flags
  merge_arrays(result, get_section_array("platform." + platform_str + ".flags"));

  // 3. compiler-specific flags
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".flags"));

  // 4. platform+compiler nested flags
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

  // 2. platform-specific links
  merge_arrays(result, get_section_array("platform." + platform_str + ".links"));

  // 3. compiler-specific links
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".links"));

  // 4. platform+compiler nested links
  merge_arrays(result, get_section_array("platform." + platform_str +
                                         ".compiler." + compiler_str + ".links"));

  return result;
}

std::vector<std::string> config_resolver::resolve_frameworks() const {
  std::vector<std::string> result;

  // Frameworks are macOS only
  if (platform_ != platform::MACOS) {
    return result;
  }

  std::string compiler_str = compiler_to_string(compiler_);

  // 1. platform-specific frameworks
  merge_arrays(result, get_section_array("platform.macos.frameworks"));

  // 2. compiler-specific frameworks (rare but possible)
  merge_arrays(result, get_section_array("compiler." + compiler_str + ".frameworks"));

  // 3. platform+compiler nested frameworks
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

linker_options
config_resolver::resolve_linker_options(const std::string &build_config) const {
  linker_options result;
  std::string platform_str = platform_to_string(platform_);
  std::string compiler_str = compiler_to_string(compiler_);
  std::string config_lower = to_lower(build_config);

  // 1. Base linker options from [linker]
  if (config_.has_key("linker")) {
    merge_linker_options(result, parse_linker_options(config_, "linker"));
  }

  // 2. Platform-specific linker options from [linker.platform.<name>]
  std::string platform_section = "linker.platform." + platform_str;
  if (config_.has_key(platform_section)) {
    merge_linker_options(result, parse_linker_options(config_, platform_section));
  }

  // 3. Compiler-specific linker options from [linker.compiler.<name>]
  std::string compiler_section = "linker.compiler." + compiler_str;
  if (config_.has_key(compiler_section)) {
    merge_linker_options(result, parse_linker_options(config_, compiler_section));
  }

  // 4. Platform+Compiler nested linker options from [linker.platform.<plat>.compiler.<comp>]
  std::string nested_section = "linker.platform." + platform_str + ".compiler." + compiler_str;
  if (config_.has_key(nested_section)) {
    merge_linker_options(result, parse_linker_options(config_, nested_section));
  }

  // 5. Build config linker options from [linker.config.<name>]
  if (!build_config.empty()) {
    std::string config_section = "linker.config." + config_lower;
    if (config_.has_key(config_section)) {
      merge_linker_options(result, parse_linker_options(config_, config_section));
    }
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
  cfg.linker = resolve_linker_options(build_config);
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
