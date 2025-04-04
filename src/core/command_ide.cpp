/**
 * @file command_ide.cpp
 * @brief Implementation of the 'ide' command to generate IDE project files
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "cforge/log.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace cforge;

/**
 * @brief Generate Visual Studio project files using CMake
 * 
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_vs_project(
    const std::filesystem::path& project_dir,
    const std::filesystem::path& build_dir,
    bool verbose
) {
    logger::print_status("Generating Visual Studio project files...");
    
    // Create build directory if it doesn't exist
    if (!std::filesystem::exists(build_dir)) {
        try {
            std::filesystem::create_directories(build_dir);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create build directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // Run CMake to generate Visual Studio project files
    std::vector<std::string> cmake_args = {
        "-B", build_dir.string(),
        "-S", project_dir.string(),
        "-G", "Visual Studio 17 2022",
        "-A", "x64"
    };
    
    bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);
    
    if (success) {
        logger::print_success("Visual Studio project files generated successfully");
        logger::print_status("Open " + build_dir.string() + "/*.sln to start working with the project");
    } else {
        logger::print_error("Failed to generate Visual Studio project files");
    }
    
    return success;
}

/**
 * @brief Generate CodeBlocks project files using CMake
 * 
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_codeblocks_project(
    const std::filesystem::path& project_dir,
    const std::filesystem::path& build_dir,
    bool verbose
) {
    logger::print_status("Generating CodeBlocks project files...");
    
    // Create build directory if it doesn't exist
    if (!std::filesystem::exists(build_dir)) {
        try {
            std::filesystem::create_directories(build_dir);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create build directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // Run CMake to generate CodeBlocks project files
    std::vector<std::string> cmake_args = {
        "-B", build_dir.string(),
        "-S", project_dir.string(),
        "-G", "CodeBlocks - Ninja"
    };
    
    bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);
    
    if (success) {
        logger::print_success("CodeBlocks project files generated successfully");
        logger::print_status("Open " + build_dir.string() + "/*.cbp to start working with the project");
    } else {
        logger::print_error("Failed to generate CodeBlocks project files");
    }
    
    return success;
}

/**
 * @brief Generate Xcode project files using CMake
 * 
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_xcode_project(
    const std::filesystem::path& project_dir,
    const std::filesystem::path& build_dir,
    bool verbose
) {
    logger::print_status("Generating Xcode project files...");
    
    // Create build directory if it doesn't exist
    if (!std::filesystem::exists(build_dir)) {
        try {
            std::filesystem::create_directories(build_dir);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create build directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // Run CMake to generate Xcode project files
    std::vector<std::string> cmake_args = {
        "-B", build_dir.string(),
        "-S", project_dir.string(),
        "-G", "Xcode"
    };
    
    bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);
    
    if (success) {
        logger::print_success("Xcode project files generated successfully");
        logger::print_status("Open " + build_dir.string() + "/*.xcodeproj to start working with the project");
    } else {
        logger::print_error("Failed to generate Xcode project files");
    }
    
    return success;
}

/**
 * @brief Generate CLion project files using CMake
 * 
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_clion_project(
    const std::filesystem::path& project_dir,
    const std::filesystem::path& build_dir,
    bool verbose
) {
    logger::print_status("Setting up project for CLion...");
    
    // Create build directory if it doesn't exist
    if (!std::filesystem::exists(build_dir)) {
        try {
            std::filesystem::create_directories(build_dir);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create build directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // CLion uses CMake directly, just need to run CMake once to generate cache
    std::vector<std::string> cmake_args = {
        "-B", build_dir.string(),
        "-S", project_dir.string()
    };
    
    bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);
    
    if (success) {
        logger::print_success("Project set up for CLion successfully");
        logger::print_status("Open the project root directory in CLion");
    } else {
        logger::print_error("Failed to set up project for CLion");
    }
    
    return success;
}

/**
 * @brief Handle the 'ide' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_ide(const cforge_context_t* ctx) {
    // Determine project directory
    std::filesystem::path project_dir = ctx->working_dir;
    
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
    
    // Get build directory from configuration or use default
    std::string build_dir_name = project_config.get_string("build.build_dir", "build");
    std::filesystem::path build_dir = project_dir / build_dir_name / "ide";
    
    // Determine verbosity
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    
    // Get IDE type from arguments
    std::string ide_type;
    if (ctx->args.args && ctx->args.args[0]) {
        ide_type = ctx->args.args[0];
    }
    
    // If IDE type is not specified, detect based on platform
    if (ide_type.empty()) {
#ifdef _WIN32
        ide_type = "vs";
#elif defined(__APPLE__)
        ide_type = "xcode";
#else
        ide_type = "codeblocks";
#endif
    }
    
    // Generate project files based on IDE type
    bool success = false;
    
    if (ide_type == "vs" || ide_type == "visual-studio") {
        success = generate_vs_project(project_dir, build_dir, verbose);
    }
    else if (ide_type == "cb" || ide_type == "codeblocks") {
        success = generate_codeblocks_project(project_dir, build_dir, verbose);
    }
    else if (ide_type == "xcode") {
        success = generate_xcode_project(project_dir, build_dir, verbose);
    }
    else if (ide_type == "clion") {
        success = generate_clion_project(project_dir, build_dir, verbose);
    }
    else {
        logger::print_error("Unknown IDE type: " + ide_type);
        logger::print_status("Available IDE types: vs (Visual Studio), cb (CodeBlocks), xcode, clion");
        return 1;
    }
    
    return success ? 0 : 1;
} 