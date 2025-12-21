/**
 * @file command_update.cpp
 * @brief Implementation of the 'update' command
 */
#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/installer.hpp"
#include "core/process_utils.hpp"
#include "core/registry.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

/**
 * @brief Handle the 'update' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_update(const cforge_context_t *ctx) {
  std::filesystem::path cwd = ctx->working_dir;
  cforge::installer installer_instance;

  // Parse flags
  std::string install_path;
  bool add_to_path = false;
  bool update_self = false;
  bool update_packages = false;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];
    if (arg == "--self" || arg == "-s") {
      update_self = true;
    } else if (arg == "--packages" || arg == "-p") {
      update_packages = true;
    } else if (arg == "--path") {
      if (i + 1 < ctx->args.arg_count) {
        install_path = ctx->args.args[++i];
      }
    } else if (arg == "--add-to-path") {
      add_to_path = true;
    }
  }

  // Require explicit flag
  if (!update_self && !update_packages) {
    cforge::logger::print_error("Please specify what to update:");
    cforge::logger::print_action("--self, -s", "Update cforge itself");
    cforge::logger::print_action("--packages, -p", "Update the package registry index");
    cforge::logger::print_action("Usage", "cforge update --self");
    cforge::logger::print_action("Usage", "cforge update --packages");
    return 1;
  }

  // Cannot use both flags at once
  if (update_self && update_packages) {
    cforge::logger::print_error("Cannot use both --self and --packages at the same time");
    return 1;
  }

  if (update_self) {
    // Self-update: clone, build, and install from GitHub
    cforge::logger::print_header("Updating cforge itself");

    // Determine install location
    if (install_path.empty()) {
      // Check env variable CFORGE_INSTALL_PATH
      const char *env_path = std::getenv("CFORGE_INSTALL_PATH");
      if (env_path && *env_path) {
        install_path = env_path;
      } else {
        install_path = installer_instance.get_default_install_path();
      }
    }
    cforge::logger::print_action("Install path", install_path);

    // Prepare temporary clone directory
    std::filesystem::path temp_dir =
        std::filesystem::temp_directory_path() / "cforge_update_temp";
    if (std::filesystem::exists(temp_dir))
      std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir);

    // Clone repository
    const std::string repo_url = "https://github.com/ChaseSunstrom/cforge.git";
    cforge::logger::print_action("Cloning", "cforge from GitHub: " + repo_url);
    bool verbose = cforge::logger::get_verbosity() == cforge::log_verbosity::VERBOSITY_VERBOSE;
    if (!cforge::execute_tool("git",
                      {"clone", "--branch", "master", repo_url, temp_dir.string()},
                      "", "Git Clone", verbose)) {
      cforge::logger::print_error("Failed to clone cforge repository");
      return 1;
    }

    // Fetch dependencies
    cforge::logger::print_action("Fetching", "dependencies");
    std::filesystem::path vendor_dir = temp_dir / "vendor";
    std::filesystem::create_directories(vendor_dir);

    if (!cforge::execute_tool("git",
                      {"clone", "https://github.com/fmtlib/fmt.git",
                       (vendor_dir / "fmt").string()},
                      "", "Clone fmt", verbose)) {
      cforge::logger::print_error("Failed to clone fmt dependency");
      return 1;
    }
    // Checkout fmt version
    cforge::execute_tool("git", {"checkout", "11.1.4"}, (vendor_dir / "fmt").string(),
                 "Checkout fmt", verbose);

    if (!cforge::execute_tool("git",
                      {"clone", "https://github.com/marzer/tomlplusplus.git",
                       (vendor_dir / "tomlplusplus").string()},
                      "", "Clone tomlplusplus", verbose)) {
      cforge::logger::print_error("Failed to clone tomlplusplus dependency");
      return 1;
    }
    // Checkout tomlplusplus version
    cforge::execute_tool("git", {"checkout", "v3.4.0"},
                 (vendor_dir / "tomlplusplus").string(), "Checkout tomlplusplus",
                 verbose);

    // Configure with CMake
    cforge::logger::print_action("Configuring", "build with CMake");
    std::filesystem::path build_dir = temp_dir / "build";
    std::filesystem::create_directories(build_dir);

    std::vector<std::string> cmake_args = {
        "-S", temp_dir.string(), "-B", build_dir.string(),
        "-DCMAKE_BUILD_TYPE=Release", "-DFMT_HEADER_ONLY=ON",
        "-DBUILD_SHARED_LIBS=OFF"};

#ifdef _WIN32
    // Use Ninja if available on Windows
    if (cforge::is_command_available("ninja")) {
      cmake_args.push_back("-G");
      cmake_args.push_back("Ninja");
    }
#endif

    if (!cforge::execute_tool("cmake", cmake_args, "", "CMake Configure", verbose, 180)) {
      cforge::logger::print_error("CMake configuration failed");
      std::filesystem::remove_all(temp_dir);
      return 1;
    }

    // Build
    cforge::logger::print_action("Building", "cforge");
    std::vector<std::string> build_args = {"--build", build_dir.string(),
                                           "--config", "Release"};

    if (!cforge::execute_tool("cmake", build_args, "", "CMake Build", verbose, 600)) {
      cforge::logger::print_error("Build failed");
      std::filesystem::remove_all(temp_dir);
      return 1;
    }

    // Find the built binary
    std::filesystem::path built_exe;
    std::vector<std::filesystem::path> possible_paths = {
        build_dir / "bin" / "Release" / "cforge.exe",
        build_dir / "bin" / "Release" / "cforge",
        build_dir / "bin" / "cforge.exe",
        build_dir / "bin" / "cforge",
        build_dir / "Release" / "cforge.exe",
        build_dir / "Release" / "cforge",
        build_dir / "cforge.exe",
        build_dir / "cforge"};

    for (const auto &path : possible_paths) {
      if (std::filesystem::exists(path)) {
        built_exe = path;
        break;
      }
    }

    if (built_exe.empty()) {
      cforge::logger::print_error("Could not find built cforge executable");
      std::filesystem::remove_all(temp_dir);
      return 1;
    }

    cforge::logger::print_verbose("Found built executable: " + built_exe.string());

    // Install the binary to the same location as `cforge install`
    // This is: <platform_path>/installed/cforge/bin/
    std::filesystem::path install_bin_dir =
        std::filesystem::path(install_path) / "installed" / "cforge" / "bin";
    std::filesystem::create_directories(install_bin_dir);

#ifdef _WIN32
    std::filesystem::path target_exe = install_bin_dir / "cforge.exe";
#else
    std::filesystem::path target_exe = install_bin_dir / "cforge";
#endif

    // Install the binary
    bool install_success = false;
    try {
      std::filesystem::path backup = target_exe;
      backup += ".old";

      // Remove old backup if it exists (ignore errors)
      if (std::filesystem::exists(backup)) {
        try {
          std::filesystem::remove(backup);
        } catch (...) {
          // Ignore - might be locked
        }
      }

      // Rename current exe to backup (if it exists)
      if (std::filesystem::exists(target_exe)) {
        try {
          std::filesystem::rename(target_exe, backup);
        } catch (...) {
          // If rename fails, try direct overwrite
        }
      }

      // Copy new binary
      std::filesystem::copy_file(
          built_exe, target_exe,
          std::filesystem::copy_options::overwrite_existing);
      cforge::logger::print_action("Installed", target_exe.string());
      install_success = true;

      // Try to remove backup (ignore errors - Windows may have it locked)
      if (std::filesystem::exists(backup)) {
        try {
          std::filesystem::remove(backup);
        } catch (...) {
          // Ignore - will be cleaned up on next update
        }
      }
    } catch (const std::exception &e) {
      cforge::logger::print_error("Failed to install binary: " + std::string(e.what()));
    }

    // Update PATH if requested (only if install succeeded)
    if (install_success && add_to_path) {
      installer_instance.update_path_env(install_bin_dir);
      cforge::logger::print_action("Updated", "PATH environment variable");
    }

    // Clean up temporary directory (handle read-only git files on Windows)
    try {
#ifdef _WIN32
      // On Windows, git objects are often read-only, need to remove that attribute
      for (auto &entry :
           std::filesystem::recursive_directory_iterator(temp_dir)) {
        try {
          std::filesystem::permissions(entry.path(),
                                       std::filesystem::perms::owner_write,
                                       std::filesystem::perm_options::add);
        } catch (...) {
        }
      }
#endif
      std::filesystem::remove_all(temp_dir);
    } catch (...) {
      // Ignore cleanup errors - temp files will be cleaned up by OS eventually
    }

    cforge::logger::finished("cforge updated successfully!");
    cforge::logger::print_action("Location", target_exe.string());
    return 0;
  }

  // Update package registry index (update_packages == true)
  cforge::logger::print_header("Updating package registry index");

  cforge::registry reg;
  if (reg.update(true)) {  // force=true to always update
    cforge::logger::finished("Package registry updated successfully");
    return 0;
  } else {
    cforge::logger::print_error("Failed to update package registry");
    return 1;
  }
}