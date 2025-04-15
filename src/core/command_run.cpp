/**
 * @file command_run.cpp
 * @brief Enhanced implementation of the 'run' command with proper workspace support
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
#include <algorithm>
#include <fstream>

using namespace cforge;

/**
 * @brief Get build directory path based on base directory and configuration
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
 * @brief Find the executable file for a project
 * 
 * @param project_path Path to the project directory
 * @param build_dir Build directory name
 * @param config Build configuration
 * @param project_name Project name
 * @return std::filesystem::path Path to executable, empty if not found
 */
static std::filesystem::path find_project_executable(
    const std::filesystem::path& project_path,
    const std::string& build_dir,
    const std::string& config,
    const std::string& project_name)
{
    logger::print_verbose("Searching for executable for project: " + project_name);
    logger::print_verbose("Project path: " + project_path.string());
    logger::print_verbose("Build directory: " + build_dir);
    logger::print_verbose("Configuration: " + config);
    
    // Convert config to lowercase for directory matching
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
    
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
        project_path / "bin" / config_lower
    };
    
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
    for (auto& pattern : executable_patterns) {
        pattern += ".exe";
    }
#endif
    
    // Function to check if a file is a valid executable
    auto is_valid_executable = [](const std::filesystem::path& path) -> bool {
        try {
#ifdef _WIN32
            return path.extension() == ".exe";
#else
            return (std::filesystem::status(path).permissions() & 
                    std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
#endif
        } catch (const std::exception& ex) {
            logger::print_verbose("Error checking executable permissions: " + std::string(ex.what()));
            return false;
        }
    };
    
    // Function to check if an executable is likely a project executable (not a CMake/test executable)
    auto is_likely_project_executable = [&project_name](const std::filesystem::path& path) -> bool {
        std::string filename = path.filename().string();
        std::string filename_lower = filename;
        std::transform(filename_lower.begin(), filename_lower.end(), filename_lower.begin(), ::tolower);
        
        // Skip CMake/test executables
        if (filename_lower.find("cmake") != std::string::npos || 
            filename_lower.find("compile") != std::string::npos ||
            filename_lower.find("test") != std::string::npos) {
            return false;
        }
        
        // Project name should be part of the executable name
        std::string project_name_lower = project_name;
        std::transform(project_name_lower.begin(), project_name_lower.end(), 
                      project_name_lower.begin(), ::tolower);
        
        return filename_lower.find(project_name_lower) != std::string::npos;
    };
    
    // Search for exact matches first
    for (const auto& search_path : search_paths) {
        if (!std::filesystem::exists(search_path)) {
            continue;
        }
        
        logger::print_verbose("Searching in: " + search_path.string());
        
        for (const auto& pattern : executable_patterns) {
            std::filesystem::path exe_path = search_path / pattern;
            if (std::filesystem::exists(exe_path) && is_valid_executable(exe_path)) {
                logger::print_status("Found executable: " + exe_path.string());
                return exe_path;
            }
        }
    }
    
    // If exact match not found, search directories for executables with similar names
    for (const auto& search_path : search_paths) {
        if (!std::filesystem::exists(search_path)) {
            continue;
        }
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator(search_path)) {
                if (!is_valid_executable(entry.path())) {
                    continue;
                }
                
                if (is_likely_project_executable(entry.path())) {
                    logger::print_status("Found executable: " + entry.path().string());
                    return entry.path();
                }
            }
        } catch (const std::exception& ex) {
            logger::print_verbose("Error scanning directory: " + search_path.string() + " - " + std::string(ex.what()));
        }
    }
    
    // Final attempt: recursive search in build directory
    logger::print_status("Performing recursive search for executable in: " + (project_path / build_dir).string());
    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(project_path / build_dir)) {
            if (!is_valid_executable(entry.path())) {
                continue;
            }
            
            if (is_likely_project_executable(entry.path())) {
                logger::print_status("Found executable in recursive search: " + entry.path().string());
                return entry.path();
            }
        }
    } catch (const std::exception& ex) {
        logger::print_verbose("Error in recursive search: " + std::string(ex.what()));
    }
    
    // List all valid executables found for debugging
    logger::print_error("No matching executable found for project: " + project_name);
    logger::print_status("Listing all executables found:");
    int found_count = 0;
    
    for (const auto& search_path : search_paths) {
        if (!std::filesystem::exists(search_path)) {
            continue;
        }
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator(search_path)) {
                if (is_valid_executable(entry.path())) {
                    logger::print_status("  - " + entry.path().string());
                    found_count++;
                }
            }
        } catch (...) {}
    }
    
    if (found_count == 0) {
        logger::print_status("No executables found. The project might not have been built correctly.");
    }
    
    return std::filesystem::path();
}

/**
 * @brief Get build configuration from various sources
 */
static std::string get_build_config(
    const cforge_context_t* ctx,
    const toml_reader* project_config) 
{
    // Priority 1: Direct configuration argument
    if (ctx->args.config != nullptr && strlen(ctx->args.config) > 0) {
        logger::print_verbose("Using build configuration from direct argument: " + std::string(ctx->args.config));
        return std::string(ctx->args.config);
    }
    
    // Priority 2: Command line argument
    if (ctx->args.args) {
        for (int i = 0; i < ctx->args.arg_count; ++i) {
            if (strcmp(ctx->args.args[i], "--config") == 0 || 
                strcmp(ctx->args.args[i], "-c") == 0) {
                if (i+1 < ctx->args.arg_count) {
                    logger::print_verbose("Using build configuration from command line: " + std::string(ctx->args.args[i+1]));
                    return std::string(ctx->args.args[i+1]);
                }
            }
        }
    }
    
    // Priority 3: Configuration from cforge.toml
    if (project_config) {
        std::string config = project_config->get_string("build.build_type", "");
        if (!config.empty()) {
            logger::print_verbose("Using build configuration from cforge.toml: " + config);
            return config;
        }
    }
    
    // Priority 4: Default to Release
    logger::print_verbose("No build configuration specified, defaulting to Release");
    return "Release";
}

/**
 * @brief Build a project before running it
 */
static bool build_project_for_run(
    const std::filesystem::path& project_dir,
    const std::string& config,
    bool verbose) 
{
    std::string build_cmd = "cmake";
    
    // First check if CMakeLists.txt exists
    if (!std::filesystem::exists(project_dir / "CMakeLists.txt")) {
        logger::print_error("CMakeLists.txt not found in project directory");
        return false;
    }
    
    // Create build directory if it doesn't exist
    std::filesystem::path build_dir = project_dir / "build";
    if (!std::filesystem::exists(build_dir)) {
        try {
            std::filesystem::create_directories(build_dir);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create build directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // Configure the project
    logger::print_status("Configuring project...");
    std::vector<std::string> config_args = {
        "-S", project_dir.string(),
        "-B", build_dir.string(),
        "-DCMAKE_BUILD_TYPE=" + config
    };
    
    bool config_success = execute_tool(build_cmd, config_args, "", "CMake Configure", verbose);
    if (!config_success) {
        logger::print_error("Failed to configure project");
        return false;
    }
    
    // Build the project
    logger::print_status("Building project...");
    std::vector<std::string> build_args = {
        "--build", build_dir.string(),
        "--config", config
    };
    
    bool build_success = execute_tool(build_cmd, build_args, "", "CMake Build", verbose);
    if (!build_success) {
        logger::print_error("Failed to build project");
        return false;
    }
    
    logger::print_success("Project built successfully");
    return true;
}

cforge_int_t cforge_cmd_run(const cforge_context_t* ctx) {
    try {
        // Determine project directory
        std::filesystem::path project_dir = ctx->working_dir;
        
        // Check if this is a workspace
        std::filesystem::path workspace_file = project_dir / WORKSPACE_FILE;
        bool is_workspace = std::filesystem::exists(workspace_file);
        
        if (is_workspace) {
            // Handle workspace run
            workspace workspace_obj;
            if (!workspace_obj.load(project_dir)) {
                logger::print_error("Failed to load workspace configuration");
                return 1;
            }
            
            logger::print_status("Running project in workspace '" + workspace_obj.get_name() + "'");
            
            // Check if a specific project was requested
            std::string project_to_run;
            if (ctx->args.project) {
                project_to_run = ctx->args.project;
            } else if (ctx->args.args) {
                for (int i = 0; i < ctx->args.arg_count; ++i) {
                    if (strcmp(ctx->args.args[i], "--project") == 0 || 
                        strcmp(ctx->args.args[i], "-p") == 0) {
                        if (i+1 < ctx->args.arg_count) {
                            project_to_run = ctx->args.args[i+1];
                            break;
                        }
                    }
                }
            }
            
            // Get the configuration to build/run
            std::string config = ctx->args.config ? ctx->args.config : "Release";
            if (ctx->args.args) {
                for (int i = 0; i < ctx->args.arg_count; ++i) {
                    if (strcmp(ctx->args.args[i], "--config") == 0 || 
                        strcmp(ctx->args.args[i], "-c") == 0) {
                        if (i+1 < ctx->args.arg_count) {
                            config = ctx->args.args[i+1];
                            break;
                        }
                    }
                }
            }
            
            // Check verbosity
            bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
            
            // Get extra arguments to pass to the executable
            std::vector<std::string> run_args;
            bool passing_args = false;
            if (ctx->args.args) {
                for (int i = 0; i < ctx->args.arg_count; ++i) {
                    if (passing_args) {
                        run_args.push_back(ctx->args.args[i]);
                        continue;
                    }
                    
                    if (strcmp(ctx->args.args[i], "--") == 0) {
                        // Start passing all remaining args to the executable
                        passing_args = true;
                    }
                }
            }
            
            // Run the project
            if (project_to_run.empty()) {
                // Run the startup project
                return workspace_obj.run_startup_project(run_args, config, verbose) ? 0 : 1;
            } else {
                // Run a specific project
                return workspace_obj.run_project(project_to_run, run_args, config, verbose) ? 0 : 1;
            }
        } else {
            // Handle single project run
            
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
                // Use directory name as fallback
                project_name = project_dir.filename().string();
            }
            
            // Get the configuration
            std::string config = get_build_config(ctx, &project_config);
            
            // Get build directory
            std::string build_dir_name = project_config.get_string("build.build_dir", "build");
            
            // Check if we should skip building
            bool skip_build = false;
            if (ctx->args.args) {
                for (int i = 0; i < ctx->args.arg_count; ++i) {
                    if (strcmp(ctx->args.args[i], "--no-build") == 0) {
                        skip_build = true;
                        break;
                    }
                }
            }
            
            // Check verbosity
            bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
            
            // Build the project if needed
            if (!skip_build) {
                if (!build_project_for_run(project_dir, config, verbose)) {
                    logger::print_error("Failed to build project");
                    return 1;
                }
            } else {
                logger::print_status("Skipping build step");
            }
            
            // Find the executable
            std::filesystem::path executable = find_project_executable(
                project_dir, build_dir_name, config, project_name);
            
            if (executable.empty()) {
                logger::print_error("Failed to find executable for project: " + project_name);
                logger::print_status("Make sure the project is built correctly");
                return 1;
            }
            
            // Get extra arguments to pass to the executable
            std::vector<std::string> run_args;
            bool passing_args = false;
            if (ctx->args.args) {
                for (int i = 0; i < ctx->args.arg_count; ++i) {
                    if (passing_args) {
                        run_args.push_back(ctx->args.args[i]);
                        continue;
                    }
                    
                    if (strcmp(ctx->args.args[i], "--") == 0) {
                        // Start passing all remaining args to the executable
                        passing_args = true;
                    }
                }
            }
            
            // Run the executable
            logger::print_status("Running: " + executable.filename().string());
            if (!run_args.empty()) {
                std::string args_str;
                for (const auto& arg : run_args) {
                    if (!args_str.empty()) args_str += " ";
                    args_str += arg;
                }
                logger::print_status("Arguments: " + args_str);
            }
            
            bool result = execute_tool(
                executable.string(), 
                run_args, 
                project_dir.string(), 
                "Project " + project_name, 
                verbose, 
                0  // No timeout
            );
            
            if (!result) {
                logger::print_error("Program execution failed");
                return 1;
            }
            
            logger::print_success("Program executed successfully");
            return 0;
        }
    } catch (const std::exception& ex) {
        logger::print_error("Exception during command execution: " + std::string(ex.what()));
        return 1;
    }
}