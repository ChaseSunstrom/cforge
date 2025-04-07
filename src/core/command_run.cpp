/**
 * @file command_run.cpp
 * @brief Implementation of the 'run' command
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"
#include "cforge/log.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <fstream>

using namespace cforge;

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
 * @brief Get build directory path based on base directory and configuration
 * 
 * @param base_dir Base build directory from configuration
 * @param config Build configuration (Release, Debug, etc.)
 * @return std::filesystem::path The configured build directory
 */
static std::string get_build_dir_for_config(const std::string& base_dir, const std::string& config) {
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
 * @brief Find the executable file in the build directory
 * 
 * @param build_dir Path to build directory
 * @param config Build configuration
 * @param project_name Name of the project
 * @return std::filesystem::path Path to executable, empty if not found
 */
static std::filesystem::path find_executable(
    const std::filesystem::path& build_dir,
    const std::string& config,
    const std::string& project_name
) {
    // Define a helper function to check if a file is a valid executable
    auto is_valid_executable = [&project_name, &config](const std::filesystem::path& path) -> bool {
        std::string filename = path.filename().string();
        
        // Skip known CMake test files and intermediate files
        if (filename.find("CMake") != std::string::npos || 
            filename.find("CompilerId") != std::string::npos ||
            filename.find("intermediate") != std::string::npos ||
            filename.find(".lib") != std::string::npos ||
            filename.find(".pdb") != std::string::npos ||
            filename.find(".exp") != std::string::npos ||
            filename.find(".ilk") != std::string::npos ||
            filename == "a.exe") {  // Skip CMake compiler test
            logger::print_verbose("Skipping CMake or intermediate file: " + filename);
            return false;
        }
        
        // Check if the filename matches the expected naming convention
        std::string lower_filename = filename;
        std::string lower_project = project_name;
        std::string lower_config = config;
        std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);
        std::transform(lower_project.begin(), lower_project.end(), lower_project.begin(), ::tolower);
        std::transform(lower_config.begin(), lower_config.end(), lower_config.begin(), ::tolower);
        
        // Check for exact match with naming convention: project_name + config
        std::string expected_prefix = lower_project + "_" + lower_config;
        if (lower_filename.find(expected_prefix) == 0) {
            logger::print_verbose("Found exact prefix match: " + filename);
            return true;
        }
        
        // Check alternative: project_name_debug for Debug builds
        if (lower_config == "debug" && lower_filename.find(lower_project + "_debug") == 0) {
            logger::print_verbose("Found debug build match: " + filename);
            return true;
        }
        
        // Check for just project name as fallback
        if (lower_filename.find(lower_project) != std::string::npos) {
            logger::print_verbose("Found project name match: " + filename);
            return true;
        }
        
        logger::print_verbose("File doesn't match naming conventions: " + filename);
        return false;
    };
    
    // Generate possible executable names based on different naming conventions
    std::vector<std::string> possible_names;
    
    // Format 1: project_name_config (e.g., test_fixed_hang_release)
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
    
    possible_names.push_back(project_name + "_" + config_lower);
    
    // Format 2: project_name_debug/release (e.g., test_fixed_hang_debug)
    if (config_lower == "debug" || config_lower == "release") {
        possible_names.push_back(project_name + "_" + config_lower);
    }
    
    // Format 3: Just project_name (e.g., test_fixed_hang)
    possible_names.push_back(project_name);
    
    // Add extension for Windows
    #ifdef _WIN32
    for (auto& name : possible_names) {
        name += ".exe";
    }
    #endif
    
    logger::print_verbose("Looking for executable with possible names:");
    for (const auto& name : possible_names) {
        logger::print_verbose("- " + name);
    }
    
    // Define search paths in order of priority
    std::vector<std::filesystem::path> search_paths;
    
    // Check if we're using Ninja Multi-Config
    std::string generator = get_cmake_generator();
    bool is_multi_config = generator.find("Ninja Multi-Config") != std::string::npos;
    
    #ifdef _WIN32
    // Windows-specific search paths
    if (is_multi_config) {
        search_paths.push_back(build_dir / "bin" / config_lower);
        search_paths.push_back(build_dir / config_lower);
    } else {
        search_paths.push_back(build_dir / "bin");
        search_paths.push_back(build_dir);
        search_paths.push_back(build_dir / config_lower);
        search_paths.push_back(build_dir / "bin" / config_lower);
    }
    #else
    // Unix-specific search paths
    search_paths.push_back(build_dir / "bin");
    search_paths.push_back(build_dir);
    #endif
    
    logger::print_verbose("Searching in directories:");
    for (const auto& path : search_paths) {
        logger::print_verbose("- " + path.string());
    }
    
    // First, look for all possible filenames in the search paths
    for (const auto& path : search_paths) {
        if (!std::filesystem::exists(path)) {
            logger::print_verbose("Directory doesn't exist: " + path.string());
            continue;
        }
        
        logger::print_status("Searching for executable in: " + path.string());
        
        for (const auto& name : possible_names) {
            std::filesystem::path expected_path = path / name;
            if (std::filesystem::exists(expected_path)) {
                logger::print_status("Found executable with expected name: " + expected_path.string());
                return expected_path;
            }
        }
    }
    
    // If exact match not found, search for valid executables
    for (const auto& path : search_paths) {
        if (!std::filesystem::exists(path)) {
            continue;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                #ifdef _WIN32
                if (entry.path().extension() == ".exe" && is_valid_executable(entry.path())) {
                #else
                if ((entry.permissions(std::filesystem::status(entry.path())) & 
                    std::filesystem::perms::owner_exec) && 
                    is_valid_executable(entry.path())) {
                #endif
                    logger::print_status("Found executable: " + entry.path().string());
                    return entry.path();
                }
            }
        }
    }
    
    // If not found in standard locations, do a recursive search as a last resort
    logger::print_status("Recursively searching for executable in build directory...");
    
    for (const auto& entry : std::filesystem::recursive_directory_iterator(build_dir)) {
        if (entry.is_regular_file()) {
            #ifdef _WIN32
            if (entry.path().extension() == ".exe" && is_valid_executable(entry.path())) {
                logger::print_verbose("Found executable in recursive search: " + entry.path().string());
            #else
            if ((entry.permissions(std::filesystem::status(entry.path())) & 
                std::filesystem::perms::owner_exec) && 
                is_valid_executable(entry.path())) {
                logger::print_verbose("Found executable in recursive search: " + entry.path().string());
            #endif
                logger::print_status("Found executable: " + entry.path().string());
                return entry.path();
            }
        }
    }
    
    // List all executables found to help with debugging
    logger::print_status("No suitable executable found. Found executables:");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(build_dir)) {
        if (entry.is_regular_file()) {
            #ifdef _WIN32
            if (entry.path().extension() == ".exe") {
                logger::print_status("- " + entry.path().string());
            #else
            if ((entry.permissions(std::filesystem::status(entry.path())) & 
                std::filesystem::perms::owner_exec)) {
                logger::print_status("- " + entry.path().string());
            #endif
            }
        }
    }
    
    return std::filesystem::path();
}

/**
 * @brief Get build configuration
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
 * @brief Run a workspace project
 * 
 * @param workspace_dir Workspace directory
 * @param project Project to run
 * @param build_config Build configuration
 * @param verbose Verbose output
 * @return cforge_int_t Exit code
 */
static cforge_int_t run_workspace_project(
    const std::filesystem::path& workspace_dir,
    const workspace_project& project,
    const std::string& build_config,
    bool verbose)
{
    // Change to project directory
    std::filesystem::current_path(project.path);
    
    // Load project configuration
    toml_reader project_config;
    std::filesystem::path config_path = project.path / CFORGE_FILE;
    if (!project_config.load(config_path.string())) {
        logger::print_error("Failed to load project configuration for '" + project.name + "'");
        return 1;
    }
    
    // Create a context for building
    cforge_context_t build_ctx = {};
    
    // Copy the path to working_dir
    strncpy(build_ctx.working_dir, project.path.string().c_str(), sizeof(build_ctx.working_dir) - 1);
    build_ctx.working_dir[sizeof(build_ctx.working_dir) - 1] = '\0';

    // Create a non-const copy of the configuration string
    char* config_copy = strdup(build_config.c_str());
    if (!config_copy) {
        logger::print_error("Failed to allocate memory for build configuration");
        return 1;
    }
    build_ctx.args.config = config_copy;
    
    // Build the project first
    logger::print_status("Building project '" + project.name + "' before running...");
    int build_result = cforge_cmd_build(&build_ctx);
    
    // Free the allocated string
    free(config_copy);
    
    if (build_result != 0) {
        logger::print_error("Failed to build project '" + project.name + "'");
        return 1;
    }
    
    // Determine build directory
    std::string base_build_dir;
    if (project_config.has_key("build.build_dir")) {
        base_build_dir = project_config.get_string("build.build_dir");
    } else {
        base_build_dir = "build";
    }
    
    // Get the config-specific build directory
    std::string build_dir = get_build_dir_for_config(base_build_dir, build_config);
    
    // Find the executable
    logger::print_status("Searching for executable in: " + build_dir);
    
    std::filesystem::path executable;
    try {
        executable = find_executable(build_dir, build_config, project.name);
    } catch (const std::exception& e) {
        logger::print_error("Exception while searching for executable: " + std::string(e.what()));
        return 1;
    }
    
    if (executable.empty()) {
        logger::print_error("Could not find executable for project: " + project.name);
        return 1;
    }
    
    logger::print_status("Executable found: " + executable.string());
    
    // Verify the executable exists and is valid
    if (!std::filesystem::exists(executable)) {
        logger::print_error("Executable was found but doesn't exist on disk: " + executable.string());
        return 1;
    }
    
    // Run the executable
    logger::print_status("Running executable: " + executable.filename().string());
    
    try {
        process_result result = execute_process(
            executable.string(), 
            {}, // No additional arguments for now
            project.path.string()
        );
        
        if (result.success) {
            logger::print_success("Program completed with exit code: 0");
        } else {
            logger::print_error("Program failed with exit code: " + std::to_string(result.exit_code));
            
            if (!result.stderr_output.empty()) {
                std::istringstream error_stream(result.stderr_output);
                std::string line;
                while (std::getline(error_stream, line)) {
                    if (!line.empty()) {
                        logger::print_error(line);
                    }
                }
            }
        }
        
        return result.exit_code;
    } catch (const std::exception& e) {
        logger::print_error("Exception while running executable: " + std::string(e.what()));
        return 1;
    }
}

cforge_int_t cforge_cmd_run(const cforge_context_t* ctx) {
    try {
        // Determine project directory
        std::filesystem::path project_dir = ctx->working_dir;
        
        // Check if this is a workspace
        std::filesystem::path workspace_config_path = project_dir / "workspace.cforge.toml";
        bool is_workspace = std::filesystem::exists(workspace_config_path);
        
        if (is_workspace) {
            // Load workspace configuration
            workspace_config workspace;
            if (!workspace.load(workspace_config_path.string())) {
                logger::print_error("Failed to load workspace configuration");
                return 1;
            }
            
            logger::print_status("Running project in workspace '" + workspace.get_name() + "'");
            
            // Get build configuration
            std::string build_config = get_build_config(ctx, nullptr);  // We'll use the same config for all projects
            
            // Determine which project to run
            const workspace_project* project_to_run = nullptr;
            
            // Check if a specific project was requested
            if (ctx->args.args) {
                for (int i = 0; ctx->args.args[i]; ++i) {
                    if (strcmp(ctx->args.args[i], "--project") == 0 && ctx->args.args[i+1]) {
                        std::string requested_project = ctx->args.args[i+1];
                        for (const auto& project : workspace.get_projects()) {
                            if (project.name == requested_project) {
                                project_to_run = &project;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
            
            // If no specific project was requested, use the startup project
            if (!project_to_run) {
                project_to_run = workspace.get_startup_project();
            }
            
            // If still no project to run, use the first project
            if (!project_to_run && !workspace.get_projects().empty()) {
                project_to_run = &workspace.get_projects().front();
            }
            
            if (!project_to_run) {
                logger::print_error("No project to run in workspace");
                return 1;
            }
            
            logger::print_status("Running project '" + project_to_run->name + "'...");
            
            // Determine verbosity
            bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
            
            // Run the project
            return run_workspace_project(project_dir, *project_to_run, build_config, verbose);
        } else {
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
            
            // Get build configuration
            std::string build_config = get_build_config(ctx, &project_config);
            
            // Determine build directory
            std::string base_build_dir;
            
            // Check command line arguments first for build directory
            if (ctx->args.args && ctx->args.args[1] && 
                (strcmp(ctx->args.args[0], "--build-dir") == 0 || 
                 strcmp(ctx->args.args[0], "-B") == 0)) {
                base_build_dir = ctx->args.args[1];
            } 
            // Then check project configuration
            else if (project_config.has_key("build.build_dir")) {
                base_build_dir = project_config.get_string("build.build_dir");
            } 
            // Default to "build"
            else {
                base_build_dir = "build";
            }
            
            // Get the config-specific build directory
            std::filesystem::path build_dir = get_build_dir_for_config(base_build_dir, build_config);
            
            // Determine verbosity
            bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
            
            // Run the project
            return run_project(project_dir, build_dir, build_config, verbose);
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception during command execution: " + std::string(ex.what()));
        return 1;
    }
} 