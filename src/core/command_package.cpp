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
    
    // Convert config to lowercase for comparison
    std::string config_lower = config;
    std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
    
    // Try multiple build directory formats
    std::vector<std::filesystem::path> common_formats;
    
    // Format: build-Debug, build-Release (most common for this project based on screenshot)
    common_formats.push_back(std::filesystem::path("build-" + config));
    
    // Format: build/Debug, build/Release
    common_formats.push_back(std::filesystem::path(base_dir + "/" + config));
    
    // Format: build-debug, build-release (lowercase)
    common_formats.push_back(std::filesystem::path("build-" + config_lower));
    
    // Format: build_Debug, build_Release
    common_formats.push_back(std::filesystem::path("build_" + config));
    
    // Format: build_debug, build_release (lowercase)
    common_formats.push_back(std::filesystem::path("build_" + config_lower));
    
    // Original format: build-debug, build-release (base_dir-config)
    common_formats.push_back(std::filesystem::path(base_dir + "-" + config_lower));
    
    // Fallback format: just base_dir
    common_formats.push_back(std::filesystem::path(base_dir));
    
    // Log which build directories we're checking
    logger::print_verbose("Checking for build directories:");
    for (const auto& path : common_formats) {
        logger::print_verbose("  - " + path.string());
    }
    
    // Look for the first existing directory in our list
    for (const auto& path : common_formats) {
        if (std::filesystem::exists(path)) {
            logger::print_verbose("Found existing build directory: " + path.string());
            return path;
        }
    }
    
    // If no directory exists, return the most likely format based on project
    // Since the build system is using "build" as base and supports Debug config,
    // we'll try to match how the build system creates directories
    logger::print_verbose("No existing build directory found, using directory format based on configuration");
    
    // Special handling for Debug configuration
    if (config == "Debug") {
        return std::filesystem::path(base_dir + "-debug"); // lowercase for Debug config
    }
    
    // For other configurations, use base-config format
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
    bool exe_found = false;
    bool msi_found = false;
    
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
        if (ext == ".exe" && filename != "a.exe") exe_found = true;
        if (ext == ".msi") msi_found = true;
        
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
                // Check if config is included in filename
                std::string config_lower = config_name;
                std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
                
                bool has_config = false;
                if (!config_name.empty()) {
                    has_config = filename.find(config_lower) != std::string::npos;
                }
                
                // Only show warning for missing config if it's not Release
                std::string display_name = path.string();
                if (!config_name.empty() && config_name != "Release" && !has_config) {
                    // Format: package.zip [missing config: debug]
                    display_name += " [missing config: " + config_lower + "]";
                }
                
                logger::print_success("Package created: " + display_name);
                reported_packages.insert(filename);
            }
        }
    }
    
    if (reported_packages.empty()) {
        logger::print_warning("No distribution packages found.");
        logger::print_status("Check that your project is configured correctly for packaging.");
    }
    
    // Final summary of packages - log helpful information about missing package types
    if (!zip_found && !tar_found && !exe_found && !msi_found) {
        logger::print_status("To create packages, add one or more of the following to your project config:");
        logger::print_status("  package.generators = [\"ZIP\"]   # Basic ZIP package, works everywhere");
        logger::print_status("  package.generators = [\"NSIS\"]  # Windows installer (.exe)");
        logger::print_status("  package.generators = [\"WIX\"]   # Windows MSI installer");
        logger::print_status("  package.generators = [\"TGZ\"]   # Tarball for Linux/macOS");
    }
    
    // Remind about config
    if (!config_name.empty() && config_name != "Release") {
        bool missing_config = false;
        for (const auto& pkg_name : reported_packages) {
            std::string config_lower = config_name;
            std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
            if (pkg_name.find(config_lower) == std::string::npos) {
                missing_config = true;
                break;
            }
        }
        
        if (missing_config) {
            logger::print_status("Tip: To include configuration in package names, edit src/core/command_package.cpp");
            logger::print_status("  and look for CPACK_PACKAGE_FILE_NAME to ensure it's included for all generators.");
        }
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
 * @param project_name Project name
 * @param project_version Project version
 * @return bool Success flag
 */
static bool run_cpack(
    const std::filesystem::path& build_dir,
    const std::vector<std::string>& generators,
    const std::string& config_name,
    bool verbose,
    const std::string& project_name = "",
    const std::string& project_version = "")
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
    if (!config_name.empty()) {
        std::string config_lower = config_name;
        std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
        
        // Use project name/version from arguments if provided
        std::string pkg_name = project_name;
        std::string pkg_version = project_version;
        
        // If not provided, try to get project info from CMakeCache
        if (pkg_name.empty() || pkg_version.empty()) {
            // Try to read project name and version from CMakeCache.txt
            std::filesystem::path cmake_cache = build_dir / "CMakeCache.txt";
            if (std::filesystem::exists(cmake_cache)) {
                try {
                    std::ifstream cache_file(cmake_cache);
                    std::string line;
                    while (std::getline(cache_file, line)) {
                        if (pkg_name.empty() && line.find("CMAKE_PROJECT_NAME:") != std::string::npos) {
                            size_t pos = line.find('=');
                            if (pos != std::string::npos) {
                                pkg_name = line.substr(pos + 1);
                            }
                        } else if (pkg_version.empty() && 
                                  (line.find("CMAKE_PROJECT_VERSION:") != std::string::npos ||
                                   line.find("PROJECT_VERSION:") != std::string::npos)) {
                            size_t pos = line.find('=');
                            if (pos != std::string::npos) {
                                pkg_version = line.substr(pos + 1);
                            }
                        }
                    }
                } catch (...) {
                    // Ignore errors reading the cache
                }
            }
        }
        
        // If still empty, fall back to directory name
        if (pkg_name.empty()) {
            pkg_name = build_dir.parent_path().filename().string();
        }
        if (pkg_version.empty()) {
            pkg_version = "1.0.0";
        }
        
        // Direct file name pattern instead of using placeholders
        std::string package_file_name = pkg_name + "-" + pkg_version + "-win64-" + config_lower;
        
        // CPack variable to customize package filename - avoid placeholders
        cpack_args.push_back("-D");
        cpack_args.push_back("CPACK_PACKAGE_FILE_NAME=" + package_file_name);
        
        // Add additional CPack variables to ensure config appears in all formats
        // NSIS-specific variable
        cpack_args.push_back("-D");
        cpack_args.push_back("CPACK_NSIS_PACKAGE_NAME=" + pkg_name + " " + config_name);
        
        // WIX-specific variable
        cpack_args.push_back("-D");
        cpack_args.push_back("CPACK_WIX_PRODUCT_NAME=" + pkg_name + " " + config_name);
        
        // Set CPack project config name
        cpack_args.push_back("-D");
        cpack_args.push_back("CPACK_PROJECT_CONFIG_NAME=" + config_name);
    }
    
    // Always set package output format to avoid using CPack subdirectories
    cpack_args.push_back("-D");
    cpack_args.push_back("CPACK_OUTPUT_FILE_PREFIX=" + package_dir.string());
    
    // Avoid creating an extra subdirectory
    cpack_args.push_back("-D");
    cpack_args.push_back("CPACK_PACKAGE_INSTALL_DIRECTORY=.");
    
    // Also set additional variables to avoid placeholder issues
    cpack_args.push_back("-D");
    cpack_args.push_back("CPACK_TEMPORARY_DIRECTORY=" + package_dir.string() + "/temp");
    
    // Ensure the temporary directory exists and is clean
    std::filesystem::path temp_dir = package_dir / "temp";
    if (std::filesystem::exists(temp_dir)) {
        try {
            std::filesystem::remove_all(temp_dir);
        } catch (const std::exception& ex) {
            logger::print_verbose("Failed to clean temporary directory: " + std::string(ex.what()));
        }
    }
    try {
        std::filesystem::create_directories(temp_dir);
    } catch (const std::exception& ex) {
        logger::print_verbose("Failed to create temporary directory: " + std::string(ex.what()));
    }
    
    // Set a specific system name to avoid placeholders
    cpack_args.push_back("-D");
    cpack_args.push_back("CPACK_SYSTEM_NAME=win64");
    
    // Add verbose flag if needed
    if (verbose) {
        cpack_args.push_back("--verbose");
    }
    
    // Always log the full command for easier debugging
    std::string full_cmd = cpack_command;
    for (const auto& arg : cpack_args) {
        full_cmd += " " + arg;
    }

    if (verbose) {
        // Don't print the entire command - it's too long and may cause heap issues
        logger::print_status("Running CPack command to create packages...");
        logger::print_verbose("CPack working directory: " + build_dir.string());
        logger::print_verbose("CPack package output directory: " + package_dir.string());
    }

    // Reduce memory pressure by not keeping the full command in memory
    full_cmd.clear();

    // Build a proper cleanup for the packages directory to make sure no temp files remain
    // and to clean up any stale temporary files from previous runs
    auto deep_cleanup_package_dir = [&package_dir]() {
        if (!std::filesystem::exists(package_dir)) {
            return;
        }
        
        logger::print_verbose("Cleaning package directory: " + package_dir.string());
        
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
                        dirname.find("_tmp") != std::string::npos ||
                        dirname.find("<") != std::string::npos) { // Remove directories with placeholders
                        logger::print_verbose("Removing intermediate directory: " + entry.path().string());
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
    
    // Execute with output capture - use a try/catch to handle any exceptions
    bool result_success = false;
    try {
        // Use a simpler execution approach to avoid memory issues in debug mode
        result_success = execute_tool(cpack_command, cpack_args, build_dir.string(), "CPack", verbose, 300);
    } catch (const std::exception& ex) {
        logger::print_error("Exception during CPack execution: " + std::string(ex.what()));
        result_success = false;
    } catch (...) {
        logger::print_error("Unknown exception during CPack execution");
        result_success = false;
    }
    
    // If the command failed with NSIS error, try to install NSIS and run again
    if (!result_success) {
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
                
                // Extract pkg_name and pkg_version to reuse them in the retry
                std::string pkg_name = project_name;
                std::string pkg_version = project_version;
                if (pkg_name.empty()) {
                    pkg_name = build_dir.parent_path().filename().string();
                }
                if (pkg_version.empty()) {
                    pkg_version = "1.0.0";
                }
                
                // Re-run cpack with the newly installed NSIS
                result_success = execute_tool(cpack_command, cpack_args, build_dir.string(), "CPack", verbose, 300);
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
                
                result_success = execute_tool(cpack_command, new_args, build_dir.string(), "CPack", verbose, 300);
            }
        }
    }
    
    if (!result_success) {
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
        if (result_success) {
            logger::print_warning("CPack reported success but no package files were found.");
            return false;
        }
    }
    
    // Consider the operation successful only if packages were found
    // (regardless of the CPack reported result)
    return packages_found;
}

/**
 * @brief Check if required tools for a CPack generator are installed
 * 
 * @param generator Generator to check
 * @return bool True if tools are available
 */
static bool check_generator_tools_installed(const std::string& generator) {
    std::string gen_upper = generator;
    std::transform(gen_upper.begin(), gen_upper.end(), gen_upper.begin(), ::toupper);
    
    // Check common generators that require additional tools
    if (gen_upper == "NSIS" || gen_upper == "NSIS64") {
        // Check for NSIS
        bool nsis_found = false;
        
#ifdef _WIN32
        // Check common NSIS installation paths
        std::vector<std::string> nsis_paths = {
            "C:\\Program Files\\NSIS\\makensis.exe",
            "C:\\Program Files (x86)\\NSIS\\makensis.exe"
        };
        
        for (const auto& path : nsis_paths) {
            if (std::filesystem::exists(path)) {
                nsis_found = true;
                break;
            }
        }
        
        // Try to run makensis to check if it's in PATH
        if (!nsis_found) {
            process_result where_result = execute_process("where", {"makensis"}, "", nullptr, nullptr, 2);
            nsis_found = where_result.success && !where_result.stdout_output.empty();
        }
#else
        // On non-Windows, try to run makensis
        process_result which_result = execute_process("which", {"makensis"}, "", nullptr, nullptr, 2);
        nsis_found = which_result.success && !which_result.stdout_output.empty();
#endif
        
        if (!nsis_found) {
            logger::print_warning("NSIS not found. To create installer packages (.exe), please install NSIS:");
            logger::print_status("  1. Download from https://nsis.sourceforge.io/Download");
            logger::print_status("  2. Run the installer and follow the installation steps");
            logger::print_status("  3. Make sure NSIS is added to your PATH");
            logger::print_status("  4. Run the package command again");
            return false;
        }
        
        return true;
    } else if (gen_upper == "WIX") {
        // Check for WiX tools (candle.exe and light.exe)
        bool wix_found = false;
        
#ifdef _WIN32
        // Try to locate candle.exe in PATH
        process_result where_result = execute_process("where", {"candle.exe"}, "", nullptr, nullptr, 2);
        wix_found = where_result.success && !where_result.stdout_output.empty();
        
        if (!wix_found) {
            // Check WiX Toolset common installation paths
            std::vector<std::string> wix_paths = {
                "C:\\Program Files\\WiX Toolset\\bin\\candle.exe",
                "C:\\Program Files (x86)\\WiX Toolset\\bin\\candle.exe",
                "C:\\Program Files\\WiX Toolset*\\bin\\candle.exe",
                "C:\\Program Files (x86)\\WiX Toolset*\\bin\\candle.exe"
            };
            
            for (const auto& path_pattern : wix_paths) {
                if (path_pattern.find('*') != std::string::npos) {
                    // Wildcard path, try to find matching directories
                    std::string base_dir = path_pattern.substr(0, path_pattern.find('*'));
                    if (std::filesystem::exists(base_dir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(base_dir)) {
                            if (entry.is_directory()) {
                                std::string potential_path = entry.path().string() + "\\bin\\candle.exe";
                                if (std::filesystem::exists(potential_path)) {
                                    wix_found = true;
                                    break;
                                }
                            }
                        }
                    }
                } else if (std::filesystem::exists(path_pattern)) {
                    wix_found = true;
                    break;
                }
            }
        }
#endif
        
        if (!wix_found) {
            logger::print_warning("WiX Toolset not found. To create MSI packages, please install WiX Toolset:");
            logger::print_status("  1. Download from https://wixtoolset.org/releases/");
            logger::print_status("  2. Install WiX Toolset and Visual Studio extension if needed");
            logger::print_status("  3. Make sure WiX bin directory is in your PATH");
            logger::print_status("  4. Run the package command again with --type WIX");
            logger::print_status("You can also use --type ZIP for a simpler package format");
            return false;
        }
        
        return true;
    } else if (gen_upper == "DEB") {
        // Check for dpkg tools
        bool dpkg_found = false;
        
#ifdef _WIN32
        // DEB generator not well-supported on Windows
        logger::print_warning("DEB generator is not well-supported on Windows.");
        logger::print_status("  Consider using --type ZIP instead for Windows.");
        logger::print_status("  If you need .deb packages, use WSL or a Linux VM.");
        return false;
#else
        // Check for dpkg-deb
        process_result which_result = execute_process("which", {"dpkg-deb"}, "", nullptr, nullptr, 2);
        dpkg_found = which_result.success && !which_result.stdout_output.empty();
        
        if (!dpkg_found) {
            logger::print_warning("dpkg tools not found. To create .deb packages, please install dpkg tools:");
            logger::print_status("  On Ubuntu/Debian: sudo apt-get install dpkg-dev");
            logger::print_status("  On Fedora/RHEL:  sudo dnf install dpkg-dev");
            logger::print_status("  Run the package command again after installation");
            return false;
        }
#endif
        
        return true;
    } else if (gen_upper == "RPM") {
        // Check for rpm tools
        bool rpm_found = false;
        
#ifdef _WIN32
        // RPM generator not well-supported on Windows
        logger::print_warning("RPM generator is not well-supported on Windows.");
        logger::print_status("  Consider using --type ZIP instead for Windows.");
        logger::print_status("  If you need .rpm packages, use WSL or a Linux VM.");
        return false;
#else
        // Check for rpmbuild
        process_result which_result = execute_process("which", {"rpmbuild"}, "", nullptr, nullptr, 2);
        rpm_found = which_result.success && !which_result.stdout_output.empty();
        
        if (!rpm_found) {
            logger::print_warning("rpmbuild not found. To create .rpm packages, please install rpm tools:");
            logger::print_status("  On Ubuntu/Debian: sudo apt-get install rpm");
            logger::print_status("  On Fedora/RHEL:  sudo dnf install rpm-build");
            logger::print_status("  Run the package command again after installation");
            return false;
        }
#endif
        
        return true;
    }
    
    // Other generators are generally available with CMake/CPack
    return true;
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
    std::string project_version = project_config.get_string("project.version", "1.0.0");
    logger::print_verbose("Packaging project: " + project_name + " version " + project_version);
    
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
    
    // Check if build directory exists
    bool build_dir_exists = std::filesystem::exists(build_dir);
    bool cache_exists = std::filesystem::exists(build_dir / "CMakeCache.txt");
    
    logger::print_verbose("Build directory exists: " + std::string(build_dir_exists ? "yes" : "no"));
    logger::print_verbose("CMakeCache.txt exists: " + std::string(cache_exists ? "yes" : "no"));
    
    // If build directory doesn't exist or CMakeCache.txt is missing, build the project first
    if (!build_dir_exists || !cache_exists) {
        if (skip_build) {
            logger::print_error("Build directory or CMakeCache.txt not found, but --no-build was specified");
            logger::print_status("Run 'cforge build --config " + build_config + "' first");
            logger::print_status("Expected build directory: " + build_dir.string());
            return false;
        }
        
        logger::print_status("Build directory not found or incomplete, building project first...");
        
        // Create a modified context with the correct configuration
        cforge_context_t build_ctx = *ctx;
        // Fix: Need to allocate memory for cforge_string_t (char*)
        if (build_ctx.args.config) {
            free(build_ctx.args.config);
        }
        build_ctx.args.config = strdup(build_config.c_str());
        
        if (!build_project(&build_ctx)) {
            logger::print_error("Failed to build the project");
            if (build_ctx.args.config) {
                free(build_ctx.args.config);
            }
            return false;
        }
        
        // Clean up allocated memory
        if (build_ctx.args.config) {
            free(build_ctx.args.config);
        }
        
        // After building, check if build dir now exists
        bool build_dir_exists_now = std::filesystem::exists(build_dir);
        logger::print_verbose("Build directory exists after build: " + std::string(build_dir_exists_now ? "yes" : "no"));
        
        // If the directory still doesn't exist, we need to check other common formats
        if (!build_dir_exists_now) {
            logger::print_status("Searching for build directory after build...");
            
            // Try to find the actual build directory created
            std::filesystem::path parent_dir = project_dir;
            
            // Try different build patterns - add more common formats, especially with and without hyphen
            std::vector<std::string> patterns = {
                "build-" + build_config,
                "build-" + std::string(build_config.begin(), build_config.end()),
                "build" + build_config, // No separator
                "build_" + build_config,
                "build/" + build_config,
                "build-" + std::string(build_config.begin(), build_config.end()),
                "build"
            };
            
            // Try lowercase versions too
            std::string config_lower = build_config;
            std::transform(config_lower.begin(), config_lower.end(), config_lower.begin(), ::tolower);
            patterns.push_back("build-" + config_lower);
            patterns.push_back("build_" + config_lower);
            
            // Find any build directory by inspecting subdirectories of project_dir
            bool found_build_dir = false;
            if (!found_build_dir) {
                logger::print_status("Looking for build directories in project...");
                try {
                    for (const auto& entry : std::filesystem::directory_iterator(parent_dir)) {
                        if (entry.is_directory()) {
                            std::string dirname = entry.path().filename().string();
                            if (dirname.find("build") != std::string::npos || 
                                dirname.find("Build") != std::string::npos) {
                                // Check if this has CMake files
                                if (std::filesystem::exists(entry.path() / "CMakeCache.txt")) {
                                    build_dir = entry.path();
                                    logger::print_status("Found build directory by inspection: " + build_dir.string());
                                    found_build_dir = true;
                                    break;
                                }
                                
                                // Also check subdirectories for common patterns
                                for (const auto& subdir : std::filesystem::directory_iterator(entry.path())) {
                                    if (subdir.is_directory()) {
                                        std::string subdirname = subdir.path().filename().string();
                                        if (subdirname == build_config || 
                                            subdirname == config_lower ||
                                            subdirname == "Debug" || 
                                            subdirname == "Release") {
                                            
                                            if (std::filesystem::exists(subdir.path() / "CMakeCache.txt")) {
                                                build_dir = subdir.path();
                                                logger::print_status("Found build subdirectory by inspection: " + build_dir.string());
                                                found_build_dir = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                
                                if (found_build_dir) break;
                            }
                        }
                    }
                } catch (const std::exception& ex) {
                    logger::print_verbose("Error inspecting directories: " + std::string(ex.what()));
                }
            }
            
            for (const auto& pattern : patterns) {
                std::filesystem::path check_dir = parent_dir / pattern;
                logger::print_verbose("Checking " + check_dir.string());
                
                if (std::filesystem::exists(check_dir)) {
                    build_dir = check_dir;
                    logger::print_status("Found build directory: " + build_dir.string());
                    found_build_dir = true;
                    break;
                }
            }
            
            // If we still can't find it, check recursive for any CMakeCache.txt
            if (!found_build_dir) {
                logger::print_status("Searching for CMake build directories...");
                
                for (const auto& entry : std::filesystem::recursive_directory_iterator(parent_dir)) {
                    if (entry.is_regular_file() && entry.path().filename() == "CMakeCache.txt") {
                        build_dir = entry.path().parent_path();
                        logger::print_status("Found CMake build directory: " + build_dir.string());
                        found_build_dir = true;
                        break;
                    }
                }
            }
            
            // Last resort - try build/bin/Debug directory which might contain executables
            if (!found_build_dir) {
                std::filesystem::path bin_dir = parent_dir / "build" / "bin" / build_config;
                if (std::filesystem::exists(bin_dir)) {
                    // Go up two levels to get the build directory
                    build_dir = bin_dir.parent_path().parent_path();
                    logger::print_status("Found build directory via bin folder: " + build_dir.string());
                    found_build_dir = true;
                }
            }
            
            // After all that searching, if we still can't find it, we fail
            if (!std::filesystem::exists(build_dir)) {
                logger::print_error("Could not find build directory after building project");
                return false;
            }
        }
    } else if (!skip_build) {
        // Always rebuild to ensure we have the latest version
        logger::print_status("Rebuilding project before packaging...");
        
        // Create a modified context with the correct configuration
        cforge_context_t build_ctx = *ctx;
        // Fix: Need to allocate memory for cforge_string_t (char*)
        if (build_ctx.args.config) {
            free(build_ctx.args.config);
        }
        build_ctx.args.config = strdup(build_config.c_str());
        
        if (!build_project(&build_ctx)) {
            logger::print_error("Failed to build the project");
            if (build_ctx.args.config) {
                free(build_ctx.args.config);
            }
            return false;
        }
        
        // Clean up allocated memory
        if (build_ctx.args.config) {
            free(build_ctx.args.config);
        }
    } else {
        logger::print_status("Skipping build as requested with --no-build");
    }
    
    // Verify that the build directory now exists
    if (!std::filesystem::exists(build_dir)) {
        logger::print_error("Build directory still doesn't exist after build: " + build_dir.string());
        return false;
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
    
    // Filter generators to only include those with tools installed
    std::vector<std::string> available_generators;
    for (const auto& generator : project_generators) {
        if (check_generator_tools_installed(generator)) {
            available_generators.push_back(generator);
        } else {
            logger::print_warning("Skipping generator " + generator + " because required tools are not installed");
        }
    }
    
    if (available_generators.empty()) {
        logger::print_error("No available package generators. Please install required tools or specify different generators.");
        return false;
    }
    
    // Log the generators being used
    if (!available_generators.empty()) {
        std::string gen_str = "Using generators: ";
        for (size_t i = 0; i < available_generators.size(); ++i) {
            if (i > 0) gen_str += ", ";
            gen_str += available_generators[i];
        }
        logger::print_status(gen_str);
    }
    
    // Package the project using the project_name and project_version already defined above
    return run_cpack(build_dir, available_generators, build_config, verbose, project_name, project_version);
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
    
    // Special case for Debug configuration - use a simpler approach to avoid memory issues
    if (build_config == "Debug") {
        logger::print_status("Using simplified process for Debug configuration");
        
        // For Debug build, just create a basic ZIP package without all the CPack options that cause issues
        std::string pkg_dir = std::filesystem::path(ctx->working_dir).string() + "/packages";
        
        // Create the package directory if it doesn't exist
        if (!std::filesystem::exists(pkg_dir)) {
            try {
                std::filesystem::create_directories(pkg_dir);
            } catch (const std::exception& ex) {
                logger::print_error("Failed to create packages directory: " + std::string(ex.what()));
                return 1;
            }
        }
        
        // Get project name and version from the project file
        std::string project_name = "";
        std::string project_version = "";
        
        // Load project configuration to get name and version
        toml_reader config;
        if (config.load(CFORGE_FILE)) {
            project_name = config.get_string("project.name", "");
            project_version = config.get_string("project.version", "");
        }
        
        // If not found in config, use the directory name
        if (project_name.empty()) {
            project_name = std::filesystem::path(ctx->working_dir).filename().string();
        }
        
        // Default version if not specified
        if (project_version.empty()) {
            project_version = "0.1.0";
        }
        
        // Create a simplified ZIP command
        std::vector<std::string> cmd_args;
#ifdef _WIN32
        std::string zip_cmd = "powershell";
        cmd_args.push_back("-Command");
        
        // Create PowerShell command to compress build directory
        std::string ps_cmd = "Compress-Archive -Path ";
        
        // Figure out common build directories for Debug
        std::vector<std::string> build_patterns = {
            "build-debug", 
            "build-Debug",
            "build\\bin\\Debug",
            "build/bin/Debug"
        };
        
        bool found_dir = false;
        std::string build_path;
        
        for (const auto& pattern : build_patterns) {
            std::string check_path = std::filesystem::path(ctx->working_dir).string() + "/" + pattern;
            std::replace(check_path.begin(), check_path.end(), '/', '\\');
            
            if (std::filesystem::exists(check_path)) {
                build_path = check_path;
                found_dir = true;
                logger::print_status("Found build directory: " + build_path);
                break;
            }
        }
        
        if (!found_dir) {
            logger::print_error("Could not find Debug build directory. Run 'cforge build --config Debug' first.");
            return 1;
        }
        
        std::string output_file = pkg_dir + "\\" + project_name + "-" + project_version + "-win64-debug.zip";
        std::replace(build_path.begin(), build_path.end(), '/', '\\');
        
        // Build the PowerShell command
        ps_cmd += "\"" + build_path + "\" -DestinationPath \"" + output_file + "\" -Force";
        cmd_args.push_back(ps_cmd);
        
        // Execute the command
        logger::print_status("Creating ZIP package with PowerShell...");
        bool success = execute_tool(zip_cmd, cmd_args, ctx->working_dir, "ZIP Package", true);
        
        if (success) {
            logger::print_success("Package created successfully: " + output_file);
            return 0;
        } else {
            logger::print_error("Failed to create package");
            return 1;
        }
#else
        // For non-Windows platforms, delegate to regular flow but use Release config
        build_config = "Release";
        logger::print_status("Switched to Release configuration for packaging on this platform");
#endif
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
                    if (package_single_project(
                            project.path, 
                            project_config, 
                            project_build_config, 
                            skip_build, 
                            project_generators, 
                            verbose,
                            ctx)) {
                        logger::print_success("Successfully packaged project: " + project.name);
                        break;
                    }
                    
                    logger::print_error("Failed to package project: " + project.name);
                    return 1;
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
        
        // Get project name and version
        std::string project_name = config.get_string("project.name", "");
        std::string project_version = config.get_string("project.version", "");
        
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