/**
 * @file command_package.cpp
 * @brief Implementation of the 'package' command to create packages for distribution
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "cforge/log.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <thread>
#include <chrono>

using namespace cforge;

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
    // If config is empty or the default "Release", use the base dir as is
    if (config.empty() || config == "Release") {
        return base_dir;
    }
    
    // Otherwise, append the lowercase config name to the build directory
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
    
    // Format: build-config (e.g., build-debug)
    return base_dir + "-" + config_lower;
}

/**
 * @brief Build the project if needed
 * 
 * @param ctx Command context
 * @return bool Success flag
 */
static bool build_project(const cforge_context_t* ctx) {
    return cforge_cmd_build(ctx) == 0;
}

/**
 * @brief Find CPack executable path
 * 
 * @return std::string Full path to CPack executable
 */
static std::string find_cpack_path() {
    // Try to get the path to cmake, which should be in the same directory as cpack
    std::string cpack_path = "cpack";

#ifdef _WIN32
    // On Windows, check common installation paths
    std::vector<std::string> common_paths = {
        "C:\\Program Files\\CMake\\bin\\cpack.exe",
        "C:\\Program Files (x86)\\CMake\\bin\\cpack.exe"
    };

    for (const auto& path : common_paths) {
        if (std::filesystem::exists(path)) {
            logger::print_verbose("Found CPack at: " + path);
            return path;
        }
    }

    // If we couldn't find cpack, try to get path from cmake
    process_result result = execute_process("where", {"cmake"}, "", nullptr, nullptr, 5);
    if (result.success && !result.stdout_output.empty()) {
        // Extract the first line
        std::string cmake_path = result.stdout_output;
        std::string::size_type pos = cmake_path.find('\n');
        if (pos != std::string::npos) {
            cmake_path = cmake_path.substr(0, pos);
        }
        
        // Trim whitespace
        cmake_path.erase(cmake_path.find_last_not_of(" \n\r\t") + 1);
        
        // Replace cmake with cpack in the path
        std::filesystem::path cpack_dir = std::filesystem::path(cmake_path).parent_path();
        std::filesystem::path cpack_exe = cpack_dir / "cpack.exe";
        
        if (std::filesystem::exists(cpack_exe)) {
            logger::print_verbose("Found CPack at: " + cpack_exe.string());
            return cpack_exe.string();
        }
    }
#endif

    // Default to just "cpack" and let the command handle errors
    return cpack_path;
}

/**
 * @brief Download and install NSIS if it's not available
 * 
 * @param verbose Verbose flag
 * @return bool Success flag
 */
static bool download_and_install_nsis(bool verbose) {
#ifdef _WIN32
    logger::print_status("NSIS not found. Attempting to download and install NSIS automatically...");

    // Create temp directory for download
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "cforge_nsis_install";
    if (!std::filesystem::exists(temp_dir)) {
        std::filesystem::create_directories(temp_dir);
    }

    std::filesystem::path nsis_installer = temp_dir / "nsis-installer.exe";

    // Download NSIS installer
    logger::print_status("Downloading NSIS installer...");
    std::vector<std::string> curl_args = {
        "-L",
        "-o", nsis_installer.string(),
        "https://sourceforge.net/projects/nsis/files/NSIS%203/3.08/nsis-3.08-setup.exe/download"
    };

    bool download_success = execute_tool("curl", curl_args, "", "NSIS Download", verbose);
    if (!download_success) {
        logger::print_error("Failed to download NSIS installer");
        return false;
    }

    // Run installer silently
    logger::print_status("Installing NSIS (this may take a moment)...");
    std::vector<std::string> install_args = {
        "/S"  // Silent install
    };

    bool install_success = execute_tool(nsis_installer.string(), install_args, "", "NSIS Install", verbose);
    if (!install_success) {
        logger::print_error("Failed to install NSIS");
        return false;
    }

    logger::print_success("NSIS installed successfully");

    // Wait a moment for installation to complete
    std::this_thread::sleep_for(std::chrono::seconds(3));

    return true;
#else
    logger::print_error("Automatic NSIS installation is only supported on Windows");
    logger::print_status("Please install NSIS manually from http://nsis.sourceforge.net");
    return false;
#endif
}

/**
 * @brief Create packages using CPack
 * 
 * @param build_dir Build directory
 * @param generators CPack generators to use
 * @param config_name Configuration name (e.g., "Release")
 * @param verbose Verbose flag
 * @return bool Success flag
 */
static bool run_cpack(
    const std::filesystem::path& build_dir,
    const std::vector<std::string>& generators,
    const std::string& config_name,
    bool verbose)
{
    // Find cpack executable
    std::string cpack_command = find_cpack_path();
    logger::print_verbose("Using CPack command: " + cpack_command);
    
    // Build the cpack command
    logger::print_status("Creating packages with CPack...");
    
    // Split the command into command and arguments
    std::vector<std::string> cpack_args;
    
    // Add configuration
    if (!config_name.empty()) {
        cpack_args.push_back("-C");
        cpack_args.push_back(config_name);
    }
    
    // Add generators if specified
    if (!generators.empty()) {
        cpack_args.push_back("-G");
        std::string gen_str;
        for (size_t i = 0; i < generators.size(); ++i) {
            if (i > 0) gen_str += ";";
            gen_str += generators[i];
        }
        cpack_args.push_back(gen_str);
    }
    
    // Add verbose flag if needed
    if (verbose) {
        cpack_args.push_back("--verbose");
    }
    
    logger::print_verbose("CPack working directory: " + build_dir.string());
    logger::print_verbose("CPack command: " + cpack_command + " " + 
                       (cpack_args.empty() ? "" : cpack_args[0] + " " +
                        (cpack_args.size() > 1 ? cpack_args[1] : "")));
    
    auto result = execute_tool(cpack_command, cpack_args, build_dir.string(), "CPack", verbose);
    
    // If the command failed with NSIS error, try to install NSIS and run again
    if (!result && (generators.empty() || 
                   std::find(generators.begin(), generators.end(), "NSIS") != generators.end()))
    {
        logger::print_status("CPack failed. Checking if NSIS is installed...");
        
        // Attempt to install NSIS automatically
        if (download_and_install_nsis(verbose)) {
            logger::print_status("Retrying package creation with CPack...");
            result = execute_tool(cpack_command, cpack_args, build_dir.string(), "CPack", verbose);
        }
    }
    
    if (!result) {
        logger::print_error("Failed to create packages with CPack");
        return false;
    }
    
    return true;
}

/**
 * @brief Handle the 'package' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_package(const cforge_context_t* ctx)
{
    logger::print_status("Starting packaging process...");
    
    // Check if the project file exists
    if (!std::filesystem::exists(CFORGE_FILE)) {
        logger::print_error("No " + std::string(CFORGE_FILE) + " file found in the current directory");
        return 1;
    }
    
    // Load project configuration
    toml_reader config;
    if (!config.load(CFORGE_FILE)) {
        logger::print_error("Failed to load " + std::string(CFORGE_FILE) + " file");
        return 1;
    }
    
    // Get project name for verbose logging
    std::string project_name = config.get_string("project.name", "cpp-project");
    logger::print_verbose("Packaging project: " + project_name);
    
    // Check if packaging is enabled
    bool packaging_enabled = config.get_bool("package.enabled", true);
    
    if (!packaging_enabled) {
        logger::print_status("Packaging is disabled in the project configuration");
        return 0;
    }
    
    // Get base build directory
    std::string base_build_dir = config.get_string("build.build_dir", "build");
    logger::print_verbose("Base build directory: " + base_build_dir);
    
    // Get build type/configuration
    std::string build_config = config.get_string("build.build_type", "Release");
    logger::print_verbose("Default build configuration from config: " + build_config);
    
    // Check for config in command line args
    if (ctx->args.config && strlen(ctx->args.config) > 0) {
        build_config = ctx->args.config;
        logger::print_verbose("Using build configuration from argument: " + build_config);
    } else if (ctx->args.args) {
        for (int i = 0; ctx->args.args[i]; ++i) {
            if ((strcmp(ctx->args.args[i], "--config") == 0 || 
                 strcmp(ctx->args.args[i], "-c") == 0) && 
                ctx->args.args[i+1]) {
                build_config = ctx->args.args[i+1];
                logger::print_verbose("Using build configuration from command line: " + build_config);
                break;
            }
        }
    }
    
    logger::print_status("Using build configuration: " + build_config);
    
    // Get the config-specific build directory
    std::filesystem::path build_dir = get_build_dir_for_config(base_build_dir, build_config);
    logger::print_verbose("Config-specific build directory: " + build_dir.string());
    
    // If build directory doesn't exist, build the project first
    if (!std::filesystem::exists(build_dir)) {
        logger::print_status("Build directory not found, building project first...");
        if (!build_project(ctx)) {
            logger::print_error("Failed to build the project");
            return 1;
        }
    }
    
    // Check if CMakeCache.txt exists in the build directory
    if (!std::filesystem::exists(build_dir / "CMakeCache.txt")) {
        logger::print_error("CMakeCache.txt not found in build directory. Project may not be configured properly.");
        logger::print_status("Try running 'cforge build --config " + build_config + "' first.");
        return 1;
    }
    
    // Get verbose flag
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    
    // Get generators from configuration if specified
    std::vector<std::string> generators = config.get_string_array("package.generators");
    
    // Check for specified generators in command line args
    if (ctx->args.args) {
        for (int i = 0; ctx->args.args[i]; ++i) {
            if ((strcmp(ctx->args.args[i], "--type") == 0 || 
                 strcmp(ctx->args.args[i], "-t") == 0) && 
                ctx->args.args[i+1]) {
                // Clear config generators and use command line generator
                generators.clear();
                generators.push_back(ctx->args.args[i+1]);
                break;
            }
        }
    }
    
    // Otherwise detect platform-specific generators
    if (generators.empty()) {
        #if defined(_WIN32)
            generators.push_back("ZIP");
            generators.push_back("NSIS");
            logger::print_verbose("Using default Windows generators: ZIP, NSIS");
        #elif defined(__APPLE__)
            generators.push_back("TGZ");
            generators.push_back("DragNDrop");
            logger::print_verbose("Using default macOS generators: TGZ, DragNDrop");
        #else
            generators.push_back("TGZ");
            generators.push_back("DEB");
            logger::print_verbose("Using default Linux generators: TGZ, DEB");
        #endif
    } else {
        std::string gen_list;
        for (const auto& gen : generators) {
            if (!gen_list.empty()) gen_list += ", ";
            gen_list += gen;
        }
        logger::print_verbose("Using configured generators: " + gen_list);
    }
    
    // Run CPack to create packages
    if (run_cpack(build_dir, generators, build_config, verbose)) {
        logger::print_success("Packages created successfully");
        return 0;
    }
    
    return 1;
} 