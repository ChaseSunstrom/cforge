/**
 * @file command_package.cpp
 * @brief Implementation of the 'package' command to create packages for distribution
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
#include <thread>
#include <chrono>
#include <sstream>
#include <set>

using namespace cforge;

#ifdef _WIN32
const char PATH_SEPARATOR = '\\';
#else
const char PATH_SEPARATOR = '/';
#endif

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
    // If config is empty, use the base dir as is
    if (config.empty()) {
        return base_dir;
    }
    
    // If it's the Release config, look for both base and -Release version
    if (config == "Release") {
        std::filesystem::path release_path = std::filesystem::path(base_dir + "-Release");
        if (std::filesystem::exists(release_path)) {
            return release_path;
        }
        return base_dir;
    }
    
    // Otherwise, append the lowercase config name to the build directory
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
    
    // Try common formats - some projects use "-", "/" or "_"
    std::vector<std::filesystem::path> paths_to_try = {
        std::filesystem::path(base_dir + "-" + config_lower),
        std::filesystem::path(base_dir + "_" + config_lower),
        std::filesystem::path(base_dir + "/" + config_lower)
    };
    
    for (const auto& path : paths_to_try) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    
    // Default to "-config" format if none exist
    return std::filesystem::path(base_dir + "-" + config_lower);
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
 * @brief Get platform-specific package generators
 * 
 * @return std::vector<std::string> Default generators for the current platform
 */
static std::vector<std::string> get_default_generators() {
    std::vector<std::string> generators;
    
#if defined(_WIN32)
    generators.push_back("ZIP");
    // Only add NSIS if likely to be available
    if (std::filesystem::exists("C:\\Program Files (x86)\\NSIS") || 
        std::filesystem::exists("C:\\Program Files\\NSIS")) {
        generators.push_back("NSIS");
    }
    logger::print_verbose(std::string("Using default Windows generators: ") + 
                       (generators.size() > 1 ? "ZIP, NSIS" : "ZIP"));
#elif defined(__APPLE__)
    generators.push_back("TGZ");
    generators.push_back("DRAGNDROP");
    logger::print_verbose("Using default macOS generators: TGZ, DRAGNDROP");
#else
    generators.push_back("TGZ");
    // Only add DEB if likely to be available
    if (is_command_available("dpkg-deb") || is_command_available("apt")) {
        generators.push_back("DEB");
    }
    // Only add RPM if likely to be available
    if (is_command_available("rpmbuild") || is_command_available("yum") || 
        is_command_available("dnf")) {
        generators.push_back("RPM");
    }
    logger::print_verbose("Using default Linux generators: TGZ" + 
                       (std::find(generators.begin(), generators.end(), "DEB") != generators.end() ? 
                        ", DEB" : "") +
                       (std::find(generators.begin(), generators.end(), "RPM") != generators.end() ? 
                        ", RPM" : ""));
#endif

    return generators;
}

/**
 * @brief Ensure all generator names are uppercased
 * 
 * @param generators Vector of generator names to process
 * @return std::vector<std::string> Vector with uppercased generator names
 */
static std::vector<std::string> uppercase_generators(const std::vector<std::string>& generators) {
    std::vector<std::string> result;
    for (const auto& gen : generators) {
        std::string upper_gen = gen;
        std::transform(upper_gen.begin(), upper_gen.end(), upper_gen.begin(), ::toupper);
        result.push_back(upper_gen);
    }
    return result;
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

    // Run installer silently - try as admin if possible
    logger::print_status("Installing NSIS (this may take a moment)...");
    
    // First try with normal privileges
    std::vector<std::string> install_args = {
        "/S"  // Silent install
    };

    bool install_success = execute_tool(nsis_installer.string(), install_args, "", "NSIS Install", verbose);
    
    if (!install_success) {
        // Try to run elevated (this might show a UAC prompt)
        logger::print_status("Attempting to install NSIS with administrator privileges...");
        
        std::vector<std::string> runas_args = {
            "/trustlevel:0x20000",
            nsis_installer.string(),
            "/S"
        };
        
        install_success = execute_tool("runas", runas_args, "", "NSIS Install (Admin)", verbose);
    }
    
    if (!install_success) {
        logger::print_error("Failed to install NSIS");
        logger::print_status("Please install NSIS manually from http://nsis.sourceforge.net");
        logger::print_status("After installing NSIS, ensure 'makensis.exe' is in your PATH");
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
 * @brief Check if a file is a package file based on its extension
 * 
 * @param filename Filename to check
 * @return bool True if the file is a package file
 */
static bool is_package_file(const std::string& filename) {
    return filename.find(".zip") != std::string::npos || 
           filename.find(".exe") != std::string::npos || 
           filename.find(".deb") != std::string::npos || 
           filename.find(".rpm") != std::string::npos || 
           filename.find(".dmg") != std::string::npos || 
           filename.find(".tar.gz") != std::string::npos ||
           filename.find(".msi") != std::string::npos;
}

/**
 * @brief Check if a file is a package file based on its path
 * 
 * @param path Path to check
 * @return bool True if the file is a package file
 */
static bool is_package_file(const std::filesystem::path& path) {
    return is_package_file(path.filename().string());
}

/**
 * @brief Search for package files in a directory and its subdirectories
 * 
 * @param dir Directory to search
 * @param recursive Whether to search recursively
 * @return std::vector<std::filesystem::path> Paths to package files found
 */
static std::vector<std::filesystem::path> find_package_files(const std::filesystem::path& dir, bool recursive = true) {
    std::vector<std::filesystem::path> results;
    
    if (!std::filesystem::exists(dir)) {
        return results;
    }
    
    try {
        if (recursive) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (is_package_file(filename)) {
                        // Verify file exists and has size > 0
                        std::error_code ec;
                        if (std::filesystem::exists(entry.path(), ec) && 
                            std::filesystem::file_size(entry.path(), ec) > 0) {
                            results.push_back(entry.path());
                        }
                    }
                }
            }
        } else {
            for (const auto& entry : std::filesystem::directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (is_package_file(filename)) {
                        // Verify file exists and has size > 0
                        std::error_code ec;
                        if (std::filesystem::exists(entry.path(), ec) && 
                            std::filesystem::file_size(entry.path(), ec) > 0) {
                            results.push_back(entry.path());
                        }
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        logger::print_warning("Error searching directory " + dir.string() + ": " + std::string(ex.what()));
    }
    
    return results;
}

/**
 * @brief Check if a package file is a final distribution package (not an intermediate file)
 * 
 * @param path Path to check
 * @return bool True if it's a final distribution package
 */
static bool is_final_package(const std::filesystem::path& path) {
    // Only consider files directly in the packages directory (not in subdirectories)
    std::string path_str = path.string();
    size_t packages_pos = path_str.find("packages" + std::string(1, PATH_SEPARATOR));
    if (packages_pos == std::string::npos) {
        return false;
    }
    
    // Exclude any files in subdirectories of the packages directory
    std::string subpath = path_str.substr(packages_pos + 9); // "packages/" is 9 chars
    if (subpath.find(PATH_SEPARATOR) != std::string::npos) {
        return false;
    }
    
    // Only include specific extensions for distribution packages
    std::string ext = path.extension().string();
    return ext == ".zip" || ext == ".exe" || ext == ".deb" || 
           ext == ".rpm" || ext == ".dmg" || ext == ".msi" || 
           (ext == ".gz" && path.string().find(".tar.gz") != std::string::npos);
}

/**
 * @brief Display only the final distribution packages
 * 
 * @param packages List of all package files found
 * @param config_name Build configuration name for display
 * @param project_name Project name for filtering
 * @return void
 */
static void display_only_final_packages(
    const std::vector<std::filesystem::path>& packages, 
    const std::string& config_name,
    const std::string& project_name)
{
    std::set<std::string> reported_packages;
    bool zip_found = false;
    bool tar_found = false;
    
    // First pass - filter and report packages
    for (const auto& path : packages) {
        std::string path_str = path.string();
        
        // Skip if not in a packages directory
        size_t packages_pos = path_str.find("packages" + std::string(1, PATH_SEPARATOR));
        if (packages_pos == std::string::npos) {
            continue;
        }
        
        // Skip if in a subdirectory
        std::string subpath = path_str.substr(packages_pos + 9); // "packages/" is 9 chars
        if (subpath.find(PATH_SEPARATOR) != std::string::npos) {
            continue;
        }
        
        // Get filename and extension
        std::string filename = path.filename().string();
        std::string ext = path.extension().string();
        
        // Track which package types we've seen
        if (ext == ".zip") zip_found = true;
        if (ext == ".gz" && path_str.find(".tar.gz") != std::string::npos) tar_found = true;
        
        // Skip executables that don't have the project name prefix
        // (to filter out compiler-generated executables like a.exe)
        if ((ext == ".exe" || ext == "") && 
            !project_name.empty() && 
            filename.find(project_name) == std::string::npos) {
            continue;
        }
        
        // Skip executables that are clearly test or utility files
        if (ext == ".exe") {
            if (filename == "a.exe" || 
                filename.find("CMake") != std::string::npos ||
                filename.find("Test") != std::string::npos ||
                filename.find("test") != std::string::npos) {
                continue;
            }
        }
        
        // Only include distribution packages
        bool is_dist_package = ext == ".zip" || ext == ".exe" || ext == ".deb" || 
                               ext == ".rpm" || ext == ".dmg" || ext == ".msi" || 
                               (ext == ".gz" && path_str.find(".tar.gz") != std::string::npos);
        
        if (is_dist_package) {
            // Avoid duplicates
            if (reported_packages.find(filename) == reported_packages.end()) {
                // Check if this is a configuration-specific package
                std::string display_name;
                if (!config_name.empty() && config_name != "Release") {
                    std::string config_lower = config_name;
                    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
                    
                    // Check if the config is already in the filename
                    if (filename.find(config_lower) == std::string::npos) {
                        // Suggest config-specific name next time
                        display_name = path.string() + " [add '-" + config_lower + "' to get config-specific packages]";
                    } else {
                        display_name = path.string();
                    }
                } else {
                    display_name = path.string();
                }
                
                logger::print_success("Package created: " + display_name);
                reported_packages.insert(filename);
            }
        }
    }
    
    if (reported_packages.empty()) {
        logger::print_warning("No distribution packages found. Check CPack output for details.");
    }
    
    // Final summary of packages - log helpful information about missing package types
    if (!zip_found && !tar_found) {
        logger::print_status("Tip: Add 'package.generators = [\"ZIP\"]' to your project config to create ZIP packages.");
    }
}

/**
 * @brief Mimics process execution result structure
 */
struct process_result_t {
    bool success;
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
};

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
    
    // Check if the build directory exists and contains CMakeCache.txt
    if (!std::filesystem::exists(build_dir)) {
        logger::print_error("Build directory does not exist: " + build_dir.string());
        return false;
    }
    
    if (!std::filesystem::exists(build_dir / "CMakeCache.txt")) {
        logger::print_error("CMakeCache.txt not found in build directory. Run 'cforge build' first.");
        return false;
    }
    
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
    
    // Specify a simpler package output path
    std::filesystem::path package_dir = build_dir.parent_path() / "packages";
    
    // Create the package directory if it doesn't exist
    if (!std::filesystem::exists(package_dir)) {
        std::filesystem::create_directories(package_dir);
    }
    
    // Make sure the package directory is absolute
    package_dir = std::filesystem::absolute(package_dir);
    
    // Add package output directory - use both ways for compatibility
    cpack_args.push_back("-B");
    cpack_args.push_back(package_dir.string());
    
    // Also set CPACK_PACKAGE_DIRECTORY which is more reliable
    cpack_args.push_back("--config");
    cpack_args.push_back("CPackConfig.cmake");
    cpack_args.push_back("-D");
    cpack_args.push_back("CPACK_PACKAGE_DIRECTORY=" + package_dir.string());
    
    // Add config to package filename - use proper format
    if (!config_name.empty() && config_name != "Release") {
        std::string config_lower = config_name;
        std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
        
        // CPack variable to customize package filename
        cpack_args.push_back("-D");
        cpack_args.push_back("CPACK_PACKAGE_FILE_NAME=<CPACK_PACKAGE_NAME>-<CPACK_PACKAGE_VERSION>-<CPACK_SYSTEM_NAME>-" + config_lower);
    }
    
    // Add verbose flag if needed
    if (verbose) {
        cpack_args.push_back("--verbose");
    }
    
    // Always log the full command for easier debugging
    std::string full_cmd = cpack_command;
    for (const auto& arg : cpack_args) {
        full_cmd += " " + arg;
    }
    logger::print_status("Running CPack command: " + full_cmd);
    logger::print_verbose("CPack working directory: " + build_dir.string());
    logger::print_verbose("CPack package output directory: " + package_dir.string());
    
    // Build a proper cleanup for the packages directory to make sure no temp files remain
    // and to clean up any stale temporary files from previous runs
    auto deep_cleanup_package_dir = [&package_dir]() {
        if (!std::filesystem::exists(package_dir)) {
            return;
        }
        
        // Remove the _CPack_Packages directory
        std::filesystem::path cpack_packages_dir = package_dir / "_CPack_Packages";
        if (std::filesystem::exists(cpack_packages_dir)) {
            try {
                logger::print_verbose("Removing intermediate files in: " + cpack_packages_dir.string());
                std::filesystem::remove_all(cpack_packages_dir);
            } catch (const std::exception& ex) {
                logger::print_verbose("Failed to remove intermediate files: " + std::string(ex.what()));
            }
        }
        
        // Also try removing any other intermediate directories that might be present
        try {
            for (const auto& entry : std::filesystem::directory_iterator(package_dir)) {
                if (entry.is_directory()) {
                    std::string dirname = entry.path().filename().string();
                    // Remove any directories that look like temporary ones from CMake/CPack
                    if (dirname.find("_CPack_") != std::string::npos ||
                        dirname.find("_cmake") != std::string::npos ||
                        dirname.find("_tmp") != std::string::npos) {
                        std::filesystem::remove_all(entry.path());
                    }
                }
            }
        } catch (const std::exception& ex) {
            logger::print_verbose("Error during directory cleanup: " + std::string(ex.what()));
        }
    };
    
    // Clean up any existing CPack intermediate files first
    deep_cleanup_package_dir();
    
    // Execute with output capture
    bool result_success = execute_tool(cpack_command, cpack_args, build_dir.string(), "CPack", verbose, 300);
    
    // Create a result structure
    process_result_t result = {
        result_success,
        result_success ? 0 : 1,
        "", // No stdout capture with execute_tool
        ""  // No stderr capture with execute_tool
    };
    
    // Parse output for created package paths
    if (result.success) {
        // Look for paths in the output that may indicate where packages were created
        logger::print_verbose("CPack completed successfully");
    } else {
        // Log error
        logger::print_error("CPack failed to execute");
    }
    
    // If the command failed with NSIS error, try to install NSIS and run again
    if (!result.success) {
        // Check if the failure was due to missing NSIS
        bool nsis_error = false;
        
        if (generators.empty() || std::find(generators.begin(), generators.end(), "NSIS") != generators.end()) {
            nsis_error = true;
        }
        
        if (nsis_error) {
            logger::print_status("CPack failed. Checking if NSIS is installed...");
            
            // Remove NSIS from generators so we can still produce other packages
            std::vector<std::string> non_nsis_generators;
            for (const auto& gen : generators) {
                if (gen != "NSIS") {
                    non_nsis_generators.push_back(gen);
                }
            }
            
            // Attempt to install NSIS automatically
            if (download_and_install_nsis(verbose)) {
                logger::print_status("Retrying package creation with CPack...");
                result.success = execute_tool(cpack_command, cpack_args, build_dir.string(), "CPack", verbose, 300);
                result.exit_code = result.success ? 0 : 1;
            } else if (!non_nsis_generators.empty()) {
                // If NSIS installation failed but we have other generators, try with them
                logger::print_status("Trying to create packages without NSIS...");
                
                std::vector<std::string> new_args = cpack_args;
                
                // Replace generator argument with non-NSIS generators
                for (size_t i = 0; i < new_args.size(); i++) {
                    if (new_args[i] == "-G" && i + 1 < new_args.size()) {
                        std::string gen_str;
                        for (size_t j = 0; j < non_nsis_generators.size(); ++j) {
                            if (j > 0) gen_str += ";";
                            gen_str += non_nsis_generators[j];
                        }
                        new_args[i + 1] = gen_str;
                        break;
                    }
                }
                
                result.success = execute_tool(cpack_command, new_args, build_dir.string(), "CPack", verbose, 300);
                result.exit_code = result.success ? 0 : 1;
            }
        }
    }
    
    if (!result.success) {
        logger::print_error("Failed to create packages with CPack");
        return false;
    }
    
    // Check for package output
    bool packages_found = false;
    try {
        // First, check for packages in the build directory that need to be moved
        if (std::filesystem::exists(build_dir)) {
            logger::print_verbose("Looking for packages in build directory to move to packages directory");
            std::vector<std::filesystem::path> package_files_to_move;
            
            for (const auto& entry : std::filesystem::recursive_directory_iterator(build_dir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    if (is_package_file(entry.path()) && 
                        entry.path().string().find("_CPack_Packages") == std::string::npos) {
                        std::filesystem::path target_path = package_dir / filename;
                        package_files_to_move.push_back(entry.path());
                    }
                }
            }
            
            // Move the found packages
            for (const auto& src_path : package_files_to_move) {
                std::filesystem::path dest_path = package_dir / src_path.filename();
                logger::print_verbose("Moving " + src_path.string() + " to " + dest_path.string());
                
                try {
                    if (std::filesystem::exists(dest_path)) {
                        std::filesystem::remove(dest_path);
                    }
                    std::filesystem::copy_file(src_path, dest_path);
                    std::filesystem::remove(src_path);
                } catch (const std::exception& ex) {
                    logger::print_verbose("Failed to move package: " + std::string(ex.what()));
                }
            }
        }
        
        // Collect all package files
        std::vector<std::filesystem::path> all_packages;
        
        // Check packages directory
        if (std::filesystem::exists(package_dir)) {
            auto packages = find_package_files(package_dir, true);
            all_packages.insert(all_packages.end(), packages.begin(), packages.end());
        }
        
        // Check build directory
        if (std::filesystem::exists(build_dir)) {
            auto packages = find_package_files(build_dir, true);
            all_packages.insert(all_packages.end(), packages.begin(), packages.end());
        }
        
        // Parse project name from build directory
        std::string project_name = "";
        try {
            // Try to get project name from the last directory component of the parent of build_dir
            std::filesystem::path project_dir = build_dir.parent_path();
            project_name = project_dir.filename().string();
        } catch (...) {
            // Ignore errors
        }

        // Display only the final distribution packages
        display_only_final_packages(all_packages, config_name, project_name);
        
        // Check if we have any packages
        packages_found = !all_packages.empty();
        
        // Final cleanup to remove intermediate files
        deep_cleanup_package_dir();
    } catch (const std::exception& ex) {
        logger::print_warning("Error checking for package files: " + std::string(ex.what()));
    }
    
    if (!packages_found) {
        logger::print_warning("No package files found. CPack may have failed or created packages elsewhere.");
        logger::print_status("Check CPack output for details about where files were created.");
        
        // If CPack reported success but we found no packages, we don't consider this a success
        if (result.success) {
            logger::print_warning("CPack reported success but no package files were found.");
            return false;
        }
    }
    
    // Consider the operation successful only if packages were found
    // (regardless of the CPack reported result)
    return packages_found;
}

/**
 * @brief Package a single project
 * 
 * @param project_dir Project directory
 * @param project_config Project configuration 
 * @param build_config Build configuration
 * @param skip_build Skip building flag
 * @param generators Package generators to use
 * @param verbose Verbose flag
 * @return bool Success flag
 */
static bool package_single_project(
    const std::filesystem::path& project_dir,
    toml_reader& project_config,
    const std::string& build_config,
    bool skip_build,
    const std::vector<std::string>& generators,
    bool verbose,
    const cforge_context_t* ctx)
{
    // Get project name for verbose logging
    std::string project_name = project_config.get_string("project.name", "cpp-project");
    logger::print_verbose("Packaging project: " + project_name);
    
    // Get base build directory
    std::string base_build_dir = project_config.get_string("build.build_dir", "build");
    logger::print_verbose("Base build directory: " + base_build_dir);
    
    // Get the config-specific build directory
    std::filesystem::path build_dir = get_build_dir_for_config(base_build_dir, build_config);
    logger::print_verbose("Config-specific build directory: " + build_dir.string());
    
    // Make build_dir absolute if it's relative
    if (build_dir.is_relative()) {
        build_dir = project_dir / build_dir;
    }
    
    // If build directory doesn't exist or CMakeCache.txt is missing, build the project first
    if (!std::filesystem::exists(build_dir) || 
        !std::filesystem::exists(build_dir / "CMakeCache.txt")) {
        if (skip_build) {
            logger::print_error("Build directory or CMakeCache.txt not found, but --no-build was specified");
            logger::print_status("Run 'cforge build --config " + build_config + "' first");
            return false;
        }
        
        logger::print_status("Build directory not found or incomplete, building project first...");
        if (!build_project(ctx)) {
            logger::print_error("Failed to build the project");
            return false;
        }
    } else if (!skip_build) {
        // Always rebuild to ensure we have the latest version
        logger::print_status("Rebuilding project before packaging...");
        if (!build_project(ctx)) {
            logger::print_error("Failed to build the project");
            return false;
        }
    } else {
        logger::print_status("Skipping build as requested with --no-build");
    }
    
    // Use generators from config or default ones
    std::vector<std::string> project_generators = generators;
    if (project_generators.empty()) {
        project_generators = project_config.get_string_array("package.generators");
        if (project_generators.empty()) {
            project_generators = get_default_generators();
        } else {
            project_generators = uppercase_generators(project_generators);
        }
    }
    
    // Log the generators being used
    if (!project_generators.empty()) {
        std::string gen_str = "Using generators: ";
        for (size_t i = 0; i < project_generators.size(); ++i) {
            if (i > 0) gen_str += ", ";
            gen_str += project_generators[i];
        }
        logger::print_status(gen_str);
    }
    
    // Run CPack to create packages
    return run_cpack(build_dir, project_generators, build_config, verbose);
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
    
    // Check if this is a workspace
    std::filesystem::path workspace_file = std::filesystem::path(ctx->working_dir) / WORKSPACE_FILE;
    bool is_workspace = std::filesystem::exists(workspace_file);
    
    // Parse common parameters
    
    // Check for build first flag
    bool skip_build = false;
    if (ctx->args.args) {
        for (int i = 0; i < ctx->args.arg_count; ++i) {
            if (strcmp(ctx->args.args[i], "--no-build") == 0) {
                skip_build = true;
                break;
            }
        }
    }
    
    // Get verbose flag
    bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
    
    // Get generators from command line if specified
    std::vector<std::string> generators;
    if (ctx->args.args) {
        for (int i = 0; i < ctx->args.arg_count; ++i) {
            if ((strcmp(ctx->args.args[i], "--type") == 0 || 
                 strcmp(ctx->args.args[i], "-t") == 0) && 
                i + 1 < ctx->args.arg_count) {
                std::string gen = ctx->args.args[i + 1];
                // Uppercase the generator name
                std::transform(gen.begin(), gen.end(), gen.begin(), ::toupper);
                generators.push_back(gen);
                logger::print_verbose("Using generator from command line: " + gen);
                break;
            }
        }
    }
    
    // Check for specific project in workspace
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
    
    // Default build config
    std::string build_config = "Release";
    
    // Get build config from args if specified
    if (ctx->args.config && strlen(ctx->args.config) > 0) {
        build_config = ctx->args.config;
        logger::print_verbose("Using build configuration from argument: " + build_config);
    } else if (ctx->args.args) {
        for (int i = 0; i < ctx->args.arg_count; ++i) {
            if ((strcmp(ctx->args.args[i], "--config") == 0 || 
                 strcmp(ctx->args.args[i], "-c") == 0) && 
                i + 1 < ctx->args.arg_count) {
                build_config = ctx->args.args[i + 1];
                logger::print_verbose("Using build configuration from command line: " + build_config);
                break;
            }
        }
    }
    
    // If in a workspace, package projects in the workspace
    if (is_workspace) {
        logger::print_status("Packaging workspace projects");
        
        // Load workspace
        workspace ws;
        if (!ws.load(ctx->working_dir)) {
            logger::print_error("Failed to load workspace configuration");
            return 1;
        }
        
        // Get all projects
        std::vector<workspace_project> projects = ws.get_projects();
        if (projects.empty()) {
            logger::print_error("No projects found in workspace");
            return 1;
        }
        
        // Package specific project if requested
        if (!specific_project.empty()) {
            bool found = false;
            for (const auto& project : projects) {
                if (project.name == specific_project) {
                    found = true;
                    logger::print_status("Packaging specific project: " + project.name);
                    
                    std::filesystem::path project_config_path = project.path / CFORGE_FILE;
                    if (!std::filesystem::exists(project_config_path)) {
                        logger::print_error("Project configuration file not found: " + project_config_path.string());
                        return 1;
                    }
                    
                    toml_reader project_config;
                    if (!project_config.load(project_config_path.string())) {
                        logger::print_error("Failed to load project configuration");
                        return 1;
                    }
                    
                    // Use generators from config or default ones
                    std::vector<std::string> project_generators = generators;
                    if (project_generators.empty()) {
                        project_generators = project_config.get_string_array("package.generators");
                        if (project_generators.empty()) {
                            project_generators = get_default_generators();
                        } else {
                            project_generators = uppercase_generators(project_generators);
                        }
                    }
                    
                    // Get build config from project config if not specified
                    std::string project_build_config = build_config;
                    if (project_build_config.empty()) {
                        project_build_config = project_config.get_string("build.build_type", "Release");
                    }
                    
                    logger::print_status("Using build configuration: " + project_build_config);
                    
                    // Package the project
                    if (!package_single_project(
                            project.path, 
                            project_config, 
                            project_build_config, 
                            skip_build, 
                            project_generators, 
                            verbose,
                            ctx)) {
                        logger::print_error("Failed to package project: " + project.name);
                        return 1;
                    }
                    
                    logger::print_success("Successfully packaged project: " + project.name);
                    break;
                }
            }
            
            if (!found) {
                logger::print_error("Project not found in workspace: " + specific_project);
                return 1;
            }
        } else {
            // Package all projects
            bool all_success = true;
            int successful_packages = 0;
            
            for (const auto& project : projects) {
                logger::print_status("Packaging project: " + project.name);
                
                std::filesystem::path project_config_path = project.path / CFORGE_FILE;
                if (!std::filesystem::exists(project_config_path)) {
                    logger::print_warning("Project configuration file not found, skipping: " + project_config_path.string());
                    continue;
                }
                
                toml_reader project_config;
                if (!project_config.load(project_config_path.string())) {
                    logger::print_warning("Failed to load project configuration, skipping: " + project.name);
                    continue;
                }
                
                // Skip if packaging is disabled for this project
                bool packaging_enabled = project_config.get_bool("package.enabled", true);
                if (!packaging_enabled) {
                    logger::print_status("Packaging is disabled for project: " + project.name);
                    continue;
                }
                
                // Use generators from config or default ones
                std::vector<std::string> project_generators = generators;
                if (project_generators.empty()) {
                    project_generators = project_config.get_string_array("package.generators");
                    if (project_generators.empty()) {
                        project_generators = get_default_generators();
                    } else {
                        project_generators = uppercase_generators(project_generators);
                    }
                }
                
                // Get build config from project config if not specified
                std::string project_build_config = build_config;
                if (project_build_config.empty()) {
                    project_build_config = project_config.get_string("build.build_type", "Release");
                }
                
                logger::print_status("Using build configuration for " + project.name + ": " + project_build_config);
                
                // Package the project
                if (package_single_project(
                        project.path, 
                        project_config, 
                        project_build_config, 
                        skip_build, 
                        project_generators, 
                        verbose,
                        ctx)) {
                    logger::print_success("Successfully packaged project: " + project.name);
                    successful_packages++;
                } else {
                    logger::print_error("Failed to package project: " + project.name);
                    all_success = false;
                }
            }
            
            if (successful_packages == 0) {
                logger::print_error("Failed to package any projects");
                return 1;
            } else if (!all_success) {
                logger::print_warning("Some projects failed to package");
                return 1;
            } else {
                logger::print_success("All projects packaged successfully");
                return 0;
            }
        }
        
        return 0;
    } else {
        // Handle single project packaging
        
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
        
        // Get build type from project config if not already specified
        if (build_config.empty()) {
            build_config = config.get_string("build.build_type", "Release");
        }
        
        logger::print_status("Using build configuration: " + build_config);
        
        // Use generators from config or default ones if not specified
        if (generators.empty()) {
            generators = config.get_string_array("package.generators");
            if (generators.empty()) {
                generators = get_default_generators();
            } else {
                generators = uppercase_generators(generators);
            }
        }
        
        // Log the generators being used
        if (!generators.empty()) {
            std::string gen_str = "Using generators: ";
            for (size_t i = 0; i < generators.size(); ++i) {
                if (i > 0) gen_str += ", ";
                gen_str += generators[i];
            }
            logger::print_status(gen_str);
        }
        
        // Package the project
        if (package_single_project(
                ctx->working_dir, 
                config, 
                build_config, 
                skip_build, 
                generators, 
                verbose,
                ctx)) {
            logger::print_success("Packages created successfully");
            return 0;
        }
        
        return 1;
    }
}