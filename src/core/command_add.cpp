/**
 * @file command_add.cpp
 * @brief Implementation of the 'add' command to add components to a project
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
 * @brief Add a dependency to the project configuration
 * 
 * @param project_dir Directory containing the project
 * @param config_file Path to the configuration file
 * @param package_name Name of the package to add
 * @param package_version Version of the package (optional)
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool add_dependency_to_config(
    const std::filesystem::path& project_dir,
    const std::filesystem::path& config_file,
    const std::string& package_name,
    const std::string& package_version,
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

    // Check if the dependency section exists
    bool has_dependencies_section = content.find("[dependencies]") != std::string::npos;
    std::string entry;
    
    if (!package_version.empty()) {
        entry = package_name + " = \"" + package_version + "\"";
    } else {
        entry = package_name + " = \"*\"";  // Use * for latest version
    }
    
    std::ofstream outfile(config_file, std::ios::app);
    if (!outfile) {
        logger::print_error("Failed to open configuration file for writing: " + config_file.string());
        return false;
    }
    
    if (!has_dependencies_section) {
        outfile << "\n[dependencies]\n";
    }
    
    outfile << entry << "\n";
    outfile.close();
    
    if (verbose) {
        logger::print_status("Added dependency: " + entry);
    }
    
    return true;
}

/**
 * @brief Run vcpkg to install the package
 * 
 * @param project_dir Directory containing the project
 * @param package_name Name of the package to install
 * @param package_version Version of the package (optional)
 * @param verbose Show verbose output
 * @return true if successful, false otherwise
 */
static bool install_package_with_vcpkg(
    const std::filesystem::path& project_dir,
    const std::string& package_name,
    const std::string& package_version,
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
        logger::print_status("Run 'cforge vcpkg' to set up vcpkg integration");
        return false;
    }
    
    // Prepare the package spec
    std::string package_spec = package_name;
    if (!package_version.empty()) {
        package_spec += ":" + package_version;
    }
    
    // Build the command
    std::string command = vcpkg_exe.string();
    std::vector<std::string> args = {"install", package_spec};
    
    // Run the command
    logger::print_status("Installing package: " + package_spec);
    
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
        logger::print_error("Failed to install package with vcpkg. Exit code: " + 
                         std::to_string(result.exit_code));
        return false;
    }
    
    return true;
}

/**
 * @brief Handle the 'add' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_add(const cforge_context_t* ctx) {
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
        logger::print_status("Usage: cforge add <package>");
        return 1;
    }
    
    // Extract package name and version
    std::string package_arg = ctx->args.args[0];
    std::string package_name;
    std::string package_version;
    
    // Check if package has a version specifier (name:version)
    size_t colon_pos = package_arg.find(':');
    if (colon_pos != std::string::npos) {
        package_name = package_arg.substr(0, colon_pos);
        package_version = package_arg.substr(colon_pos + 1);
    } else {
        package_name = package_arg;
    }
    
    // Check for verbosity
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    
    // Add to config file
    bool config_success = add_dependency_to_config(project_dir, config_file, package_name, package_version, verbose);
    if (!config_success) {
        logger::print_error("Failed to add dependency to configuration");
        return 1;
    }
    
    // Install package with vcpkg
    bool install_success = install_package_with_vcpkg(project_dir, package_name, package_version, verbose);
    if (!install_success) {
        logger::print_warning("Failed to install package with vcpkg");
        logger::print_status("You can try installing manually with 'cforge vcpkg install " + package_name + "'");
    }
    
    logger::print_success("Successfully added dependency: " + package_name);
    return 0;
} 