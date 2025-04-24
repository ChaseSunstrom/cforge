/**
 * @file workspace.cpp
 * @brief Enhanced implementation of workspace management utilities
 */

#include "core/workspace.hpp"
#include "cforge/log.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>

namespace cforge {

// Utility functions - defined at the top to avoid "used before declared" errors
static std::string to_lower_case(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

static std::string get_cmake_generator() {
#ifdef _WIN32
  // Check if Ninja is available - prefer it if it is
  if (is_command_available("ninja", 15)) {
    logger::print_verbose(
        "Ninja is available, using Ninja Multi-Config generator");
    return "Ninja Multi-Config";
  }

  logger::print_verbose(
      "Ninja not found, falling back to Visual Studio generator");
  return "Visual Studio 17 2022";
#else
  return "Unix Makefiles";
#endif
}

static std::filesystem::path
get_build_dir_for_config(const std::string &base_dir,
                         const std::string &config) {
  // If config is empty, use the base directory
  if (config.empty()) {
    return base_dir;
  }

  // Transform config name to lowercase for directory naming
  std::string config_lower = config;
  std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(),
                 ::tolower);

  // For Ninja Multi-Config, we don't append the config to the build directory
  std::string generator = get_cmake_generator();
  if (generator.find("Ninja Multi-Config") != std::string::npos) {
    // Create the build directory if it doesn't exist
    std::filesystem::path build_path(base_dir);
    if (!std::filesystem::exists(build_path)) {
      std::filesystem::create_directories(build_path);
    }
    return base_dir;
  }

  // Format build directory based on configuration
  std::string build_dir = base_dir + "-" + config_lower;

  // Create the build directory if it doesn't exist
  std::filesystem::path build_path(build_dir);
  if (!std::filesystem::exists(build_path)) {
    std::filesystem::create_directories(build_path);
  }

  return build_dir;
}

static bool run_cmake_configure(const std::vector<std::string> &cmake_args,
                                const std::string &build_dir, bool verbose) {
  // Set a longer timeout for Windows
#ifdef _WIN32
  int timeout = 180; // 3 minutes for Windows
#else
  int timeout = 120; // 2 minutes for other platforms
#endif

  // Run the CMake command with appropriate timeout
  bool result = execute_tool("cmake", cmake_args, "", "CMake Configure",
                             verbose, timeout);

  // Verify that the configuration was successful by checking for CMakeCache.txt
  std::filesystem::path build_path(build_dir);
  bool cmake_success =
      result && std::filesystem::exists(build_path / "CMakeCache.txt");

  if (!cmake_success) {
    if (result) {
      logger::print_error("CMake appeared to run, but CMakeCache.txt was not "
                          "created. This may indicate a configuration error.");
    } else {
      logger::print_error("CMake configuration failed. See errors above.");
    }
    logger::print_warning(
        "You might need to clean the build directory and try again.");
    return false;
  }

  return true;
}

workspace_config::workspace_config()
    : name_("cpp-workspace"), description_("A C++ workspace") {}

workspace_config::workspace_config(const std::string &workspace_file)
    : name_("cpp-workspace"), description_("A C++ workspace") {
  load(workspace_file);
}

// Accessors for workspace_config
void workspace_config::set_name(const std::string &name) { name_ = name; }

void workspace_config::set_description(const std::string &description) {
  description_ = description;
}

const std::string &workspace_config::get_name() const { return name_; }

const std::string &workspace_config::get_description() const {
  return description_;
}

bool workspace::load(const std::filesystem::path &workspace_path) {
  workspace_path_ = workspace_path;

  // Check if the workspace configuration file exists
  std::filesystem::path config_path = workspace_path / WORKSPACE_FILE;
  if (!std::filesystem::exists(config_path)) {
    logger::print_error("Workspace configuration file not found: " +
                        config_path.string());
    return false;
  }

  // Load the configuration
  config_ = std::make_unique<toml_reader>();
  if (!config_->load(config_path.string())) {
    logger::print_error("Failed to parse workspace configuration file: " +
                        config_path.string());
    return false;
  }

  // Get workspace name
  workspace_name_ = config_->get_string("workspace.name", "");
  if (workspace_name_.empty()) {
    // Use the directory name as the workspace name if not specified
    workspace_name_ = workspace_path.filename().string();
  }

  // Load projects
  load_projects();

  // Get the default startup project
  startup_project_ =
      config_->get_string("workspace.default_startup_project", "");

  return true;
}

bool workspace::is_loaded() const { return config_ != nullptr; }

std::string workspace::get_name() const { return workspace_name_; }

std::filesystem::path workspace::get_path() const { return workspace_path_; }

std::vector<workspace_project> workspace::get_projects() const {
  return projects_;
}

workspace_project workspace::get_startup_project() const {
  // Find the startup project
  for (const auto &project : projects_) {
    if (project.is_startup) {
      return project;
    }
  }

  // If no project is marked as startup but we have a startup_project_ name,
  // find that project
  if (!startup_project_.empty()) {
    for (const auto &project : projects_) {
      if (project.name == startup_project_) {
        return project;
      }
    }
  }

  // If still no startup project is found but we have projects, return the first
  // one
  if (!projects_.empty()) {
    return projects_[0];
  }

  // Return an empty project if no startup project is set
  return workspace_project{};
}

bool workspace::set_startup_project(const std::string &project_name) {
  // Find the project by name
  bool found = false;
  for (auto &project : projects_) {
    // Update the startup flag
    if (project.name == project_name) {
      project.is_startup = true;
      found = true;
    } else {
      project.is_startup = false;
    }
  }

  if (!found) {
    logger::print_error("Project not found in workspace: " + project_name);
    return false;
  }

  // Update the startup project name
  startup_project_ = project_name;

  // Update the workspace configuration file
  std::filesystem::path config_path = workspace_path_ / WORKSPACE_FILE;
  workspace_config config;
  if (config.load(config_path.string())) {
    config.set_startup_project(project_name);
    config.save(config_path.string());
  }

  return true;
}

/**
 * @brief Find the executable file for a project
 *
 * @param project_path Path to the project directory
 * @param build_dir Build directory name
 * @param config Build configuration
 * @param project_name Project name
 * @return std::filesystem::path Path to executable, empty if not found
 */
static std::filesystem::path
find_project_executable(const std::filesystem::path &project_path,
                        const std::string &build_dir, const std::string &config,
                        const std::string &project_name) {
  logger::print_verbose("Searching for executable for project: " +
                        project_name);
  logger::print_verbose("Build directory: " + build_dir);
  logger::print_verbose("Configuration: " + config);

  // Convert config to lowercase for directory matching
  std::string config_lower = config;
  std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(),
                 ::tolower);

  // Define common executable locations to search
  std::vector<std::filesystem::path> search_paths = {
      project_path / build_dir / "bin",
      project_path / build_dir / "bin" / config,
      project_path / build_dir / "bin" / config_lower,
      project_path / build_dir / config,
      project_path / build_dir / config_lower,
      project_path / build_dir,
      project_path / "bin",
      project_path / "bin" / config,
      project_path / "bin" / config_lower};

  // Common executable name patterns to try
  std::vector<std::string> executable_patterns = {
      project_name + "_" + config_lower,
      project_name,
      project_name + "_" + config,
      project_name + "_d",       // Debug convention
      project_name + "_debug",   // Debug convention
      project_name + "_release", // Release convention
      project_name + "_r"        // Release convention
  };

#ifdef _WIN32
  // Add .exe extension for Windows
  for (auto &pattern : executable_patterns) {
    pattern += ".exe";
  }
#endif

  // Function to check if a file is a valid executable
  auto is_valid_executable = [](const std::filesystem::path &path) -> bool {
    try {
#ifdef _WIN32
      return path.extension() == ".exe";
#else
      return (std::filesystem::status(path).permissions() &
              std::filesystem::perms::owner_exec) !=
             std::filesystem::perms::none;
#endif
    } catch (const std::exception &ex) {
      logger::print_verbose("Error checking executable permissions: " +
                            std::string(ex.what()));
      return false;
    }
  };

  // Search for exact matches first
  for (const auto &search_path : search_paths) {
    if (!std::filesystem::exists(search_path)) {
      continue;
    }

    for (const auto &pattern : executable_patterns) {
      std::filesystem::path exe_path = search_path / pattern;
      if (std::filesystem::exists(exe_path) && is_valid_executable(exe_path)) {
        logger::print_verbose("Found executable: " + exe_path.string());
        return exe_path;
      }
    }
  }

  // If exact match not found, search directories for executables with similar
  // names
  for (const auto &search_path : search_paths) {
    if (!std::filesystem::exists(search_path)) {
      continue;
    }

    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(search_path)) {
        if (!is_valid_executable(entry.path())) {
          continue;
        }

        std::string filename = entry.path().filename().string();
        std::string filename_lower = filename;
        std::transform(filename_lower.begin(), filename_lower.end(),
                       filename_lower.begin(), ::tolower);

        // Skip CMake/system executables
        if (filename_lower.find("cmake") != std::string::npos ||
            filename_lower.find("ninja") != std::string::npos ||
            filename_lower.find("make") != std::string::npos ||
            filename_lower.find("a.out") != std::string::npos ||
            filename_lower.find("test") != std::string::npos) {
          continue;
        }

        // Check if filename contains project name
        std::string project_name_lower = project_name;
        std::transform(project_name_lower.begin(), project_name_lower.end(),
                       project_name_lower.begin(), ::tolower);

        if (filename_lower.find(project_name_lower) != std::string::npos) {
          logger::print_verbose("Found executable with partial match: " +
                                entry.path().string());
          return entry.path();
        }
      }
    } catch (const std::exception &ex) {
      logger::print_verbose(
          "Error scanning directory: " + search_path.string() + " - " +
          std::string(ex.what()));
    }
  }

  // Final attempt: recursive search in build directory
  logger::print_status("Performing recursive search for executable in: " +
                       (project_path / build_dir).string());
  try {
    for (auto &entry : std::filesystem::recursive_directory_iterator(
             project_path / build_dir)) {
      if (!is_valid_executable(entry.path())) {
        continue;
      }

      std::string filename = entry.path().filename().string();
      std::string filename_lower = filename;
      std::transform(filename_lower.begin(), filename_lower.end(),
                     filename_lower.begin(), ::tolower);

      // Skip CMake/system executables
      if (filename_lower.find("cmake") != std::string::npos ||
          filename_lower.find("test") != std::string::npos) {
        continue;
      }

      // Check if filename contains project name
      std::string project_name_lower = project_name;
      std::transform(project_name_lower.begin(), project_name_lower.end(),
                     project_name_lower.begin(), ::tolower);

      if (filename_lower.find(project_name_lower) != std::string::npos) {
        logger::print_verbose("Found executable in recursive search: " +
                              entry.path().string());
        return entry.path();
      }
    }
  } catch (const std::exception &ex) {
    logger::print_verbose("Error in recursive search: " +
                          std::string(ex.what()));
  }

  logger::print_error("No executable found for project: " + project_name);
  return std::filesystem::path();
}

/**
 * @brief Generate proper CMake linking options for dependent projects
 *
 * @param project Project to generate options for
 * @param projects All projects in the workspace
 * @param config Build configuration
 * @return std::vector<std::string> CMake options
 */
static std::vector<std::string>
generate_cmake_linking_options(const workspace_project &project,
                               const std::vector<workspace_project> &projects,
                               const std::string &config) {
  std::vector<std::string> options;

  // Build paths for dependencies
  for (const auto &dep_name : project.dependencies) {
    // Find the dependency project
    auto it = std::find_if(
        projects.begin(), projects.end(),
        [&dep_name](const workspace_project &p) { return p.name == dep_name; });

    if (it != projects.end()) {
      const auto &dep = *it;

      // Add include directory
      options.push_back("-DCMAKE_INCLUDE_PATH=" +
                        (dep.path / "include").string());

      // Add library directory
      options.push_back("-DCMAKE_LIBRARY_PATH=" + (dep.path / "lib").string());

      // Add as a dependency
      options.push_back("-DCFORGE_DEP_" + dep.name + "=ON");

      // Add dependency's include path
      options.push_back("-DCFORGE_" + dep.name +
                        "_INCLUDE=" + (dep.path / "include").string());

      // Add dependency's library path
      options.push_back("-DCFORGE_" + dep.name +
                        "_LIB=" + (dep.path / "lib").string());
    }
  }

  return options;
}

/**
 * @brief Generate CMakeLists.txt from project configuration
 *
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool
generate_cmakelists_from_toml(const std::filesystem::path &project_dir,
                              const toml_reader &project_config, bool verbose) {
  std::filesystem::path cmakelists_path = project_dir / "CMakeLists.txt";

  // Check if we need to generate the file
  bool file_exists = std::filesystem::exists(cmakelists_path);

  if (file_exists) {
    // If the file already exists, we don't need to generate it
    logger::print_verbose("CMakeLists.txt already exists, using existing file");
    return true;
  }

  logger::print_status(
      "Generating CMakeLists.txt from cforge.toml configuration");

  // Get project name and version from configuration
  std::string project_name =
      project_config.get_string("project.name", "cpp-project");
  std::string project_version =
      project_config.get_string("project.version", "0.1.0");
  std::string project_description = project_config.get_string(
      "project.description", "A C++ project created with cforge");
  std::string cpp_standard =
      project_config.get_string("project.cpp_standard", "17");

  // Open the file for writing
  std::ofstream cmakelists(cmakelists_path);
  if (!cmakelists) {
    logger::print_error("Failed to create CMakeLists.txt");
    return false;
  }

  cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
  cmakelists << "project(" << project_name << " VERSION " << project_version
             << " LANGUAGES CXX)\n\n";

  // C++ standard
  cmakelists << "# Set C++ standard\n";
  cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_standard << ")\n";
  cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
  cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

  // Output directories
  cmakelists << "# Set output directories\n";
  cmakelists << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
  cmakelists << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
  cmakelists
      << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n\n";

  // vcpkg integration
  cmakelists
      << "# vcpkg integration - uncomment the line below if using vcpkg\n";
  cmakelists << "# set(CMAKE_TOOLCHAIN_FILE "
                "\"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\" CACHE "
                "STRING \"\")\n\n";

  // Dependencies
  cmakelists << "# Find dependencies (if using vcpkg)\n";
  cmakelists << "# find_package(SomePackage REQUIRED)\n\n";

  // Source files
  cmakelists << "# Add source files\n";
  cmakelists << "file(GLOB_RECURSE SOURCES src/*.cpp)\n\n";

  // Define the executable name
  cmakelists << "# Define executable name with configuration suffix\n";
  cmakelists << "string(TOLOWER \"${CMAKE_BUILD_TYPE}\" build_type_lower)\n";
  cmakelists << "set(EXECUTABLE_NAME ${PROJECT_NAME}_${build_type_lower})\n\n";

  // Add executable
  cmakelists << "# Add executable\n";
  cmakelists << "add_executable(${EXECUTABLE_NAME} ${SOURCES})\n\n";

  // Include directories
  cmakelists << "# Include directories\n";
  cmakelists << "target_include_directories(${EXECUTABLE_NAME} PRIVATE\n";
  cmakelists << "    ${CMAKE_CURRENT_SOURCE_DIR}/include\n";
  cmakelists << ")\n\n";

  // Link libraries
  cmakelists << "# Link libraries\n";
  cmakelists
      << "# target_link_libraries(${EXECUTABLE_NAME} PRIVATE SomePackage)\n\n";

  // Compiler warnings
  cmakelists << "# Enable compiler warnings\n";
  cmakelists << "if(MSVC)\n";
  cmakelists << "    target_compile_options(${EXECUTABLE_NAME} PRIVATE /W4)\n";
  cmakelists << "else()\n";
  cmakelists << "    target_compile_options(${EXECUTABLE_NAME} PRIVATE -Wall "
                "-Wextra -Wpedantic)\n";
  cmakelists << "endif()\n\n";

  // Tests - check if tests are explicitly enabled in the configuration
  bool tests_enabled = project_config.get_bool("test.enabled", false);

  // Only add test-related configuration if tests are explicitly enabled
  if (tests_enabled) {
    cmakelists << "# Testing configuration\n";
    cmakelists << "enable_testing()\n";
    cmakelists << "option(BUILD_TESTING \"Build the testing tree.\" ON)\n\n";

    cmakelists << "if(BUILD_TESTING)\n";
    cmakelists << "    # Add test directory\n";
    cmakelists << "    add_subdirectory(tests)\n";
    cmakelists << "endif()\n\n";
  } else {
    cmakelists << "# Testing is disabled by default\n";
    cmakelists << "# To enable testing, set test.enabled=true in cforge.toml "
                  "and create a tests directory\n\n";
  }

  // Installation
  cmakelists << "# Installation configuration\n";
  cmakelists << "install(TARGETS ${EXECUTABLE_NAME}\n";
  cmakelists << "    RUNTIME DESTINATION bin\n";
  cmakelists << "    LIBRARY DESTINATION lib\n";
  cmakelists << "    ARCHIVE DESTINATION lib\n";
  cmakelists << ")\n\n";

  cmakelists << "# Install headers\n";
  cmakelists << "install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/\n";
  cmakelists << "    DESTINATION include\n";
  cmakelists << "    FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
  cmakelists << ")\n\n";

  // CPack
  cmakelists << "# Packaging configuration with CPack\n";
  cmakelists << "include(CPack)\n";
  cmakelists << "set(CPACK_PACKAGE_NAME ${PROJECT_NAME})\n";
  cmakelists << "set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})\n";
  cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_NAME} - "
             << project_description << "\")\n";
  cmakelists << "set(CPACK_PACKAGE_VENDOR \"Your Organization\")\n";
  cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_FILE "
                "\"${CMAKE_CURRENT_SOURCE_DIR}/README.md\")\n";
  cmakelists << "set(CPACK_RESOURCE_FILE_LICENSE "
                "\"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE\")\n\n";

  // OS specific packaging settings
  cmakelists << "# OS specific packaging settings\n";
  cmakelists << "if(WIN32)\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;NSIS\")\n";
  cmakelists << "    set(CPACK_NSIS_PACKAGE_NAME \"${PROJECT_NAME}\")\n";
  cmakelists << "    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)\n";
  cmakelists << "elseif(APPLE)\n";
  cmakelists << "    set(CPACK_GENERATOR \"TGZ;DragNDrop\")\n";
  cmakelists << "else()\n";
  cmakelists << "    set(CPACK_GENERATOR \"TGZ;DEB\")\n";
  cmakelists << "    set(CPACK_DEBIAN_PACKAGE_MAINTAINER \"Your Name\")\n";
  cmakelists << "    set(CPACK_DEBIAN_PACKAGE_SECTION \"devel\")\n";
  cmakelists << "endif()\n";

  cmakelists.close();

  if (verbose) {
    logger::print_verbose(
        "Generated CMakeLists.txt with the following configuration:");
    logger::print_verbose("- Project name: " + project_name);
    logger::print_verbose("- C++ standard: " + cpp_standard);
    logger::print_verbose("- Tests enabled: " +
                          std::string(tests_enabled ? "yes" : "no"));
  }

  logger::print_success("Generated CMakeLists.txt file");
  return true;
}

bool workspace::build_all(const std::string &config, int num_jobs,
                          bool verbose) const {
  if (projects_.empty()) {
    logger::print_warning("No projects in workspace");
    return false;
  }

  // Get build order respecting dependencies
  std::vector<std::string> build_order;
  std::set<std::string> visited;

  // Topological sort for dependency-aware build order
  std::function<void(const std::string &)> visit =
      [&](const std::string &project_name) {
        if (visited.find(project_name) != visited.end()) {
          return;
        }

        visited.insert(project_name);

        // Find project dependencies
        for (const auto &project : projects_) {
          if (project.name == project_name) {
            for (const auto &dep : project.dependencies) {
              visit(dep);
            }
            break;
          }
        }

        build_order.push_back(project_name);
      };

  // Visit all projects
  for (const auto &project : projects_) {
    visit(project.name);
  }

  logger::print_status("Building " + std::to_string(build_order.size()) +
                       " projects in workspace: " + workspace_name_);

  if (verbose) {
    logger::print_status("Build order:");
    for (size_t i = 0; i < build_order.size(); ++i) {
      logger::print_status("  " + std::to_string(i + 1) + ". " +
                           build_order[i]);
    }
  }

  bool all_success = true;

  // Build each project in order
  for (const auto &project_name : build_order) {
    // Find project in workspace
    auto it = std::find_if(projects_.begin(), projects_.end(),
                           [&project_name](const workspace_project &p) {
                             return p.name == project_name;
                           });

    if (it == projects_.end()) {
      logger::print_error("Project not found in workspace: " + project_name);
      all_success = false;
      continue;
    }

    const auto &project = *it;
    logger::print_status("Building project: " + project.name);

    // Create build directory if it doesn't exist
    std::filesystem::path build_dir = project.path / "build";
    if (!std::filesystem::exists(build_dir)) {
      try {
        std::filesystem::create_directories(build_dir);
      } catch (const std::exception &ex) {
        logger::print_error("Failed to create build directory: " +
                            std::string(ex.what()));
        all_success = false;
        continue;
      }
    }

    // Check if the project has a CMakeLists.txt file or needs to be generated
    std::filesystem::path cmake_path = project.path / "CMakeLists.txt";
    std::filesystem::path config_path = project.path / CFORGE_FILE;

    if (!std::filesystem::exists(cmake_path)) {
      if (std::filesystem::exists(config_path)) {
        // Try to generate CMakeLists.txt from cforge.toml
        toml_reader project_config;
        if (project_config.load(config_path.string())) {
          if (!generate_cmakelists_from_toml(project.path, project_config,
                                             verbose)) {
            logger::print_error(
                "Failed to generate CMakeLists.txt for project: " +
                project.name);
            all_success = false;
            continue;
          }
        } else {
          logger::print_error("Failed to load cforge.toml for project: " +
                              project.name);
          all_success = false;
          continue;
        }
      } else {
        logger::print_error("No cforge.toml found for project: " +
                            project.name);
        all_success = false;
        continue;
      }
    }

    // Generate CMake options with dependency linking
    std::vector<std::string> cmake_args = {"-S", project.path.string(), "-B",
                                           build_dir.string(),
                                           "-DCMAKE_BUILD_TYPE=" + config};

    // Add dependency linking options
    std::vector<std::string> link_options =
        generate_cmake_linking_options(project, projects_, config);
    cmake_args.insert(cmake_args.end(), link_options.begin(),
                      link_options.end());

    // Set jobs if specified
    if (num_jobs > 0) {
      cmake_args.push_back("-DCMAKE_BUILD_PARALLEL_LEVEL=" +
                           std::to_string(num_jobs));
    }

    // Run CMake configure
    logger::print_status("Configuring project with CMake...");
    bool configure_success =
        execute_tool("cmake", cmake_args, "", "CMake Configure", verbose);

    if (!configure_success) {
      logger::print_error("Failed to configure project: " + project.name);
      all_success = false;
      continue;
    }

    // Build the project
    std::vector<std::string> build_args = {"--build", build_dir.string(),
                                           "--config", config};

    // Set parallel jobs for build
    if (num_jobs > 0) {
      build_args.push_back("--parallel");
      build_args.push_back(std::to_string(num_jobs));
    }

    logger::print_status("Building project: " + project.name);
    bool build_success =
        execute_tool("cmake", build_args, "", "CMake Build", verbose);

    if (!build_success) {
      logger::print_error("Failed to build project: " + project.name);
      all_success = false;
      continue;
    }

    logger::print_success("Successfully built project: " + project.name);
  }

  if (all_success) {
    logger::print_success("All projects built successfully");
  } else {
    logger::print_warning("Some projects failed to build");
  }

  return all_success;
}

bool workspace::build_project(const std::string &project_name,
                              const std::string &config, int num_jobs,
                              bool verbose, const std::string &target) const {
  // Find the project
  const workspace_project *project = get_project_by_name(project_name);
  if (!project) {
    logger::print_error("Project '" + project_name +
                        "' not found in workspace");
    return false;
  }

  // Save current directory
  std::filesystem::path current_dir = std::filesystem::current_path();

  try {
    // Change to project directory
    std::filesystem::current_path(project->path);
    logger::print_status("Building project '" + project->name + "'...");

    // Load project configuration
    toml_reader project_config;
    std::filesystem::path config_path = project->path / CFORGE_FILE;
    if (!std::filesystem::exists(config_path)) {
      logger::print_error("Project '" + project->name + "' is missing " +
                          std::string(CFORGE_FILE));
      return false;
    }

    if (!project_config.load(config_path.string())) {
      logger::print_error("Failed to load project configuration for '" +
                          project->name + "'");
      return false;
    }

    // Determine build directory
    std::string base_build_dir;
    if (project_config.has_key("build.directory")) {
      base_build_dir = project_config.get_string("build.directory");
    } else if (project_config.has_key("build.build_dir")) {
      // Legacy key support
      base_build_dir = project_config.get_string("build.build_dir");
    } else {
      base_build_dir = "build";
    }

    // Get the config-specific build directory
    std::filesystem::path build_dir =
        get_build_dir_for_config(base_build_dir, config);

    // If build dir doesn't exist, create it
    if (!std::filesystem::exists(build_dir)) {
      logger::print_status("Creating build directory: " + build_dir.string());
      std::filesystem::create_directories(build_dir);
    }

    // Always regenerate CMakeLists.txt to ensure it matches the current
    // cforge.toml
    std::filesystem::path cmakelists_path = project->path / "CMakeLists.txt";
    logger::print_status("Generating CMakeLists.txt for project '" +
                         project->name + "'...");

    // Remove existing CMakeLists.txt if it exists
    if (std::filesystem::exists(cmakelists_path)) {
      logger::print_verbose("Removing existing CMakeLists.txt");
      std::filesystem::remove(cmakelists_path);
    }

    // Generate new CMakeLists.txt
    if (!generate_cmakelists_from_toml(project->path, project_config,
                                       verbose)) {
      logger::print_error("Failed to generate CMakeLists.txt for project '" +
                          project->name + "'");
      return false;
    }

    logger::print_success("Generated CMakeLists.txt for project '" +
                          project->name + "'");

    // Run CMake configure
    std::vector<std::string> cmake_args;
    cmake_args.push_back("-S");
    cmake_args.push_back(".");
    cmake_args.push_back("-B");
    cmake_args.push_back(build_dir.string());
    cmake_args.push_back("-G");
    cmake_args.push_back(get_cmake_generator());

    // Add build type for non-multi-config generators
    if (get_cmake_generator().find("Multi-Config") == std::string::npos) {
      cmake_args.push_back("-DCMAKE_BUILD_TYPE=" + config);
    }

    // Add extra arguments from cforge.toml if available
    std::string args_key =
        "build.config." + to_lower_case(config) + ".cmake_args";
    if (project_config.has_key(args_key)) {
      std::vector<std::string> extra_args =
          project_config.get_string_array(args_key);
      for (const auto &arg : extra_args) {
        cmake_args.push_back(arg);
      }
    }

    // Run cmake configure
    if (!run_cmake_configure(cmake_args, build_dir.string(), verbose)) {
      logger::print_error("CMake configure failed for project '" +
                          project->name + "'");
      return false;
    }

    // Run build
    std::vector<std::string> build_args;
    build_args.push_back("--build");
    build_args.push_back(build_dir.string());

    // Add config for multi-config generators
    if (get_cmake_generator().find("Multi-Config") != std::string::npos) {
      build_args.push_back("--config");
      build_args.push_back(config);
    }

    // Add parallel jobs
    if (num_jobs > 0) {
      build_args.push_back("--parallel");
      build_args.push_back(std::to_string(num_jobs));
    }

    // Add target if specified
    if (!target.empty()) {
      build_args.push_back("--target");
      build_args.push_back(target);
    }

    // Add verbose flag if needed
    if (verbose) {
      build_args.push_back("--verbose");
    }

    // Run build
    if (!execute_tool("cmake", build_args, "", "Build " + project->name,
                      verbose)) {
      logger::print_error("Build failed for project '" + project->name + "'");
      return false;
    }

    logger::print_success("Project '" + project->name + "' built successfully");

    // Restore current directory
    std::filesystem::current_path(current_dir);
    return true;
  } catch (const std::exception &ex) {
    logger::print_error("Error building project '" + project_name +
                        "': " + std::string(ex.what()));
    // Restore current directory
    std::filesystem::current_path(current_dir);
    return false;
  }
}

bool workspace::run_startup_project(const std::vector<std::string> &args,
                                    const std::string &config,
                                    bool verbose) const {
  // Get the startup project
  workspace_project startup = get_startup_project();

  if (startup.name.empty()) {
    logger::print_error("No startup project set in workspace");
    return false;
  }

  // Run the startup project
  return run_project(startup.name, args, config, verbose);
}

bool workspace::run_project(const std::string &project_name,
                            const std::vector<std::string> &args,
                            const std::string &config, bool verbose) const {
  // Find the project by name
  auto it = std::find_if(projects_.begin(), projects_.end(),
                         [&project_name](const workspace_project &p) {
                           return p.name == project_name;
                         });

  if (it == projects_.end()) {
    logger::print_error("Project not found in workspace: " + project_name);
    return false;
  }

  const auto &project = *it;
  logger::print_status("Running project: " + project.name);

  // Make sure the project is built
  if (!build_project(project.name, config, 0, verbose)) {
    logger::print_error("Failed to build project: " + project.name);
    return false;
  }

  // Find the executable
  std::filesystem::path executable =
      find_project_executable(project.path, "build", config, project.name);

  if (executable.empty()) {
    logger::print_error("Executable not found for project: " + project.name);
    return false;
  }

  logger::print_status("Running executable: " + executable.string());

  // Display program output header
  logger::print_status("Program Output\n────────────");

  // Create custom callbacks to display raw program output
  std::function<void(const std::string &)> stdout_callback =
      [](const std::string &chunk) { std::cout << chunk << std::flush; };

  std::function<void(const std::string &)> stderr_callback =
      [](const std::string &chunk) { std::cerr << chunk << std::flush; };

  // Execute the program with custom output handling
  process_result result =
      execute_process(executable.string(), args, project.path.string(),
                      stdout_callback, stderr_callback,
                      0 // No timeout
      );

  // Add a blank line after program output
  std::cout << std::endl;

  if (!result.success) {
    logger::print_error("Project execution failed: " + project.name);
    return false;
  }

  logger::print_success("Project executed successfully: " + project.name);
  return true;
}

bool workspace::is_workspace_dir(const std::filesystem::path &dir) {
  // Check if the workspace configuration file exists
  return std::filesystem::exists(dir / WORKSPACE_FILE);
}

bool workspace::create_workspace(const std::filesystem::path &workspace_path,
                                 const std::string &workspace_name) {
  // Create the workspace directory if it doesn't exist
  if (!std::filesystem::exists(workspace_path)) {
    try {
      std::filesystem::create_directories(workspace_path);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create workspace directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // Create the workspace configuration file
  std::filesystem::path config_path = workspace_path / WORKSPACE_FILE;

  // Don't overwrite existing configuration
  if (std::filesystem::exists(config_path)) {
    logger::print_warning("Workspace configuration file already exists: " +
                          config_path.string());
    return true;
  }

  // Create the configuration file
  std::ofstream config_file(config_path);
  if (!config_file) {
    logger::print_error("Failed to create workspace configuration file: " +
                        config_path.string());
    return false;
  }

  // Write the configuration
  config_file << "# Workspace configuration for cforge\n\n";
  config_file << "[workspace]\n";
  config_file << "name = \"" << workspace_name << "\"\n";
  config_file << "description = \"A C++ workspace created with cforge\"\n";
  config_file << "projects = []\n";
  config_file << "# default_startup_project = \"main_project\"\n";

  config_file.close();

  // Create standard directories
  try {
    std::filesystem::create_directories(workspace_path / "projects");
  } catch (const std::exception &ex) {
    logger::print_warning("Failed to create projects directory: " +
                          std::string(ex.what()));
  }

  logger::print_success("Workspace created successfully: " + workspace_name);
  return true;
}

void workspace::load_projects() {
  projects_.clear();

  // Get the list of projects from the TOML config
  std::vector<std::string> project_strings =
      config_->get_string_array("workspace.projects");

  // Parse each project string and add to the projects list
  workspace_config workspace_cfg;
  std::filesystem::path config_path = workspace_path_ / WORKSPACE_FILE;
  if (!workspace_cfg.load(config_path.string())) {
    logger::print_error("Failed to load workspace configuration file");
    return;
  }

  // Use the parsed projects from workspace_config
  projects_ = workspace_cfg.get_projects();

  // Process each project path - make sure relative paths are resolved correctly
  for (auto &project : projects_) {
    // If the path is relative, make it relative to the workspace path
    if (!project.path.is_absolute()) {
      project.path = workspace_path_ / project.path;
    }

    // Check if project directory exists
    if (!std::filesystem::exists(project.path)) {
      logger::print_warning("Project directory does not exist: " +
                            project.path.string());
      continue;
    }

    // Check if it's a valid cforge project
    if (!std::filesystem::exists(project.path / CFORGE_FILE)) {
      logger::print_warning("Not a valid cforge project (missing " +
                            std::string(CFORGE_FILE) +
                            "): " + project.path.string());
      continue;
    }

    // Update startup flag if this is the startup project
    project.is_startup = (project.name == startup_project_);

    // Try to read the project name from cforge.toml to validate
    toml_reader project_config;
    std::filesystem::path project_config_path = project.path / CFORGE_FILE;
    std::string config_project_name;

    if (project_config.load(project_config_path.string())) {
      config_project_name = project_config.get_string("project.name", "");

      // Validate the project name matches the config
      if (!config_project_name.empty() && config_project_name != project.name) {
        logger::print_warning("Project name mismatch: '" + project.name +
                              "' in workspace vs '" + config_project_name +
                              "' in project config");
      }

      // Try to find dependencies for this project
      if (project_config.has_key("dependencies")) {
        std::vector<std::string> deps =
            project_config.get_table_keys("dependencies");
        for (const auto &dep : deps) {
          // Check if this dependency is another project in the workspace
          for (const auto &other_project : projects_) {
            if (other_project.name == dep) {
              // Add as dependency if it's not already there
              if (std::find(project.dependencies.begin(),
                            project.dependencies.end(),
                            dep) == project.dependencies.end()) {
                project.dependencies.push_back(dep);
                logger::print_verbose("Added dependency: " + project.name +
                                      " -> " + dep);
              }
              break;
            }
          }
        }
      }
    }
  }
}

bool workspace_config::load(const std::string &workspace_file) {
  toml_reader reader;
  if (!reader.load(workspace_file)) {
    logger::print_error("Failed to load workspace configuration file");
    return false;
  }

  // Load basic workspace info
  name_ = reader.get_string("workspace.name", "cpp-workspace");
  description_ = reader.get_string("workspace.description", "A C++ workspace");

  // Load projects
  std::vector<std::string> project_paths =
      reader.get_string_array("workspace.projects");
  if (!project_paths.empty()) {
    for (const auto &project_path : project_paths) {
      workspace_project project;

      // Parse project data from the path string - format:
      // "name:path:is_startup_project"
      std::vector<std::string> parts;
      std::string::size_type start = 0;
      std::string::size_type end = 0;

      // Split by colons, but handle Windows drive letters (e.g., C:\)
      while (start < project_path.length()) {
        end = project_path.find(':', start);
        if (end == std::string::npos) {
          // Last part
          parts.push_back(project_path.substr(start));
          break;
        }

        // Check if this colon is part of a Windows drive letter
        if (end == 1 && project_path.length() > 2 &&
            project_path[end + 1] == '\\') {
          // This is a Windows drive letter, find the next colon
          start = end + 1;
          continue;
        }

        parts.push_back(project_path.substr(start, end - start));
        start = end + 1;
      }

      if (parts.size() >= 1) {
        project.name = parts[0];
      }

      if (parts.size() >= 2) {
        project.path = std::filesystem::path(parts[1]);
      } else if (!project.name.empty()) {
        // If path is not specified, use the name as the path
        project.path = project.name;
      }

      if (parts.size() >= 3) {
        project.is_startup_project = (parts[2] == "true");
      }

      // Add the project to the list
      projects_.push_back(project);
    }
  }

  // Check for default startup project
  std::string default_startup =
      reader.get_string("workspace.default_startup_project", "");
  if (!default_startup.empty()) {
    set_startup_project(default_startup);
  }

  return true;
}

bool workspace_config::save(const std::string &workspace_file) const {
  std::ofstream file(workspace_file);
  if (!file) {
    logger::print_error("Failed to create workspace configuration file");
    return false;
  }

  // Get the workspace directory
  std::filesystem::path workspace_dir =
      std::filesystem::path(workspace_file).parent_path();

  // Write workspace info
  file << "[workspace]\n";
  file << "name = \"" << name_ << "\"\n";
  file << "description = \"" << description_ << "\"\n\n";

  // Find startup project
  std::string startup_project;
  for (const auto &project : projects_) {
    if (project.is_startup_project) {
      startup_project = project.name;
      break;
    }
  }

  // Write startup project if found
  if (!startup_project.empty()) {
    file << "default_startup_project = \"" << startup_project << "\"\n\n";
  }

  // Write projects as a string array
  file << "# Projects in format: name:path:is_startup_project\n";
  file << "projects = [\n";

  for (size_t i = 0; i < projects_.size(); ++i) {
    const auto &project = projects_[i];

    // Create a relative path if possible
    std::filesystem::path path_to_save;
    if (project.path.is_absolute()) {
      try {
        // Try to make the path relative to workspace directory
        path_to_save = std::filesystem::relative(project.path, workspace_dir);
        logger::print_verbose("Converted absolute path to relative: " +
                              path_to_save.string());
      } catch (...) {
        // If we can't create a relative path, use the absolute one
        path_to_save = project.path;
        logger::print_verbose("Using absolute path: " + path_to_save.string());
      }
    } else {
      // Already a relative path
      path_to_save = project.path;
    }

    file << "  \"" << project.name << ":" << path_to_save.string() << ":"
         << (project.is_startup_project ? "true" : "false") << "\"";

    if (i < projects_.size() - 1) {
      file << ",";
    }
    file << "\n";
  }

  file << "]\n\n";

  // Write additional information as comments
  file << "# Dependencies between projects are determined automatically\n";
  file << "# based on the dependencies section in each project's cforge.toml "
          "file\n";

  return true;
}

const workspace_project *workspace_config::get_startup_project() const {
  for (const auto &project : projects_) {
    if (project.is_startup_project) {
      return &project;
    }
  }
  return nullptr;
}

bool workspace_config::has_project(const std::string &name) const {
  for (const auto &project : projects_) {
    if (project.name == name) {
      return true;
    }
  }
  return false;
}

bool workspace_config::add_project_dependency(const std::string &project_name,
                                              const std::string &dependency) {
  // Find the project
  for (auto &project : projects_) {
    if (project.name == project_name) {
      // Check if dependency exists
      if (!has_project(dependency)) {
        logger::print_error("Dependency project '" + dependency +
                            "' does not exist in workspace");
        return false;
      }

      // Check for circular dependencies
      std::set<std::string> visited;
      std::queue<std::string> to_visit;
      to_visit.push(dependency);

      while (!to_visit.empty()) {
        std::string current = to_visit.front();
        to_visit.pop();

        if (current == project_name) {
          logger::print_error("Circular dependency detected: " + project_name +
                              " -> " + dependency);
          return false;
        }

        if (visited.insert(current).second) {
          // Add dependencies of current project to visit
          for (const auto &proj : projects_) {
            if (proj.name == current) {
              for (const auto &dep : proj.dependencies) {
                to_visit.push(dep);
              }
              break;
            }
          }
        }
      }

      // Check if the dependency already exists
      if (std::find(project.dependencies.begin(), project.dependencies.end(),
                    dependency) != project.dependencies.end()) {
        logger::print_status("Dependency already exists: " + project_name +
                             " -> " + dependency);
        return true;
      }

      // Add dependency
      project.dependencies.push_back(dependency);
      logger::print_status("Added dependency: " + project_name + " -> " +
                           dependency);
      return true;
    }
  }

  logger::print_error("Project '" + project_name + "' not found in workspace");
  return false;
}

bool workspace_config::set_startup_project(const std::string &project_name) {
  bool found = false;

  // Clear existing startup project and set new one
  for (auto &project : projects_) {
    if (project.name == project_name) {
      project.is_startup_project = true;
      found = true;
    } else {
      project.is_startup_project = false;
    }
  }

  if (!found) {
    logger::print_error("Project '" + project_name +
                        "' not found in workspace");
    return false;
  }

  return true;
}

std::vector<std::string> workspace_config::get_build_order() const {
  std::vector<std::string> build_order;
  std::set<std::string> visited;

  // Define a recursive lambda function for topological sort
  std::function<void(const std::string &)> visit =
      [&](const std::string &project_name) {
        if (visited.find(project_name) != visited.end()) {
          return;
        }

        visited.insert(project_name);

        // Find project dependencies
        for (const auto &project : projects_) {
          if (project.name == project_name) {
            for (const auto &dep : project.dependencies) {
              visit(dep);
            }
            break;
          }
        }

        build_order.push_back(project_name);
      };

  // Visit all projects
  for (const auto &project : projects_) {
    visit(project.name);
  }

  return build_order;
}

const std::vector<workspace_project> &workspace_config::get_projects() const {
  return projects_;
}

std::vector<workspace_project> &workspace_config::get_projects() {
  return projects_;
}

} // namespace cforge