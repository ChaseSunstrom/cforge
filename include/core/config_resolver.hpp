/**
 * @file config_resolver.hpp
 * @brief Configuration resolution and merging utilities
 *
 * This module handles:
 * - Platform detection (windows, linux, macos)
 * - Compiler detection (msvc, gcc, clang, apple_clang, mingw)
 * - Configuration merging with proper priority ordering
 */

#ifndef CFORGE_CONFIG_RESOLVER_HPP
#define CFORGE_CONFIG_RESOLVER_HPP

#include <map>
#include <string>
#include <vector>

#include "core/toml_reader.hpp"
#include "core/portable_flags.hpp"

namespace cforge {

/**
 * @brief Enumeration of supported platforms
 */
enum class platform {
  WINDOWS,
  LINUX,
  MACOS,
  UNKNOWN
};

/**
 * @brief Enumeration of supported compilers
 */
enum class compiler {
  MSVC,
  GCC,
  CLANG,
  APPLE_CLANG,
  MINGW,
  UNKNOWN
};

/**
 * @brief Resolved configuration with merged values
 */
struct resolved_config {
  std::vector<std::string> defines;
  std::vector<std::string> flags;
  std::vector<std::string> links;
  std::vector<std::string> frameworks;  // macOS only
  std::vector<std::string> cmake_args;
  std::map<std::string, std::string> cmake_options;
  linker_options linker;  // Resolved linker options
};

/**
 * @brief Get the current platform
 * @return The detected platform
 */
platform get_current_platform();

/**
 * @brief Get the platform name as a string
 * @param platform The platform enum value
 * @return The platform name (lowercase)
 */
std::string platform_to_string(platform platform);

/**
 * @brief Parse a platform string to enum
 * @param str The platform string (case-insensitive)
 * @return The platform enum value
 */
platform string_to_platform(const std::string &str);

/**
 * @brief Get the current compiler based on environment/detection
 * @return The detected compiler
 */
compiler detect_compiler();

/**
 * @brief Get the compiler name as a string
 * @param compiler The compiler enum value
 * @return The compiler name (lowercase with underscores)
 */
std::string compiler_to_string(compiler compiler);

/**
 * @brief Parse a compiler string to enum
 * @param str The compiler string (case-insensitive)
 * @return The compiler enum value
 */
compiler string_to_compiler(const std::string &str);

/**
 * @brief Check if a platform list contains the current platform
 * @param platforms Vector of platform names
 * @return True if current platform is in the list or list is empty
 */
bool matches_current_platform(const std::vector<std::string> &platforms);

/**
 * @brief Configuration resolver class
 *
 * Handles merging configuration from multiple sources:
 * - Base configuration
 * - Platform-specific overrides
 * - Compiler-specific overrides
 * - Platform+Compiler nested overrides
 * - Build config (debug/release) overrides
 *
 * Priority order (lowest to highest):
 * base < platform < compiler < platform.compiler < build.config
 */
class config_resolver {
public:
  /**
   * @brief Constructor
   * @param config The TOML configuration reader
   */
  explicit config_resolver(const toml_reader &config);

  /**
   * @brief Set the target platform (defaults to current platform)
   * @param platform The target platform
   */
  void set_platform(platform platform);

  /**
   * @brief Set the target compiler (defaults to detected compiler)
   * @param compiler The target compiler
   */
  void set_compiler(compiler compiler);

  /**
   * @brief Resolve defines for a given build configuration
   * @param build_config The build configuration name (debug, release, etc.)
   * @return Merged list of defines
   */
  std::vector<std::string> resolve_defines(const std::string &build_config = "") const;

  /**
   * @brief Resolve compiler flags for a given build configuration
   * @param build_config The build configuration name
   * @return Merged list of compiler flags
   */
  std::vector<std::string> resolve_flags(const std::string &build_config = "") const;

  /**
   * @brief Resolve link libraries
   * @return Merged list of libraries to link
   */
  std::vector<std::string> resolve_links() const;

  /**
   * @brief Resolve macOS frameworks
   * @return List of frameworks (empty on non-macOS)
   */
  std::vector<std::string> resolve_frameworks() const;

  /**
   * @brief Resolve CMake arguments for a given build configuration
   * @param build_config The build configuration name
   * @return Merged list of CMake arguments
   */
  std::vector<std::string> resolve_cmake_args(const std::string &build_config = "") const;

  /**
   * @brief Resolve linker options for a given build configuration
   *
   * Merges linker options from multiple sources with priority:
   * linker < linker.platform < linker.compiler < linker.platform.compiler < linker.config
   *
   * @param build_config The build configuration name
   * @return Merged linker options
   */
  linker_options resolve_linker_options(const std::string &build_config = "") const;

  /**
   * @brief Get a fully resolved configuration
   * @param build_config The build configuration name
   * @return Resolved configuration struct with all merged values
   */
  resolved_config resolve(const std::string &build_config = "") const;

  /**
   * @brief Check if a section exists for current platform/compiler
   * @param section_prefix The section prefix (e.g., "dependencies.system")
   * @return True if section exists
   */
  bool has_section(const std::string &section_prefix) const;

  /**
   * @brief Get the current platform
   */
  platform get_platform() const { return platform_; }

  /**
   * @brief Get the current compiler
   */
  compiler get_compiler() const { return compiler_; }

private:
  const toml_reader &config_;
  platform platform_;
  compiler compiler_;

  /**
   * @brief Merge string arrays (append unique values)
   */
  void merge_arrays(std::vector<std::string> &target,
                   const std::vector<std::string> &source) const;

  /**
   * @brief Get values from a section key
   */
  std::vector<std::string> get_section_array(const std::string &key) const;
};

} // namespace cforge

#endif // CFORGE_CONFIG_RESOLVER_HPP
