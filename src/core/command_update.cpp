/**
 * @file command_update.cpp
 * @brief Implementation of the 'update' command
 */
#include "core/file_system.h"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "cforge/log.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <map>

using namespace cforge;

/**
 * @brief Get dependencies from cforge.toml file
 * 
 * @param config_file Path to the cforge.toml file
 * @return std::map<std::string, std::string> Map of dependencies (name -> version)
 */
static std::map<std::string, std::string> get_dependencies_from_config(
    const std::filesystem::path& config_file
) {
    std::map<std::string, std::string> dependencies;
    
    toml_reader config;
    if (!config.load(config_file.string())) {
        logger::print_warning("Failed to parse configuration file: " + config_file.string());
        return dependencies;
    }
    
    // Get all dependency sections in the TOML file
    std::vector<std::string> dependency_keys = config.get_table_keys("dependencies");
    
    for (const auto& key : dependency_keys) {
        std::string version = config.get_string("dependencies." + key, "*");
        dependencies[key] = version;
    }
    
    return dependencies;
}

/**
 * @brief Update vcpkg itself
 * 
 * @param project_dir Project directory
 * @param verbose Verbose output
 * @return true if successful, false otherwise
 */
static bool update_vcpkg(
    const std::filesystem::path& project_dir,
    bool verbose
) {
    std::filesystem::path vcpkg_dir = project_dir / "vcpkg";
    
    if (!std::filesystem::exists(vcpkg_dir)) {
        logger::print_error("vcpkg not found at: " + vcpkg_dir.string());
        logger::print_status("Run 'cforge vcpkg' to set up vcpkg integration");
        return false;
    }
    
    // Update vcpkg repository
    std::string git_cmd = "git";
    std::vector<std::string> git_args = {
        "pull",
        "--rebase"
    };
    
    logger::print_status("Updating vcpkg...");
    
    auto result = execute_process(
        git_cmd,
        git_args,
        vcpkg_dir.string(),
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
        logger::print_error("Failed to update vcpkg. Exit code: " +
                         std::to_string(result.exit_code));
        return false;
    }
    
    // Run bootstrap again to ensure it's up to date
    std::string bootstrap_cmd;
    std::vector<std::string> bootstrap_args;
    
    #ifdef _WIN32
    bootstrap_cmd = (vcpkg_dir / "bootstrap-vcpkg.bat").string();
    #else
    bootstrap_cmd = (vcpkg_dir / "bootstrap-vcpkg.sh").string();
    bootstrap_args = {"-disableMetrics"};
    #endif
    
    logger::print_status("Running vcpkg bootstrap...");
    
    auto bootstrap_result = execute_process(
        bootstrap_cmd,
        bootstrap_args,
        vcpkg_dir.string(),
        [verbose](const std::string& line) {
            if (verbose) {
                logger::print_verbose(line);
            }
        },
        [](const std::string& line) {
            logger::print_error(line);
        }
    );
    
    if (!bootstrap_result.success) {
        logger::print_error("Failed to bootstrap vcpkg. Exit code: " +
                         std::to_string(bootstrap_result.exit_code));
        return false;
    }
    
    return true;
}

/**
 * @brief Update dependencies with vcpkg
 * 
 * @param project_dir Project directory
 * @param dependencies Map of dependencies
 * @param verbose Verbose output
 * @return true if successful, false otherwise
 */
static bool update_dependencies_with_vcpkg(
    const std::filesystem::path& project_dir,
    const std::map<std::string, std::string>& dependencies,
    bool verbose
) {
    if (dependencies.empty()) {
        logger::print_status("No dependencies to update");
        return true;
    }
    
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
    
    // First, run vcpkg update
    std::string command = vcpkg_exe.string();
    std::vector<std::string> args = {"update"};
    
    logger::print_status("Running vcpkg update...");
    
    auto result = execute_process(
        command,
        args,
        "",
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
        logger::print_warning("vcpkg update failed. Exit code: " +
                        std::to_string(result.exit_code));
        // Continue anyway, might still be able to update packages
    }
    
    // Update each dependency
    bool all_successful = true;
    
    for (const auto& [name, version] : dependencies) {
        std::string package_spec = name;
        if (version != "*") {
            package_spec += ":" + version;
        }
        
        std::vector<std::string> install_args = {"install", package_spec};
        
        logger::print_status("Updating package: " + package_spec);
        
        auto install_result = execute_process(
            command,
            install_args,
            "",
            [verbose](const std::string& line) {
                if (verbose) {
                    logger::print_verbose(line);
                }
            },
            [](const std::string& line) {
                logger::print_error(line);
            }
        );
        
        if (!install_result.success) {
            logger::print_error("Failed to update package with vcpkg. Exit code: " +
                             std::to_string(install_result.exit_code));
            all_successful = false;
        }
    }
    
    return all_successful;
}

/**
 * @brief Handle the 'update' command
 * 
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_update(const cforge_context_t* ctx) {
    // Verify we're in a project directory
    std::filesystem::path project_dir = ctx->working_dir;
    std::filesystem::path config_file = project_dir / "cforge.toml";
    
    if (!std::filesystem::exists(config_file)) {
        logger::print_error("Not a cforge project directory (cforge.toml not found)");
        logger::print_status("Run 'cforge init' to create a new project");
        return 1;
    }
    
    // Check for verbosity
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    
    // Update vcpkg first
    bool vcpkg_updated = update_vcpkg(project_dir, verbose);
    if (!vcpkg_updated) {
        logger::print_warning("Failed to update vcpkg");
    }
    
    // Get dependencies from config
    auto dependencies = get_dependencies_from_config(config_file);
    
    // Update dependencies
    bool dependencies_updated = update_dependencies_with_vcpkg(project_dir, dependencies, verbose);
    
    if (dependencies_updated && vcpkg_updated) {
        logger::print_success("Successfully updated all dependencies");
        return 0;
    } else if (dependencies_updated) {
        logger::print_warning("Updated dependencies but failed to update vcpkg itself");
        return 0;
    } else {
        logger::print_error("Failed to update one or more dependencies");
        return 1;
    }
} 