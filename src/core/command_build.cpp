/**
 * @file command_build.cpp
 * @brief Implementation of the 'build' command
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"
#include "cforge/log.hpp"
#include "core/error_format.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <numeric>
#include <fstream>

using namespace cforge;

// Default build configuration if not specified
#define DEFAULT_BUILD_CONFIG "Release"

/**
 * @brief Get the generator string for CMake based on platform
 * 
 * @return std::string CMake generator string
 */
static std::string get_cmake_generator() {
#ifdef _WIN32
    // Check if Ninja is available - prefer it if it is
    if (is_command_available("ninja", 15)) {
        logger::print_verbose("Ninja is available, using Ninja Multi-Config generator");
        return "Ninja Multi-Config";
    }
    
    logger::print_verbose("Ninja not found, falling back to Visual Studio generator");
    return "Visual Studio 17 2022";
#else
    return "Unix Makefiles";
#endif
}

/**
 * @brief Parse build configuration from command line arguments
 * 
 * @param ctx Command context
 * @param project_config TOML reader for project config
 * @return std::string Build configuration
 */
static std::string get_build_config(const cforge_context_t* ctx, const toml_reader* project_config) {
    // Priority 1: Direct configuration argument
    if (ctx->args.config != nullptr && strlen(ctx->args.config) > 0) {
        logger::print_verbose("Using build configuration from direct argument: " + std::string(ctx->args.config));
        return std::string(ctx->args.config);
    }
    
    // Priority 2: Command line argument
    if (ctx->args.args) {
        for (int i = 0; ctx->args.args[i]; ++i) {
            if (strcmp(ctx->args.args[i], "--config") == 0 || 
                strcmp(ctx->args.args[i], "-c") == 0) {
                if (ctx->args.args[i+1]) {
                    logger::print_verbose("Using build configuration from command line: " + std::string(ctx->args.args[i+1]));
                    return std::string(ctx->args.args[i+1]);
                }
            }
        }
    }
    
    // Priority 3: Configuration from cforge.toml
    std::string config = project_config->get_string("build.build_type", "");
    if (!config.empty()) {
        logger::print_verbose("Using build configuration from cforge.toml: " + config);
        return config;
    }
    
    // Priority 4: Default to Release
    logger::print_verbose("No build configuration specified, defaulting to Release");
    return "Release";
}

/**
 * @brief Get build directory path based on base directory and configuration
 * 
 * @param base_dir Base build directory from configuration
 * @param config Build configuration (Release, Debug, etc.)
 * @return std::filesystem::path The configured build directory
 */
static std::filesystem::path get_build_dir_for_config(
    const std::string& base_dir,
    const std::string& config)
{
    // If config is empty, use the base directory
    if (config.empty()) {
        return base_dir;
    }
    
    // Transform config name to lowercase for directory naming
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);

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

/**
 * @brief Check if Visual Studio is available
 * 
 * @return bool True if Visual Studio is available
 */
static bool is_visual_studio_available() {
    // Check common Visual Studio installation paths
    std::vector<std::string> vs_paths = {
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Community\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Professional\\Common7\\IDE\\devenv.exe",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\Enterprise\\Common7\\IDE\\devenv.exe"
    };
    
    for (const auto& path : vs_paths) {
        if (std::filesystem::exists(path)) {
            logger::print_verbose("Found Visual Studio at: " + path);
            return true;
        }
    }
    
    return false;
}

/**
 * @brief Check if CMake is available on the system
 * 
 * @return bool True if CMake is available
 */
static bool is_cmake_available() {
    bool available = is_command_available("cmake");
    if (!available) {
        logger::print_warning("CMake not found in PATH using detection check.");
        logger::print_status("Please install CMake from https://cmake.org/download/ and make sure it's in your PATH.");
        logger::print_status("We'll still attempt to run the cmake command in case this is a false negative.");
        
        // Suggest alternative build methods
        if (is_visual_studio_available()) {
            logger::print_status("Visual Studio is available. You can open the project in Visual Studio and build it there.");
            logger::print_status("1. Open Visual Studio");
            logger::print_status("2. Select 'Open a local folder'");
            logger::print_status("3. Navigate to your project folder and select it");
            logger::print_status("4. Visual Studio will automatically configure the CMake project");
        }
    }
    return true; // Always return true to allow the build to proceed
}

/**
 * @brief Generate a CMakeLists.txt file from cforge.toml configuration
 * 
 * @param project_dir Project directory
 * @param project_config Project configuration from cforge.toml
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_cmakelists_from_toml(
    const std::filesystem::path& project_dir,
    const toml_reader& project_config,
    bool verbose
) {
    std::filesystem::path cmakelists_path = project_dir / "CMakeLists.txt";
    
    // Check if we need to generate the file
    bool file_exists = std::filesystem::exists(cmakelists_path);
    
    if (file_exists) {
        // If the file already exists, we don't need to generate it
        logger::print_verbose("CMakeLists.txt already exists, using existing file");
        return true;
    }
    
    logger::print_status("Generating CMakeLists.txt from cforge.toml configuration");
    
    // Get project name and version from configuration
    std::string project_name = project_config.get_string("project.name", "cpp-project");
    std::string project_version = project_config.get_string("project.version", "0.1.0");
    std::string project_description = project_config.get_string("project.description", "A C++ project created with cforge");
    std::string cpp_standard = project_config.get_string("project.cpp_standard", "17");
    
    // Open the file for writing
    std::ofstream cmakelists(cmakelists_path);
    if (!cmakelists) {
        logger::print_error("Failed to create CMakeLists.txt");
        return false;
    }
    
    cmakelists << "cmake_minimum_required(VERSION 3.14)\n\n";
    cmakelists << "project(" << project_name << " VERSION " << project_version << " LANGUAGES CXX)\n\n";
    
    // C++ standard
    cmakelists << "# Set C++ standard\n";
    cmakelists << "set(CMAKE_CXX_STANDARD " << cpp_standard << ")\n";
    cmakelists << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    cmakelists << "set(CMAKE_CXX_EXTENSIONS OFF)\n\n";
    
    // Output directories
    cmakelists << "# Set output directories\n";
    cmakelists << "set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
    cmakelists << "set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)\n";
    cmakelists << "set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)\n\n";
    
    // vcpkg integration
    cmakelists << "# vcpkg integration - uncomment the line below if using vcpkg\n";
    cmakelists << "# set(CMAKE_TOOLCHAIN_FILE \"$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake\" CACHE STRING \"\")\n\n";
    
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
    cmakelists << "# target_link_libraries(${EXECUTABLE_NAME} PRIVATE SomePackage)\n\n";
    
    // Compiler warnings
    cmakelists << "# Enable compiler warnings\n";
    cmakelists << "if(MSVC)\n";
    cmakelists << "    target_compile_options(${EXECUTABLE_NAME} PRIVATE /W4)\n";
    cmakelists << "else()\n";
    cmakelists << "    target_compile_options(${EXECUTABLE_NAME} PRIVATE -Wall -Wextra -Wpedantic)\n";
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
        cmakelists << "# To enable testing, set test.enabled=true in cforge.toml and create a tests directory\n\n";
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
    cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_NAME} - " << project_description << "\")\n";
    cmakelists << "set(CPACK_PACKAGE_VENDOR \"Your Organization\")\n";
    cmakelists << "set(CPACK_PACKAGE_DESCRIPTION_FILE \"${CMAKE_CURRENT_SOURCE_DIR}/README.md\")\n";
    cmakelists << "set(CPACK_RESOURCE_FILE_LICENSE \"${CMAKE_CURRENT_SOURCE_DIR}/LICENSE\")\n\n";
    
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
        logger::print_verbose("Generated CMakeLists.txt with the following configuration:");
        logger::print_verbose("- Project name: " + project_name);
        logger::print_verbose("- C++ standard: " + cpp_standard);
        logger::print_verbose("- Tests enabled: " + std::string(tests_enabled ? "yes" : "no"));
    }
    
    logger::print_success("Generated CMakeLists.txt file");
    return true;
}

/**
 * @brief Run CMake configure step
 * 
 * @param cmake_args CMake arguments
 * @param build_dir Build directory
 * @param verbose Verbose output
 * @return bool Success flag
 */
static bool run_cmake_configure(
    const std::vector<std::string>& cmake_args,
    const std::string& build_dir,
    bool verbose
) {
    // Set a longer timeout for Windows
#ifdef _WIN32
    int timeout = 180; // 3 minutes for Windows
#else
    int timeout = 120; // 2 minutes for other platforms
#endif

    // Run the CMake command with appropriate timeout
    bool result = execute_tool("cmake", cmake_args, "", "CMake Configure", verbose, timeout);
    
    // Verify that the configuration was successful by checking for CMakeCache.txt
    std::filesystem::path build_path(build_dir);
    bool cmake_success = result && std::filesystem::exists(build_path / "CMakeCache.txt");
    
    if (!cmake_success) {
        if (result) {
            logger::print_error("CMake appeared to run, but CMakeCache.txt was not created. This may indicate a configuration error.");
        } else {
            logger::print_error("CMake configuration failed. See errors above.");
        }
        logger::print_warning("You might need to clean the build directory and try again.");
        return false;
    }
    
    return true;
}

/**
 * @brief Convert a string to lowercase
 * 
 * @param str String to convert
 * @return std::string Lowercase string
 */
static std::string to_lower_case(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), 
                  [](unsigned char c){ return std::tolower(c); });
    return result;
}

/**
 * @brief Build the project with CMake
 * 
 * @param project_dir Project directory
 * @param build_config Build configuration
 * @param num_jobs Number of parallel jobs (0 for default)
 * @param verbose Verbose output
 * @param target Optional target to build
 * @return bool Success flag
 */
static bool build_project(
    const std::filesystem::path& project_dir,
    const std::string& build_config,
    int num_jobs,
    bool verbose,
    const std::string& target = "")
{
    logger::print_status("Building project...");
    
    // Determine build directory
    std::string base_build_dir = "build";
    
    // First look for a cforge.toml file
    std::filesystem::path config_path = project_dir / CFORGE_FILE;
    toml_reader project_config;
    
    if (!std::filesystem::exists(config_path)) {
        logger::print_warning("No " + std::string(CFORGE_FILE) + " file found, using default settings");
    } else if (!project_config.load(config_path.string())) {
        logger::print_warning("Failed to parse " + std::string(CFORGE_FILE) + ", using default settings");
    } else {
        // Get build directory from config
        if (project_config.has_key("build.build_dir")) {
            base_build_dir = project_config.get_string("build.build_dir");
            logger::print_verbose("Using build directory from config: " + base_build_dir);
        }
    }
    
    // Get the config-specific build directory
    std::filesystem::path build_dir = get_build_dir_for_config(base_build_dir, build_config);
    logger::print_verbose("Using build directory: " + build_dir.string());
    
    // If build dir doesn't exist, create it
    if (!std::filesystem::exists(build_dir)) {
        logger::print_status("Creating build directory: " + build_dir.string());
        std::filesystem::create_directories(build_dir);
    }
    
    // Check if CMakeLists.txt exists, if not, generate one
    std::filesystem::path cmakelists_path = project_dir / "CMakeLists.txt";
    if (!std::filesystem::exists(cmakelists_path)) {
        logger::print_status("Generating CMakeLists.txt...");
        
        // Get project details from cforge.toml
        std::string cpp_version = project_config.get_string("project.cpp_standard", "17");
        std::string project_name = project_config.get_string("project.name", "cpp-project");
        
        // Generate CMakeLists.txt
        logger::print_status("Generating CMakeLists.txt from cforge.toml configuration");
        
        if (!generate_cmakelists_from_toml(project_dir, project_config, verbose)) {
            logger::print_error("Failed to generate CMakeLists.txt");
            return false;
        }
        
        logger::print_success("Generated CMakeLists.txt file");
    }
    
    // Run CMake to configure the project
    logger::print_status("Running CMake Configure...");
    
    std::string generator = get_cmake_generator();
    logger::print_verbose("Using CMake generator: " + generator);
    
    // Convert to relative paths for command
    std::filesystem::path relative_build_dir = build_dir.is_absolute() 
        ? std::filesystem::relative(build_dir, project_dir) 
        : build_dir;
    
    std::string cmake_command = "cmake -S . -B " + relative_build_dir.string();
    
    // Add generator
    cmake_command += " -G \"" + generator + "\"";
    
    // Add C++ standard if specified
    std::string cpp_version = project_config.get_string("project.cpp_standard", "");
    if (!cpp_version.empty()) {
        cmake_command += " -DCMAKE_CXX_STANDARD=" + cpp_version;
    }
    
    // Add build type for non-multi-config generators
    if (generator.find("Multi-Config") == std::string::npos) {
        cmake_command += " -DCMAKE_BUILD_TYPE=" + build_config;
    }
    
    // Add extra arguments from cforge.toml if available
    std::string args_key = "build.config." + to_lower_case(build_config) + ".cmake_args";
    if (project_config.has_key(args_key)) {
        std::vector<std::string> extra_args = project_config.get_string_array(args_key);
        for (const auto& arg : extra_args) {
            cmake_command += " " + arg;
        }
    }
    
    // Run cmake command
    logger::print_verbose("Running command: " + cmake_command);
    
    std::string current_dir = std::filesystem::current_path().string();
    logger::print_verbose("Current directory: " + current_dir);
    
    process_result cmake_result = execute_process(
        cmake_command,
        {},
        project_dir.string()
    );
    
    if (!cmake_result.success) {
        logger::print_error("CMake configure failed with exit code: " + std::to_string(cmake_result.exit_code));
        if (!cmake_result.stderr_output.empty()) {
            // Format CMake error output using Rust-like formatting
            std::string formatted_error = format_build_errors(cmake_result.stderr_output);
            // If the formatter didn't handle it (returned the original), print it ourselves
            if (!formatted_error.empty()) {
                logger::print_error(formatted_error);
            }
        }
        if (!cmake_result.stdout_output.empty()) {
            // Format standard output that might contain errors
            std::string formatted_output = format_build_errors(cmake_result.stdout_output);
            // If the formatter didn't handle it (returned the original), print it ourselves
            if (!formatted_output.empty()) {
                logger::print_error(formatted_output);
            }
        }
        return false;
    }
    
    logger::print_success("CMake Configure completed successfully");
    
    // Show the contents of the build directory for debugging
    logger::print_verbose("Contents of build directory after CMake:");
    try {
        for (const auto& entry : std::filesystem::directory_iterator(build_dir)) {
            logger::print_verbose("- " + entry.path().string());
        }
    } catch (const std::exception& ex) {
        logger::print_error("Error listing build directory: " + std::string(ex.what()));
    }
    
    // Verify that we have a CMakeCache.txt to confirm the configuration worked
    std::filesystem::path cmake_cache = build_dir / "CMakeCache.txt";
    if (!std::filesystem::exists(cmake_cache)) {
        logger::print_error("CMake appeared to run, but CMakeCache.txt was not created. This may indicate a configuration error.");
        logger::print_warning("You might need to clean the build directory and try again.");
        
        // Show the contents of the build directory for debugging
        logger::print_verbose("Contents of build directory:");
        
        for (const auto& entry : std::filesystem::directory_iterator(build_dir)) {
            logger::print_verbose("- " + entry.path().string());
        }
        
        return false;
    }
    
    // Run the build command
    logger::print_status("Building project in " + build_config + " mode");
    
    std::string build_command = "cmake --build " + relative_build_dir.string();
    
    // Add configuration for multi-config generators
    if (generator.find("Visual Studio") != std::string::npos || 
        generator.find("Ninja Multi-Config") != std::string::npos) {
        build_command += " --config " + build_config;
    }
    
    // Add target if specified
    if (!target.empty()) {
        build_command += " --target " + target;
    }
    
    // Add parallel jobs
    if (num_jobs > 0) {
        build_command += " --parallel " + std::to_string(num_jobs);
    }
    
    // Add verbose flag if needed
    if (verbose) {
        build_command += " --verbose";
    }
    
    logger::print_verbose("Running build command: " + build_command);
    
    process_result build_result = execute_process(
        build_command,
        {},
        project_dir.string()
    );
    
    // Handle case where build errors are in stdout instead of stderr
    if (!build_result.success && build_result.stderr_output.empty() && !build_result.stdout_output.empty()) {
        logger::print_verbose("Build stderr is empty but stdout has content, checking stdout for errors");
        build_result.stderr_output = build_result.stdout_output;
    }
    
    if (!build_result.success) {
        logger::print_error("Build failed with exit code: " + std::to_string(build_result.exit_code));
        if (!build_result.stderr_output.empty()) {
            // Format build error output using Rust-like formatting
            std::string formatted_error = format_build_errors(build_result.stderr_output);
            // If the formatter didn't handle it (returned the original), print it ourselves
            if (!formatted_error.empty()) {
                logger::print_error(formatted_error);
            }
        } else {
            logger::print_error("No error output was captured from the build process.");
        }
        return false;
    }
    
    logger::print_success("Build completed successfully");
    return true;
}

/**
 * @brief Build a workspace project
 * 
 * @param workspace_dir Workspace directory
 * @param project Project to build
 * @param build_config Build configuration
 * @param num_jobs Number of parallel jobs
 * @param verbose Verbose output
 * @param target Optional target to build
 * @return bool Success flag
 */
static bool build_workspace_project(
    const std::filesystem::path& workspace_dir,
    const workspace_project& project,
    const std::string& build_config,
    int num_jobs,
    bool verbose,
    const std::string& target)
{
    // Change to project directory
    std::filesystem::current_path(project.path);
    
    // Load project configuration
    toml_reader project_config;
    std::filesystem::path config_path = project.path / CFORGE_FILE;
    if (!project_config.load(config_path.string())) {
        logger::print_error("Failed to load project configuration for '" + project.name + "'");
        return false;
    }
    
    // Determine build directory
    std::string base_build_dir;
    if (project_config.has_key("build.build_dir")) {
        base_build_dir = project_config.get_string("build.build_dir");
    } else {
        base_build_dir = "build";
    }
    
    // Get the config-specific build directory
    std::filesystem::path build_dir = get_build_dir_for_config(base_build_dir, build_config);
    
    // Build the project
    logger::print_status("Building project '" + project.name + "'...");
    bool success = build_project(workspace_dir, build_config, num_jobs, verbose, target);
    
    if (!success) {
        logger::print_error("Failed to build project '" + project.name + "'");
        return false;
    }
    
    return true;
}

/**
 * @brief Handle the 'build' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_build(const cforge_context_t* ctx) {
    try {
        std::filesystem::path project_dir = ctx->working_dir;
        
        // Parse common parameters first
        // Get the build configuration
        std::string build_config = "Release"; // Default
        if (ctx->args.config && strlen(ctx->args.config) > 0) {
            build_config = ctx->args.config;
        }
        
        // Check for --config or -c flag
        if (ctx->args.args) {
            for (int i = 0; i < ctx->args.arg_count; ++i) {
                std::string arg = ctx->args.args[i];
                if ((arg == "--config" || arg == "-c") && i + 1 < ctx->args.arg_count) {
                    build_config = ctx->args.args[i + 1];
                    break;
                }
            }
        }
        
        // Determine verbosity
        bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
        
        // Get number of jobs
        int num_jobs = 0;
        if (ctx->args.args) {
            for (int i = 0; i < ctx->args.arg_count; ++i) {
                if (strcmp(ctx->args.args[i], "-j") == 0 && i + 1 < ctx->args.arg_count) {
                    try {
                        num_jobs = std::stoi(ctx->args.args[i + 1]);
                    } catch (const std::exception&) {
                        // Invalid value, ignore
                    }
                    break;
                } else if (strncmp(ctx->args.args[i], "-j", 2) == 0 && strlen(ctx->args.args[i]) > 2) {
                    // Handle -j4 format (without space)
                    try {
                        num_jobs = std::stoi(ctx->args.args[i] + 2);
                    } catch (const std::exception&) {
                        // Invalid value, ignore
                    }
                    break;
                }
            }
        }
        
        // Get target (if specified)
        std::string target;
        if (ctx->args.args) {
            for (int i = 0; i < ctx->args.arg_count; ++i) {
                if ((strcmp(ctx->args.args[i], "--target") == 0 || 
                     strcmp(ctx->args.args[i], "-t") == 0) && 
                    i + 1 < ctx->args.arg_count) {
                    target = ctx->args.args[i + 1];
                    break;
                }
            }
        }

        // Check for specific project in current context
        std::string specific_project;
        if (ctx->args.args) {
            for (int i = 0; i < ctx->args.arg_count; ++i) {
                if ((strcmp(ctx->args.args[i], "--project") == 0 || 
                     strcmp(ctx->args.args[i], "-p") == 0) && 
                    i + 1 < ctx->args.arg_count) {
                    specific_project = ctx->args.args[i + 1];
                    break;
                }
            }
        }
        
        // Check if this is a workspace
        std::filesystem::path workspace_file = project_dir / WORKSPACE_FILE;
        bool is_workspace = std::filesystem::exists(workspace_file);
        
        if (is_workspace) {
            // Handle workspace build
            logger::print_status("Building in workspace context");
            
            workspace workspace;
            if (!workspace.load(project_dir)) {
                logger::print_error("Failed to load workspace configuration");
                return 1;
            }
            
            // Log workspace info
            logger::print_status("Workspace: " + workspace.get_name());
            logger::print_status("Build configuration: " + build_config);
            
            if (!specific_project.empty()) {
                // Build specific project only
                logger::print_status("Building specific project: " + specific_project);
                
                if (!workspace.build_project(specific_project, build_config, num_jobs, verbose)) {
                    logger::print_error("Failed to build project: " + specific_project);
                    return 1;
                }
                
                logger::print_success("Project '" + specific_project + "' built successfully");
                return 0;
            } else {
                // Build all projects in workspace
                logger::print_status("Building all projects in workspace");
                
                if (!workspace.build_all(build_config, num_jobs, verbose)) {
                    logger::print_error("Failed to build all projects in workspace");
                    return 1;
                }
                
                logger::print_success("All projects in workspace built successfully");
                return 0;
            }
        } else {
            // Handle single project build
            logger::print_status("Building in single project context");
            
            // Check if cforge.toml exists
            std::filesystem::path config_path = project_dir / CFORGE_FILE;
            if (!std::filesystem::exists(config_path)) {
                logger::print_error("Not a valid cforge project (missing " + std::string(CFORGE_FILE) + ")");
                return 1;
            }
            
            // Load project configuration
            toml_reader project_config;
            if (!project_config.load(config_path.string())) {
                logger::print_error("Failed to parse " + std::string(CFORGE_FILE));
                return 1;
            }
            
            // Get project name
            std::string project_name = project_config.get_string("project.name", "");
            if (project_name.empty()) {
                project_name = std::filesystem::path(project_dir).filename().string();
            }
            
            // Log project info
            logger::print_status("Project: " + project_name);
            logger::print_status("Build configuration: " + build_config);
            
            // Determine build directory
            std::string base_build_dir;
            
            // Check command line arguments first for build directory
            if (ctx->args.args) {
                for (int i = 0; i < ctx->args.arg_count; ++i) {
                    if ((strcmp(ctx->args.args[i], "--build-dir") == 0 || 
                         strcmp(ctx->args.args[i], "-B") == 0) && 
                        i + 1 < ctx->args.arg_count) {
                        base_build_dir = ctx->args.args[i + 1];
                        break;
                    }
                }
            }
            
            // Then check project configuration
            if (base_build_dir.empty() && project_config.has_key("build.build_dir")) {
                base_build_dir = project_config.get_string("build.build_dir");
            } 
            
            // Default to "build"
            if (base_build_dir.empty()) {
                base_build_dir = "build";
            }
            
            // Get the config-specific build directory
            std::filesystem::path build_dir = get_build_dir_for_config(base_build_dir, build_config);
            
            // Build the project
            logger::print_status("Building project...");
            
            if (!build_project(project_dir, build_config, num_jobs, verbose, target)) {
                logger::print_error("Build failed");
                return 1;
            }
            
            logger::print_success("Project built successfully");
            return 0;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception during build: " + std::string(ex.what()));
        return 1;
    }
    
    return 0;
} 