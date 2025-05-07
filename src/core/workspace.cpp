/**
 * @file workspace.cpp
 * @brief Enhanced implementation of workspace management utilities
 */

#include "core/workspace.hpp"
#include "cforge/log.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/dependency_hash.hpp"
#include "core/toml_reader.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>

namespace cforge {

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
  startup_project_ = config_->get_string("workspace.main_project", "");

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

std::pair<bool, std::filesystem::path>
is_in_workspace(const std::filesystem::path &path) {
  // Check if the current directory has a workspace.toml file
  std::filesystem::path workspace_file = path / WORKSPACE_FILE;
  if (std::filesystem::exists(workspace_file)) {
    return {true, path};
  }

  // Check parent directories
  std::filesystem::path current = path;
  while (current.has_parent_path() && current != current.parent_path()) {
    current = current.parent_path();
    workspace_file = current / WORKSPACE_FILE;
    if (std::filesystem::exists(workspace_file)) {
      return {true, current};
    }
  }

  // Not in a workspace
  return {false, {}};
}

void
configure_git_dependencies_in_cmake(const toml_reader &project_config,
                                    const std::string &deps_dir,
                                    std::ofstream &cmakelists)
{
  // Check if we have Git dependencies
  if (!project_config.has_key("dependencies.git")) {
    return;
  }

  cmakelists << "# Git dependencies\n";
  cmakelists << "include(FetchContent)\n";

  // Make sure the dependencies directory exists
  cmakelists << "# Ensure dependencies directory exists\n";
  cmakelists << "set(DEPS_DIR \"${CMAKE_CURRENT_SOURCE_DIR}/" << deps_dir
             << "\")\n";
  cmakelists << "file(MAKE_DIRECTORY ${DEPS_DIR})\n\n";

  // Configure Git to prefer HTTPS but allow other protocols
  cmakelists << "# Configure Git to prefer HTTPS but allow other protocols\n";
  cmakelists << "set(FETCHCONTENT_GIT_PROTOCOL \"https\")\n\n";

  // Loop through all git dependencies
  auto git_deps = project_config.get_table_keys("dependencies.git");
  for (const auto &dep : git_deps) {
    std::string url =
        project_config.get_string("dependencies.git." + dep + ".url", "");
    if (url.empty()) {
      continue;
    }

    // Get reference (tag, branch, or commit)
    std::string tag =
        project_config.get_string("dependencies.git." + dep + ".tag", "");
    std::string branch =
        project_config.get_string("dependencies.git." + dep + ".branch", "");
    std::string commit =
        project_config.get_string("dependencies.git." + dep + ".commit", "");

    // Get custom directory if specified
    std::string custom_dir = project_config.get_string("dependencies.git." + dep + ".directory", "");
    std::string dep_dir = custom_dir.empty() ? deps_dir : custom_dir;

    // Get dependency options
    bool make_available = project_config.get_bool(
        "dependencies.git." + dep + ".make_available", true);
    bool include =
        project_config.get_bool("dependencies.git." + dep + ".include", true);
    bool link =
        project_config.get_bool("dependencies.git." + dep + ".link", true);
    std::string target_name = project_config.get_string(
        "dependencies.git." + dep + ".target_name", "");

    cmakelists << "# " << dep << " dependency\n";
    cmakelists << "message(STATUS \"Setting up " << dep << " dependency from "
               << url << "\")\n";

    // FetchContent declaration
    cmakelists << "FetchContent_Declare(" << dep << "\n";
    cmakelists << "    GIT_REPOSITORY " << url << "\n";
    if (!tag.empty()) {
        cmakelists << "    GIT_TAG " << tag << "\n";
    } else if (!branch.empty()) {
        cmakelists << "    GIT_TAG " << branch << "\n";
    } else if (!commit.empty()) {
        cmakelists << "    GIT_TAG " << commit << "\n";
    }

    // Use custom directory if specified
    cmakelists << "    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/" << dep_dir << "/" << dep << "\n";

    // Add shallow clone option if configured
    bool shallow = project_config.get_bool("dependencies.git." + dep + ".shallow", false);
    if (shallow) {
        cmakelists << "    GIT_SHALLOW 1\n";
    }

    cmakelists << ")\n";

    // Process include directories
    if (include) {
      cmakelists << "# Include directories for " << dep << "\n";

      std::vector<std::string> include_dirs;
      std::string include_dirs_key =
          "dependencies.git." + dep + ".include_dirs";

      if (project_config.has_key(include_dirs_key)) {
        include_dirs = project_config.get_string_array(include_dirs_key);
      } else {
        // Default include directories
        include_dirs.push_back("include");
        include_dirs.push_back(".");
      }

      for (const auto &inc_dir : include_dirs) {
        cmakelists << "include_directories(${CMAKE_CURRENT_SOURCE_DIR}/" << dep_dir << "/" << dep << "/"
                   << inc_dir << ")\n";
      }
      cmakelists << "\n";
    }

    // Special handling for common libraries
    if (dep == "fmt") {
      // For fmt, configure options
      cmakelists << "# For fmt, configure options\n";
      cmakelists << "set(FMT_TEST OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(FMT_DOC OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(FMT_SYSTEM_HEADERS ON CACHE BOOL \"\" FORCE)\n";
      if (make_available) {
        cmakelists << "FetchContent_MakeAvailable(" << dep << ")\n\n";
      } else {
        cmakelists << "FetchContent_GetProperties(" << dep << ")\n";
        cmakelists << "if(NOT " << dep << "_POPULATED)\n";
        cmakelists << "    FetchContent_Populate(" << dep << ")\n";
        cmakelists << "endif()\n\n";
      }
    } else if (dep == "spdlog") {
      // For spdlog, similar configuration
      cmakelists << "# For spdlog, configure options\n";
      cmakelists << "set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(SPDLOG_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)\n";
      if (make_available) {
        cmakelists << "FetchContent_MakeAvailable(" << dep << ")\n\n";
      } else {
        cmakelists << "FetchContent_GetProperties(" << dep << ")\n";
        cmakelists << "if(NOT " << dep << "_POPULATED)\n";
        cmakelists << "    FetchContent_Populate(" << dep << ")\n";
        cmakelists << "endif()\n\n";
      }
    } else {
      // General case for other dependencies
      if (make_available) {
        cmakelists << "FetchContent_MakeAvailable(" << dep << ")\n\n";
      } else {
        cmakelists << "FetchContent_GetProperties(" << dep << ")\n";
        cmakelists << "if(NOT " << dep << "_POPULATED)\n";
        cmakelists << "    FetchContent_Populate(" << dep << ")\n";
        cmakelists << "endif()\n\n";
      }
    }
  }
}


/**
 * @brief Generate a CMakeLists.txt file from cforge.toml configuration
 *
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
bool generate_cmakelists_from_toml(const std::filesystem::path &project_dir,
                              const toml_reader &project_config, bool verbose) {
    // Load dependency hashes
    dependency_hash dep_hashes;
    dep_hashes.load(project_dir);

    // Calculate current cforge.toml hash
    std::filesystem::path toml_path = project_dir / "cforge.toml";
    if (!std::filesystem::exists(toml_path)) {
        logger::print_error("cforge.toml not found at: " + toml_path.string());
        return false;
    }

    // Read file content to verify it's not empty
    std::string toml_content;
    {
        std::ifstream toml_in(toml_path);
        if (!toml_in) {
            logger::print_error("Failed to open cforge.toml");
            return false;
        }
        std::ostringstream oss;
        oss << toml_in.rdbuf();
        toml_content = oss.str();
        if (toml_content.empty()) {
            logger::print_error("cforge.toml is empty");
            return false;
        }
    }

    std::string toml_hash = dep_hashes.calculate_file_content_hash(toml_content);
    // Path to CMakeLists.txt in project directory
    std::filesystem::path cmakelists_path = project_dir / "CMakeLists.txt";
    bool file_exists = std::filesystem::exists(cmakelists_path);
    std::string stored_toml_hash = dep_hashes.get_hash("cforge.toml");

    // If CMakeLists.txt exists and TOML hash is unchanged, skip generation
    if (file_exists && !stored_toml_hash.empty() && toml_hash == stored_toml_hash) {
        if (verbose) {
            logger::print_verbose("CMakeLists.txt already exists and up to date, skipping generation");
        }
        return true;
    }

    // If CMakeLists.txt exists but external deps present, regenerate
    if (file_exists) {
        if (!project_config.has_key("dependencies.git") && !project_config.has_key("dependencies.vcpkg")) {
            if (verbose) {
                logger::print_verbose("CMakeLists.txt exists and no external deps, using existing file");
            }
            return true;
        }
        logger::print_status("Dependencies detected, regenerating CMakeLists.txt from cforge.toml");
    }
    
    logger::print_status("Generating CMakeLists.txt from cforge.toml...");
    
    // Check if we're in a workspace
    auto [is_workspace, workspace_dir] = is_in_workspace(project_dir);
    
    // Create CMakeLists.txt in the project directory
    std::ofstream cmakelists(cmakelists_path);
    if (!cmakelists.is_open()) {
      logger::print_error("Failed to create CMakeLists.txt in project directory");
      return false;
    }

    // Get the right build directory for the configuration
    std::string build_config =
        project_config.get_string("build.build_type", "Debug");
    std::filesystem::path build_base_dir = project_dir / "build";
    std::filesystem::path build_dir =
        get_build_dir_for_config(build_base_dir.string(), build_config);

    // Create build directory if it doesn't exist
    if (!std::filesystem::exists(build_dir)) {
      logger::print_verbose("Creating build directory: " + build_dir.string());
      try {
        std::filesystem::create_directories(build_dir);
      } catch (const std::filesystem::filesystem_error &e) {
        logger::print_error("Failed to create build directory: " +
                            std::string(e.what()));
        return false;
      }
    }

    // Get project metadata
    std::string project_name =
        project_config.get_string("project.name", "cpp-project");
    std::string project_version =
        project_config.get_string("project.version", "0.1.0");
    std::string project_description = project_config.get_string(
        "project.description", "A C++ project created with cforge");

    // Write initial CMake configuration
    cmakelists << "# CMakeLists.txt for " << project_name << " v"
               << project_version << "\n";
    cmakelists << "# Generated by cforge - C++ project management tool\n\n";

    cmakelists << "cmake_minimum_required(VERSION 3.15)\n\n";
    cmakelists << "# Project configuration\n";
    cmakelists << "project(" << project_name << " VERSION " << project_version
               << " LANGUAGES CXX)\n\n";

    // Use CMAKE_CURRENT_SOURCE_DIR for project source directory
    cmakelists << "# Set source directory\n";
    cmakelists << "set(SOURCE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}\")\n\n";

    // Get author information
    std::vector<std::string> authors =
        project_config.get_string_array("project.authors");
    std::string author_string;
    if (!authors.empty()) {
      for (size_t i = 0; i < authors.size(); ++i) {
        if (i > 0)
          author_string += ", ";
        author_string += authors[i];
      }
    } else {
      author_string = "CForge User";
    }

    // Project description
    cmakelists << "# Project description\n";
    cmakelists << "set(PROJECT_DESCRIPTION \"" << project_description << "\")\n";
    cmakelists << "set(PROJECT_AUTHOR \"" << author_string << "\")\n\n";

    // Get C++ standard
    std::string cpp_standard =
        project_config.get_string("project.cpp_standard", "17");

    // Set C++ standard
    cmakelists << "# Set C++ standard\n";
    cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_standard << ")\n";
    cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

    // Get binary type (executable, shared_lib, static_lib, or header_only)
    std::string binary_type =
        project_config.get_string("project.binary_type", "executable");

    // Get build settings
    std::string build_type =
        project_config.get_string("build.build_type", "Debug");

    // Set up build configurations
    cmakelists << "# Build configurations\n";
    cmakelists << "if(NOT CMAKE_BUILD_TYPE)\n";
    cmakelists << "    set(CMAKE_BUILD_TYPE \"" << build_type << "\")\n";
    cmakelists << "endif()\n\n";

    cmakelists << "message(STATUS \"Building with ${CMAKE_BUILD_TYPE} "
                  "configuration\")\n\n";

    // Configure output directories for all configurations using generator expressions
    cmakelists << "# Configure output directories\n";
    cmakelists << "if(DEFINED CMAKE_CONFIGURATION_TYPES)\n";
    cmakelists << "  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)\n";
    cmakelists << "    string(TOUPPER ${cfg} CFG_UPPER)\n";
    cmakelists << "    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} \"${CMAKE_BINARY_DIR}/lib/${cfg}\")\n";
    cmakelists << "    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} \"${CMAKE_BINARY_DIR}/lib/${cfg}\")\n";
    cmakelists << "    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} \"${CMAKE_BINARY_DIR}/bin/${cfg}\")\n";
    cmakelists << "  endforeach()\n";
    cmakelists << "else()\n";
    cmakelists << "  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
    cmakelists << "  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
    cmakelists << "  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}\")\n";
    cmakelists << "endif()\n\n";

    // Get dependencies directory (default: deps)
    std::string deps_dir =
        project_config.get_string("dependencies.directory", "deps");

    // Handle Git dependencies
    configure_git_dependencies_in_cmake(project_config, deps_dir, cmakelists);

    // Source files - use the original project source directory
    cmakelists << "# Add source files\n";
    cmakelists << "file(GLOB_RECURSE SOURCES\n";
    cmakelists << "    \"${SOURCE_DIR}/src/*.cpp\"\n";
    cmakelists << "    \"${SOURCE_DIR}/src/*.c\"\n";
    cmakelists << ")\n\n";

    // Check for additional sources
    if (project_config.has_key("project.additional_sources")) {
      auto additional_sources =
          project_config.get_string_array("project.additional_sources");
      if (!additional_sources.empty()) {
        cmakelists << "# Add additional source files\n";
        for (const auto &source : additional_sources) {
          cmakelists << "file(GLOB_RECURSE ADDITIONAL_SOURCES_" << source
                     << " \"${SOURCE_DIR}/" << source << "\")\n";
          cmakelists << "list(APPEND SOURCES ${ADDITIONAL_SOURCES_" << source
                     << "})\n";
        }
        cmakelists << "\n";
      }
    }

    // Define target
    cmakelists << "# Add target\n";
    if (binary_type == "executable") {
      cmakelists << "add_executable(${PROJECT_NAME} ${SOURCES})\n\n";
    } else if (binary_type == "shared_lib") {
      cmakelists << "add_library(${PROJECT_NAME} SHARED ${SOURCES})\n\n";
    } else if (binary_type == "static_lib") {
      cmakelists << "add_library(${PROJECT_NAME} STATIC ${SOURCES})\n\n";
    } else if (binary_type == "header_only") {
      cmakelists << "add_library(${PROJECT_NAME} INTERFACE)\n\n";
    } else {
      // Default to executable
      cmakelists << "add_executable(${PROJECT_NAME} ${SOURCES})\n\n";
    }

    // Add include directories - use the original project include directory
    if (binary_type == "header_only") {
      cmakelists << "# Include directories\n";
      cmakelists << "target_include_directories(${PROJECT_NAME} INTERFACE\n";
      cmakelists << "    \"${SOURCE_DIR}/include\"\n";
      cmakelists << ")\n\n";
    } else {
      cmakelists << "# Include directories\n";
      cmakelists << "target_include_directories(${PROJECT_NAME} PUBLIC\n";
      cmakelists << "    \"${SOURCE_DIR}/include\"\n";
      cmakelists << ")\n\n";
    }

    // Handle workspace project dependencies includes
    if (project_config.has_key("dependencies.project")) {
      cmakelists << "# Workspace project include dependencies\n";
      auto deps = project_config.get_table_keys("dependencies.project");
      for (const auto &dep : deps) {
        // Check for custom include dirs override
        std::string dirs_key = "dependencies.project." + dep + ".include_dirs";
        if (project_config.has_key(dirs_key)) {
          auto inc_dirs = project_config.get_string_array(dirs_key);
          for (const auto &inc_dir : inc_dirs) {
            cmakelists << "target_include_directories(${PROJECT_NAME} PUBLIC \"${CMAKE_CURRENT_SOURCE_DIR}/../" << dep << "/" << inc_dir << "\")\n";
          }
        } else {
          // Fallback to include flag and default include path
          bool inc = project_config.get_bool("dependencies.project." + dep + ".include", true);
          if (inc) {
            cmakelists << "target_include_directories(${PROJECT_NAME} PUBLIC \"${CMAKE_CURRENT_SOURCE_DIR}/../" << dep << "/include\")\n";
          }
        }
      }
      cmakelists << "\n";
    }

    // Add additional include directories
    if (project_config.has_key("project.additional_includes")) {
      auto additional_includes =
          project_config.get_string_array("project.additional_includes");
      if (!additional_includes.empty()) {
        cmakelists << "# Add additional include directories\n";
        for (const auto &include : additional_includes) {
          if (binary_type == "header_only") {
            cmakelists << "target_include_directories(${PROJECT_NAME} INTERFACE "
                          "\"${SOURCE_DIR}/"
                       << include << "\")\n";
          } else {
            cmakelists << "target_include_directories(${PROJECT_NAME} PUBLIC "
                          "\"${SOURCE_DIR}/"
                       << include << "\")\n";
          }
        }
        cmakelists << "\n";
      }
    }

    // Add global build.defines
    if (project_config.has_key("build.defines")) {
      auto build_defs = project_config.get_string_array("build.defines");
      if (!build_defs.empty()) {
        cmakelists << "# Global compiler definitions\n";
        for (const auto &d : build_defs) {
          if (binary_type == "header_only") {
            cmakelists << "target_compile_definitions(${PROJECT_NAME} INTERFACE " << d << ")\n";
          } else {
            cmakelists << "target_compile_definitions(${PROJECT_NAME} PUBLIC " << d << ")\n";
          }
        }
        cmakelists << "\n";
      }
    }
    // Add config-specific build.config.<config>.defines
    {
      std::string defs_key = "build.config." + string_to_lower(build_type) + ".defines";
      if (project_config.has_key(defs_key)) {
        auto cfg_defs = project_config.get_string_array(defs_key);
        if (!cfg_defs.empty()) {
          cmakelists << "# Definitions for config '" << build_type << "'\n";
          cmakelists << "if(CMAKE_BUILD_TYPE STREQUAL \"" << build_type << "\")\n";
          for (const auto &d : cfg_defs) {
            cmakelists << "  target_compile_definitions(${PROJECT_NAME} PUBLIC " << d << ")\n";
          }
          cmakelists << "endif()\n\n";
        }
      }
    }

    // Link libraries
    cmakelists << "# Link libraries\n";
    if (binary_type == "header_only") {
      cmakelists << "target_link_libraries(${PROJECT_NAME} INTERFACE\n";
    } else {
      cmakelists << "target_link_libraries(${PROJECT_NAME} PUBLIC\n";
    }

    // Standard libraries
    cmakelists << "    ${CMAKE_THREAD_LIBS_INIT}\n";

    // Link vcpkg dependencies
    if (project_config.has_key("dependencies.vcpkg")) {
      auto vcpkg_deps = project_config.get_table_keys("dependencies.vcpkg");
      for (const auto &dep : vcpkg_deps) {
        cmakelists << "    " << dep << "::" << dep << "\n";
      }
    }

    // Link Git dependencies
    if (project_config.has_key("dependencies.git")) {
      auto git_deps = project_config.get_table_keys("dependencies.git");
      for (const auto &dep : git_deps) {
        // Check if this dependency should be linked
        bool link =
            project_config.get_bool("dependencies.git." + dep + ".link", true);

        if (link) {
          cmakelists << "    " << dep << "::" << dep << "\n";
        }
      }
    }

    // Add additional libraries
    if (project_config.has_key("build.libraries")) {
      auto libraries = project_config.get_string_array("build.libraries");
      for (const auto &lib : libraries) {
        cmakelists << "    " << lib << "\n";
      }
    }

    // Handle workspace project dependencies linking
    if (project_config.has_key("dependencies.project")) {
      auto deps = project_config.get_table_keys("dependencies.project");
      for (const auto &dep : deps) {
        bool link = project_config.get_bool("dependencies.project." + dep + ".link", true);
        if (link) {
          std::string target_name = project_config.get_string("dependencies.project." + dep + ".target_name", dep);
          cmakelists << "    " << target_name << "\n";
        }
      }
    }

    cmakelists << ")\n\n";

    // Add compiler options
    cmakelists << "# Compiler options\n";
    if (binary_type == "header_only") {
      // Header-only libraries don't have compile options
      cmakelists << "# No compile options for header-only libraries\n\n";
    } else {
      cmakelists << "if(MSVC)\n";
      cmakelists << "    target_compile_options(${PROJECT_NAME} PRIVATE /W4)\n";
      cmakelists << "else()\n";
      cmakelists << "    target_compile_options(${PROJECT_NAME} PRIVATE -Wall "
                    "-Wextra -Wpedantic)\n";
      cmakelists << "endif()\n\n";
    }

    // Add tests if available
    cmakelists << "# Tests\n";
    std::filesystem::path tests_dir = project_dir / "tests";
    if (std::filesystem::exists(tests_dir) &&
        std::filesystem::is_directory(tests_dir)) {
      cmakelists << "if(BUILD_TESTING)\n";
      cmakelists << "    enable_testing()\n";
      cmakelists << "    add_subdirectory(\"${SOURCE_DIR}/tests\" "
                    "${CMAKE_BINARY_DIR}/tests)\n";
      cmakelists << "endif()\n\n";
    } else {
      cmakelists << "# No tests directory found\n\n";
    }

    // Installation configuration
    if (binary_type == "executable") {
        cmakelists << "# Installation configuration\n";
        cmakelists << "include(GNUInstallDirs)\n\n";
        
        // Install the executable
        cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
        cmakelists << "    RUNTIME\n";
        cmakelists << "        DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
        cmakelists << "        COMPONENT Runtime\n";
        cmakelists << "    LIBRARY\n";
        cmakelists << "        DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
        cmakelists << "        COMPONENT Runtime\n";
        cmakelists << "    ARCHIVE\n";
        cmakelists << "        DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
        cmakelists << "        COMPONENT Runtime\n";
        cmakelists << ")\n\n";

        // Install PDB files for Windows Debug builds
        cmakelists << "if(MSVC AND CMAKE_BUILD_TYPE STREQUAL \"Debug\")\n";
        cmakelists << "    install(FILES \"$<TARGET_PDB_FILE:${PROJECT_NAME}>\"\n";
        cmakelists << "            DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
        cmakelists << "            COMPONENT Debug\n";
        cmakelists << "            OPTIONAL\n";
        cmakelists << "    )\n";
        cmakelists << "endif()\n\n";

        // Install any additional files specified in the TOML
        if (project_config.has_key("package.include_files")) {
            auto include_files = project_config.get_string_array("package.include_files");
            if (!include_files.empty()) {
                cmakelists << "# Install additional files\n";
                for (const auto& file : include_files) {
                    cmakelists << "install(FILES \"${CMAKE_CURRENT_SOURCE_DIR}/" << file << "\"\n";
                    cmakelists << "        DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}\n";
                    cmakelists << "        COMPONENT Runtime\n";
                    cmakelists << ")\n";
                }
                cmakelists << "\n";
            }
        }
    } else if (binary_type == "shared_lib" || binary_type == "static_lib") {
        cmakelists << "# Installation configuration\n";
        cmakelists << "include(GNUInstallDirs)\n\n";
        
        // Install the library
        cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
        cmakelists << "    RUNTIME\n";
        cmakelists << "        DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
        cmakelists << "        COMPONENT Runtime\n";
        cmakelists << "    LIBRARY\n";
        cmakelists << "        DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
        cmakelists << "        COMPONENT Runtime\n";
        cmakelists << "    ARCHIVE\n";
        cmakelists << "        DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
        cmakelists << "        COMPONENT Runtime\n";
        cmakelists << ")\n\n";

        // Install headers
        cmakelists << "install(DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}/include/\"\n";
        cmakelists << "    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
        cmakelists << "    COMPONENT Development\n";
        cmakelists << "    FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
        cmakelists << ")\n\n";
    }

    // CPack configuration
    cmakelists << "# CPack configuration\n";
    cmakelists << "set(CPACK_PACKAGE_NAME \"${PROJECT_NAME}\")\n";
    cmakelists << "set(CPACK_PACKAGE_VENDOR \"" << project_config.get_string("package.vendor", "Unknown") << "\")\n";
    cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"" << project_config.get_string("project.description", "A C++ project") << "\")\n";
    cmakelists << "set(CPACK_PACKAGE_VERSION \"" << project_config.get_string("project.version", "1.0.0") << "\")\n";
    cmakelists << "set(CPACK_PACKAGE_INSTALL_DIRECTORY \"${PROJECT_NAME}\")\n";
    cmakelists << "set(CPACK_RESOURCE_FILE_LICENSE \"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE\")\n\n";

    // Set package file name with configuration
    cmakelists << "set(CPACK_PACKAGE_FILE_NAME \"${PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}\")\n";
    cmakelists << "set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)\n";
    cmakelists << "set(CPACK_DEB_COMPONENT_INSTALL ON)\n";
    cmakelists << "set(CPACK_RPM_COMPONENT_INSTALL ON)\n\n";

    // Configure components
    cmakelists << "# Package components\n";
    cmakelists << "set(CPACK_COMPONENTS_ALL Runtime)\n";
    if (binary_type == "shared_lib" || binary_type == "static_lib") {
        cmakelists << "list(APPEND CPACK_COMPONENTS_ALL Development)\n";
    }
    if (binary_type == "executable" && project_config.get_bool("package.include_debug", false)) {
        cmakelists << "list(APPEND CPACK_COMPONENTS_ALL Debug)\n";
    }
    cmakelists << "\n";

    // Component descriptions
    cmakelists << "set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME \"Runtime Files\")\n";
    cmakelists << "set(CPACK_COMPONENT_RUNTIME_DESCRIPTION \"Runtime libraries and executables\")\n";
    if (binary_type == "shared_lib" || binary_type == "static_lib") {
        cmakelists << "set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME \"Development Files\")\n";
        cmakelists << "set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION \"Development headers and libraries\")\n";
        cmakelists << "set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime)\n";
    }
    cmakelists << "\n";

    // Generator-specific settings
    cmakelists << "if(WIN32)\n";
    cmakelists << "    set(CPACK_GENERATOR \"ZIP;NSIS\")\n";
    cmakelists << "    set(CPACK_NSIS_MODIFY_PATH ON)\n";
    cmakelists << "    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)\n";
    cmakelists << "    set(CPACK_NSIS_PACKAGE_NAME \"${PROJECT_NAME}\")\n";
    cmakelists << "    set(CPACK_NSIS_DISPLAY_NAME \"${PROJECT_NAME}\")\n";
    cmakelists << "    set(CPACK_NSIS_INSTALL_ROOT \"$PROGRAMFILES64\")\n";
    cmakelists << "elseif(APPLE)\n";
    cmakelists << "    set(CPACK_GENERATOR \"ZIP;TGZ\")\n";
    cmakelists << "else()\n";
    cmakelists << "    set(CPACK_GENERATOR \"ZIP;TGZ;DEB\")\n";
    cmakelists << "    set(CPACK_DEBIAN_PACKAGE_MAINTAINER \"${CPACK_PACKAGE_VENDOR}\")\n";
    cmakelists << "    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)\n";
    cmakelists << "endif()\n\n";

    // Packaging directory settings
    cmakelists << "# Packaging directory settings\n";
    cmakelists << "set(CPACK_OUTPUT_FILE_PREFIX \"${CMAKE_BINARY_DIR}/packages\")\n";
    cmakelists << "set(CPACK_PACKAGING_INSTALL_PREFIX \"${CMAKE_INSTALL_PREFIX}\")\n\n";

    // Include CPack
    cmakelists << "include(CPack)\n";

    cmakelists.close();
    logger::print_verbose("Generated CMakeLists.txt in project directory: " +
                          cmakelists_path.string());
    logger::print_success("Generated CMakeLists.txt file in project directory");

    // Store the new toml hash
    dep_hashes.set_hash("cforge.toml", toml_hash);
    dep_hashes.save(project_dir);

    // Workspace integration support: include/link paths passed via -DCMAKE_INCLUDE_PATH/-DCMAKE_LIBRARY_PATH
    {
        auto [is_workspace, ws_dir] = is_in_workspace(project_dir);
        if (is_workspace) {
            cmakelists << "# Workspace integration support\n";
            cmakelists << "if(CMAKE_INCLUDE_PATH)\n";
            cmakelists << "    include_directories(${CMAKE_INCLUDE_PATH})\n";
            cmakelists << "endif()\n";
            cmakelists << "if(CMAKE_LIBRARY_PATH)\n";
            cmakelists << "    link_directories(${CMAKE_LIBRARY_PATH})\n";
            cmakelists << "endif()\n\n";
        }
    }

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
        "build.config." + string_to_lower(config) + ".cmake_args";
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
  // Collect all startup projects
  std::vector<std::string> to_run;
  for (const auto &project : projects_) {
    if (project.is_startup) {
      to_run.push_back(project.name);
    }
  }
  // Fallback to default startup project if none marked
  if (to_run.empty()) {
    workspace_project default_proj = get_startup_project();
    if (default_proj.name.empty()) {
      logger::print_error("No startup project set in workspace");
      return false;
    }
    to_run.push_back(default_proj.name);
  }
  bool all_success = true;
  for (const auto &proj_name : to_run) {
    if (!run_project(proj_name, args, config, verbose)) {
      all_success = false;
    }
  }
  return all_success;
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
  config_file << "# main_project = \"main_project\"\n";

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

    // Update startup flag based on table-of-tables or legacy main_project
    project.is_startup = project.is_startup_project || (project.name == startup_project_);

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

bool generate_workspace_cmakelists(const std::filesystem::path &workspace_dir,
                                   const toml_reader &workspace_config,
                                   bool verbose) {
    // Load dependency hashes
    dependency_hash dep_hashes;
    dep_hashes.load(workspace_dir);

    // Calculate current workspace toml hash
    std::filesystem::path toml_path = workspace_dir / "cforge.workspace.toml";
    if (!std::filesystem::exists(toml_path)) {
        logger::print_error("cforge.workspace.toml not found at: " + toml_path.string());
        return false;
    }

    // Read file content to verify it's not empty
    std::string toml_content;
    {
        std::ifstream toml_in(toml_path);
        if (!toml_in) {
            logger::print_error("Failed to open cforge.workspace.toml");
            return false;
        }
        std::ostringstream oss;
        oss << toml_in.rdbuf();
        toml_content = oss.str();
        if (toml_content.empty()) {
            logger::print_error("cforge.workspace.toml is empty");
            return false;
        }
    }

    std::string toml_hash = dep_hashes.calculate_file_content_hash(toml_content);
    std::string stored_toml_hash = dep_hashes.get_hash("cforge.workspace.toml");
    
    // Only skip regeneration if CMakeLists.txt already exists and the TOML hash is unchanged
    std::filesystem::path cmakelists_path = workspace_dir / "CMakeLists.txt";
    bool file_exists = std::filesystem::exists(cmakelists_path);
    if (file_exists && !stored_toml_hash.empty() && toml_hash == stored_toml_hash) {
        if (verbose) {
            logger::print_verbose("Workspace CMakeLists.txt is up to date and already exists, skipping generation");
        }
        return true;
    }

    logger::print_status("Generating workspace CMakeLists.txt from " + std::string(WORKSPACE_FILE));

    // Create CMakeLists.txt if needed
    std::ofstream cmakelists(cmakelists_path);
    if (!cmakelists.is_open()) {
        logger::print_error("Failed to create workspace CMakeLists.txt");
        return false;
    }

    // Generate workspace CMakeLists.txt content
    // Get workspace name
    std::string workspace_name = workspace_config.get_string("workspace.name", workspace_dir.filename().string());
    cmakelists << "# Workspace CMakeLists.txt for " << workspace_name << "\n";
    cmakelists << "# Generated by cforge - C++ project management tool\n\n";
    // Minimum CMake version
    cmakelists << "cmake_minimum_required(VERSION " << CMAKE_MIN_VERSION << ")\n\n";
    // Workspace configuration
    cmakelists << "# Workspace configuration\n";
    cmakelists << "project(" << workspace_name << " LANGUAGES CXX)\n\n";
    // Set C++ standard for the entire workspace
    std::string cpp_std = workspace_config.get_string("workspace.cpp_standard", "17");
    cmakelists << "# Set C++ standard for the entire workspace\n";
    cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_std << ")\n";
    cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
    // Configure output directories
    cmakelists << "# Configure output directories\n";
    cmakelists << "if(DEFINED CMAKE_CONFIGURATION_TYPES)\n";
    cmakelists << "  foreach(cfg IN LISTS CMAKE_CONFIGURATION_TYPES)\n";
    cmakelists << "    string(TOUPPER ${cfg} CFG_UPPER)\n";
    cmakelists << "    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CFG_UPPER} \"${CMAKE_BINARY_DIR}/lib/${cfg}\")\n";
    cmakelists << "    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CFG_UPPER} \"${CMAKE_BINARY_DIR}/lib/${cfg}\")\n";
    cmakelists << "    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG_UPPER} \"${CMAKE_BINARY_DIR}/bin/${cfg}\")\n";
    cmakelists << "  endforeach()\n";
    cmakelists << "else()\n";
    cmakelists << "  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
    cmakelists << "  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}\")\n";
    cmakelists << "  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}\")\n";
    cmakelists << "endif()\n\n";
    // Add all projects in the workspace
    cmakelists << "# Add all projects in the workspace\n";
    {
        workspace ws;
        if (ws.load(workspace_dir)) {
            auto projects = ws.get_projects();
            for (const auto &proj : projects) {
                std::filesystem::path rel_path = proj.path;
                if (rel_path.is_absolute()) {
                    try {
                        rel_path = std::filesystem::relative(rel_path, workspace_dir);
                    } catch (...) {}
                }
                cmakelists << "add_subdirectory(" << rel_path.string() << ")\n";
            }
        }
        cmakelists << "\n";
    }

    // Rest of the existing workspace CMakeLists.txt generation code...
    // ... existing code ...

    // Store the new workspace toml hash
    dep_hashes.set_hash("cforge.workspace.toml", toml_hash);
    dep_hashes.save(workspace_dir);

    return true;
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

  // Load projects using array-of-tables [[workspace.project]] with fallback
  projects_.clear();
  bool had_table_startup = false;
  try {
    auto raw = toml::parse_file(workspace_file);
    auto node = raw["workspace"]["project"];
    if (node && node.is_array_of_tables()) {
      // New format as array of tables
      for (auto &elem : *node.as_array()) {
        if (!elem.is_table()) continue;
        toml::table &tbl = *elem.as_table();
        workspace_project project;
        project.name = tbl["name"].value_or("");
        project.path = tbl["path"].value_or(project.name);
        // Read startup flag as boolean or string (accept "true" as true)
        auto flag_node = tbl["startup"];
        if (flag_node && flag_node.is_boolean()) {
          project.is_startup_project = flag_node.value_or(false);
        } else if (flag_node && flag_node.is_string()) {
          std::string s = flag_node.value_or(std::string());
          std::transform(s.begin(), s.end(), s.begin(), ::tolower);
          project.is_startup_project = (s == "true");
        } else {
          project.is_startup_project = false;
        }
        if (project.is_startup_project) {
          had_table_startup = true;
        }
        projects_.push_back(project);
      }
    } else {
      // Legacy parsing: string array format "name:path:is_startup_project"
      std::vector<std::string> project_paths =
          reader.get_string_array("workspace.projects");
      if (!project_paths.empty()) {
        for (const auto &project_path : project_paths) {
          workspace_project project;
          std::vector<std::string> parts;
          std::string::size_type start = 0, end = 0;
          while (start < project_path.length()) {
            end = project_path.find(':', start);
            if (end == std::string::npos) {
              parts.push_back(project_path.substr(start));
              break;
            }
            if (end == 1 && project_path.length() > 2 &&
                (project_path[end + 1] == '\\' || project_path[end + 1] == '/')) {
              // Skip colon in Windows drive letter
              start = end + 1;
              continue;
            }
            parts.push_back(project_path.substr(start, end - start));
            start = end + 1;
          }
          if (parts.size() >= 1) project.name = parts[0];
          if (parts.size() >= 2)
            project.path = std::filesystem::path(parts[1]);
          else if (!project.name.empty())
            project.path = project.name;
          if (parts.size() >= 3)
            project.is_startup_project = (parts[2] == "true");
          if (project.is_startup_project) {
            had_table_startup = true;
          }
          projects_.push_back(project);
        }
      }
    }
  } catch (const std::exception &e) {
    logger::print_error("Error parsing workspace projects: " + std::string(e.what()));
  }

  // Check for default startup project only if none in table-of-tables
  std::string main_project = reader.get_string("workspace.main_project", "");
  if (!had_table_startup && !main_project.empty()) {
    set_startup_project(main_project);
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
    file << "main_project = \"" << startup_project << "\"\n\n";
  }

  // Write projects as array of tables [[workspace.project]]
  for (const auto &project : projects_) {
    file << "[[workspace.project]]\n";
    file << "name = \"" << project.name << "\"\n";
    // Compute relative or absolute path
    std::filesystem::path path_to_save;
    if (project.path.is_absolute()) {
      try {
        path_to_save = std::filesystem::relative(project.path, workspace_dir);
      } catch (...) {
        path_to_save = project.path;
      }
    } else {
      path_to_save = project.path;
    }
    file << "path = \"" << path_to_save.string() << "\"\n";
    file << "startup = " << (project.is_startup_project ? "true" : "false") << "\n\n";
  }

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