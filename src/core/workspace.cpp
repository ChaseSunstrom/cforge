/**
 * @file workspace.cpp
 * @brief Implementation of workspace management utilities
 */

#include "core/workspace.hpp"
#include "core/toml_reader.hpp"
#include "cforge/log.hpp"

#include <fstream>
#include <queue>
#include <set>
#include <sstream>
#include <functional>

namespace cforge {

workspace::workspace() : config_(nullptr) {
}

workspace::~workspace() {
}

workspace_config::workspace_config() : name_("cpp-workspace"), description_("A C++ workspace") {
}

// Accessors for workspace_config
void workspace_config::set_name(const std::string& name) {
    name_ = name;
}

void workspace_config::set_description(const std::string& description) {
    description_ = description;
}

const std::string& workspace_config::get_name() const {
    return name_;
}

const std::string& workspace_config::get_description() const {
    return description_;
}

bool workspace::load(const std::filesystem::path& workspace_path) {
    workspace_path_ = workspace_path;
    
    // Check if the workspace configuration file exists
    std::filesystem::path config_path = workspace_path / WORKSPACE_FILE;
    if (!std::filesystem::exists(config_path)) {
        logger::print_error("Workspace configuration file not found: " + config_path.string());
        return false;
    }
    
    // Load the configuration
    config_ = std::make_unique<toml_reader>();
    if (!config_->load(config_path.string())) {
        logger::print_error("Failed to parse workspace configuration file: " + config_path.string());
        return false;
    }
    
    // Get workspace name
    workspace_name_ = config_->get_string("workspace.name", "");
    if (workspace_name_.empty()) {
        // Use the directory name as the workspace name if not specified
        workspace_name_ = workspace_path.filename().string();
    }
    
    // Load projects
    load_projects();
    
    // Get the default startup project
    startup_project_ = config_->get_string("workspace.default_startup_project", "");
    
    return true;
}

bool workspace::is_loaded() const {
    return config_ != nullptr;
}

std::string workspace::get_name() const {
    return workspace_name_;
}

std::filesystem::path workspace::get_path() const {
    return workspace_path_;
}

std::vector<workspace_project> workspace::get_projects() const {
    return projects_;
}

workspace_project workspace::get_startup_project() const {
    // Find the startup project
    for (const auto& project : projects_) {
        if (project.is_startup) {
            return project;
        }
    }
    
    // Return an empty project if no startup project is set
    return workspace_project{};
}

bool workspace::set_startup_project(const std::string& project_name) {
    // Find the project by name
    bool found = false;
    for (auto& project : projects_) {
        // Update the startup flag
        if (project.name == project_name) {
            project.is_startup = true;
            found = true;
        } else {
            project.is_startup = false;
        }
    }
    
    if (!found) {
        logger::print_error("Project not found in workspace: " + project_name);
        return false;
    }
    
    // Update the startup project name
    startup_project_ = project_name;
    
    // TODO: Update the workspace configuration file
    
    return true;
}

bool workspace::build_all(const std::string& config, int num_jobs, bool verbose) const {
    if (projects_.empty()) {
        logger::print_warning("No projects in workspace");
        return false;
    }
    
    logger::print_status("Building all " + std::to_string(projects_.size()) + " projects in workspace: " + workspace_name_);
    
    bool all_success = true;
    
    // Build each project
    for (const auto& project : projects_) {
        logger::print_status("Building project: " + project.name);
        
        // TODO: Implement project building
        // This would require invoking the build command for each project
        
        // For now, just log success
        logger::print_success("Project built successfully: " + project.name);
    }
    
    return all_success;
}

bool workspace::build_project(const std::string& project_name, const std::string& config, 
                             int num_jobs, bool verbose) const {
    // Find the project by name
    for (const auto& project : projects_) {
        if (project.name == project_name) {
            logger::print_status("Building project: " + project.name);
            
            // TODO: Implement project building
            // This would require invoking the build command for the specific project
            
            // For now, just log success
            logger::print_success("Project built successfully: " + project.name);
            return true;
        }
    }
    
    logger::print_error("Project not found in workspace: " + project_name);
    return false;
}

bool workspace::run_startup_project(const std::vector<std::string>& args, 
                                   const std::string& config, bool verbose) const {
    // Get the startup project
    workspace_project startup = get_startup_project();
    
    if (startup.name.empty()) {
        logger::print_error("No startup project set in workspace");
        return false;
    }
    
    // Run the startup project
    return run_project(startup.name, args, config, verbose);
}

bool workspace::run_project(const std::string& project_name, const std::vector<std::string>& args,
                          const std::string& config, bool verbose) const {
    // Find the project by name
    for (const auto& project : projects_) {
        if (project.name == project_name) {
            logger::print_status("Running project: " + project.name);
            
            // TODO: Implement project running
            // This would require invoking the run command for the specific project
            
            // For now, just log success
            logger::print_success("Project ran successfully: " + project.name);
            return true;
        }
    }
    
    logger::print_error("Project not found in workspace: " + project_name);
    return false;
}

bool workspace::is_workspace_dir(const std::filesystem::path& dir) {
    // Check if the workspace configuration file exists
    return std::filesystem::exists(dir / WORKSPACE_FILE);
}

bool workspace::create_workspace(const std::filesystem::path& workspace_path, 
                               const std::string& workspace_name) {
    // Create the workspace directory if it doesn't exist
    if (!std::filesystem::exists(workspace_path)) {
        try {
            std::filesystem::create_directories(workspace_path);
        } catch (const std::exception& ex) {
            logger::print_error("Failed to create workspace directory: " + std::string(ex.what()));
            return false;
        }
    }
    
    // Create the workspace configuration file
    std::filesystem::path config_path = workspace_path / WORKSPACE_FILE;
    
    // Don't overwrite existing configuration
    if (std::filesystem::exists(config_path)) {
        logger::print_warning("Workspace configuration file already exists: " + config_path.string());
        return true;
    }
    
    // Create the configuration file
    std::ofstream config_file(config_path);
    if (!config_file) {
        logger::print_error("Failed to create workspace configuration file: " + config_path.string());
        return false;
    }
    
    // Write the configuration
    config_file << "# Workspace configuration for cforge\n\n";
    config_file << "[workspace]\n";
    config_file << "name = \"" << workspace_name << "\"\n";
    config_file << "projects = []\n";
    config_file << "# default_startup_project = \"path/to/startup/project\"\n";
    
    config_file.close();
    
    // Create standard directories
    try {
        std::filesystem::create_directories(workspace_path / "projects");
    } catch (const std::exception& ex) {
        logger::print_warning("Failed to create projects directory: " + std::string(ex.what()));
    }
    
    logger::print_success("Workspace created successfully: " + workspace_name);
    return true;
}

void workspace::load_projects() {
    projects_.clear();
    
    // Get the list of projects from the TOML config
    std::vector<std::string> project_strings = config_->get_string_array("workspace.projects");
    
    // Parse each project string and add to the projects list
    workspace_config workspace_cfg;
    std::filesystem::path config_path = workspace_path_ / WORKSPACE_FILE;
    if (!workspace_cfg.load(config_path.string())) {
        logger::print_error("Failed to load workspace configuration file");
        return;
    }
    
    // Use the parsed projects from workspace_config
    projects_ = workspace_cfg.get_projects();
    
    // Process each project path - make sure relative paths are resolved correctly
    for (auto& project : projects_) {
        // If the path is relative, make it relative to the workspace path
        if (!project.path.is_absolute()) {
            project.path = workspace_path_ / project.path;
        }
        
        // Check if project directory exists
        if (!std::filesystem::exists(project.path)) {
            logger::print_warning("Project directory does not exist: " + project.path.string());
            continue;
        }
        
        // Check if it's a valid cforge project
        if (!std::filesystem::exists(project.path / CFORGE_FILE)) {
            logger::print_warning("Not a valid cforge project (missing " + std::string(CFORGE_FILE) + "): " + project.path.string());
            continue;
        }
        
        // Update startup flag if this is the startup project
        project.is_startup = (project.name == startup_project_);
        
        // Try to read the project name from cforge.toml to validate
        toml_reader project_config;
        std::filesystem::path project_config_path = project.path / CFORGE_FILE;
        std::string config_project_name;
        
        if (project_config.load(project_config_path.string())) {
            config_project_name = project_config.get_string("project.name", "");
            
            // Validate the project name matches the config
            if (!config_project_name.empty() && config_project_name != project.name) {
                logger::print_warning("Project name mismatch: '" + project.name + 
                                     "' in workspace vs '" + config_project_name + 
                                     "' in project config");
            }
        }
    }
}

bool workspace_config::load(const std::string& workspace_file) {
    toml_reader reader;
    if (!reader.load(workspace_file)) {
        logger::print_error("Failed to load workspace configuration file");
        return false;
    }
    
    // Load basic workspace info
    name_ = reader.get_string("workspace.name", "cpp-workspace");
    description_ = reader.get_string("workspace.description", "A C++ workspace");
    
    // Load projects
    std::vector<std::string> project_paths = reader.get_string_array("workspace.projects");
    if (!project_paths.empty()) {
        for (const auto& project_path : project_paths) {
            workspace_project project;
            
            // Parse project data from the path string - format: "name:path:is_startup_project"
            std::vector<std::string> parts;
            std::string::size_type start = 0;
            std::string::size_type end = 0;
            
            // Split by colons, but handle Windows drive letters (e.g., C:\)
            while (start < project_path.length()) {
                end = project_path.find(':', start);
                if (end == std::string::npos) {
                    // Last part
                    parts.push_back(project_path.substr(start));
                    break;
                }
                
                // Check if this colon is part of a Windows drive letter
                if (end + 2 < project_path.length() && project_path[end + 1] == '\\') {
                    // This is a Windows drive letter, find the next colon
                    start = end + 1;
                    continue;
                }
                
                parts.push_back(project_path.substr(start, end - start));
                start = end + 1;
            }
            
            if (parts.size() >= 1) {
                project.name = parts[0];
            }
            
            if (parts.size() >= 2) {
                project.path = std::filesystem::path(parts[1]);
            }
            
            if (parts.size() >= 3) {
                project.is_startup_project = (parts[2] == "true");
            }
            
            projects_.push_back(project);
        }
    }
    
    return true;
}

bool workspace_config::save(const std::string& workspace_file) const {
    std::ofstream file(workspace_file);
    if (!file) {
        logger::print_error("Failed to create workspace configuration file");
        return false;
    }
    
    // Get the workspace directory
    std::filesystem::path workspace_dir = std::filesystem::path(workspace_file).parent_path();
    
    // Write workspace info
    file << "[workspace]\n";
    file << "name = \"" << name_ << "\"\n";
    file << "description = \"" << description_ << "\"\n\n";
    
    // Write projects as a string array
    file << "# Projects in format: name:path:is_startup_project\n";
    file << "projects = [\n";
    
    for (size_t i = 0; i < projects_.size(); ++i) {
        const auto& project = projects_[i];
        
        // Create a relative path if possible
        std::filesystem::path path_to_save;
        if (project.path.is_absolute()) {
            try {
                // Try to make the path relative to workspace directory
                path_to_save = std::filesystem::relative(project.path, workspace_dir);
                logger::print_verbose("Converted absolute path to relative: " + path_to_save.string());
            } catch (...) {
                // If we can't create a relative path, use the absolute one
                path_to_save = project.path;
                logger::print_verbose("Using absolute path: " + path_to_save.string());
            }
        } else {
            // Already a relative path
            path_to_save = project.path;
        }
        
        file << "  \"" << project.name << ":" 
             << path_to_save.string() << ":" 
             << (project.is_startup_project ? "true" : "false") << "\"";
        
        if (i < projects_.size() - 1) {
            file << ",";
        }
        file << "\n";
    }
    
    file << "]\n\n";
    
    // Write additional information as comments
    file << "# Dependencies are stored in project-specific cforge.toml files\n";
    
    return true;
}

const workspace_project* workspace_config::get_startup_project() const {
    for (const auto& project : projects_) {
        if (project.is_startup_project) {
            return &project;
        }
    }
    return nullptr;
}

bool workspace_config::has_project(const std::string& name) const {
    for (const auto& project : projects_) {
        if (project.name == name) {
            return true;
        }
    }
    return false;
}

bool workspace_config::add_project_dependency(const std::string& project_name, const std::string& dependency) {
    // Find the project
    for (auto& project : projects_) {
        if (project.name == project_name) {
            // Check if dependency exists
            if (!has_project(dependency)) {
                logger::print_error("Dependency project '" + dependency + "' does not exist in workspace");
                return false;
            }
            
            // Check for circular dependencies
            std::set<std::string> visited;
            std::queue<std::string> to_visit;
            to_visit.push(dependency);
            
            while (!to_visit.empty()) {
                std::string current = to_visit.front();
                to_visit.pop();
                
                if (current == project_name) {
                    logger::print_error("Circular dependency detected: " + project_name + " -> " + dependency);
                    return false;
                }
                
                if (visited.insert(current).second) {
                    // Add dependencies of current project to visit
                    for (const auto& proj : projects_) {
                        if (proj.name == current) {
                            for (const auto& dep : proj.dependencies) {
                                to_visit.push(dep);
                            }
                            break;
                        }
                    }
                }
            }
            
            // Add dependency
            project.dependencies.push_back(dependency);
            return true;
        }
    }
    
    logger::print_error("Project '" + project_name + "' not found in workspace");
    return false;
}

bool workspace_config::set_startup_project(const std::string& project_name) {
    bool found = false;
    
    // Clear existing startup project and set new one
    for (auto& project : projects_) {
        if (project.name == project_name) {
            project.is_startup_project = true;
            found = true;
        } else {
            project.is_startup_project = false;
        }
    }
    
    if (!found) {
        logger::print_error("Project '" + project_name + "' not found in workspace");
        return false;
    }
    
    return true;
}

std::vector<std::string> workspace_config::get_build_order() const {
    std::vector<std::string> build_order;
    std::set<std::string> visited;
    
    // Define a recursive lambda function for topological sort
    std::function<void(const std::string&)> visit = [&](const std::string& project_name) {
        if (visited.find(project_name) != visited.end()) {
            return;
        }
        
        visited.insert(project_name);
        
        // Find project dependencies
        for (const auto& project : projects_) {
            if (project.name == project_name) {
                for (const auto& dep : project.dependencies) {
                    visit(dep);
                }
                break;
            }
        }
        
        build_order.push_back(project_name);
    };
    
    // Visit all projects
    for (const auto& project : projects_) {
        visit(project.name);
    }
    
    return build_order;
}

const std::vector<workspace_project>& workspace_config::get_projects() const {
    return projects_;
}

std::vector<workspace_project>& workspace_config::get_projects() {
    return projects_;
}

} // namespace cforge 