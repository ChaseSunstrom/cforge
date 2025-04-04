/**
 * @file installer.cpp
 * @brief Implementation of installation utilities for cforge
 */

#include "core/installer.hpp"
#include "core/process_utils.hpp"
#include "core/constants.h"
#include "core/file_system.h"
#include "core/process.h"
#include "cforge/log.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <direct.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

namespace cforge {

// Helper function to serve as cforge_print_verbose equivalent
inline void print_verbose(const std::string& message) {
    if (logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE) {
        logger::print_status(message);
    }
}

installer::installer() {
}

std::string installer::get_current_version() const {
    return CFORGE_VERSION;
}

bool installer::install(const std::string& install_path, bool add_to_path) {
    std::filesystem::path target_path;
    
    if (install_path.empty()) {
        target_path = get_platform_specific_path();
    } else {
        target_path = install_path;
    }
    
    logger::print_status("Installing cforge to: " + target_path.string());
    
    // Check if path is writable
    if (!is_path_writeable(target_path)) {
        logger::print_error("Installation path is not writable: " + target_path.string());
        return false;
    }
    
    // Create the directory if it doesn't exist
    if (!std::filesystem::exists(target_path)) {
        try {
            std::filesystem::create_directories(target_path);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create installation directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // Get current executable path
    std::filesystem::path current_exe = std::filesystem::current_path();
    
    // Copy files to target path
    std::vector<std::string> exclude_patterns = {
        "\\.git", 
        "build", 
        "\\.vscode", 
        "CMakeFiles", 
        "CMakeCache\\.txt",
        "\\.github"
    };
    
    if (!copy_files(current_exe, target_path, exclude_patterns)) {
        logger::print_error("Failed to copy files to installation directory");
        return false;
    }
    
    // Create executable links or shortcuts
    std::filesystem::path bin_path = target_path / "bin";
    if (!create_executable_links(bin_path)) {
        logger::print_warning("Failed to create executable links");
    }
    
    // Update PATH environment variable if requested
    if (add_to_path) {
        if (!update_path_env(bin_path)) {
            logger::print_warning("Failed to update PATH environment variable");
        } else {
            logger::print_success("Added installation directory to PATH environment variable");
        }
    }
    
    logger::print_success("cforge has been installed successfully to " + target_path.string());
    return true;
}

bool installer::update() {
    logger::print_status("Updating cforge...");
    
    // Check if cforge is installed
    if (!is_installed()) {
        logger::print_error("cforge is not installed. Please run 'cforge install' first.");
        return false;
    }
    
    std::string install_location = get_install_location();
    logger::print_status("Current installation: " + install_location);
    
    // Handle the update by reinstalling to the same location
    return install(install_location);
}

bool installer::install_project(const std::string& project_path, const std::string& install_path, bool add_to_path) {
    logger::print_status("Installing project from: " + project_path);
    
    // Check if project exists
    std::filesystem::path project_dir(project_path);
    if (!std::filesystem::exists(project_dir)) {
        logger::print_error("Project directory does not exist: " + project_path);
        return false;
    }
    
    // Check if it's a cforge project by looking for cforge.toml
    std::filesystem::path project_config = project_dir / CFORGE_FILE;
    if (!std::filesystem::exists(project_config)) {
        logger::print_error("Not a valid cforge project (missing " + std::string(CFORGE_FILE) + ")");
        return false;
    }
    
    // Read project configuration
    auto config = read_project_config(project_path);
    if (!config) {
        logger::print_error("Failed to read project configuration");
        return false;
    }
    
    // Get project name and details
    std::string project_name = config->get_string("project.name");
    std::string project_version = config->get_string("project.version");
    std::string project_type = config->get_string("project.type", "executable");
    
    if (project_name.empty()) {
        logger::print_error("Project name not specified in " + std::string(CFORGE_FILE));
        return false;
    }
    
    logger::print_status("Project: " + project_name + " (version " + project_version + ")");
    
    // Determine install path
    std::filesystem::path target_path;
    if (install_path.empty()) {
        if (project_type == "executable") {
            // For executables, install to a standard bin location
            std::filesystem::path platform_path = get_platform_specific_path();
            target_path = std::filesystem::path(platform_path) / "installed" / project_name;
        } else {
            // For libraries, install to a standard lib location
            std::filesystem::path platform_path = get_platform_specific_path();
            target_path = std::filesystem::path(platform_path) / "lib" / project_name;
        }
    } else {
        target_path = install_path;
    }
    
    logger::print_status("Installing to: " + target_path.string());
    
    // Check if path is writable
    if (!is_path_writeable(target_path)) {
        logger::print_error("Installation path is not writable: " + target_path.string());
        return false;
    }
    
    // Create the directory if it doesn't exist
    if (!std::filesystem::exists(target_path)) {
        try {
            std::filesystem::create_directories(target_path);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create installation directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // Build the project first
    logger::print_status("Building project before installation...");
    
    // Change to project directory
    std::filesystem::path original_path = std::filesystem::current_path();
    std::filesystem::current_path(project_dir);
    
    // Check if there's a CMakeLists.txt file
    bool has_cmake = std::filesystem::exists(project_dir / "CMakeLists.txt");
    bool build_success = false;
    
    if (has_cmake) {
        // Create build directory if it doesn't exist
        std::filesystem::path build_dir = project_dir / "build";
        if (!std::filesystem::exists(build_dir)) {
            try {
                std::filesystem::create_directories(build_dir);
                print_verbose("Created build directory: " + build_dir.string());
            } catch (const std::exception& ex) {
                logger::print_warning("Failed to create build directory: " + std::string(ex.what()));
            }
        }
        
        // Check if CMakeCache.txt exists and remove it to avoid generator platform mismatch issues
        std::filesystem::path cmake_cache = build_dir / "CMakeCache.txt";
        if (std::filesystem::exists(cmake_cache)) {
            try {
                logger::print_status("Removing existing CMake cache to avoid platform mismatch issues");
                std::filesystem::remove(cmake_cache);
            } catch (const std::exception& ex) {
                logger::print_warning("Failed to remove CMake cache: " + std::string(ex.what()));
            }
        }
        
        // Also remove CMakeFiles directory if it exists
        std::filesystem::path cmake_files = build_dir / "CMakeFiles";
        if (std::filesystem::exists(cmake_files)) {
            try {
                logger::print_status("Removing existing CMake files");
                std::filesystem::remove_all(cmake_files);
            } catch (const std::exception& ex) {
                logger::print_warning("Failed to remove CMake files: " + std::string(ex.what()));
            }
        }
        
        // Run CMake configuration step
        logger::print_status("Configuring project with CMake...");
        std::filesystem::current_path(build_dir);
        
        try {
#ifdef _WIN32
            // On Windows, use Ninja generator with MSVC for better compatibility
            // First check if Ninja is available
            bool ninja_available = is_command_available("ninja");
            bool cmake_available = is_command_available("cmake");
            
            if (!cmake_available) {
                logger::print_error("CMake is not available. Please install CMake and make sure it's in your PATH.");
                logger::print_status("You can download CMake from https://cmake.org/download/");
                return false;
            }
            
            if (ninja_available) {
                logger::print_status("Using Ninja generator for faster builds");
                std::vector<std::string> cmake_args = {"..", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"};
                build_success = execute_tool("cmake", cmake_args, build_dir.string(), "CMake Configure", true, 120);
                
                if (build_success) {
                    // Build the project with Ninja
                    logger::print_status("Building project in Release mode...");
                    std::vector<std::string> ninja_args = {};
                    build_success = execute_tool("ninja", ninja_args, build_dir.string(), "Ninja Build", true, 300);
                }
            } else {
                // Fallback to Visual Studio generator
                logger::print_status("Ninja not found, using Visual Studio generator");
                std::vector<std::string> cmake_args = {"..", "-A", "x64"};
                build_success = execute_tool("cmake", cmake_args, build_dir.string(), "CMake Configure", true, 120);
                
                if (build_success) {
                    // Build the project with CMake
                    logger::print_status("Building project in Release mode...");
                    std::vector<std::string> build_args = {"--build", ".", "--config", "Release"};
                    build_success = execute_tool("cmake", build_args, build_dir.string(), "CMake Build", true, 300);
                }
            }
#else
            // On Unix, use Unix Makefiles generator
            bool cmake_available = is_command_available("cmake");
            bool make_available = is_command_available("make");
            
            if (!cmake_available) {
                logger::print_error("CMake is not available. Please install CMake and make sure it's in your PATH.");
                logger::print_status("You can install CMake using your package manager (apt, yum, brew, etc.)");
                return false;
            }
            
            std::vector<std::string> cmake_args = {"..", "-DCMAKE_BUILD_TYPE=Release"};
            build_success = execute_tool("cmake", cmake_args, build_dir.string(), "CMake Configure", true, 120);
            
            if (build_success) {
                if (!make_available) {
                    logger::print_error("Make is not available. Please install Make and make sure it's in your PATH.");
                    return false;
                }
                
                // Build the project with make
                logger::print_status("Building project in Release mode...");
                std::vector<std::string> make_args = {"-j4"};
                build_success = execute_tool("make", make_args, build_dir.string(), "Make Build", true, 300);
            }
#endif

            if (!build_success) {
                logger::print_warning("Build process failed. Will attempt to install anyway in case pre-built binaries exist.");
            }
        } catch (const std::exception& ex) {
            logger::print_warning("Error during build: " + std::string(ex.what()));
            build_success = false;
        }
    } else {
        // No CMake, try looking for a Makefile
        if (std::filesystem::exists(project_dir / "Makefile")) {
            logger::print_status("Building project with Makefile...");
            try {
#ifdef _WIN32
                bool make_available = is_command_available("make") || 
                                     is_command_available("mingw32-make") || 
                                     is_command_available("nmake");
                if (!make_available) {
                    logger::print_warning("No make tool found (make, mingw32-make, nmake). Skipping build.");
                    build_success = false;
                } else {
                    // Try different make tools
                    if (is_command_available("make")) {
                        int make_result = system("make");
                        build_success = (make_result == 0);
                    } else if (is_command_available("mingw32-make")) {
                        int make_result = system("mingw32-make");
                        build_success = (make_result == 0);
                    } else if (is_command_available("nmake")) {
                        int make_result = system("nmake");
                        build_success = (make_result == 0);
                    }
                }
#else
                bool make_available = is_command_available("make");
                if (!make_available) {
                    logger::print_warning("Make is not available. Skipping build.");
                    build_success = false;
                } else {
                    int make_result = system("make");
                    build_success = (make_result == 0);
                }
#endif
            } catch (const std::exception& ex) {
                logger::print_warning("Error during build: " + std::string(ex.what()));
                build_success = false;
            }
        } else {
            // Assume the project is pre-built
            logger::print_status("No build system detected. Assuming pre-built project.");
            build_success = true;
        }
    }
    
    if (!build_success) {
        logger::print_warning("Build process failed or was skipped. Will try to find pre-built executables.");
    } else {
        logger::print_success("Build completed successfully.");
    }
    
    // Restore original path
    std::filesystem::current_path(original_path);
    
    // Copy files to target path
    std::vector<std::string> exclude_patterns = {
        "\\.git", 
        "build", 
        "\\.vscode", 
        "CMakeFiles", 
        "CMakeCache\\.txt",
        "\\.github",
        "src",
        "tests"
    };
    
    if (project_type == "executable") {
        // For executables, only copy the binary and necessary resources
        logger::print_status("Installing executable...");
        
        // Copy the binary - check both Release and Debug folders
        std::filesystem::path bin_dir = project_dir / "bin" / "Release";
        bool found_executable = false;
        
        // If Release directory doesn't exist, try Debug
        if (!std::filesystem::exists(bin_dir)) {
            bin_dir = project_dir / "bin" / "Debug";
            print_verbose("Release directory not found, trying Debug directory: " + bin_dir.string());
        }
        
        // Also check the bin directory directly
        if (!std::filesystem::exists(bin_dir)) {
            bin_dir = project_dir / "bin";
            print_verbose("Neither Release nor Debug directory found, trying bin directory: " + bin_dir.string());
        }
        
        // Function to check if an executable is a valid project executable and not a CMAKE temp file
        auto is_valid_executable = [&project_name](const std::filesystem::path& path) -> bool {
            std::string filename = path.filename().string();
            
            // Exclude known CMake test files
            if (filename.find("CMakeC") != std::string::npos || 
                filename.find("CMakeCXX") != std::string::npos ||
                filename.find("CompilerId") != std::string::npos) {
                return false;
            }
            
            // Try to match the project name - case insensitive comparison
            std::string lower_filename = filename;
            std::string lower_project = project_name;
            std::transform(lower_filename.begin(), lower_filename.end(), lower_filename.begin(), ::tolower);
            std::transform(lower_project.begin(), lower_project.end(), lower_project.begin(), ::tolower);
            
            // If filename contains the project name, it's likely the right executable
            if (lower_filename.find(lower_project) != std::string::npos) {
                return true;
            }
            
            // Normalize project name by replacing hyphens with underscores, just like we do during project creation
            std::string normalized_project = lower_project;
            std::replace(normalized_project.begin(), normalized_project.end(), '-', '_');
            
            // Check again with normalized project name
            if (normalized_project != lower_project && lower_filename.find(normalized_project) != std::string::npos) {
                return true;
            }
            
            // Check for common executable names
            if (lower_filename.find("app") != std::string::npos || 
                lower_filename.find("main") != std::string::npos || 
                lower_filename.find("launcher") != std::string::npos) {
                return true;
            }
            
            // If we're still looking, sometimes the executable might be named differently
            // But it's usually not a test file with certain patterns
            return filename.find("test_") == std::string::npos && 
                   filename.find("cmake") == std::string::npos && 
                   filename.find("config") == std::string::npos;
        };
        
        if (std::filesystem::exists(bin_dir)) {
            print_verbose("Searching for executables in: " + bin_dir.string());
            for (const auto& entry : std::filesystem::directory_iterator(bin_dir)) {
                if (entry.is_regular_file()) {
                    // In Windows, check for .exe extension
#ifdef _WIN32
                    if (entry.path().extension() == ".exe") {
                        print_verbose("Found potential executable: " + entry.path().string());
                        
                        // Make sure it's not a CMake test file
                        if (!is_valid_executable(entry.path())) {
                            print_verbose("Skipping CMake test executable: " + entry.path().string());
                            continue;
                        }
#else
                    // In Unix-like systems, check for executable permission
                    if (entry.permissions(std::filesystem::status(entry.path())) & std::filesystem::perms::owner_exec) {
                        if (!is_valid_executable(entry.path())) {
                            print_verbose("Skipping test executable: " + entry.path().string());
                            continue;
                        }
#endif
                        print_verbose("Using project executable: " + entry.path().string());
                        try {
                            std::filesystem::create_directories(target_path);
                            std::filesystem::copy_file(
                                entry.path(), 
                                target_path / entry.path().filename(), 
                                std::filesystem::copy_options::overwrite_existing
                            );
                            print_verbose("Copied " + entry.path().string() + " to " + (target_path / entry.path().filename()).string());
                            found_executable = true;
                        } catch (const std::exception& ex) {
                            logger::print_error("Failed to copy binary: " + std::string(ex.what()));
                        }
                    }
                }
            }
        }
        
        // If no executable found yet, look in the build directory
        if (!found_executable) {
            print_verbose("No executable found in bin directory, searching build directory...");
            std::filesystem::path build_dir = project_dir / "build";
            
            if (std::filesystem::exists(build_dir)) {
                print_verbose("Searching recursively in build directory: " + build_dir.string());
                // Recursively search for executables in build directory
                for (const auto& entry : std::filesystem::recursive_directory_iterator(build_dir)) {
                    if (entry.is_regular_file()) {
#ifdef _WIN32
                        if (entry.path().extension() == ".exe") {
                            print_verbose("Found potential executable: " + entry.path().string());
                            
                            // Skip CMake test files
                            if (!is_valid_executable(entry.path())) {
                                print_verbose("Skipping CMake test executable: " + entry.path().string());
                                continue;
                            }
                            
                            print_verbose("Using project executable: " + entry.path().string());
#else
                        if (entry.permissions(std::filesystem::status(entry.path())) & std::filesystem::perms::owner_exec) {
                            if (!is_valid_executable(entry.path())) {
                                print_verbose("Skipping test executable: " + entry.path().string());
                                continue;
                            }
#endif
                            try {
                                std::filesystem::create_directories(target_path);
                                std::filesystem::copy_file(
                                    entry.path(), 
                                    target_path / entry.path().filename(), 
                                    std::filesystem::copy_options::overwrite_existing
                                );
                                print_verbose("Copied " + entry.path().string() + " to " + (target_path / entry.path().filename()).string());
                                found_executable = true;
                                break; // Stop after finding first valid executable
                            } catch (const std::exception& ex) {
                                logger::print_error("Failed to copy binary: " + std::string(ex.what()));
                            }
                        }
                    }
                }
            }
        }
        
        // Final fallback: search entire project directory for .exe files (Windows only)
#ifdef _WIN32
        if (!found_executable) {
            print_verbose("Still no executable found, searching throughout project directory for any valid .exe files...");
            try {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(project_dir)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".exe") {
                        // Skip CMake test files
                        if (!is_valid_executable(entry.path())) {
                            print_verbose("Skipping CMake test executable: " + entry.path().string());
                            continue;
                        }
                        
                        print_verbose("Found executable: " + entry.path().string());
                        try {
                            std::filesystem::create_directories(target_path);
                            std::filesystem::copy_file(
                                entry.path(), 
                                target_path / entry.path().filename(), 
                                std::filesystem::copy_options::overwrite_existing
                            );
                            print_verbose("Copied " + entry.path().string() + " to " + (target_path / entry.path().filename()).string());
                            found_executable = true;
                            break; // Stop after finding first executable
                        } catch (const std::exception& ex) {
                            logger::print_error("Failed to copy binary: " + std::string(ex.what()));
                        }
                    }
                }
            } catch (const std::exception& ex) {
                logger::print_warning("Error searching for executables: " + std::string(ex.what()));
            }
        }
#endif
        
        // If there's a resources directory, copy it too
        std::filesystem::path resources_dir = project_dir / "resources";
        if (std::filesystem::exists(resources_dir)) {
            try {
                copy_files(resources_dir, target_path / "resources");
                print_verbose("Copied resources directory");
            } catch (const std::exception& ex) {
                logger::print_warning("Failed to copy resources: " + std::string(ex.what()));
            }
        }
        
        // Create executable links or shortcuts for the installed application
        if (!create_executable_links(target_path)) {
            logger::print_warning("Failed to create executable links for installed application");
        }
    } else {
        // For libraries, copy everything except exclude patterns
        if (!copy_files(project_dir, target_path, exclude_patterns)) {
            logger::print_error("Failed to copy files to installation directory");
            return false;
        }
        
        // Copy headers to include directory
        std::filesystem::path include_dir = project_dir / "include";
        if (std::filesystem::exists(include_dir)) {
            std::filesystem::path platform_path = get_platform_specific_path();
            std::filesystem::path target_include = std::filesystem::path(platform_path) / "include" / project_name;
            
            try {
                std::filesystem::create_directories(target_include);
                copy_files(include_dir, target_include);
                print_verbose("Copied headers to " + target_include.string());
            } catch (const std::exception& ex) {
                logger::print_warning("Failed to copy headers: " + std::string(ex.what()));
            }
        }
        
        // Copy libraries to lib directory
        std::filesystem::path lib_dir = project_dir / "lib" / "Release";
        if (std::filesystem::exists(lib_dir)) {
            std::filesystem::path platform_path = get_platform_specific_path();
            std::filesystem::path target_lib = std::filesystem::path(platform_path) / "lib";
            
            try {
                std::filesystem::create_directories(target_lib);
                for (const auto& entry : std::filesystem::directory_iterator(lib_dir)) {
                    if (entry.is_regular_file()) {
                        std::filesystem::copy_file(
                            entry.path(), 
                            target_lib / entry.path().filename(), 
                            std::filesystem::copy_options::overwrite_existing
                        );
                        print_verbose("Copied " + entry.path().string() + " to " + target_lib.string());
                    }
                }
            } catch (const std::exception& ex) {
                logger::print_warning("Failed to copy libraries: " + std::string(ex.what()));
            }
        }
    }
    
    // Create executable links or shortcuts for the installed application
    if (!create_executable_links(target_path)) {
        logger::print_warning("Failed to create executable links for installed application");
    }
    
    // Update PATH environment variable if requested
    if (add_to_path) {
        if (!update_path_env(target_path)) {
            logger::print_warning("Failed to update PATH environment variable");
        } else {
            logger::print_success("Added installation directory to PATH environment variable");
        }
    }
    
    logger::print_success("Project " + project_name + " has been installed successfully to " + target_path.string());
    return true;
}

std::string installer::get_default_install_path() const {
    return get_platform_specific_path();
}

bool installer::is_installed() const {
    std::string install_path = get_install_location();
    return !install_path.empty() && std::filesystem::exists(install_path);
}

std::string installer::get_install_location() const {
    // Try to find cforge installation
    
#ifdef _WIN32
    // On Windows, check the registry first
    HKEY h_key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\cforge", 0, KEY_READ, &h_key) == ERROR_SUCCESS) {
        char buffer[MAX_PATH];
        DWORD buffer_size = sizeof(buffer);
        
        if (RegQueryValueExA(h_key, "InstallLocation", NULL, NULL, (LPBYTE)buffer, &buffer_size) == ERROR_SUCCESS) {
            RegCloseKey(h_key);
            return std::string(buffer);
        }
        
        RegCloseKey(h_key);
    }
    
    // Check Program Files
    char program_files[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, program_files) == S_OK) {
        std::filesystem::path path = std::filesystem::path(program_files) / "cforge";
        if (std::filesystem::exists(path)) {
            return path.string();
        }
    }
#else
    // On Unix-like systems, check standard locations
    std::vector<std::string> locations = {
        "/usr/local/bin/cforge",
        "/usr/bin/cforge",
        "/opt/cforge"
    };
    
    for (const auto& loc : locations) {
        if (std::filesystem::exists(loc)) {
            return loc;
        }
    }
#endif
    
    // As a fallback, check the PATH
    std::string path_env = getenv("PATH") ? getenv("PATH") : "";
    std::vector<std::string> paths;
    
#ifdef _WIN32
    std::string delimiter = ";";
#else
    std::string delimiter = ":";
#endif
    
    size_t pos = 0;
    std::string token;
    while ((pos = path_env.find(delimiter)) != std::string::npos) {
        token = path_env.substr(0, pos);
        paths.push_back(token);
        path_env.erase(0, pos + delimiter.length());
    }
    paths.push_back(path_env);
    
    for (const auto& p : paths) {
        std::filesystem::path potential_path = std::filesystem::path(p);
#ifdef _WIN32
        potential_path /= "cforge.exe";
#else
        potential_path /= "cforge";
#endif
        
        if (std::filesystem::exists(potential_path)) {
            return std::filesystem::canonical(potential_path).parent_path().string();
        }
    }
    
    return "";
}

bool installer::copy_files(const std::filesystem::path& source, const std::filesystem::path& dest,
                          const std::vector<std::string>& exclude_patterns) {
    try {
        // Create destination directory if it doesn't exist
        if (!std::filesystem::exists(dest)) {
            std::filesystem::create_directories(dest);
        }
        
        // Function to check if path should be excluded
        auto should_exclude = [&exclude_patterns](const std::filesystem::path& path) -> bool {
            // Get a std::string once, don't create multiple string objects
            std::string path_str = path.filename().string();
            
            // Special case for hidden files/directories (starting with .)
            if (!path_str.empty() && path_str[0] == '.') {
                return true;
            }
            
            for (const auto& pattern : exclude_patterns) {
                try {
                    // Avoid regex if possible - use simple string matching for common patterns
                    if (pattern.find("*") == std::string::npos && 
                        pattern.find("?") == std::string::npos && 
                        pattern.find("[") == std::string::npos && 
                        pattern.find(".") == std::string::npos) {
                        // Simple substring match for basic patterns
                        if (path_str.find(pattern) != std::string::npos) {
                            return true;
                        }
                    } else if (pattern.find("\\") == std::string::npos && 
                               pattern == ".*" && 
                               !path_str.empty() && path_str[0] == '.') {
                        // Special case for ".*" pattern to match hidden files
                        return true;
                    } else {
                        // Use regex for more complex patterns
                        // Create a completely new copy of the strings to avoid iterator issues
                        std::string regex_pattern = pattern;
                        std::string check_path = path_str;
                        
                        // Compile regex
                        std::regex regex(regex_pattern);
                        
                        // Use regex_match for full string match instead of regex_search for partial match
                        // This is safer and less likely to cause issues
                        if (std::regex_match(check_path.begin(), check_path.end(), regex)) {
                            return true;
                        }
                    }
                } catch (const std::regex_error& e) {
                    // Failed to compile regex, fall back to simple substring match
                    if (path_str.find(pattern) != std::string::npos) {
                        return true;
                    }
                }
            }
            return false;
        };
        
        // Copy files and directories recursively
        for (const auto& entry : std::filesystem::directory_iterator(source)) {
            if (should_exclude(entry.path())) {
                continue;
            }
            
            std::filesystem::path target = dest / entry.path().filename();
            
            if (entry.is_directory()) {
                copy_files(entry.path(), target, exclude_patterns);
            } else if (entry.is_regular_file()) {
                std::filesystem::copy_file(
                    entry.path(), 
                    target, 
                    std::filesystem::copy_options::overwrite_existing
                );
                print_verbose("Copied " + entry.path().string() + " to " + target.string());
            }
        }
        
        return true;
    } catch (const std::exception& ex) {
        logger::print_error("Error copying files: " + std::string(ex.what()));
        return false;
    }
}

std::string installer::get_platform_specific_path() const {
#ifdef _WIN32
    // On Windows, use Program Files
    char program_files[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_PROGRAM_FILES, NULL, 0, program_files) == S_OK) {
        return std::string(program_files) + "\\cforge";
    }
    return "C:\\Program Files\\cforge";
#else
    // On Unix-like systems, use /usr/local
    return "/usr/local/cforge";
#endif
}

bool installer::is_path_writeable(const std::filesystem::path& path) const {
    if (!std::filesystem::exists(path)) {
        // If path doesn't exist, check if parent directory is writable
        auto parent = path.parent_path();
        if (!std::filesystem::exists(parent)) {
            return false;
        }
        
        // Create a temporary file to test permissions
        auto test_file = parent / "cforge_write_test";
        try {
            std::ofstream file(test_file);
            bool result = file.is_open();
            file.close();
            if (std::filesystem::exists(test_file)) {
                std::filesystem::remove(test_file);
            }
            return result;
        } catch (...) {
            return false;
        }
    } else {
        // Path exists, check if we can write to it
        auto test_file = path / "cforge_write_test";
        try {
            std::ofstream file(test_file);
            bool result = file.is_open();
            file.close();
            if (std::filesystem::exists(test_file)) {
                std::filesystem::remove(test_file);
            }
            return result;
        } catch (...) {
            return false;
        }
    }
}

bool installer::create_executable_links(const std::filesystem::path& bin_path) const {
#ifdef _WIN32
    // On Windows, create shortcuts or add to PATH
    
    // Make sure the directory exists
    if (!std::filesystem::exists(bin_path)) {
        try {
            std::filesystem::create_directories(bin_path);
            print_verbose("Created directory: " + bin_path.string());
        } catch (const std::exception& ex) {
            logger::print_warning("Failed to create bin directory: " + std::string(ex.what()));
        }
    }
    
    // First try to find executables directly in the specified path
    print_verbose("Searching for executables in: " + bin_path.string());
    
    // Helper function to determine if an executable is a valid project executable (not a CMake test file)
    auto is_valid_executable = [](const std::filesystem::path& path) -> bool {
        std::string filename = path.filename().string();
        
        // Exclude known CMake test files
        if (filename.find("CMakeC") != std::string::npos || 
            filename.find("CMakeCXX") != std::string::npos ||
            filename.find("CompilerId") != std::string::npos) {
            return false;
        }
        
        // Usually not a test file with certain patterns
        return filename.find("test_") == std::string::npos && 
               filename.find("cmake") == std::string::npos && 
               filename.find("config") == std::string::npos;
    };
    
    // Find the executable in the bin directory
    std::filesystem::path exe_path;
    
    // Multiple search locations in priority order
    std::vector<std::filesystem::path> search_locations = {
        bin_path,                              // First check the bin path
        bin_path.parent_path(),                // Then check parent dir
        bin_path.parent_path() / "Debug",      // Check Debug folder
        bin_path.parent_path() / "Release",    // Check Release folder
        bin_path.parent_path() / "build",      // Check build folder
        bin_path.parent_path() / "build-release" // Check build-release folder
    };
    
    for (const auto& location : search_locations) {
        if (!std::filesystem::exists(location)) {
            print_verbose("Directory doesn't exist: " + location.string());
            continue;
        }
        
        logger::print_status("Looking for executables in: " + location.string());
        
        for (const auto& entry : std::filesystem::directory_iterator(location)) {
            if (entry.is_regular_file() && entry.path().extension() == ".exe") {
                // Skip CMake temporary files
                if (!is_valid_executable(entry.path())) {
                    print_verbose("Skipping CMake test executable: " + entry.path().string());
                    continue;
                }
                
                exe_path = entry.path();
                logger::print_status("Found valid executable: " + exe_path.string());
                
                // Copy to bin directory if not already there
                if (location != bin_path) {
                    try {
                        // Use the parent directory name for the exe if it's just "a.exe"
                        std::filesystem::path target_filename = exe_path.filename();
                        if (target_filename.string() == "a.exe") {
                            // Extract project name from directory name
                            std::string project_name = bin_path.parent_path().filename().string();
                            // Remove any hyphens and replace with underscores
                            std::replace(project_name.begin(), project_name.end(), '-', '_');
                            
                            target_filename = project_name + ".exe";
                            logger::print_status("Renaming generic 'a.exe' to '" + target_filename.string() + "'");
                        }
                        
                        std::filesystem::path target = bin_path / target_filename;
                        std::filesystem::copy_file(
                            exe_path, 
                            target, 
                            std::filesystem::copy_options::overwrite_existing
                        );
                        exe_path = target;
                        logger::print_status("Copied executable to bin directory: " + exe_path.string());
                    } catch (const std::exception& ex) {
                        logger::print_warning("Failed to copy executable: " + std::string(ex.what()));
                        // Continue with original path
                    }
                }
                break;
            }
        }
        
        if (!exe_path.empty()) {
            break; // Found an executable, so stop searching
        }
    }
    
    if (exe_path.empty()) {
        logger::print_error("Could not find executable");
        return false;
    }
    
    // Add to registry - only for cforge itself, not for installed projects
    if (exe_path.filename().string().find("cforge") != std::string::npos) {
        HKEY h_key;
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\cforge", 0, NULL, 
                            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &h_key, NULL) == ERROR_SUCCESS) {
            std::string install_location = bin_path.parent_path().string();
            RegSetValueExA(h_key, "InstallLocation", 0, REG_SZ, 
                          (const BYTE*)install_location.c_str(), (DWORD)install_location.size() + 1);
            
            RegCloseKey(h_key);
            print_verbose("Added registry entry for cforge");
        } else {
            logger::print_warning("Failed to create registry entry (may need administrator privileges)");
        }
    }
    
    // For installed projects, we don't need to create Start Menu shortcuts
    if (exe_path.filename().string().find("cforge") != std::string::npos) {
        // Create Start Menu shortcut
        char start_menu[MAX_PATH];
        if (SHGetFolderPathA(NULL, CSIDL_COMMON_PROGRAMS, NULL, 0, start_menu) == S_OK) {
            std::filesystem::path shortcut_dir = std::filesystem::path(start_menu) / "cforge";
            
            if (!std::filesystem::exists(shortcut_dir)) {
                std::filesystem::create_directories(shortcut_dir);
            }
            
            // Use COM to create the shortcut
            IShellLinkA* p_shell_link = NULL;
            IPersistFile* p_persist_file = NULL;
            
            HRESULT hr = CoInitialize(NULL);
            if (SUCCEEDED(hr)) {
                hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, 
                                     IID_IShellLinkA, (void**)&p_shell_link);
                
                if (SUCCEEDED(hr)) {
                    p_shell_link->SetPath(exe_path.string().c_str());
                    p_shell_link->SetDescription("cforge C/C++ Build System");
                    p_shell_link->SetWorkingDirectory(bin_path.parent_path().string().c_str());
                    
                    hr = p_shell_link->QueryInterface(IID_IPersistFile, (void**)&p_persist_file);
                    
                    if (SUCCEEDED(hr)) {
                        std::filesystem::path shortcut_path = shortcut_dir / "cforge.lnk";
                        
                        // Convert to wide string
                        std::wstring w_path(shortcut_path.string().begin(), shortcut_path.string().end());
                        
                        hr = p_persist_file->Save(w_path.c_str(), TRUE);
                        
                        if (SUCCEEDED(hr)) {
                            print_verbose("Created Start Menu shortcut");
                        } else {
                            logger::print_warning("Failed to save shortcut");
                        }
                        
                        p_persist_file->Release();
                    }
                    
                    p_shell_link->Release();
                }
                
                CoUninitialize();
            }
        }
    }
    
    return true;
#else
    // On Unix-like systems, create symlinks in standard locations
    std::filesystem::path exe_path;
    
    // Find the executable
    for (const auto& entry : std::filesystem::directory_iterator(bin_path)) {
        if (entry.is_regular_file() && entry.permissions(std::filesystem::status(entry.path())) & std::filesystem::perms::owner_exec) {
            exe_path = entry.path();
            print_verbose("Found executable: " + exe_path.string());
            break;
        }
    }
    
    if (exe_path.empty()) {
        // Try to find in parent directory
        for (const auto& entry : std::filesystem::directory_iterator(bin_path.parent_path())) {
            if (entry.is_regular_file() && entry.permissions(std::filesystem::status(entry.path())) & std::filesystem::perms::owner_exec) {
                exe_path = entry.path();
                print_verbose("Found executable in parent directory: " + exe_path.string());
                break;
            }
        }
    }
    
    if (exe_path.empty()) {
        logger::print_error("Could not find executable");
        return false;
    }
    
    // Create symlink in /usr/local/bin only for cforge itself, not for installed projects
    if (exe_path.filename().string() == "cforge") {
        try {
            std::filesystem::path usr_local_bin("/usr/local/bin");
            if (std::filesystem::exists(usr_local_bin)) {
                std::filesystem::path link_path = usr_local_bin / "cforge";
                
                // Remove existing symlink if it exists
                if (std::filesystem::exists(link_path)) {
                    std::filesystem::remove(link_path);
                }
                
                std::filesystem::create_symlink(exe_path, link_path);
                print_verbose("Created symlink in /usr/local/bin");
            }
        } catch (const std::exception& ex) {
            logger::print_warning("Failed to create symlink in /usr/local/bin: " + std::string(ex.what()));
        }
    }
#endif
    
    return true;
}

bool installer::update_path_env(const std::filesystem::path& bin_path) const {
#ifdef _WIN32
    // Get the bin directory
    std::filesystem::path path_to_add = bin_path;
    
    // Ensure it ends with \bin for consistency
    if (path_to_add.filename() != "bin") {
        path_to_add /= "bin";
    }
    
    // Make sure the directory exists
    if (!std::filesystem::exists(path_to_add)) {
        try {
            std::filesystem::create_directories(path_to_add);
            print_verbose("Created bin directory: " + path_to_add.string());
        } catch (const std::exception& ex) {
            logger::print_warning("Failed to create bin directory: " + std::string(ex.what()));
        }
    }
    
    logger::print_status("Adding to PATH environment variable: " + path_to_add.string());
    
    // Get the current PATH
    HKEY hkey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ | KEY_WRITE, &hkey) != ERROR_SUCCESS) {
        logger::print_error("Failed to open registry key for PATH environment variable");
        return false;
    }
    
    char current_path[32767] = {0};  // Max environment variable length
    DWORD buf_size = sizeof(current_path);
    DWORD type = 0;
    
    if (RegQueryValueExA(hkey, "PATH", NULL, &type, (BYTE*)current_path, &buf_size) != ERROR_SUCCESS) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            RegCloseKey(hkey);
            logger::print_error("Failed to query PATH environment variable");
            return false;
        }
        
        // PATH doesn't exist yet, create it
        current_path[0] = '\0';
    }
    
    // Check if the path is already in PATH
    std::string path_str = current_path;
    std::string path_to_add_str = path_to_add.string();
    
    // Normalize the path for comparison
    std::replace(path_to_add_str.begin(), path_to_add_str.end(), '/', '\\');
    
    // Check if path is already in PATH
    if (path_str.find(path_to_add_str) != std::string::npos) {
        logger::print_status("Path already in PATH environment variable");
        RegCloseKey(hkey);
        return true;
    }
    
    // Add path to PATH
    std::string new_path;
    if (path_str.empty()) {
        new_path = path_to_add_str;
    } else {
        // Add a semicolon if the current PATH doesn't end with one
        if (path_str.back() != ';') {
            new_path = path_str + ";" + path_to_add_str;
        } else {
            new_path = path_str + path_to_add_str;
        }
    }
    
    // Update the PATH
    if (RegSetValueExA(hkey, "PATH", 0, REG_EXPAND_SZ, (BYTE*)new_path.c_str(), new_path.length() + 1) != ERROR_SUCCESS) {
        RegCloseKey(hkey);
        logger::print_error("Failed to update PATH environment variable");
        return false;
    }
    
    RegCloseKey(hkey);
    
    // Also try to update the system PATH if admin privileges are available
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", 
                     0, KEY_READ | KEY_WRITE, &hkey) == ERROR_SUCCESS) {
        
        buf_size = sizeof(current_path);
        if (RegQueryValueExA(hkey, "PATH", NULL, &type, (BYTE*)current_path, &buf_size) == ERROR_SUCCESS) {
            path_str = current_path;
            
            if (path_str.find(path_to_add_str) == std::string::npos) {
                // Add to system PATH too
                if (path_str.back() != ';') {
                    new_path = path_str + ";" + path_to_add_str;
                } else {
                    new_path = path_str + path_to_add_str;
                }
                
                if (RegSetValueExA(hkey, "PATH", 0, REG_EXPAND_SZ, (BYTE*)new_path.c_str(), new_path.length() + 1) == ERROR_SUCCESS) {
                    logger::print_status("Also added to system PATH (may require system restart)");
                }
            }
        }
        
        RegCloseKey(hkey);
    }
    
    // Broadcast environment change message
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, NULL);
    
    logger::print_status("Added to PATH environment variable. Changes may require a new terminal or system restart.");
    return true;
#else
    // On Unix-like systems, update .bashrc or similar
    
    // Get home directory
    const char* home_dir = getenv("HOME");
    if (!home_dir) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) {
            home_dir = pw->pw_dir;
        }
    }
    
    if (!home_dir) {
        logger::print_warning("Could not determine home directory");
        return false;
    }
    
    std::filesystem::path bashrc_path = std::filesystem::path(home_dir) / ".bashrc";
    std::filesystem::path zshrc_path = std::filesystem::path(home_dir) / ".zshrc";
    
    std::filesystem::path rc_path;
    if (std::filesystem::exists(zshrc_path)) {
        rc_path = zshrc_path;
    } else if (std::filesystem::exists(bashrc_path)) {
        rc_path = bashrc_path;
    } else {
        // Create .bashrc if it doesn't exist
        rc_path = bashrc_path;
        std::ofstream file(rc_path);
        file.close();
    }
    
    // Check if bin_path is already in PATH
    std::string bin_path_str = bin_path.string();
    std::string line;
    bool already_in_path = false;
    
    {
        std::ifstream file(rc_path);
        while (std::getline(file, line)) {
            if (line.find(bin_path_str) != std::string::npos && line.find("PATH") != std::string::npos) {
                already_in_path = true;
                break;
            }
        }
    }
    
    if (!already_in_path) {
        // Add bin_path to PATH
        std::ofstream file(rc_path, std::ios_base::app);
        file << "\n# Added by cforge installer\n";
        file << "export PATH=\"" << bin_path_str << ":$PATH\"\n";
        file.close();
        
        print_verbose("Added to PATH in " + rc_path.string());
    }
    
    return true;
#endif
}

std::unique_ptr<toml_reader> installer::read_project_config(const std::string& project_path) const {
    std::filesystem::path config_path = std::filesystem::path(project_path) / CFORGE_FILE;
    
    if (!std::filesystem::exists(config_path)) {
        return nullptr;
    }
    
    auto reader = std::make_unique<toml_reader>();
    if (!reader->load(config_path.string())) {
        return nullptr;
    }
    
    return reader;
}

} // namespace cforge 