/**
 * @file installer.cpp
 * @brief Implementation of installation utilities for cforge
 */

#include "core/installer.hpp"
#include "cforge/log.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process.h"
#include "core/process_utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>

#ifdef _WIN32
#include <direct.h>
#include <shlobj.h>
#include <windows.h>
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace cforge {

// Helper function to serve as cforge_print_verbose equivalent
inline void print_verbose(const std::string &message) {
  if (logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE) {
    logger::print_status(message);
  }
}

std::string installer::get_current_version() const { return CFORGE_VERSION; }

bool installer::install(const std::string &install_path, bool add_to_path) {
  std::filesystem::path target_path;

  if (install_path.empty()) {
    target_path = get_platform_specific_path();
  } else {
    target_path = install_path;
  }

  logger::print_status("Installing cforge to: " + target_path.string());

  // Ensure parent directory exists
  {
    auto parent_dir = target_path.parent_path();
    if (!std::filesystem::exists(parent_dir)) {
      try {
        std::filesystem::create_directories(parent_dir);
        print_verbose("Created parent directory: " + parent_dir.string());
      } catch (const std::exception &e) {
        logger::print_error("Failed to create parent directory: " + std::string(e.what()));
        return false;
      }
    }
  }

  // Check if path is writable
  if (!is_path_writeable(target_path)) {
    logger::print_error("Installation path is not writable: " + target_path.string());
    return false;
  }

  // Create the directory if it doesn't exist
  if (!std::filesystem::exists(target_path)) {
    try {
      std::filesystem::create_directories(target_path);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create installation directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // Get current executable path
  std::filesystem::path current_exe = std::filesystem::current_path();

  // Copy files to target path
  std::vector<std::string> exclude_patterns = {"\\.git",           "build",
                                               "\\.vscode",        "CMakeFiles",
                                               "CMakeCache\\.txt", "\\.github"};

  if (!copy_files(current_exe, target_path, exclude_patterns)) {
    logger::print_error("Failed to copy files to installation directory");
    return false;
  }

  // Create executable links or shortcuts
  std::filesystem::path bin_path = target_path / "bin";
  if (!create_executable_links(bin_path)) {
    logger::print_warning("Failed to create executable links");
  }

  // Update PATH environment variable if requested
  if (add_to_path) {
    if (!update_path_env(bin_path)) {
      logger::print_warning("Failed to update PATH environment variable");
    } else {
      logger::print_success(
          "Added installation directory to PATH environment variable");
    }
  }

  logger::print_success("cforge has been installed successfully to " +
                        target_path.string());
  return true;
}

bool installer::update() {
  logger::print_status("Starting cforge self-update...");

  // Verify installation
  if (!is_installed()) {
    logger::print_error("cforge is not installed. Please run 'cforge install' first.");
    return false;
  }

  std::string install_location = get_install_location();
  logger::print_status("Installation location: " + install_location);
  std::filesystem::path install_path(install_location);
  std::filesystem::path git_dir = install_path / ".git";

  // If this installation was a Git checkout, pull latest
  if (std::filesystem::exists(git_dir) && std::filesystem::is_directory(git_dir)) {
    logger::print_status("Detected Git repository. Pulling latest changes...");
    auto result = execute_process(
        "git",
        std::vector<std::string>{"-C", install_path.string(), "pull", "--rebase"},
        "",
        [](const std::string &line) { logger::print_status(line); },
        [](const std::string &line) { logger::print_error(line); });
    if (!result.success) {
      logger::print_error("Git pull failed (exit code " + std::to_string(result.exit_code) + ")");
      return false;
    }
    logger::print_status("Reinstalling updated cforge...");
    return install(install_location);
  }

  // Otherwise, clone a fresh copy and install
  std::filesystem::path parent = install_path.parent_path();
  std::filesystem::path tmp = parent / "cforge_update_tmp";
  if (std::filesystem::exists(tmp)) {
    std::filesystem::remove_all(tmp);
  }
  logger::print_status("Cloning cforge repository...");
  auto clone_result = execute_process(
      "git",
      std::vector<std::string>{"clone", CFORGE_REPO_URL, tmp.string()},
      "",
      [](const std::string &line) { logger::print_status(line); },
      [](const std::string &line) { logger::print_error(line); });
  if (!clone_result.success) {
    logger::print_error("Git clone failed (exit code " + std::to_string(clone_result.exit_code) + ")");
    return false;
  }
  logger::print_status("Installing new version...");
  bool ok = install(tmp.string());
  std::filesystem::remove_all(tmp);
  return ok;
}

bool installer::install_project(const std::string &project_path,
                                const std::string &install_path,
                                bool add_to_path,
                                const std::string &project_name_override,
                                const std::string &build_config,
                                const std::string &env_var) {
  // Report project source in verbose mode to avoid duplication
  print_verbose("Installing project from: " + project_path);

  // Determine build configuration (default to Release)
  std::string cfg = build_config.empty() ? "Release" : build_config;
  // Determine install path
  std::string effective_install_path = install_path;

  // Check if project exists
  std::filesystem::path project_dir(project_path);
  if (!std::filesystem::exists(project_dir)) {
    logger::print_error("Project directory does not exist: " + project_path);
    return false;
  }

  // Check if it's a cforge project by looking for cforge.toml
  std::filesystem::path project_config = project_dir / CFORGE_FILE;
  if (!std::filesystem::exists(project_config)) {
    logger::print_error("Not a valid cforge project (missing " +
                        std::string(CFORGE_FILE) + ")");
    return false;
  }

  // Read project configuration
  auto config = read_project_config(project_path);
  if (!config) {
    logger::print_error("Failed to read project configuration");
    return false;
  }

  // Get project name and details
  std::string project_name = config->get_string("project.name");
  std::string project_version = config->get_string("project.version");
  std::string project_type = config->get_string("project.type", "executable");

  if (project_name.empty()) {
    logger::print_error("Project name not specified in " +
                        std::string(CFORGE_FILE));
    return false;
  }

  logger::print_status("Project: " + project_name + " (version " +
                       project_version + ")");

  // Determine install path
  std::filesystem::path target_path;
  if (effective_install_path.empty()) {
    if (project_type == "executable") {
      // For executables, install to a standard bin location
      std::filesystem::path platform_path = get_platform_specific_path();
      target_path = platform_path / "installed" / project_name;
    } else {
      // For libraries, install to a standard lib location
      std::filesystem::path platform_path = get_platform_specific_path();
      target_path = platform_path / "lib" / project_name;
    }
  } else {
    target_path = effective_install_path;
  }

  logger::print_status("Installing to: " + target_path.string());

  // Ensure parent directory exists for project install
  {
    auto parent_dir = target_path.parent_path();
    if (!std::filesystem::exists(parent_dir)) {
      try {
        std::filesystem::create_directories(parent_dir);
        print_verbose("Created install parent directory: " + parent_dir.string());
      } catch (const std::exception &e) {
        logger::print_error("Failed to create install parent directory: " + std::string(e.what()));
        return false;
      }
    }
  }

  // Check if path is writable
  if (!is_path_writeable(target_path)) {
    logger::print_error("Installation path is not writable: " + target_path.string());
    return false;
  }

  // Create the directory if it doesn't exist
  if (!std::filesystem::exists(target_path)) {
    try {
      std::filesystem::create_directories(target_path);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create installation directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // Build the project first (verbose) or skip if in a workspace
  print_verbose("Building project before installation...");
  // Save original working directory
  std::filesystem::path original_path = std::filesystem::current_path();
  // Detect if this project is inside a workspace
  std::filesystem::path cwd = project_dir;
  std::filesystem::path workspace_root;
  bool in_workspace = false;
  while (true) {
    if (std::filesystem::exists(cwd / WORKSPACE_FILE)) {
      workspace_root = cwd;
      in_workspace = true;
      break;
    }
    if (cwd == cwd.parent_path()) break;
    cwd = cwd.parent_path();
  }
  bool build_success = true;
  if (in_workspace) {
    print_verbose("Detected workspace at " + workspace_root.string() + "; skipping per-project build and using workspace build artifacts");
  } else {
    // Change to project directory for build
    std::filesystem::current_path(project_dir);
    // Check if there's a CMakeLists.txt file
    bool has_cmake = std::filesystem::exists(project_dir / "CMakeLists.txt");
    build_success = false;
    if (has_cmake) {
      // Build the project with CMake
      logger::print_status("Building project in Release mode...");
      std::vector<std::string> cmake_args = {"..", "-G", "Ninja",
                                             "-DCMAKE_BUILD_TYPE=Release"};
      build_success = execute_tool("cmake", cmake_args, project_dir.string(),
                                   "CMake Configure", 
                                   logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE,
                                   120);

      if (build_success) {
        // Build the project with Ninja
        logger::print_status("Building project in Release mode...");
        std::vector<std::string> ninja_args = {};
        build_success = execute_tool("ninja", ninja_args, project_dir.string(),
                                     "Ninja Build", 
                                     logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE,
                                     300);
      }
    } else {
      // No CMake, try looking for a Makefile
      if (std::filesystem::exists(project_dir / "Makefile")) {
        print_verbose("Building project with Makefile...");
        try {
#ifdef _WIN32
          bool make_available = is_command_available("make") ||
                                is_command_available("mingw32-make") ||
                                is_command_available("nmake");
          if (!make_available) {
            logger::print_warning("No make tool found (make, mingw32-make, "
                                  "nmake). Skipping build.");
            build_success = false;
          } else {
            // Try different make tools
            if (is_command_available("make")) {
              int make_result = system("make");
              build_success = (make_result == 0);
            } else if (is_command_available("mingw32-make")) {
              int make_result = system("mingw32-make");
              build_success = (make_result == 0);
            } else if (is_command_available("nmake")) {
              int make_result = system("nmake");
              build_success = (make_result == 0);
            }
          }
#else
          bool make_available = is_command_available("make");
          if (!make_available) {
            logger::print_warning("Make is not available. Skipping build.");
            build_success = false;
          } else {
            int make_result = system("make");
            build_success = (make_result == 0);
          }
#endif
        } catch (const std::exception &ex) {
          logger::print_warning("Error during build: " + std::string(ex.what()));
          build_success = false;
        }
      } else {
        // Assume the project is pre-built
        print_verbose("No build system detected. Assuming pre-built project.");
        build_success = true;
      }
    }
    if (!build_success) {
      logger::print_warning("Build failed or skipped; searching for pre-built executables.");
    } else {
      print_verbose("Build completed successfully.");
    }
    // Restore working directory after build
    std::filesystem::current_path(original_path);
  }

  // Copy files to target path
  std::vector<std::string> exclude_patterns = {
      "\\.git",           "build",     "\\.vscode", "CMakeFiles",
      "CMakeCache\\.txt", "\\.github", "src",       "tests"};

  if (project_type == "executable") {
    // For executables, only copy the binary and necessary resources
    logger::print_status("Installing executable...");

    // Create 'bin' subdirectory in the install path
    std::filesystem::path install_bin = target_path / "bin";
    if (!std::filesystem::exists(install_bin)) {
      try {
        std::filesystem::create_directories(install_bin);
        print_verbose("Created install 'bin' directory: " + install_bin.string());
      } catch (const std::exception &e) {
        logger::print_warning("Failed to create install bin directory: " + std::string(e.what()));
      }
    }

    // Copy the binary - from build output directory or shared workspace build
    // Detect if project is inside a workspace
    std::filesystem::path current = project_dir;
    std::filesystem::path workspace_root;
    bool in_workspace = false;
    while (true) {
      if (std::filesystem::exists(current / WORKSPACE_FILE)) {
        workspace_root = current;
        in_workspace = true;
        break;
      }
      if (current == current.parent_path()) break;
      current = current.parent_path();
    }
    std::filesystem::path build_base;
    if (in_workspace) {
      build_base = workspace_root / DEFAULT_BUILD_DIR;
      logger::print_verbose("Using workspace build directory for install: " + build_base.string());
    } else {
      build_base = project_dir / DEFAULT_BUILD_DIR;
    }
    std::filesystem::path bin_dir = build_base / "bin" / cfg;
    print_verbose("Looking for executables in: " + bin_dir.string());
    bool found_executable = false;

    // Fallback: check build/bin
    if (!std::filesystem::exists(bin_dir)) {
      bin_dir = build_base / "bin";
      print_verbose("Fallback to build/bin: " + bin_dir.string());
    }

    // Fallback: check project/bin/<config> only when not in a workspace
    if (!in_workspace && !std::filesystem::exists(bin_dir)) {
      bin_dir = project_dir / "bin" / cfg;
      print_verbose("Fallback to project/bin/config: " + bin_dir.string());
    }

    // Fallback: check project/bin only when not in a workspace
    if (!in_workspace && !std::filesystem::exists(bin_dir)) {
      bin_dir = project_dir / "bin";
      print_verbose("Fallback to project/bin: " + bin_dir.string());
    }

    // Function to check if an executable is a valid project executable and not
    // a CMAKE temp file
    auto is_valid_executable =
        [&project_name](const std::filesystem::path &path) -> bool {
      std::string filename = path.filename().string();

      // Exclude known CMake test files
      if (filename.find("CMakeC") != std::string::npos ||
          filename.find("CMakeCXX") != std::string::npos ||
          filename.find("CompilerId") != std::string::npos) {
        return false;
      }

      // Try to match the project name - case insensitive comparison
      std::string lower_filename = filename;
      std::string lower_project = project_name;
      std::transform(lower_filename.begin(), lower_filename.end(),
                     lower_filename.begin(), ::tolower);
      std::transform(lower_project.begin(), lower_project.end(),
                     lower_project.begin(), ::tolower);

      // If filename contains the project name, it's likely the right executable
      if (lower_filename.find(lower_project) != std::string::npos) {
        return true;
      }

      // Normalize project name by replacing hyphens with underscores, just like
      // we do during project creation
      std::string normalized_project = lower_project;
      std::replace(normalized_project.begin(), normalized_project.end(), '-',
                   '_');

      // Check again with normalized project name
      if (normalized_project != lower_project &&
          lower_filename.find(normalized_project) != std::string::npos) {
        return true;
      }

      // Check for common executable names
      if (lower_filename.find("app") != std::string::npos ||
          lower_filename.find("main") != std::string::npos ||
          lower_filename.find("launcher") != std::string::npos) {
        return true;
      }

      // If we're still looking, sometimes the executable might be named
      // differently But it's usually not a test file with certain patterns
      return filename.find("test_") == std::string::npos &&
             filename.find("cmake") == std::string::npos &&
             filename.find("config") == std::string::npos;
    };

    if (std::filesystem::exists(bin_dir)) {
      print_verbose("Searching for executables in: " + bin_dir.string());
      for (const auto &entry : std::filesystem::directory_iterator(bin_dir)) {
        if (entry.is_regular_file()) {
          // Windows: check for .exe extension
#ifdef _WIN32
          if (entry.path().extension() == ".exe") {
            print_verbose("Found potential executable: " + entry.path().string());

            // Make sure it's not a CMake test file
            if (!is_valid_executable(entry.path())) {
              print_verbose("Skipping CMake test executable: " + entry.path().string());
              continue;
            }
          }
#else
          // Unix-like: check for owner execute permission
          if (((entry.status().permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)) {
             if (!is_valid_executable(entry.path())) {
                print_verbose("Skipping test executable: " + entry.path().string());
                continue;
             }
          }
#endif
          print_verbose("Using project executable: " + entry.path().string());
          {
            auto target = install_bin / entry.path().filename();
            if (std::filesystem::exists(target)) {
              std::filesystem::remove(target);
            }
            std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
          }
          found_executable = true;
        }
      }
    }

    // If no executable found yet and not in a workspace, look in the build directory
    if (!found_executable && !in_workspace) {
      print_verbose(
          "No executable found in bin directory, searching build directory...");
      std::filesystem::path build_dir = project_dir / "build";

      if (std::filesystem::exists(build_dir)) {
        print_verbose("Searching recursively in build directory: " +
                      build_dir.string());
        // Recursively search for executables in build directory
        for (const auto &entry :
             std::filesystem::recursive_directory_iterator(build_dir)) {
          if (entry.is_regular_file()) {
#ifdef _WIN32
            if (entry.path().extension() == ".exe") {
              print_verbose("Found potential executable: " +
                            entry.path().string());

              // Skip CMake test files
              if (!is_valid_executable(entry.path())) {
                print_verbose("Skipping CMake test executable: " +
                              entry.path().string());
                continue;
              }

              print_verbose("Using project executable: " +
                            entry.path().string());
#else
            if (((entry.status().permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)) {
               if (!is_valid_executable(entry.path())) {
                  print_verbose("Skipping test executable: " + entry.path().string());
                  continue;
               }
             
               print_verbose("Using project executable: " + entry.path().string());
#endif
              {
                auto target = install_bin / entry.path().filename();
                if (std::filesystem::exists(target)) {
                  std::filesystem::remove(target);
                }
                std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
              }
            }
          }
        }
      }
    }

    // Final fallback: search entire project directory for .exe files (Windows
    // only)
#ifdef _WIN32
    if (!found_executable) {
      print_verbose("Still no executable found, searching throughout project "
                    "directory for any valid .exe files...");
      try {
        for (const auto &entry :
             std::filesystem::recursive_directory_iterator(project_dir)) {
          if (entry.is_regular_file() && entry.path().extension() == ".exe") {
            // Skip CMake test files
            if (!is_valid_executable(entry.path())) {
              print_verbose("Skipping CMake test executable: " +
                            entry.path().string());
              continue;
            }

            print_verbose("Found executable: " + entry.path().string());
            {
              auto target = install_bin / entry.path().filename();
              if (std::filesystem::exists(target)) {
                std::filesystem::remove(target);
              }
              std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
            }
            found_executable = true;
          }
        }
      } catch (const std::exception &ex) {
        logger::print_warning("Error searching for executables: " +
                              std::string(ex.what()));
      }
    }
#endif

    // If there's a resources directory, copy it too
    std::filesystem::path resources_dir = project_dir / "resources";
    if (std::filesystem::exists(resources_dir)) {
      try {
        copy_files(resources_dir, target_path / "resources");
        print_verbose("Copied resources directory");
      } catch (const std::exception &ex) {
        logger::print_warning("Failed to copy resources: " +
                              std::string(ex.what()));
      }
    }

    // Skip default link creation; executables already placed in install/bin
  } else {
    // For libraries, copy everything except exclude patterns
    if (!copy_files(project_dir, target_path, exclude_patterns)) {
      logger::print_error("Failed to copy files to installation directory");
      return false;
    }

    // Copy headers to include directory
    std::filesystem::path include_dir = project_dir / "include";
    if (std::filesystem::exists(include_dir)) {
      std::filesystem::path platform_path = get_platform_specific_path();
      std::filesystem::path target_include =
          std::filesystem::path(platform_path) / "include" / project_name;

      try {
        std::filesystem::create_directories(target_include);
        copy_files(include_dir, target_include);
        print_verbose("Copied headers to " + target_include.string());
      } catch (const std::exception &ex) {
        logger::print_warning("Failed to copy headers: " +
                              std::string(ex.what()));
      }
    }

    // Copy libraries to lib directory
    std::filesystem::path lib_dir = project_dir / "lib" / "Release";
    if (std::filesystem::exists(lib_dir)) {
      std::filesystem::path platform_path = get_platform_specific_path();
      std::filesystem::path target_lib =
          std::filesystem::path(platform_path) / "lib";

      try {
        std::filesystem::create_directories(target_lib);
        for (const auto &entry : std::filesystem::directory_iterator(lib_dir)) {
          if (entry.is_regular_file()) {
            {
              auto target = target_lib / entry.path().filename();
              if (std::filesystem::exists(target)) {
                std::filesystem::remove(target);
              }
              std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
            }
            print_verbose("Copied " + entry.path().string() + " to " +
                          target_lib.string());
          }
        }
      } catch (const std::exception &ex) {
        logger::print_warning("Failed to copy libraries: " +
                              std::string(ex.what()));
      }
    }
  }

  // Update PATH to include install/bin
  std::filesystem::path install_bin = target_path / "bin";
  if (!update_path_env(install_bin)) {
    logger::print_warning("Failed to update PATH environment variable");
  } else {
    logger::print_status("Added installation 'bin' to PATH environment variable");
  }

  // Set custom environment variable if requested
  if (!project_name_override.empty()) {
    update_env_var(project_name_override, target_path);
  }

  logger::print_success("Project " + project_name + " installed to " + target_path.string());
  return true;
}

std::string installer::get_default_install_path() const {
  return get_platform_specific_path();
}

bool installer::is_installed() const {
  std::string install_path = get_install_location();
  return !install_path.empty() && std::filesystem::exists(install_path);
}

std::string installer::get_install_location() const {
  // Try to find cforge installation

#ifdef _WIN32
  // On Windows, check the registry first
  HKEY h_key;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\cforge", 0, KEY_READ,
                    &h_key) == ERROR_SUCCESS) {
    char buffer[MAX_PATH];
    DWORD buffer_size = sizeof(buffer);

    if (RegQueryValueExA(h_key, "InstallLocation", NULL, NULL, (LPBYTE)buffer,
                         &buffer_size) == ERROR_SUCCESS) {
      RegCloseKey(h_key);
      return std::string(buffer);
    }

    RegCloseKey(h_key);
  }

  // Check Program Files
  char program_files[MAX_PATH];
  if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, program_files) ==
      S_OK) {
    std::filesystem::path path =
        std::filesystem::path(program_files) / "cforge";
    if (std::filesystem::exists(path)) {
      return path.string();
    }
  }
#else
  // On Unix-like systems, check standard locations
  std::vector<std::string> locations = {"/usr/local/bin/cforge",
                                        "/usr/bin/cforge", "/opt/cforge"};

  for (const auto &loc : locations) {
    if (std::filesystem::exists(loc)) {
      return loc;
    }
  }
#endif

  // As a fallback, check the PATH
  std::string path_env = getenv("PATH") ? getenv("PATH") : "";
  std::vector<std::string> paths;

#ifdef _WIN32
  std::string delimiter = ";";
#else
  std::string delimiter = ":";
#endif

  size_t pos = 0;
  std::string token;
  while ((pos = path_env.find(delimiter)) != std::string::npos) {
    token = path_env.substr(0, pos);
    paths.push_back(token);
    path_env.erase(0, pos + delimiter.length());
  }
  paths.push_back(path_env);

  for (const auto &p : paths) {
    std::filesystem::path potential_path = std::filesystem::path(p);
#ifdef _WIN32
    potential_path /= "cforge.exe";
#else
    potential_path /= "cforge";
#endif

    if (std::filesystem::exists(potential_path)) {
      return std::filesystem::canonical(potential_path).parent_path().string();
    }
  }

  return "";
}

bool installer::copy_files(const std::filesystem::path &source,
                           const std::filesystem::path &dest,
                           const std::vector<std::string> &exclude_patterns) {
  try {
    // Create destination directory if it doesn't exist
    if (!std::filesystem::exists(dest)) {
      std::filesystem::create_directories(dest);
    }

    // Function to check if path should be excluded
    auto should_exclude =
        [&exclude_patterns](const std::filesystem::path &path) -> bool {
      // Get a std::string once, don't create multiple string objects
      std::string path_str = path.filename().string();

      // Special case for hidden files/directories (starting with .)
      if (!path_str.empty() && path_str[0] == '.') {
        return true;
      }

      for (const auto &pattern : exclude_patterns) {
        try {
          // Avoid regex if possible - use simple string matching for common
          // patterns
          if (pattern.find("*") == std::string::npos &&
              pattern.find("?") == std::string::npos &&
              pattern.find("[") == std::string::npos &&
              pattern.find(".") == std::string::npos) {
            // Simple substring match for basic patterns
            if (path_str.find(pattern) != std::string::npos) {
              return true;
            }
          } else if (pattern.find("\\") == std::string::npos &&
                     pattern == ".*" && !path_str.empty() &&
                     path_str[0] == '.') {
            // Special case for ".*" pattern to match hidden files
            return true;
          } else {
            // Use regex for more complex patterns
            // Create a completely new copy of the strings to avoid iterator
            // issues
            std::string regex_pattern = pattern;
            std::string check_path = path_str;

            // Compile regex
            std::regex regex(regex_pattern);

            // Use regex_match for full string match instead of regex_search for
            // partial match This is safer and less likely to cause issues
            if (std::regex_match(check_path.begin(), check_path.end(), regex)) {
              return true;
            }
          }
        } catch (const std::regex_error &e) {
          // Failed to compile regex, fall back to simple substring match
          if (path_str.find(pattern) != std::string::npos) {
            return true;
          }
        }
      }
      return false;
    };

    // Copy files and directories recursively
    for (const auto &entry : std::filesystem::directory_iterator(source)) {
      if (should_exclude(entry.path())) {
        continue;
      }

      std::filesystem::path target = dest / entry.path().filename();

      if (entry.is_directory()) {
        copy_files(entry.path(), target, exclude_patterns);
      } else if (entry.is_regular_file()) {
        {
          auto target = dest / entry.path().filename();
          if (std::filesystem::exists(target)) {
            std::filesystem::remove(target);
          }
          std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
        }
        print_verbose("Copied " + entry.path().string() + " to " +
                      target.string());
      }
    }

    return true;
  } catch (const std::exception &ex) {
    logger::print_error("Error copying files: " + std::string(ex.what()));
    return false;
  }
}

std::string installer::get_platform_specific_path() const {
#ifdef _WIN32
  // On Windows, use Program Files
  char program_files[MAX_PATH];
  if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, program_files) ==
      S_OK) {
    return std::string(program_files) + "\\cforge";
  }
  return "C:\\Program Files\\cforge";
#else
  // On Unix-like systems, use /usr/local
  return "/usr/local/cforge";
#endif
}

bool installer::is_path_writeable(const std::filesystem::path &path) const {
  if (!std::filesystem::exists(path)) {
    // If path doesn't exist, check if parent directory is writable
    auto parent = path.parent_path();
    if (!std::filesystem::exists(parent)) {
      return false;
    }

    // Create a temporary file to test permissions
    auto test_file = parent / "cforge_write_test";
    try {
      std::ofstream file(test_file);
      bool result = file.is_open();
      file.close();
      if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
      }
      return result;
    } catch (...) {
      return false;
    }
  } else {
    // Path exists, check if we can write to it
    auto test_file = path / "cforge_write_test";
    try {
      std::ofstream file(test_file);
      bool result = file.is_open();
      file.close();
      if (std::filesystem::exists(test_file)) {
        std::filesystem::remove(test_file);
      }
      return result;
    } catch (...) {
      return false;
    }
  }
}

bool installer::create_executable_links(
    const std::filesystem::path &bin_path) const {
#ifdef _WIN32
  // On Windows, create shortcuts or add to PATH

  // Make sure the directory exists
  if (!std::filesystem::exists(bin_path)) {
    try {
      std::filesystem::create_directories(bin_path);
      print_verbose("Created directory: " + bin_path.string());
    } catch (const std::exception &ex) {
      logger::print_warning("Failed to create bin directory: " +
                            std::string(ex.what()));
    }
  }

  // First try to find executables directly in the specified path
  print_verbose("Searching for executables in: " + bin_path.string());

  // Helper function to determine if an executable is a valid project executable
  // (not a CMake test file)
  auto is_valid_executable = [](const std::filesystem::path &path) -> bool {
    std::string filename = path.filename().string();

    // Exclude known CMake test files
    if (filename.find("CMakeC") != std::string::npos ||
        filename.find("CMakeCXX") != std::string::npos ||
        filename.find("CompilerId") != std::string::npos) {
      return false;
    }

    // Usually not a test file with certain patterns
    return filename.find("test_") == std::string::npos &&
           filename.find("cmake") == std::string::npos &&
           filename.find("config") == std::string::npos;
  };

  // Find the executable in the bin directory
  std::filesystem::path exe_path;

  // Multiple search locations in priority order
  std::vector<std::filesystem::path> search_locations = {
      bin_path,                                // First check the bin path
      bin_path.parent_path(),                  // Then check parent dir
      bin_path.parent_path() / "Debug",        // Check Debug folder
      bin_path.parent_path() / "Release",      // Check Release folder
      bin_path.parent_path() / "build",        // Check build folder
      bin_path.parent_path() / "build-release" // Check build-release folder
  };

  for (const auto &location : search_locations) {
    if (!std::filesystem::exists(location)) {
      print_verbose("Directory doesn't exist: " + location.string());
      continue;
    }

    logger::print_status("Looking for executables in: " + location.string());

    for (const auto &entry : std::filesystem::directory_iterator(location)) {
      if (entry.is_regular_file() && entry.path().extension() == ".exe") {
        // Skip CMake temporary files
        if (!is_valid_executable(entry.path())) {
          print_verbose("Skipping CMake test executable: " +
                        entry.path().string());
          continue;
        }

        exe_path = entry.path();
        logger::print_status("Found valid executable: " + exe_path.string());

        // Copy to bin directory if not already there
        if (location != bin_path) {
          try {
            // Use the parent directory name for the exe if it's just "a.exe"
            std::filesystem::path target_filename = exe_path.filename();
            if (target_filename.string() == "a.exe") {
              // Extract project name from directory name
              std::string project_name =
                  bin_path.parent_path().filename().string();
              // Remove any hyphens and replace with underscores
              std::replace(project_name.begin(), project_name.end(), '-', '_');

              target_filename = project_name + ".exe";
              logger::print_status("Renaming generic 'a.exe' to '" +
                                   target_filename.string() + "'");
            }

            std::filesystem::path target = bin_path / target_filename;
            {
              auto target = bin_path / target_filename;
              if (std::filesystem::exists(target)) {
                std::filesystem::remove(target);
              }
              std::filesystem::copy_file(exe_path, target, std::filesystem::copy_options::overwrite_existing);
            }
            exe_path = target;
            logger::print_status("Copied executable to bin directory: " +
                                 exe_path.string());
          } catch (const std::exception &ex) {
            logger::print_warning("Failed to copy executable: " +
                                  std::string(ex.what()));
            // Continue with original path
          }
        }
        break;
      }
    }

    if (!exe_path.empty()) {
      break; // Found an executable, so stop searching
    }
  }

  if (exe_path.empty()) {
    logger::print_error("Could not find executable");
    return false;
  }

  // Add to registry - only for cforge itself, not for installed projects
  if (exe_path.filename().string().find("cforge") != std::string::npos) {
    HKEY h_key;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\cforge", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &h_key,
                        NULL) == ERROR_SUCCESS) {
      std::string install_location = bin_path.parent_path().string();
      RegSetValueExA(h_key, "InstallLocation", 0, REG_SZ,
                     (const BYTE *)install_location.c_str(),
                     (DWORD)install_location.size() + 1);

      RegCloseKey(h_key);
      print_verbose("Added registry entry for cforge");
    } else {
      logger::print_warning("Failed to create registry entry (may need "
                            "administrator privileges)");
    }
  }

  // For installed projects, we don't need to create Start Menu shortcuts
  if (exe_path.filename().string().find("cforge") != std::string::npos) {
    // Create Start Menu shortcut
    char start_menu[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, start_menu) ==
        S_OK) {
      std::filesystem::path shortcut_dir =
          std::filesystem::path(start_menu) / "cforge";

      if (!std::filesystem::exists(shortcut_dir)) {
        std::filesystem::create_directories(shortcut_dir);
      }

      // Use COM to create the shortcut
      IShellLinkA *p_shell_link = NULL;
      IPersistFile *p_persist_file = NULL;

      HRESULT hr = CoInitialize(NULL);
      if (SUCCEEDED(hr)) {
        hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                              IID_IShellLinkA, (void **)&p_shell_link);

        if (SUCCEEDED(hr)) {
          p_shell_link->SetPath(exe_path.string().c_str());
          p_shell_link->SetDescription("cforge C/C++ Build System");
          p_shell_link->SetWorkingDirectory(
              bin_path.parent_path().string().c_str());

          hr = p_shell_link->QueryInterface(IID_IPersistFile,
                                            (void **)&p_persist_file);

          if (SUCCEEDED(hr)) {
            std::filesystem::path shortcut_path = shortcut_dir / "cforge.lnk";

            // Convert to wide string
            std::wstring w_path(shortcut_path.string().begin(),
                                shortcut_path.string().end());

            hr = p_persist_file->Save(w_path.c_str(), TRUE);

            if (SUCCEEDED(hr)) {
              print_verbose("Created Start Menu shortcut");
            } else {
              logger::print_warning("Failed to save shortcut");
            }

            p_persist_file->Release();
          }

          p_shell_link->Release();
        }

        CoUninitialize();
      }
    }
  }

  return true;
#else
  // On Unix-like systems, create symlinks in standard locations
  std::filesystem::path exe_path;

  // Find the executable
  for (const auto &entry : std::filesystem::directory_iterator(bin_path)) {
    if (entry.is_regular_file() &&
        ((entry.status().permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)) {
      exe_path = entry.path();
      print_verbose("Found executable: " + exe_path.string());
      break;
    }
  }

  if (exe_path.empty()) {
    // Try to find in parent directory
    for (const auto &entry : std::filesystem::directory_iterator(bin_path.parent_path())) {
      if (entry.is_regular_file() &&
          ((entry.status().permissions() & std::filesystem::perms::owner_exec) != std::filesystem::perms::none)) {
        exe_path = entry.path();
        print_verbose("Found executable in parent directory: " + exe_path.string());
        break;
      }
    }
  }

  if (exe_path.empty()) {
    logger::print_error("Could not find executable");
    return false;
  }

  // Create symlink in /usr/local/bin only for cforge itself, not for installed
  // projects
  if (exe_path.filename().string() == "cforge") {
    try {
      std::filesystem::path usr_local_bin("/usr/local/bin");
      if (std::filesystem::exists(usr_local_bin)) {
        std::filesystem::path link_path = usr_local_bin / "cforge";

        // Remove existing symlink if it exists
        if (std::filesystem::exists(link_path)) {
          std::filesystem::remove(link_path);
        }

        std::filesystem::create_symlink(exe_path, link_path);
        print_verbose("Created symlink in /usr/local/bin");
      }
    } catch (const std::exception &ex) {
      logger::print_warning("Failed to create symlink in /usr/local/bin: " +
                            std::string(ex.what()));
    }
  }
#endif

  return true;
}

bool installer::update_path_env(const std::filesystem::path &bin_path) const {
#ifdef _WIN32
  // Get the bin directory
  std::filesystem::path path_to_add = bin_path;

  // Ensure it ends with \bin for consistency
  if (path_to_add.filename() != "bin") {
    path_to_add /= "bin";
  }

  // Make sure the directory exists
  if (!std::filesystem::exists(path_to_add)) {
    try {
      std::filesystem::create_directories(path_to_add);
      print_verbose("Created bin directory: " + path_to_add.string());
    } catch (const std::exception &ex) {
      logger::print_warning("Failed to create bin directory: " +
                            std::string(ex.what()));
    }
  }

  logger::print_status("Adding to PATH environment variable: " +
                       path_to_add.string());

  // Get the current PATH
  HKEY hkey;
  if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ | KEY_WRITE,
                    &hkey) != ERROR_SUCCESS) {
    logger::print_error(
        "Failed to open registry key for PATH environment variable");
    return false;
  }

  char current_path[32767] = {0}; // Max environment variable length
  DWORD buf_size = sizeof(current_path);
  DWORD type = 0;

  if (RegQueryValueExA(hkey, "PATH", NULL, &type, (BYTE *)current_path,
                       &buf_size) != ERROR_SUCCESS) {
    if (GetLastError() != ERROR_FILE_NOT_FOUND) {
      RegCloseKey(hkey);
      logger::print_error("Failed to query PATH environment variable");
      return false;
    }

    // PATH doesn't exist yet, create it
    current_path[0] = '\0';
  }

  // Check if the path is already in PATH
  std::string path_str = current_path;
  std::string path_to_add_str = path_to_add.string();

  // Normalize the path for comparison
  std::replace(path_to_add_str.begin(), path_to_add_str.end(), '/', '\\');

  // Check if path is already in PATH
  if (path_str.find(path_to_add_str) != std::string::npos) {
    logger::print_status("Path already in PATH environment variable");
    RegCloseKey(hkey);
    return true;
  }

  // Add path to PATH
  std::string new_path;
  if (path_str.empty()) {
    new_path = path_to_add_str;
  } else {
    // Add a semicolon if the current PATH doesn't end with one
    if (path_str.back() != ';') {
      new_path = path_str + ";" + path_to_add_str;
    } else {
      new_path = path_str + path_to_add_str;
    }
  }

  // Update the PATH
  if (RegSetValueExA(hkey, "PATH", 0, REG_EXPAND_SZ, (BYTE *)new_path.c_str(),
                     new_path.length() + 1) != ERROR_SUCCESS) {
    RegCloseKey(hkey);
    logger::print_error("Failed to update PATH environment variable");
    return false;
  }

  RegCloseKey(hkey);

  // Also try to update the system PATH if admin privileges are available
  if (RegOpenKeyExA(
          HKEY_LOCAL_MACHINE,
          "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 0,
          KEY_READ | KEY_WRITE, &hkey) == ERROR_SUCCESS) {

    buf_size = sizeof(current_path);
    if (RegQueryValueExA(hkey, "PATH", NULL, &type, (BYTE *)current_path,
                         &buf_size) == ERROR_SUCCESS) {
      path_str = current_path;

      if (path_str.find(path_to_add_str) == std::string::npos) {
        // Add to system PATH too
        if (path_str.back() != ';') {
          new_path = path_str + ";" + path_to_add_str;
        } else {
          new_path = path_str + path_to_add_str;
        }

        if (RegSetValueExA(hkey, "PATH", 0, REG_EXPAND_SZ,
                           (BYTE *)new_path.c_str(),
                           new_path.length() + 1) == ERROR_SUCCESS) {
          logger::print_status(
              "Also added to system PATH (may require system restart)");
        }
      }
    }

    RegCloseKey(hkey);
  }

  // Broadcast environment change message
  SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                      (LPARAM) "Environment", SMTO_ABORTIFHUNG, 5000, NULL);

  logger::print_status("Added to PATH environment variable. Changes may "
                       "require a new terminal or system restart.");
  return true;
#else
  // On Unix-like systems, update .bashrc or similar

  // Get home directory
  const char *home_dir = getenv("HOME");
  if (!home_dir) {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
      home_dir = pw->pw_dir;
    }
  }

  if (!home_dir) {
    logger::print_warning("Could not determine home directory");
    return false;
  }

  std::filesystem::path bashrc_path =
      std::filesystem::path(home_dir) / ".bashrc";
  std::filesystem::path zshrc_path = std::filesystem::path(home_dir) / ".zshrc";

  std::filesystem::path rc_path;
  if (std::filesystem::exists(zshrc_path)) {
    rc_path = zshrc_path;
  } else if (std::filesystem::exists(bashrc_path)) {
    rc_path = bashrc_path;
  } else {
    // Create .bashrc if it doesn't exist
    rc_path = bashrc_path;
    std::ofstream file(rc_path);
    file.close();
  }

  // Check if bin_path is already in PATH
  std::string bin_path_str = bin_path.string();
  std::string line;
  bool already_in_path = false;

  {
    std::ifstream file(rc_path);
    while (std::getline(file, line)) {
      if (line.find(bin_path_str) != std::string::npos &&
          line.find("PATH") != std::string::npos) {
        already_in_path = true;
        break;
      }
    }
  }

  if (!already_in_path) {
    // Add bin_path to PATH
    std::ofstream file(rc_path, std::ios_base::app);
    file << "\n# Added by cforge installer\n";
    file << "export PATH=\"" << bin_path_str << ":$PATH\"\n";
    file.close();

    print_verbose("Added to PATH in " + rc_path.string());
  }

  return true;
#endif
}

std::unique_ptr<toml_reader>
installer::read_project_config(const std::string &project_path) const {
  std::filesystem::path config_path =
      std::filesystem::path(project_path) / CFORGE_FILE;

  if (!std::filesystem::exists(config_path)) {
    return nullptr;
  }

  auto reader = std::make_unique<toml_reader>();
  if (!reader->load(config_path.string())) {
    return nullptr;
  }

  return reader;
}

bool installer::update_env_var(const std::string &var_name,
                               const std::filesystem::path &value) const {
  std::string val = value.string();
#ifdef _WIN32
  if (!SetEnvironmentVariableA(var_name.c_str(), val.c_str())) {
    logger::print_error("Failed to set environment variable " + var_name);
    return false;
  }
#else
  if (setenv(var_name.c_str(), val.c_str(), 1) != 0) {
    logger::print_error("Failed to set environment variable " + var_name);
    return false;
  }
#endif
  logger::print_status("Set environment variable " + var_name + " = " + val);
  return true;
}

} // namespace cforge