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
    // Build the cpack command
    logger::print_status("Creating packages with CPack...");
    
    // Split the command into command and arguments
    std::string command = "cpack";
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
    
    auto result = execute_tool(command, cpack_args, build_dir.string(), "CPack", verbose);
    
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
    
    // Check if packaging is enabled
    bool packaging_enabled = config.get_bool("package.enabled", true);
    
    if (!packaging_enabled) {
        logger::print_status("Packaging is disabled in the project configuration");
        return 0;
    }
    
    // Get base build directory
    std::string base_build_dir = config.get_string("build.build_dir", "build");
    
    // Get build type/configuration
    std::string build_config = config.get_string("build.build_type", "Release");
    
    // Check for config in command line args
    if (ctx->args.args) {
        for (int i = 0; ctx->args.args[i]; ++i) {
            if (strcmp(ctx->args.args[i], "--config") == 0 || 
                strcmp(ctx->args.args[i], "-c") == 0) {
                if (ctx->args.args[i+1]) {
                    build_config = ctx->args.args[i+1];
                    break;
                }
            }
        }
    }
    
    // Get the config-specific build directory
    std::filesystem::path build_dir = get_build_dir_for_config(base_build_dir, build_config);
    
    // If build directory doesn't exist, build the project first
    if (!std::filesystem::exists(build_dir)) {
        logger::print_status("Build directory not found, building project first...");
        if (!build_project(ctx)) {
            logger::print_error("Failed to build the project");
            return 1;
        }
    }
    
    // Get verbose flag
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    
    // Get generators from configuration if specified
    std::vector<std::string> generators = config.get_string_array("package.generators");
    
    // Otherwise detect platform-specific generators
    if (generators.empty()) {
        #if defined(_WIN32)
            generators.push_back("ZIP");
            generators.push_back("NSIS");
        #elif defined(__APPLE__)
            generators.push_back("TGZ");
            generators.push_back("DragNDrop");
        #else
            generators.push_back("TGZ");
            generators.push_back("DEB");
        #endif
    }
    
    // Run CPack to create packages
    if (run_cpack(build_dir, generators, build_config, verbose)) {
        logger::print_success("Packages created successfully");
        return 0;
    }
    
    return 1;
} 