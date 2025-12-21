/**
 * @file platform.hpp
 * @brief Centralized platform detection and platform-specific utilities
 *
 * This header provides compile-time platform detection constants and
 * runtime utility functions for cross-platform compatibility.
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace cforge {
namespace platform {

// =============================================================================
// Compile-time Platform Detection
// =============================================================================

#if defined(_WIN32) || defined(_WIN64)
  inline constexpr bool is_windows = true;
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
#else
  inline constexpr bool is_windows = false;
#endif

#if defined(__APPLE__) && defined(__MACH__)
  inline constexpr bool is_macos = true;
#else
  inline constexpr bool is_macos = false;
#endif

#if defined(__linux__)
  inline constexpr bool is_linux = true;
#else
  inline constexpr bool is_linux = false;
#endif

// Unix-like platforms (Linux + macOS)
inline constexpr bool is_unix = is_linux || is_macos;

// =============================================================================
// Compile-time Compiler Detection
// =============================================================================

#if defined(_MSC_VER) && !defined(__clang__)
  inline constexpr bool is_msvc = true;
#else
  inline constexpr bool is_msvc = false;
#endif

#if defined(__MINGW32__) || defined(__MINGW64__)
  inline constexpr bool is_mingw = true;
#else
  inline constexpr bool is_mingw = false;
#endif

#if defined(__clang__)
  inline constexpr bool is_clang = true;
  #if defined(__apple_build_version__)
    inline constexpr bool is_apple_clang = true;
  #else
    inline constexpr bool is_apple_clang = false;
  #endif
#else
  inline constexpr bool is_clang = false;
  inline constexpr bool is_apple_clang = false;
#endif

#if defined(__GNUC__) && !defined(__clang__)
  inline constexpr bool is_gcc = true;
#else
  inline constexpr bool is_gcc = false;
#endif

// =============================================================================
// Platform Name Strings
// =============================================================================

/**
 * @brief Get the current platform name as a string
 * @return "windows", "macos", or "linux"
 */
inline std::string get_platform_name() {
    if constexpr (is_windows) {
        return "windows";
    } else if constexpr (is_macos) {
        return "macos";
    } else {
        return "linux";
    }
}

/**
 * @brief Get the current compiler name as a string
 * @return "msvc", "mingw", "apple_clang", "clang", "gcc", or "unknown"
 */
inline std::string get_compiler_name() {
    if constexpr (is_msvc) {
        return "msvc";
    } else if constexpr (is_mingw) {
        return "mingw";
    } else if constexpr (is_apple_clang) {
        return "apple_clang";
    } else if constexpr (is_clang) {
        return "clang";
    } else if constexpr (is_gcc) {
        return "gcc";
    } else {
        return "unknown";
    }
}

// =============================================================================
// Platform-specific Path Utilities
// =============================================================================

/**
 * @brief Get the path separator for the current platform
 * @return "\\" on Windows, "/" on Unix-like systems
 */
inline constexpr const char* path_separator() {
    if constexpr (is_windows) {
        return "\\";
    } else {
        return "/";
    }
}

/**
 * @brief Get the executable extension for the current platform
 * @return ".exe" on Windows, "" on Unix-like systems
 */
inline constexpr const char* executable_extension() {
    if constexpr (is_windows) {
        return ".exe";
    } else {
        return "";
    }
}

/**
 * @brief Get the shared library extension for the current platform
 * @return ".dll" on Windows, ".dylib" on macOS, ".so" on Linux
 */
inline constexpr const char* shared_library_extension() {
    if constexpr (is_windows) {
        return ".dll";
    } else if constexpr (is_macos) {
        return ".dylib";
    } else {
        return ".so";
    }
}

/**
 * @brief Get the static library extension for the current platform
 * @return ".lib" on Windows (MSVC), ".a" on Unix-like systems
 */
inline constexpr const char* static_library_extension() {
    if constexpr (is_windows && is_msvc) {
        return ".lib";
    } else {
        return ".a";
    }
}

// =============================================================================
// Visual Studio Detection (Windows only)
// =============================================================================

/**
 * @brief Get common Visual Studio installation paths
 * @return Vector of paths to check for VS installation
 */
inline std::vector<std::string> get_visual_studio_paths() {
    return {
        "C:\\Program Files\\Microsoft Visual Studio\18\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\18\\Professional\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\18\\Enterprise\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Professional\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2017\\Enterprise\\Common7\\IDE\\devenv.exe"
    };
}

// =============================================================================
// Doxygen Detection
// =============================================================================

/**
 * @brief Get common Doxygen installation paths for the current platform
 * @return Vector of paths to check for Doxygen
 */
inline std::vector<std::string> get_doxygen_paths() {
    if constexpr (is_windows) {
        return {
            "C:\\Program Files\\doxygen\\bin\\doxygen.exe",
            "C:\\Program Files (x86)\\doxygen\\bin\\doxygen.exe"
        };
    } else if constexpr (is_macos) {
        return {
            "/opt/homebrew/bin/doxygen",      // Apple Silicon (M1/M2/M3)
            "/usr/local/bin/doxygen",          // Intel Mac / Homebrew
            "/usr/bin/doxygen",                // System installation
            "/opt/local/bin/doxygen"           // MacPorts
        };
    } else {
        return {
            "/usr/bin/doxygen",
            "/usr/local/bin/doxygen"
        };
    }
}

// =============================================================================
// Terminal Emulator Detection (Linux)
// =============================================================================

/**
 * @brief Get terminal emulator commands in preference order (Linux)
 * @return Vector of terminal emulator commands to try
 */
inline std::vector<std::string> get_linux_terminals() {
    return {
        "x-terminal-emulator",    // Debian/Ubuntu default
        "gnome-terminal",         // GNOME
        "konsole",                // KDE
        "xfce4-terminal",         // XFCE
        "mate-terminal",          // MATE
        "lxterminal",             // LXDE
        "tilix",                  // Tilix
        "terminator",             // Terminator
        "alacritty",              // Alacritty
        "kitty",                  // Kitty
        "xterm"                   // Fallback
    };
}

/**
 * @brief Build a command to spawn a new terminal with the given command
 * @param cmd Command to run in the terminal
 * @param terminal_emulator Optional specific terminal to use (empty = auto-detect)
 * @return Full command string to spawn terminal
 */
inline std::string build_terminal_command(const std::string& cmd,
                                          const std::string& terminal_emulator = "") {
    if constexpr (is_windows) {
        return "start \"CForge Run\" cmd /k \"" + cmd + "\"";
    } else if constexpr (is_macos) {
        return "osascript -e 'tell application \"Terminal\" to do script \"" + cmd + "\"'";
    } else {
        // Linux: use specified terminal or default
        std::string term = terminal_emulator.empty() ? "x-terminal-emulator" : terminal_emulator;

        // Different terminals have different argument formats
        if (term == "gnome-terminal" || term == "mate-terminal") {
            return term + " -- " + cmd + " &";
        } else if (term == "konsole") {
            return term + " -e " + cmd + " &";
        } else {
            // Default format works for most terminals
            return term + " -e '" + cmd + "' &";
        }
    }
}

// =============================================================================
// CMake Generator Detection
// =============================================================================

/**
 * @brief Get the default CMake generator for the current platform
 * @return CMake generator string
 * @note This is a simple default; actual detection may use cmake --help
 */
inline std::string get_default_cmake_generator() {
    if constexpr (is_windows) {
        return "Visual Studio 17 2022";  // Default to VS 2022, with fallback logic elsewhere
    } else if constexpr (is_macos) {
        return "Xcode";  // Or "Unix Makefiles" depending on preference
    } else {
        return "Unix Makefiles";
    }
}

} // namespace platform
} // namespace cforge
