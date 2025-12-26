/**
 * @file portable_flags.hpp
 * @brief Portable compiler flags abstraction for cross-platform builds
 *
 * This module provides an abstraction layer for compiler flags, allowing users
 * to specify intent-based options (like "optimize = speed") that automatically
 * translate to the correct flags for each compiler (MSVC, GCC, CLANG).
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/types.h"

namespace cforge {

// Forward declaration
class toml_reader;

/**
 * @brief Portable build options that map to compiler-specific flags
 *
 * These options can be specified in:
 * - [build.config.<config>] sections (per-configuration)
 * - [platform.<name>] sections (per-platform)
 * - [compiler.<name>] sections (per-compiler)
 */
struct portable_options {
  // Optimization level: "none", "debug", "size", "speed", "aggressive"
  std::string optimize;

  // Warning level: "none", "default", "all", "strict", "pedantic"
  std::string warnings;

  // Treat warnings as errors
  bool warnings_as_errors = false;

  // Include debug symbols
  bool debug_info = false;

  // Runtime sanitizers: "address", "undefined", "thread", "memory", "leak"
  std::vector<std::string> sanitizers;

  // Link-time optimization
  bool lto = false;

  // C++ exceptions (true = enabled)
  bool exceptions = true;

  // Runtime type information (true = enabled)
  bool rtti = true;

  // Standard library: "default", "libc++", "libstdc++"
  std::string stdlib;

  // Security hardening: "none", "basic", "full"
  std::string hardening;

  // Symbol visibility: "default", "hidden"
  std::string visibility;

  // Check if any options are set
  bool has_any() const;
};

/**
 * @brief Portable linker options that map to linker-specific flags
 *
 * These options can be specified in:
 * - [linker] section (global)
 * - [linker.platform.<name>] sections (per-platform)
 * - [linker.compiler.<name>] sections (per-compiler)
 * - [linker.platform.<name>.compiler.<name>] sections (combined)
 * - [linker.config.<name>] sections (per-configuration)
 */
struct linker_options {
  // Raw linker flags (passed directly to linker)
  std::vector<std::string> flags;

  // Library search directories (-L / /LIBPATH:)
  std::vector<std::string> library_dirs;

  // Strip symbols from binary: true/false
  bool strip = false;

  // Remove unused code sections (--gc-sections / /OPT:REF)
  bool dead_code_strip = false;

  // Preferred linker: "default", "lld", "gold", "mold", "bfd"
  std::string linker;

  // Runtime library search paths (rpath)
  std::vector<std::string> rpath;

  // Use static C/C++ runtime: true/false
  bool static_runtime = false;

  // Allow undefined symbols: true/false (default false)
  bool allow_undefined = false;

  // Generate map file: true/false
  bool map_file = false;

  // Linker scripts (GCC/Clang: -T, multiple allowed)
  std::vector<std::string> scripts;

  // Module definition file (MSVC: /DEF:)
  std::string def_file;

  // Symbol version script (Linux: --version-script)
  std::string version_script;

  // Exported symbols file (macOS: -exported_symbols_list)
  std::string exported_symbols;

  // Unexported symbols file (macOS: -unexported_symbols_list)
  std::string unexported_symbols;

  // Order file for symbol ordering (macOS: -order_file)
  std::string order_file;

  // Windows subsystem: "console", "windows"
  std::string subsystem;

  // Windows entry point override
  std::string entry_point;

  // macOS install name for dylibs
  std::string install_name;

  // Whole archive linking (force include all symbols)
  bool whole_archive = false;

  // Position independent executable (PIE)
  bool pie = false;

  // Relocation read-only (RELRO): "none", "partial", "full"
  std::string relro;

  // Check if any options are set
  bool has_any() const;
};

/**
 * @brief CMake-level options from [build] section
 */
struct cmake_options {
  // CMAKE_EXPORT_COMPILE_COMMANDS
  bool export_compile_commands = false;

  // CMAKE_POSITION_INDEPENDENT_CODE
  bool position_independent_code = false;

  // CMAKE_INTERPROCEDURAL_OPTIMIZATION
  bool interprocedural_optimization = false;

  // CMAKE_CXX_VISIBILITY_PRESET + CMAKE_VISIBILITY_INLINES_HIDDEN
  bool visibility_hidden = false;

  // Custom CMake variables from [build.cmake_variables]
  std::map<std::string, std::string> variables;

  // Check if any options are set
  bool has_any() const;
};

/**
 * @brief Compiler type for flag translation
 */
enum class compiler_type { msvc, gcc, clang, apple_clang, mingw, unknown };

/**
 * @brief Parse portable options from a TOML section
 *
 * Can parse from any section that may contain portable options:
 * - "build.config.debug", "build.config.release", etc.
 * - "platform.windows", "platform.linux", etc.
 * - "compiler.msvc", "compiler.gcc", etc.
 *
 * @param config TOML reader instance
 * @param section Section path (e.g., "build.config.release")
 * @return Parsed portable options
 */
portable_options parse_portable_options(const toml_reader &config,
                                        const std::string &section);

/**
 * @brief Parse CMake options from [build] section
 *
 * @param config TOML reader instance
 * @return Parsed CMake options
 */
cmake_options parse_cmake_options(const toml_reader &config);

/**
 * @brief Parse linker options from a TOML section
 *
 * Can parse from any section that may contain linker options:
 * - "linker" (global)
 * - "linker.platform.windows", "linker.platform.linux", etc.
 * - "linker.compiler.msvc", "linker.compiler.gcc", etc.
 * - "linker.config.debug", "linker.config.release", etc.
 *
 * @param config TOML reader instance
 * @param section Section path (e.g., "linker.platform.linux")
 * @return Parsed linker options
 */
linker_options parse_linker_options(const toml_reader &config,
                                    const std::string &section);

/**
 * @brief Merge two linker_options structs (source into target)
 *
 * Arrays are appended (unique values only), strings/bools override if set.
 *
 * @param target Target options to merge into
 * @param source Source options to merge from
 */
void merge_linker_options(linker_options &target, const linker_options &source);

/**
 * @brief Translate portable options to MSVC flags
 *
 * @param opts Portable options
 * @return Vector of MSVC compiler flags
 */
std::vector<std::string> translate_to_msvc(const portable_options &opts);

/**
 * @brief Translate portable options to MSVC linker flags
 *
 * @param opts Portable options
 * @return Vector of MSVC linker flags
 */
std::vector<std::string> translate_to_msvc_link(const portable_options &opts);

/**
 * @brief Translate portable options to GCC flags
 *
 * @param opts Portable options
 * @return Vector of GCC compiler flags
 */
std::vector<std::string> translate_to_gcc(const portable_options &opts);

/**
 * @brief Translate portable options to GCC linker flags
 *
 * @param opts Portable options
 * @return Vector of GCC linker flags
 */
std::vector<std::string> translate_to_gcc_link(const portable_options &opts);

/**
 * @brief Translate portable options to Clang flags
 *
 * @param opts Portable options
 * @return Vector of Clang compiler flags
 */
std::vector<std::string> translate_to_clang(const portable_options &opts);

/**
 * @brief Translate portable options to Clang linker flags
 *
 * @param opts Portable options
 * @return Vector of Clang linker flags
 */
std::vector<std::string> translate_to_clang_link(const portable_options &opts);

/**
 * @brief Translate linker options to MSVC linker flags
 *
 * @param opts Linker options
 * @return Vector of MSVC linker flags
 */
std::vector<std::string> translate_linker_to_msvc(const linker_options &opts);

/**
 * @brief Translate linker options to GCC linker flags
 *
 * @param opts Linker options
 * @return Vector of GCC linker flags
 */
std::vector<std::string> translate_linker_to_gcc(const linker_options &opts);

/**
 * @brief Translate linker options to Clang linker flags
 *
 * @param opts Linker options
 * @return Vector of Clang linker flags
 */
std::vector<std::string> translate_linker_to_clang(const linker_options &opts);

/**
 * @brief Generate CMake code for portable options
 *
 * Generates compiler-agnostic CMake code that applies the correct flags
 * based on the detected compiler.
 *
 * @param opts Portable options
 * @param target_name CMake target name (use ${PROJECT_NAME} for main target)
 * @param indent Indentation string (default: "")
 * @return CMake code string
 */
std::string generate_portable_flags_cmake(const portable_options &opts,
                                          const std::string &target_name,
                                          const std::string &indent = "");

/**
 * @brief Generate CMake code for CMake options
 *
 * Generates CMake variable settings from cmake_options struct.
 *
 * @param opts CMake options
 * @return CMake code string
 */
std::string generate_cmake_options(const cmake_options &opts);

/**
 * @brief Generate CMake code for configuration-specific portable options
 *
 * Wraps the portable flags in CMAKE_BUILD_TYPE conditionals.
 *
 * @param config_name Configuration name (e.g., "Debug", "Release")
 * @param opts Portable options for this configuration
 * @param target_name CMake target name
 * @return CMake code string
 */
std::string
generate_config_portable_flags_cmake(const std::string &config_name,
                                     const portable_options &opts,
                                     const std::string &target_name);

/**
 * @brief Join a vector of flags into a space-separated string
 *
 * @param flags Vector of flags
 * @return Space-separated string
 */
std::string join_flags(const std::vector<std::string> &flags);

/**
 * @brief Generate CMake code for linker options
 *
 * Generates compiler-agnostic CMake code that applies the correct linker flags
 * based on the detected compiler.
 *
 * @param opts Linker options
 * @param target_name CMake target name (use ${PROJECT_NAME} for main target)
 * @param indent Indentation string (default: "")
 * @return CMake code string
 */
std::string generate_linker_flags_cmake(const linker_options &opts,
                                        const std::string &target_name,
                                        const std::string &indent = "");

/**
 * @brief Generate CMake code for configuration-specific linker options
 *
 * Wraps the linker flags in CMAKE_BUILD_TYPE conditionals.
 *
 * @param config_name Configuration name (e.g., "Debug", "Release")
 * @param opts Linker options for this configuration
 * @param target_name CMake target name
 * @return CMake code string
 */
std::string generate_config_linker_flags_cmake(const std::string &config_name,
                                               const linker_options &opts,
                                               const std::string &target_name);

} // namespace cforge
