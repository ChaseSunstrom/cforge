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
                                    std::ofstream &cmakelists) {
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
    cmakelists << "    SOURCE_DIR ${DEPS_DIR}/" << dep << "\n";
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
        cmakelists << "include_directories(${DEPS_DIR}/" << dep << "/"
                   << inc_dir << ")\n";
      }
      cmakelists << "\n";
    }

    // Special handling for common libraries
    if (dep == "json" || dep == "nlohmann_json") {
      // For nlohmann/json, we typically don't need to build anything
      cmakelists << "# For nlohmann/json, we just need the include directory\n";
      cmakelists << "FetchContent_GetProperties(" << dep << ")\n";
      cmakelists << "if(NOT " << dep << "_POPULATED)\n";
      cmakelists
          << "    message(STATUS \"Making nlohmann/json available...\")\n";
      cmakelists << "    FetchContent_Populate(" << dep << ")\n";
      cmakelists << "endif()\n\n";
    } else if (dep == "fmt") {
      // For fmt, we need to build the library
      cmakelists << "# For fmt, we need to build the library\n";
      cmakelists << "set(FMT_TEST OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(FMT_DOC OFF CACHE BOOL \"\" FORCE)\n";
      cmakelists << "set(FMT_SYSTEM_HEADERS ON CACHE BOOL \"\" FORCE)\n";
      cmakelists << "FetchContent_MakeAvailable(" << dep << ")\n\n";
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
  // Path to CMakeLists.txt in project directory
  std::filesystem::path cmakelists_path = project_dir / "CMakeLists.txt";
  
  // Check if existing CMakeLists.txt is present
  bool has_cmake = std::filesystem::exists(cmakelists_path);
  // Compute hash of cforge.toml to detect changes
  std::filesystem::path toml_path = project_dir / CFORGE_FILE;
  std::string toml_content;
  {
    std::ifstream toml_in(toml_path);
    std::ostringstream oss;
    oss << toml_in.rdbuf();
    toml_content = oss.str();
  }
  std::string current_hash = std::to_string(std::hash<std::string>{}(toml_content));
  std::filesystem::path hash_file = project_dir / ".cforge_toml_hash";
  // If hash file exists and matches and CMakeLists exists, skip regeneration
  if (std::filesystem::exists(hash_file)) {
    std::ifstream hash_in(hash_file);
    std::string old_hash;
    std::getline(hash_in, old_hash);
    if (old_hash == current_hash && has_cmake) {
      logger::print_verbose("No changes in cforge.toml, skipping CMakeLists.txt regeneration");
      return true;
    }
  }
  // Update hash file with current TOML hash
  {
    std::ofstream hash_out(hash_file, std::ios::trunc);
    hash_out << current_hash;
  }
  
  // Skip generation if CMakeLists.txt already exists in project folder
  bool file_exists = std::filesystem::exists(cmakelists_path);
  
  if (file_exists) {
    // Regenerate if dependencies are defined
    if (!project_config.has_key("dependencies.git") && !project_config.has_key("dependencies.vcpkg")) {
      logger::print_verbose("CMakeLists.txt already exists in project directory, using existing file");
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

  cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
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

  // Check for vcpkg dependencies and add toolchain file if needed
  if (project_config.has_key("dependencies.vcpkg")) {
    cmakelists << "# vcpkg integration\n";
    cmakelists << "if(DEFINED ENV{VCPKG_ROOT})\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "elseif(EXISTS "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\")\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "endif()\n\n";
  }

  // Add vcpkg dependencies
  if (project_config.has_key("dependencies.vcpkg")) {
    cmakelists << "# Dependencies\n";

    // Iterate through all vcpkg dependencies
    auto vcpkg_deps = project_config.get_table_keys("dependencies.vcpkg");
    for (const auto &dep : vcpkg_deps) {
      std::string version;
      std::vector<std::string> components;

      // Check if dependency is specified with version and components
      if (project_config.has_key("dependencies.vcpkg." + dep + ".version")) {
        version = project_config.get_string(
            "dependencies.vcpkg." + dep + ".version", "");

        // Check for components
        if (project_config.has_key("dependencies.vcpkg." + dep +
                                   ".components")) {
          components = project_config.get_string_array("dependencies.vcpkg." +
                                                       dep + ".components");
        }
      } else {
        // Simple dependency specification
        version = project_config.get_string("dependencies.vcpkg." + dep, "");
      }

      cmakelists << "# Find " << dep << "\n";
      if (!version.empty()) {
        cmakelists << "find_package(" << dep << " " << version;
      } else {
        cmakelists << "find_package(" << dep;
      }

      // Add components if any
      if (!components.empty()) {
        cmakelists << " COMPONENTS";
        for (const auto &comp : components) {
          cmakelists << " " << comp;
        }
      }

      cmakelists << " REQUIRED)\n";
    }
    cmakelists << "\n";
  }

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

  // Installation
  cmakelists << "# Installation\n";
  cmakelists << "include(GNUInstallDirs)\n";

  if (binary_type == "executable") {
    // For executables
    cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
    cmakelists << "    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    cmakelists << "    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << "    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << ")\n\n";

    // Also copy PDB files for Windows Debug builds
    cmakelists << "if(MSVC AND CMAKE_BUILD_TYPE STREQUAL \"Debug\")\n";
    cmakelists << "    install(FILES "
                  "\"${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}/"
                  "$<TARGET_FILE_NAME:${PROJECT_NAME}>.pdb\"\n";
    cmakelists << "            DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    cmakelists << "            CONFIGURATIONS Debug)\n";
    cmakelists << "endif()\n\n";
  } else if (binary_type == "shared_lib" || binary_type == "static_lib") {
    // For libraries
    cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
    cmakelists << "    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}\n";
    cmakelists << "    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << "    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}\n";
    cmakelists << ")\n\n";

    // Install headers
    cmakelists << "install(DIRECTORY \"${SOURCE_DIR}/include/\"\n";
    cmakelists << "    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
    cmakelists << "    FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
    cmakelists << ")\n\n";
  } else if (binary_type == "header_only") {
    // For header-only libraries
    cmakelists << "install(TARGETS ${PROJECT_NAME}\n";
    cmakelists << "    EXPORT ${PROJECT_NAME}Targets\n";
    cmakelists << ")\n\n";

    // Install headers
    cmakelists << "install(DIRECTORY \"${SOURCE_DIR}/include/\"\n";
    cmakelists << "    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}\n";
    cmakelists << "    FILES_MATCHING PATTERN \"*.h\" PATTERN \"*.hpp\"\n";
    cmakelists << ")\n\n";
  }

  // CPack configuration
  cmakelists << "# CPack configuration\n";
  cmakelists << "set(CPACK_PACKAGE_NAME ${PROJECT_NAME})\n";
  cmakelists << "set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})\n";
  cmakelists << "set(CPACK_PACKAGE_VENDOR \"${PROJECT_AUTHOR}\")\n";
  cmakelists
      << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_DESCRIPTION}\")\n";
  cmakelists << "set(CPACK_RESOURCE_FILE_LICENSE \"${SOURCE_DIR}/LICENSE\")\n";
  cmakelists
      << "set(CPACK_RESOURCE_FILE_README \"${SOURCE_DIR}/README.md\")\n\n";

  // OS-specific settings
  cmakelists << "# OS-specific settings\n";
  cmakelists << "if(WIN32)\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;NSIS\")\n";
  cmakelists << "    set(CPACK_NSIS_MODIFY_PATH ON)\n";
  cmakelists << "elseif(APPLE)\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;TGZ\")\n";
  cmakelists << "else()\n";
  cmakelists << "    set(CPACK_GENERATOR \"ZIP;TGZ;DEB\")\n";
  cmakelists << "endif()\n\n";

  // Binary packages with config
  cmakelists << "# Binary packages with config\n";
  cmakelists << "set(CPACK_PACKAGE_FILE_NAME "
                "\"${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CMAKE_"
                "SYSTEM_NAME}-${CMAKE_BUILD_TYPE}\")\n\n";

  // Components
  cmakelists << "# Component-based installation\n";
  cmakelists << "set(CPACK_COMPONENTS_ALL Runtime Development)\n";
  cmakelists << "set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME \"Runtime Files\")\n";
  cmakelists << "set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME \"Development "
                "Files\")\n";
  cmakelists << "set(CPACK_COMPONENT_RUNTIME_DESCRIPTION \"Runtime libraries "
                "and executables\")\n";
  cmakelists << "set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION \"Development "
                "headers and libraries\")\n\n";

  // Include CPack
  cmakelists << "# Include CPack\n";
  cmakelists << "include(CPack)\n";

  cmakelists.close();
  logger::print_verbose("Generated CMakeLists.txt in project directory: " +
                        cmakelists_path.string());
  logger::print_success("Generated CMakeLists.txt file in project directory");
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
  // Compute hash of workspace configuration to detect changes
  std::filesystem::path toml_path = workspace_dir / WORKSPACE_FILE;
  std::string toml_content;
  {
    std::ifstream toml_in(toml_path);
    std::ostringstream oss;
    oss << toml_in.rdbuf();
    toml_content = oss.str();
  }
  std::string current_hash = std::to_string(std::hash<std::string>{}(toml_content));
  std::filesystem::path hash_file = workspace_dir / ".cforge_workspace_toml_hash";
  std::filesystem::path cmakelists_path = workspace_dir / "CMakeLists.txt";
  bool has_ws_cmake = std::filesystem::exists(cmakelists_path);
  if (std::filesystem::exists(hash_file)) {
    std::ifstream hash_in(hash_file);
    std::string old_hash;
    std::getline(hash_in, old_hash);
    // Skip regeneration only if hash unchanged and CMakeLists exists
    if (old_hash == current_hash && has_ws_cmake) {
      logger::print_verbose("No changes in " + std::string(WORKSPACE_FILE) + ", skipping workspace CMakeLists.txt generation");
      return true;
    }
  }
  // Update hash file with current workspace TOML hash
  {
    std::ofstream hash_out(hash_file, std::ios::trunc);
    hash_out << current_hash;
  }
  // Generating workspace CMakeLists.txt
  logger::print_status("Generating workspace CMakeLists.txt from " + std::string(WORKSPACE_FILE));

  // Create CMakeLists.txt if needed
  std::ofstream cmakelists(cmakelists_path);
  if (!cmakelists.is_open()) {
    logger::print_error("Failed to create workspace CMakeLists.txt");
    return false;
  }

  // Get workspace metadata
  std::string workspace_name =
      workspace_config.get_string("workspace.name", "cpp-workspace");
  std::string workspace_description = workspace_config.get_string(
      "workspace.description", "A C++ workspace created with cforge");

  // Write initial CMake configuration
  cmakelists << "# Workspace CMakeLists.txt for " << workspace_name << "\n";
  cmakelists << "# Generated by cforge - C++ project management tool\n\n";

  cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
  cmakelists << "# Workspace configuration\n";
  cmakelists << "project(" << workspace_name << " LANGUAGES CXX)\n\n";

  // Workspace description
  cmakelists << "# Workspace description\n";
  cmakelists << "set(WORKSPACE_DESCRIPTION \"" << workspace_description
             << "\")\n\n";

  // Common build settings
  cmakelists << "# Common build settings\n";
  cmakelists << "set(CMAKE_CXX_STANDARD "
             << workspace_config.get_string("workspace.cpp_standard", "17")
             << ")\n";
  cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
  cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";

  // Set up build configurations
  std::string build_type =
      workspace_config.get_string("workspace.build_type", "Debug");
  cmakelists << "# Build configuration\n";
  cmakelists << "if(NOT CMAKE_BUILD_TYPE)\n";
  cmakelists << "    set(CMAKE_BUILD_TYPE \"" << build_type << "\")\n";
  cmakelists << "endif()\n\n";

  cmakelists << "message(STATUS \"Building workspace with ${CMAKE_BUILD_TYPE} "
                "configuration\")\n\n";

  // Configure output directories for all generators
  cmakelists << "# Configure output directories\n";
  cmakelists << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib/$<CONFIG>\")\n";
  cmakelists << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib/$<CONFIG>\")\n";
  cmakelists << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/bin/$<CONFIG>\")\n\n";

  // Check for workspace-wide dependencies
  if (workspace_config.has_key("dependencies.git")) {
    std::string deps_dir =
        workspace_config.get_string("dependencies.directory", "deps");
    cmakelists << "# Workspace-level Git dependencies\n";
    configure_git_dependencies_in_cmake(workspace_config, deps_dir, cmakelists);
  }

  // Check for vcpkg dependencies
  if (workspace_config.has_key("dependencies.vcpkg")) {
    cmakelists << "# vcpkg integration\n";
    cmakelists << "if(DEFINED ENV{VCPKG_ROOT})\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "elseif(EXISTS "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\")\n";
    cmakelists << "    set(CMAKE_TOOLCHAIN_FILE "
                  "\"${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/"
                  "vcpkg.cmake\"\n";
    cmakelists << "        CACHE STRING \"Vcpkg toolchain file\")\n";
    cmakelists << "endif()\n\n";

    // Add vcpkg dependencies
    cmakelists << "# Dependencies\n";
    auto vcpkg_deps = workspace_config.get_table_keys("dependencies.vcpkg");
    for (const auto &dep : vcpkg_deps) {
      std::string version = workspace_config.get_string(
          "dependencies.vcpkg." + dep + ".version", "");
      std::vector<std::string> components = workspace_config.get_string_array(
          "dependencies.vcpkg." + dep + ".components");

      // Add find_package command
      cmakelists << "find_package(" << dep;

      // Add version if specified
      if (!version.empty()) {
        cmakelists << " " << version;
      }

      // Add components if specified
      if (!components.empty()) {
        cmakelists << " COMPONENTS";
        for (const auto &comp : components) {
          cmakelists << " " << comp;
        }
      }

      cmakelists << " REQUIRED)\n";
    }
    cmakelists << "\n";
  }

  // Add Thread package
  cmakelists << "find_package(Threads REQUIRED)\n\n";

  // Add all projects
  cmakelists << "# Add all projects in the workspace\n";

  // Discover projects in the workspace
  std::vector<std::string> projects;
  for (const auto &entry : std::filesystem::directory_iterator(workspace_dir)) {
    if (entry.is_directory() &&
        std::filesystem::exists(entry.path() / CFORGE_FILE) &&
        entry.path().filename() != DEFAULT_BUILD_DIR) {
      projects.push_back(entry.path().filename().string());
    }
  }

  if (workspace_config.has_key("workspace.projects")) {
    // Use projects defined in the workspace configuration (format: name:path:is_startup)
    auto config_projects = workspace_config.get_string_array("workspace.projects");
    if (!config_projects.empty()) {
      projects.clear();
      for (const auto &proj_entry : config_projects) {
        // Extract the project name before the first colon
        size_t pos = proj_entry.find(':');
        std::string proj_name = (pos != std::string::npos)
                                   ? proj_entry.substr(0, pos)
                                   : proj_entry;
        projects.push_back(proj_name);
      }
    }
  }

  if (projects.empty()) {
    logger::print_warning("No projects found in workspace");
  } else {
    logger::print_status("Found " + std::to_string(projects.size()) +
                         " projects in workspace");

    // First, analyze project dependencies to determine the correct order
    std::map<std::string, std::vector<std::string>> project_dependencies;

    // Collect all project dependencies
    for (const auto &project : projects) {
      std::filesystem::path project_config_path =
          workspace_dir / project / "cforge.toml";
      if (std::filesystem::exists(project_config_path)) {
        try {
          toml::table project_table =
              toml::parse_file(project_config_path.string());
          toml_reader project_config(project_table);

          // Check for project dependencies
          if (project_config.has_key("dependencies.project")) {
            auto project_deps =
                project_config.get_table_keys("dependencies.project");
            project_dependencies[project] = project_deps;
            logger::print_verbose("Project '" + project + "' depends on " +
                                  std::to_string(project_deps.size()) +
                                  " other workspace projects");
          } else {
            // No dependencies
            project_dependencies[project] = std::vector<std::string>();
          }
        } catch (const toml::parse_error &e) {
          logger::print_warning("Failed to parse cforge.toml for project '" +
                                project + "': " + std::string(e.what()));
          // Assume no dependencies if we can't parse the file
          project_dependencies[project] = std::vector<std::string>();
        }
      } else {
        // No config file, assume no dependencies
        project_dependencies[project] = std::vector<std::string>();
      }
    }

    // Function to check for circular dependencies
    std::function<bool(const std::string &, std::set<std::string> &,
                       std::set<std::string> &)>
        detect_cycle = [&](const std::string &project,
                           std::set<std::string> &visited,
                           std::set<std::string> &in_path) {
          if (in_path.find(project) != in_path.end())
            return true; // Circular dependency found
          if (visited.find(project) != visited.end())
            return false; // Already processed

          visited.insert(project);
          in_path.insert(project);

          for (const auto &dep : project_dependencies[project]) {
            if (detect_cycle(dep, visited, in_path))
              return true;
          }

          in_path.erase(project);
          return false;
        };

    // Check for circular dependencies
    std::set<std::string> visited, in_path;
    bool has_circular = false;
    for (const auto &project : projects) {
      if (visited.find(project) == visited.end()) {
        if (detect_cycle(project, visited, in_path)) {
          logger::print_error(
              "Circular dependency detected involving project '" + project +
              "'");
          has_circular = true;
          break;
        }
      }
    }

    if (has_circular) {
      logger::print_warning("Circular dependencies found, projects will be "
                            "added in original order");
    } else {
      // Sort projects based on dependencies (topological sort)
      std::vector<std::string> sorted_projects;
      visited.clear();

      std::function<void(const std::string &)> visit =
          [&](const std::string &project) {
            if (visited.find(project) != visited.end())
              return;
            visited.insert(project);

            for (const auto &dep : project_dependencies[project]) {
              visit(dep);
            }

            sorted_projects.push_back(project);
          };

      for (const auto &project : projects) {
        visit(project);
      }

      // Update the projects list to respect dependency order
      if (!sorted_projects.empty()) {
        projects = sorted_projects;
        logger::print_verbose("Projects sorted by dependency order");
      }
    }

    // Add each project
    for (const auto &project : projects) {
      cmakelists << "# Project: " << project << "\n";
      cmakelists << "if(EXISTS \"${CMAKE_CURRENT_SOURCE_DIR}/" << project
                 << "/CMakeLists.txt\")\n";
      cmakelists << "    add_subdirectory(" << project << ")\n";
      cmakelists << "else()\n";
      cmakelists << "    message(WARNING \"Project " << project
                 << " has no CMakeLists.txt file\")\n";
      cmakelists << "endif()\n\n";
    }
  }

  // Workspace-level targets
  if (workspace_config.has_key("workspace.targets")) {
    cmakelists << "# Workspace-level targets\n";
    auto targets = workspace_config.get_table_keys("workspace.targets");

    for (const auto &target : targets) {
      std::string target_type = workspace_config.get_string(
          "workspace.targets." + target + ".type", "custom");
      cmakelists << "# Target: " << target << " (Type: " << target_type
                 << ")\n";

      if (target_type == "executable") {
        // Handle executable targets
        std::vector<std::string> sources = workspace_config.get_string_array(
            "workspace.targets." + target + ".sources");

        if (!sources.empty()) {
          cmakelists << "add_executable(" << target << "\n";
          for (const auto &source : sources) {
            cmakelists << "    " << source << "\n";
          }
          cmakelists << ")\n";

          // Add dependencies
          auto dependencies = workspace_config.get_string_array(
              "workspace.targets." + target + ".depends");
          if (!dependencies.empty()) {
            cmakelists << "add_dependencies(" << target << "\n";
            for (const auto &dep : dependencies) {
              cmakelists << "    " << dep << "\n";
            }
            cmakelists << ")\n";
          }

          // Link libraries
          auto libraries = workspace_config.get_string_array(
              "workspace.targets." + target + ".links");
          if (!libraries.empty()) {
            cmakelists << "target_link_libraries(" << target << " PRIVATE\n";
            for (const auto &lib : libraries) {
              cmakelists << "    " << lib << "\n";
            }
            cmakelists << ")\n";
          }

          cmakelists << "\n";
        }
      } else if (target_type == "custom") {
        // Handle custom targets
        std::string command = workspace_config.get_string(
            "workspace.targets." + target + ".command", "");
        if (!command.empty()) {
          cmakelists << "add_custom_target(" << target << "\n";
          cmakelists << "    COMMAND " << command << "\n";
          cmakelists << "    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}\n";
          cmakelists << "    COMMENT \"Running custom target " << target
                     << "\"\n";
          cmakelists << ")\n\n";
        }
      }
    }
  }

  cmakelists.close();
  logger::print_success("Generated workspace CMakeLists.txt file");
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