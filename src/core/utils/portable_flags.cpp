/**
 * @file portable_flags.cpp
 * @brief Implementation of portable compiler flags translation
 */

#include "core/portable_flags.hpp"
#include "core/toml_reader.hpp"

#include <algorithm>
#include <sstream>

namespace cforge {

bool portable_options::has_any() const {
  return !optimize.empty() || !warnings.empty() || warnings_as_errors ||
         debug_info || !sanitizers.empty() || lto || !exceptions || !rtti ||
         !stdlib.empty() || !hardening.empty() || !visibility.empty();
}

bool cmake_options::has_any() const {
  return export_compile_commands || position_independent_code ||
         interprocedural_optimization || visibility_hidden ||
         !variables.empty();
}

portable_options parse_portable_options(const toml_reader &config,
                                        const std::string &section) {
  portable_options opts;

  // Parse string options
  opts.optimize = config.get_string(section + ".optimize", "");
  opts.warnings = config.get_string(section + ".warnings", "");
  opts.stdlib = config.get_string(section + ".stdlib", "");
  opts.hardening = config.get_string(section + ".hardening", "");
  opts.visibility = config.get_string(section + ".visibility", "");

  // Parse boolean options
  opts.warnings_as_errors =
      config.get_bool(section + ".warnings_as_errors", false);
  opts.debug_info = config.get_bool(section + ".debug_info", false);
  opts.lto = config.get_bool(section + ".lto", false);
  opts.exceptions = config.get_bool(section + ".exceptions", true);
  opts.rtti = config.get_bool(section + ".rtti", true);

  // Parse sanitizers array
  opts.sanitizers = config.get_string_array(section + ".sanitizers");

  return opts;
}

cmake_options parse_cmake_options(const toml_reader &config) {
  cmake_options opts;

  // Parse from [build] section
  opts.export_compile_commands =
      config.get_bool("build.export_compile_commands", false);
  opts.position_independent_code =
      config.get_bool("build.position_independent_code", false);
  opts.interprocedural_optimization =
      config.get_bool("build.interprocedural_optimization", false);
  opts.visibility_hidden = config.get_bool("build.visibility_hidden", false);

  // Parse custom variables from [build.cmake_variables]
  opts.variables = config.get_string_map("build.cmake_variables");

  return opts;
}

std::vector<std::string> translate_to_msvc(const portable_options &opts) {
  std::vector<std::string> flags;

  // Optimization
  if (opts.optimize == "none" || opts.optimize == "debug") {
    flags.push_back("/Od");
  } else if (opts.optimize == "size") {
    flags.push_back("/O1");
    flags.push_back("/Os");
  } else if (opts.optimize == "speed") {
    flags.push_back("/O2");
  } else if (opts.optimize == "aggressive") {
    flags.push_back("/Ox");
  }

  // Warnings
  if (opts.warnings == "none") {
    flags.push_back("/W0");
  } else if (opts.warnings == "default") {
    flags.push_back("/W3");
  } else if (opts.warnings == "all") {
    flags.push_back("/W4");
  } else if (opts.warnings == "strict") {
    flags.push_back("/W4");
    flags.push_back("/WX");
  } else if (opts.warnings == "pedantic") {
    flags.push_back("/W4");
    flags.push_back("/WX");
    flags.push_back("/permissive-");
  }

  // Warnings as errors (if not already set by warnings level)
  if (opts.warnings_as_errors && opts.warnings != "strict" &&
      opts.warnings != "pedantic") {
    flags.push_back("/WX");
  }

  // Debug info
  if (opts.debug_info) {
    flags.push_back("/Zi");
  }

  // Sanitizers (MSVC only supports address sanitizer)
  for (const auto &san : opts.sanitizers) {
    if (san == "address") {
      flags.push_back("/fsanitize=address");
    }
    // Note: undefined, thread, memory, leak are not supported on MSVC
  }

  // LTO (compile-time flag)
  if (opts.lto) {
    flags.push_back("/GL");
  }

  // Exceptions
  if (opts.exceptions) {
    flags.push_back("/EHsc");
  } else {
    flags.push_back("/EHs-c-");
  }

  // RTTI
  if (opts.rtti) {
    flags.push_back("/GR");
  } else {
    flags.push_back("/GR-");
  }

  // Security hardening
  if (opts.hardening == "basic") {
    flags.push_back("/GS");
    flags.push_back("/sdl");
  } else if (opts.hardening == "full") {
    flags.push_back("/GS");
    flags.push_back("/sdl");
    flags.push_back("/GUARD:CF");
  }

  return flags;
}

std::vector<std::string> translate_to_msvc_link(const portable_options &opts) {
  std::vector<std::string> flags;

  // LTO (link-time flag)
  if (opts.lto) {
    flags.push_back("/LTCG");
  }

  // Security hardening (linker flags)
  if (opts.hardening == "full") {
    flags.push_back("/DYNAMICBASE");
    flags.push_back("/NXCOMPAT");
    flags.push_back("/GUARD:CF");
  }

  return flags;
}

std::vector<std::string> translate_to_gcc(const portable_options &opts) {
  std::vector<std::string> flags;

  // Optimization
  if (opts.optimize == "none") {
    flags.push_back("-O0");
  } else if (opts.optimize == "debug") {
    flags.push_back("-Og");
  } else if (opts.optimize == "size") {
    flags.push_back("-Os");
  } else if (opts.optimize == "speed") {
    flags.push_back("-O2");
  } else if (opts.optimize == "aggressive") {
    flags.push_back("-O3");
  }

  // Warnings
  if (opts.warnings == "none") {
    flags.push_back("-w");
  } else if (opts.warnings == "all") {
    flags.push_back("-Wall");
    flags.push_back("-Wextra");
  } else if (opts.warnings == "strict") {
    flags.push_back("-Wall");
    flags.push_back("-Wextra");
    flags.push_back("-Werror");
  } else if (opts.warnings == "pedantic") {
    flags.push_back("-Wall");
    flags.push_back("-Wextra");
    flags.push_back("-Wpedantic");
    flags.push_back("-Werror");
  }

  // Warnings as errors
  if (opts.warnings_as_errors && opts.warnings != "strict" &&
      opts.warnings != "pedantic") {
    flags.push_back("-Werror");
  }

  // Debug info
  if (opts.debug_info) {
    flags.push_back("-g");
  }

  // Sanitizers
  for (const auto &san : opts.sanitizers) {
    if (san == "address") {
      flags.push_back("-fsanitize=address");
    } else if (san == "undefined") {
      flags.push_back("-fsanitize=undefined");
    } else if (san == "thread") {
      flags.push_back("-fsanitize=thread");
    } else if (san == "memory") {
      // Memory sanitizer is Clang-only, skip for GCC
    } else if (san == "leak") {
      flags.push_back("-fsanitize=leak");
    }
  }

  // LTO
  if (opts.lto) {
    flags.push_back("-flto");
  }

  // Exceptions
  if (!opts.exceptions) {
    flags.push_back("-fno-exceptions");
  }

  // RTTI
  if (!opts.rtti) {
    flags.push_back("-fno-rtti");
  }

  // Standard library (GCC defaults to libstdc++)
  if (opts.stdlib == "libc++") {
    // GCC doesn't support libc++, ignore
  }

  // Security hardening
  if (opts.hardening == "basic") {
    flags.push_back("-fstack-protector-strong");
    flags.push_back("-D_FORTIFY_SOURCE=2");
  } else if (opts.hardening == "full") {
    flags.push_back("-fstack-protector-all");
    flags.push_back("-D_FORTIFY_SOURCE=2");
    flags.push_back("-fPIE");
  }

  // Visibility
  if (opts.visibility == "hidden") {
    flags.push_back("-fvisibility=hidden");
    flags.push_back("-fvisibility-inlines-hidden");
  } else if (opts.visibility == "default") {
    flags.push_back("-fvisibility=default");
  }

  // Colored diagnostics
  flags.push_back("-fdiagnostics-color=always");

  return flags;
}

std::vector<std::string> translate_to_gcc_link(const portable_options &opts) {
  std::vector<std::string> flags;

  // Sanitizers need to be passed to linker too
  for (const auto &san : opts.sanitizers) {
    if (san == "address") {
      flags.push_back("-fsanitize=address");
    } else if (san == "undefined") {
      flags.push_back("-fsanitize=undefined");
    } else if (san == "thread") {
      flags.push_back("-fsanitize=thread");
    } else if (san == "leak") {
      flags.push_back("-fsanitize=leak");
    }
  }

  // LTO
  if (opts.lto) {
    flags.push_back("-flto");
  }

  // Security hardening (linker flags)
  if (opts.hardening == "full") {
    flags.push_back("-pie");
  }

  return flags;
}

std::vector<std::string> translate_to_clang(const portable_options &opts) {
  std::vector<std::string> flags;

  // Optimization (same as GCC)
  if (opts.optimize == "none") {
    flags.push_back("-O0");
  } else if (opts.optimize == "debug") {
    flags.push_back("-Og");
  } else if (opts.optimize == "size") {
    flags.push_back("-Os");
  } else if (opts.optimize == "speed") {
    flags.push_back("-O2");
  } else if (opts.optimize == "aggressive") {
    flags.push_back("-O3");
  }

  // Warnings (same as GCC)
  if (opts.warnings == "none") {
    flags.push_back("-w");
  } else if (opts.warnings == "all") {
    flags.push_back("-Wall");
    flags.push_back("-Wextra");
  } else if (opts.warnings == "strict") {
    flags.push_back("-Wall");
    flags.push_back("-Wextra");
    flags.push_back("-Werror");
  } else if (opts.warnings == "pedantic") {
    flags.push_back("-Wall");
    flags.push_back("-Wextra");
    flags.push_back("-Wpedantic");
    flags.push_back("-Werror");
  }

  // Warnings as errors
  if (opts.warnings_as_errors && opts.warnings != "strict" &&
      opts.warnings != "pedantic") {
    flags.push_back("-Werror");
  }

  // Debug info
  if (opts.debug_info) {
    flags.push_back("-g");
  }

  // Sanitizers (Clang supports all)
  for (const auto &san : opts.sanitizers) {
    if (san == "address") {
      flags.push_back("-fsanitize=address");
    } else if (san == "undefined") {
      flags.push_back("-fsanitize=undefined");
    } else if (san == "thread") {
      flags.push_back("-fsanitize=thread");
    } else if (san == "memory") {
      flags.push_back("-fsanitize=memory");
    } else if (san == "leak") {
      flags.push_back("-fsanitize=leak");
    }
  }

  // LTO
  if (opts.lto) {
    flags.push_back("-flto");
  }

  // Exceptions
  if (!opts.exceptions) {
    flags.push_back("-fno-exceptions");
  }

  // RTTI
  if (!opts.rtti) {
    flags.push_back("-fno-rtti");
  }

  // Standard library (Clang supports both)
  if (opts.stdlib == "libc++") {
    flags.push_back("-stdlib=libc++");
  } else if (opts.stdlib == "libstdc++") {
    flags.push_back("-stdlib=libstdc++");
  }

  // Security hardening
  if (opts.hardening == "basic") {
    flags.push_back("-fstack-protector-strong");
    flags.push_back("-D_FORTIFY_SOURCE=2");
  } else if (opts.hardening == "full") {
    flags.push_back("-fstack-protector-all");
    flags.push_back("-D_FORTIFY_SOURCE=2");
    flags.push_back("-fPIE");
  }

  // Visibility
  if (opts.visibility == "hidden") {
    flags.push_back("-fvisibility=hidden");
    flags.push_back("-fvisibility-inlines-hidden");
  } else if (opts.visibility == "default") {
    flags.push_back("-fvisibility=default");
  }

  // Colored diagnostics (Clang uses different flag)
  flags.push_back("-fcolor-diagnostics");

  return flags;
}

std::vector<std::string> translate_to_clang_link(const portable_options &opts) {
  std::vector<std::string> flags;

  // Sanitizers
  for (const auto &san : opts.sanitizers) {
    if (san == "address") {
      flags.push_back("-fsanitize=address");
    } else if (san == "undefined") {
      flags.push_back("-fsanitize=undefined");
    } else if (san == "thread") {
      flags.push_back("-fsanitize=thread");
    } else if (san == "memory") {
      flags.push_back("-fsanitize=memory");
    } else if (san == "leak") {
      flags.push_back("-fsanitize=leak");
    }
  }

  // LTO
  if (opts.lto) {
    flags.push_back("-flto");
  }

  // Standard library
  if (opts.stdlib == "libc++") {
    flags.push_back("-stdlib=libc++");
  } else if (opts.stdlib == "libstdc++") {
    flags.push_back("-stdlib=libstdc++");
  }

  // Security hardening
  if (opts.hardening == "full") {
    flags.push_back("-pie");
  }

  return flags;
}

std::string join_flags(const std::vector<std::string> &flags) {
  std::ostringstream oss;
  for (cforge_size_t i = 0; i < flags.size(); ++i) {
    if (i > 0)
      oss << " ";
    oss << flags[i];
  }
  return oss.str();
}

std::string generate_portable_flags_cmake(const portable_options &opts,
                                          const std::string &target_name,
                                          const std::string &indent) {
  if (!opts.has_any()) {
    return "";
  }

  std::ostringstream cmake;

  auto msvc_flags = translate_to_msvc(opts);
  auto msvc_link = translate_to_msvc_link(opts);
  auto gcc_flags = translate_to_gcc(opts);
  auto gcc_link = translate_to_gcc_link(opts);
  auto clang_flags = translate_to_clang(opts);
  auto clang_link = translate_to_clang_link(opts);

  cmake << indent << "# Portable compiler flags\n";
  cmake << indent
        << "if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL \"Clang\")\n";

  if (!msvc_flags.empty()) {
    cmake << indent << "    target_compile_options(" << target_name
          << " PRIVATE";
    for (const auto &flag : msvc_flags) {
      cmake << " " << flag;
    }
    cmake << ")\n";
  }

  if (!msvc_link.empty()) {
    cmake << indent << "    target_link_options(" << target_name << " PRIVATE";
    for (const auto &flag : msvc_link) {
      cmake << " " << flag;
    }
    cmake << ")\n";
  }

  cmake << indent << "elseif(CMAKE_CXX_COMPILER_ID STREQUAL \"GNU\")\n";

  if (!gcc_flags.empty()) {
    cmake << indent << "    target_compile_options(" << target_name
          << " PRIVATE";
    for (const auto &flag : gcc_flags) {
      cmake << " " << flag;
    }
    cmake << ")\n";
  }

  if (!gcc_link.empty()) {
    cmake << indent << "    target_link_options(" << target_name << " PRIVATE";
    for (const auto &flag : gcc_link) {
      cmake << " " << flag;
    }
    cmake << ")\n";
  }

  cmake << indent << "elseif(CMAKE_CXX_COMPILER_ID MATCHES \"Clang\")\n";

  if (!clang_flags.empty()) {
    cmake << indent << "    target_compile_options(" << target_name
          << " PRIVATE";
    for (const auto &flag : clang_flags) {
      cmake << " " << flag;
    }
    cmake << ")\n";
  }

  if (!clang_link.empty()) {
    cmake << indent << "    target_link_options(" << target_name << " PRIVATE";
    for (const auto &flag : clang_link) {
      cmake << " " << flag;
    }
    cmake << ")\n";
  }

  cmake << indent << "endif()\n";

  return cmake.str();
}

std::string generate_cmake_options(const cmake_options &opts) {
  if (!opts.has_any()) {
    return "";
  }

  std::ostringstream cmake;

  cmake << "# CMake options\n";

  if (opts.export_compile_commands) {
    cmake << "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n";
  }

  if (opts.position_independent_code) {
    cmake << "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n";
  }

  if (opts.interprocedural_optimization) {
    cmake << "set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)\n";
  }

  if (opts.visibility_hidden) {
    cmake << "set(CMAKE_CXX_VISIBILITY_PRESET hidden)\n";
    cmake << "set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)\n";
  }

  // Custom variables
  for (const auto &[key, value] : opts.variables) {
    cmake << "set(" << key << " \"" << value << "\")\n";
  }

  cmake << "\n";

  return cmake.str();
}

std::string
generate_config_portable_flags_cmake(const std::string &config_name,
                                     const portable_options &opts,
                                     const std::string &target_name) {
  if (!opts.has_any()) {
    return "";
  }

  std::ostringstream cmake;

  cmake << "# " << config_name << " configuration flags\n";
  cmake << "if(CMAKE_BUILD_TYPE STREQUAL \"" << config_name << "\")\n";
  cmake << generate_portable_flags_cmake(opts, target_name, "    ");
  cmake << "endif()\n\n";

  return cmake.str();
}

} // namespace cforge
