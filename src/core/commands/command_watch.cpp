/**
 * @file command_watch.cpp
 * @brief Implementation of the watch command for auto-rebuild on file changes
 */

// Prevent Windows min/max macros from conflicting with std::min/max
#ifdef _WIN32
#define NOMINMAX
#endif

#include "cforge/log.hpp"
#include "core/build_utils.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/types.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <map>
#include <thread>

namespace fs = std::filesystem;

namespace {

// Global flag for graceful shutdown
std::atomic<bool> g_should_exit{false};

#ifdef _WIN32
BOOL WINAPI console_handler(DWORD signal) {
  if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT) {
    g_should_exit = true;
    return TRUE;
  }
  return FALSE;
}
#else
void signal_handler(int) { g_should_exit = true; }
#endif

/**
 * @brief Get the last write time of a file
 */
std::filesystem::file_time_type get_mtime(const fs::path &path) {
  try {
    return fs::last_write_time(path);
  } catch (...) {
    return std::filesystem::file_time_type::min();
  }
}

/**
 * @brief File watcher that tracks modification times
 */
class FileWatcher {
public:
  FileWatcher(const fs::path &root_dir,
              const std::vector<std::string> &extensions)
      : m_root(root_dir), m_extensions(extensions) {
    scan_files();
  }

  /**
   * @brief Check if any tracked files have changed
   * @return true if changes detected
   */
  bool check_for_changes() {
    bool changed = false;

    // Check existing files for modifications
    for (auto &[path, mtime] : m_files) {
      if (!fs::exists(path)) {
        // File was deleted
        changed = true;
        m_deleted_files.push_back(path);
      } else {
        auto new_mtime = get_mtime(path);
        if (new_mtime != mtime) {
          mtime = new_mtime;
          m_changed_files.push_back(path);
          changed = true;
        }
      }
    }

    // Remove deleted files from tracking
    for (const auto &path : m_deleted_files) {
      m_files.erase(path);
    }

    // Scan for new files
    scan_for_new_files();
    if (!m_new_files.empty()) {
      changed = true;
    }

    return changed;
  }

  /**
   * @brief Get and clear the list of changed files
   */
  std::vector<fs::path> get_changed_files() {
    auto files = std::move(m_changed_files);
    m_changed_files.clear();
    return files;
  }

  /**
   * @brief Get and clear the list of new files
   */
  std::vector<fs::path> get_new_files() {
    auto files = std::move(m_new_files);
    m_new_files.clear();
    return files;
  }

  /**
   * @brief Get and clear the list of deleted files
   */
  std::vector<fs::path> get_deleted_files() {
    auto files = std::move(m_deleted_files);
    m_deleted_files.clear();
    return files;
  }

  cforge_size_t file_count() const { return m_files.size(); }

private:
  void scan_files() { scan_directory(m_root); }

  void scan_directory(const fs::path &dir) {
    if (!fs::exists(dir))
      return;

    try {
      for (const auto &entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file())
          continue;

        // Skip build directories
        std::string path_str = entry.path().string();
        if (path_str.find("build") != std::string::npos ||
            path_str.find(".git") != std::string::npos ||
            path_str.find("deps") != std::string::npos ||
            path_str.find("vendor") != std::string::npos) {
          continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        for (const auto &valid_ext : m_extensions) {
          if (ext == valid_ext) {
            m_files[entry.path()] = get_mtime(entry.path());
            break;
          }
        }
      }
    } catch (const std::exception &) {
      // Ignore errors during scanning
    }
  }

  void scan_for_new_files() {
    try {
      for (const auto &entry : fs::recursive_directory_iterator(m_root)) {
        if (!entry.is_regular_file())
          continue;

        std::string path_str = entry.path().string();
        if (path_str.find("build") != std::string::npos ||
            path_str.find(".git") != std::string::npos ||
            path_str.find("deps") != std::string::npos ||
            path_str.find("vendor") != std::string::npos) {
          continue;
        }

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        bool valid = false;
        for (const auto &valid_ext : m_extensions) {
          if (ext == valid_ext) {
            valid = true;
            break;
          }
        }

        if (valid && m_files.find(entry.path()) == m_files.end()) {
          m_files[entry.path()] = get_mtime(entry.path());
          m_new_files.push_back(entry.path());
        }
      }
    } catch (const std::exception &) {
      // Ignore errors
    }
  }

  fs::path m_root;
  std::vector<std::string> m_extensions;
  std::map<fs::path, std::filesystem::file_time_type> m_files;
  std::vector<fs::path> m_changed_files;
  std::vector<fs::path> m_new_files;
  std::vector<fs::path> m_deleted_files;
};

/**
 * @brief Run the build command using proper cforge build utilities
 *
 * This function handles:
 * - CMakeLists.txt regeneration from cforge.toml when needed
 * - CMake reconfiguration when needed
 * - The actual build with proper config support
 */
bool run_build(const fs::path &project_dir, const std::string &config,
               bool verbose, bool toml_changed = false) {
  auto start = std::chrono::steady_clock::now();

  // Get build directory
  fs::path build_dir = cforge::get_build_dir_for_config(
      (project_dir / DEFAULT_BUILD_DIR).string(), config, true);

  // Prepare project for build (handles CMakeLists.txt regeneration and CMake config)
  auto prep_result = cforge::prepare_project_for_build(
      project_dir, build_dir, config, verbose,
      toml_changed,  // Force regeneration if toml changed
      false          // Don't force reconfigure unless needed
  );

  if (!prep_result.success) {
    cforge::logger::print_error(prep_result.error_message);
    return false;
  }

  if (prep_result.cmakelists_regenerated) {
    cforge::logger::print_action("Regenerated", "CMakeLists.txt from cforge.toml");
  }

  if (prep_result.cmake_reconfigured) {
    cforge::logger::print_action("Reconfigured", "CMake build system");
  }

  // Run the actual build
  bool build_success = cforge::run_cmake_build(build_dir, config, "", 0, verbose);

  auto end = std::chrono::steady_clock::now();
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  if (build_success) {
    cforge::logger::finished("build",
                     fmt::format("{:.2f}s", duration.count() / 1000.0));
    return true;
  } else {
    cforge::logger::print_error("Build failed");
    return false;
  }
}

} // anonymous namespace

/**
 * @brief Handle the 'watch' command for auto-rebuild
 */
cforge_int_t cforge_cmd_watch(const cforge_context_t *ctx) {
  fs::path project_dir = ctx->working_dir;

  // Parse arguments
  std::string config = "";
  bool verbose = false;
  bool run_after_build = false;
  cforge_int_t poll_interval_ms = 500; // Default: check every 500ms

  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if ((arg == "-c" || arg == "--config") && i + 1 < ctx->args.arg_count) {
      config = ctx->args.args[++i];
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "--run" || arg == "-r") {
      run_after_build = true;
    } else if (arg == "--interval" && i + 1 < ctx->args.arg_count) {
      poll_interval_ms = std::stoi(ctx->args.args[++i]);
    } else if (arg == "--release") {
      config = "Release";
    } else if (arg == "--debug") {
      config = "Debug";
    }
  }

  // Check for cforge.toml
  fs::path config_file = project_dir / "cforge.toml";
  if (!fs::exists(config_file)) {
    cforge::logger::print_error("No cforge.toml found in current directory");
    return 1;
  }

  // Set up signal handlers for graceful shutdown
#ifdef _WIN32
  SetConsoleCtrlHandler(console_handler, TRUE);
#else
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
#endif

  // Set up file watcher
  std::vector<std::string> extensions = {
      ".cpp", ".cc", ".cxx", ".c", ".hpp", ".hxx", ".h",
      ".toml" // Also watch config changes
  };

  FileWatcher watcher(project_dir, extensions);

  cforge::logger::print_header("Watching for changes...");
  cforge::logger::print_status("Tracking " + std::to_string(watcher.file_count()) +
                       " files");
  cforge::logger::print_status("Build config: " + (config.empty() ? "Debug" : config));
  cforge::logger::print_status("Press Ctrl+C to stop");
  fmt::print("\n");

  // Use Debug as default config if none specified
  std::string effective_config = config.empty() ? "Debug" : config;

  // Do an initial build
  cforge::logger::building(project_dir.filename().string());
  bool last_build_succeeded = run_build(project_dir, effective_config, verbose, false);
  fmt::print("\n");

  // Watch loop
  while (!g_should_exit) {
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));

    if (watcher.check_for_changes()) {
      auto changed = watcher.get_changed_files();
      auto added = watcher.get_new_files();
      auto deleted = watcher.get_deleted_files();

      // Check if any toml file changed (triggers CMakeLists.txt regeneration)
      bool toml_changed = false;
      auto check_toml = [&toml_changed](const std::vector<fs::path> &files) {
        for (const auto &file : files) {
          if (file.extension() == ".toml") {
            toml_changed = true;
            break;
          }
        }
      };
      check_toml(changed);
      check_toml(added);

      // Clear screen (optional, can be made configurable)
      // fmt::print("\033[2J\033[H");

      // Report changes
      fmt::print("\n");
      cforge::logger::print_status("Changes detected:");

      for (const auto &file : changed) {
        cforge::logger::print_action("Modified", file.filename().string());
      }
      for (const auto &file : added) {
        cforge::logger::print_action("Added", file.filename().string());
      }
      for (const auto &file : deleted) {
        cforge::logger::print_action("Removed", file.filename().string());
      }

      if (toml_changed) {
        cforge::logger::print_action("Config", "cforge.toml changed, will regenerate CMakeLists.txt");
      }

      fmt::print("\n");

      // Rebuild
      cforge::logger::building(project_dir.filename().string());
      last_build_succeeded = run_build(project_dir, effective_config, verbose, toml_changed);

      // Run if requested and build succeeded
      if (run_after_build && last_build_succeeded) {
        // Find and run the executable
        cforge::toml_reader reader;
        reader.load(config_file.string());
        std::string project_name = reader.get_string("project.name");

        if (!project_name.empty()) {
          // Use the build utilities to find the binary
          fs::path build_dir = cforge::get_build_dir_for_config(
              (project_dir / DEFAULT_BUILD_DIR).string(), effective_config, false);

          fs::path exe_path = cforge::find_project_binary(
              build_dir, project_name, effective_config, "executable");

          if (!exe_path.empty() && fs::exists(exe_path)) {
            fmt::print("\n");
            cforge::logger::running(exe_path.filename().string());
            fmt::print("{}\n", std::string(40, '-'));

            auto run_result = cforge::execute_process(
                exe_path.string(), {}, project_dir.string(),
                [](const std::string &line) { fmt::print("{}\n", line); },
                [](const std::string &line) {
                  fmt::print(stderr, "{}\n", line);
                });

            fmt::print("{}\n", std::string(40, '-'));
            if (run_result.exit_code != 0) {
              cforge::logger::print_warning("Process exited with code " +
                                    std::to_string(run_result.exit_code));
            }
          } else {
            cforge::logger::print_warning("Could not find executable: " + project_name);
          }
        }
      }

      fmt::print("\n");
      cforge::logger::print_status("Watching for changes... (Ctrl+C to stop)");
    }
  }

  fmt::print("\n");
  cforge::logger::print_status("Watch mode stopped");

  return 0;
}
