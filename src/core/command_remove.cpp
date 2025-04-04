/**
 * @file command_remove.cpp
 * @brief Implementation of the 'remove' command to remove components from a project
 */

#include "core/commands.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "cforge/log.hpp"
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

using namespace cforge;

/**
 * @brief Remove a dependency from the project configuration
 * 
 * @param config_file Path to the configuration file
 * @param package_name Name of the package to remove
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool remove_dependency_from_config(
    const std::filesystem::path& config_file,
    const std::string& package_name,
    bool verbose
) {
    // Read existing config file
    std::string content;
    std::ifstream file(config_file);
    if (!file) {
        logger::print_error("Failed to read configuration file: " + config_file.string());
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    file.close();

    // Create a regex pattern to match the dependency entry
    std::regex pattern("^\\s*" + package_name + "\\s*=\\s*\"[^\"]*\"\\s*$");
                      
    // Replace the entry with an empty string
    std::string result;
    std::string line;
    std::istringstream iss(content);
    
    while (std::getline(iss, line)) {
        std::regex line_pattern("\\s*" + package_name + "\\s*=\\s*\"[^\"]*\"\\s*");
        if (!std::regex_match(line, line_pattern)) {
            result += line + "\n";
        }
    }
    
    // Check if the content was changed
    if (result == content) {
        logger::print_warning("Dependency '" + package_name + "' not found in configuration file");
        return false;
    }
    
    // Remove empty dependency section if needed
    std::istringstream iss_result(result);
    std::string line_result;
    bool in_dependencies_section = false;
    bool dependencies_section_empty = true;
    std::stringstream cleaned;
    
    while (std::getline(iss_result, line_result)) {
        // Check for section header
        if (line_result.find("[dependencies]") != std::string::npos) {
            in_dependencies_section = true;
            dependencies_section_empty = true;
            
            // Don't add the section yet, wait to see if it's empty
            continue;
        } 
        else if (in_dependencies_section && line_result.find("[") != std::string::npos) {
            // New section, end of dependencies
            in_dependencies_section = false;
            
            // If dependencies section was empty, don't add it
            if (!dependencies_section_empty) {
                cleaned << "[dependencies]" << std::endl;
            }
        }
        
        // Skip empty lines in dependencies section
        if (in_dependencies_section && !line_result.empty() && line_result.find_first_not_of(" \t\r\n") != std::string::npos) {
            dependencies_section_empty = false;
            cleaned << line_result << std::endl;
        } 
        else if (!in_dependencies_section) {
            cleaned << line_result << std::endl;
        }
    }
    
    // Write back to file
    std::ofstream outfile(config_file);
    if (!outfile) {
        logger::print_error("Failed to open configuration file for writing: " + config_file.string());
        return false;
    }
    
    outfile << cleaned.str();
    outfile.close();
    
    if (verbose) {
        logger::print_status("Removed dependency: " + package_name);
    }
    
    return true;
}

/**
 * @brief Run vcpkg to remove the package
 * 
 * @param project_dir Directory containing the project
 * @param package_name Name of the package to remove
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool remove_package_with_vcpkg(
    const std::filesystem::path& project_dir,
    const std::string& package_name,
    bool verbose
) {
    // Determine vcpkg executable
    std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
    std::filesystem::path vcpkg_exe;
    
#ifdef _WIN32
    vcpkg_exe = vcpkg_dir / "vcpkg.exe";
#else
    vcpkg_exe = vcpkg_dir / "vcpkg";
#endif
    
    if (!std::filesystem::exists(vcpkg_exe)) {
        logger::print_error("vcpkg not found at: " + vcpkg_exe.string());
        return false;
    }
    
    // Build the command
    std::string command = vcpkg_exe.string();
    std::vector<std::string> args = {"remove", package_name};
    
    // Run the command
    logger::print_status("Removing package: " + package_name);
    
    auto result = execute_process(
        command,
        args,
        "",  // working directory
        [verbose](const std::string& line) {
            if (verbose) {
                logger::print_verbose(line);
            }
        },
        [](const std::string& line) {
            logger::print_error(line);
        }
    );
    
    if (!result.success) {
        logger::print_error("Failed to remove package with vcpkg. Exit code: " + 
                         std::to_string(result.exit_code));
        return false;
    }
    
    return true;
}

/**
 * @brief Handle the 'remove' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_remove(const cforge_context_t* ctx) {
    // Verify we're in a project directory
    std::filesystem::path project_dir = ctx->working_dir;
    std::filesystem::path config_file = project_dir / "cforge.toml";
    
    if (!std::filesystem::exists(config_file)) {
        logger::print_error("Not a cforge project directory (cforge.toml not found)");
        logger::print_status("Run 'cforge init' to create a new project");
        return 1;
    }
    
    // Check if package name was provided
    if (!ctx->args.args || !ctx->args.args[0] || ctx->args.args[0][0] == '-') {
        logger::print_error("Package name not specified");
        logger::print_status("Usage: cforge remove <package>");
        return 1;
    }
    
    // Extract package name
    std::string package_name = ctx->args.args[0];
    
    // Check for verbosity
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    
    // Remove from config file
    bool config_success = remove_dependency_from_config(config_file, package_name, verbose);
    if (!config_success) {
        logger::print_warning("Failed to remove dependency from configuration");
        // Continue anyway as we might need to clean up vcpkg
    }
    
    // Remove package with vcpkg
    bool remove_success = remove_package_with_vcpkg(project_dir, package_name, verbose);
    if (!remove_success) {
        logger::print_warning("Failed to remove package with vcpkg");
        logger::print_status("You can try removing manually with 'cforge vcpkg remove " + package_name + "'");
    }
    
    if (config_success || remove_success) {
        logger::print_success("Successfully removed dependency: " + package_name);
        return 0;
    } else {
        logger::print_error("Failed to remove dependency: " + package_name);
        return 1;
    }
} 