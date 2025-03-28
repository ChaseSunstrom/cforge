use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::{Path, PathBuf};
use colored::Colorize;
use crate::config::{auto_adjust_config, load_project_config, load_workspace_config, save_workspace_config, ProjectConfig, WorkspaceConfig};
use crate::output_utils::{format_project_name, is_quiet, print_error, print_header, print_status, print_success, print_warning, TaskList};
use crate::{find_library_files, generate_clion_workspace, generate_ide_files, generate_vscode_workspace, get_build_type, get_effective_compiler_label, install_dependencies, list_project_targets, progress_bar, run_script, DEFAULT_BUILD_DIR, WORKSPACE_FILE};
use crate::project::{build_project, clean_project, generate_package_config, install_project, list_project_items, package_project, run_project, test_project};

pub fn is_workspace() -> bool {
    Path::new(WORKSPACE_FILE).exists()
}

pub fn build_workspace_with_dependency_order(
    project: Option<String>,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    // Load workspace configuration
    print_header("Workspace Build", None);

    let workspace_config = load_workspace_config()?;

    // Determine which projects to build
    let projects = match project {
        Some(ref proj) => {
            print_status(&format!("Building specific project: {}", format_project_name(proj)));
            vec![proj.clone()]
        },
        None => {
            print_status("Building all workspace projects");
            workspace_config.workspace.projects.clone()
        }
    };

    // Exit early if there are no projects
    if projects.is_empty() {
        print_warning("No projects found in workspace", None);
        return Ok(());
    }

    // Build project paths
    let mut project_paths = Vec::new();
    for project_name in &projects {
        let path = if Path::new(project_name).exists() {
            PathBuf::from(project_name)
        } else if Path::new("projects").join(project_name).exists() {
            PathBuf::from("projects").join(project_name)
        } else {
            PathBuf::from(project_name)
        };

        project_paths.push(path);
    }

    // Build dependency graph
    let dependency_graph = match build_dependency_graph(&workspace_config, &project_paths) {
        Ok(graph) => graph,
        Err(e) => {
            print_error(&format!("Failed to build dependency graph: {}", e), None, None);
            return Err(e);
        }
    };

    // Determine build order based on dependencies
    let build_order = match resolve_build_order(&dependency_graph, &projects) {
        Ok(order) => order,
        Err(e) => {
            print_error(&format!("Failed to resolve build order: {}", e), None, None);
            return Err(e);
        }
    };

    // Early return if build order is empty
    if build_order.is_empty() {
        print_warning("No buildable projects found", None);
        return Ok(());
    }


    // Show build order
    if !is_quiet() {
        print_status(&format!("Build order: {}",
                              build_order.iter()
                                  .map(|p| format_project_name(p).to_string())
                                  .collect::<Vec<_>>()
                                  .join(" → ")));
    }

    // Create task list
    let mut task_list = TaskList::new(build_order.clone());

    // Only display task list if we have projects to build
    if !build_order.is_empty() {
        task_list.display();
    }

    // Build projects in order
    for (i, project_name) in build_order.iter().enumerate() {
        task_list.start_task(i);

        let path = if Path::new(project_name).exists() {
            PathBuf::from(project_name)
        } else if Path::new("projects").join(project_name).exists() {
            PathBuf::from("projects").join(project_name)
        } else {
            PathBuf::from(project_name)
        };

        let mut config = match load_project_config(Some(&path)) {
            Ok(cfg) => cfg,
            Err(e) => {
                print_warning(&format!("Could not load config for {}: {}", project_name, e), None);
                print_warning("Skipping project and continuing...", None);
                continue;
            }
        };

        // Auto-adjust configuration if needed
        if let Err(e) = auto_adjust_config(&mut config) {
            print_warning(&format!("Error adjusting config for {}: {}", project_name, e), None);
            print_warning("Using default configuration", None);
        }

        // Try to build project
        let build_result = build_project(&config, &path, config_type, variant_name, target, Some(&workspace_config));

        if let Err(e) = build_result {
            print_warning("Continuing with other projects...", None);
        } else {
            task_list.complete_task(i);
        }

        // Generate package config after build
        let spinner = progress_bar(&format!("Generating package config for {}", project_name));
        if let Err(e) = generate_package_config(&path, project_name) {
            spinner.failure(&format!("Error: {}", e));
        } else {
            spinner.success();
        }
    }

    // Completion message
    if task_list.all_completed() {
        print_success("Workspace build completed successfully", None);
    } else {
        print_warning("Workspace build completed with some issues", None);
    }

    Ok(())
}

pub fn build_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    // Determine which projects to build
    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects.clone(),
    };

    // Build dependency graph
    let mut dependency_graph = HashMap::new();

    // Process each project to find its dependencies
    for project_path in &projects {
        let path = PathBuf::from(project_path);
        if let Ok(config) = load_project_config(Some(&path)) {
            let deps: Vec<String> = config.dependencies.workspace.iter()
                .map(|dep| dep.name.clone())
                .collect();
            dependency_graph.insert(project_path.clone(), deps);
        } else {
            // If we can't load the config, assume no dependencies
            dependency_graph.insert(project_path.clone(), Vec::new());
        }
    }

    // Determine build order based on dependencies
    let build_order = resolve_build_order(&dependency_graph, &projects)?;

    // Build projects in order
    for project_path in &build_order {
        println!("{}", format!("Building project: {}", project_path).blue());
        let path = PathBuf::from(project_path);
        let config = load_project_config(Some(&path))?;

        // Build the project
        build_project(&config, &path, config_type, variant_name, target, Some(&workspace_config))?;

        // After successful build, generate package config
        generate_package_config(&path, project_path)?;
    }

    println!("{}", "Workspace build completed".green());
    Ok(())
}

pub fn clean_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    print_header("Workspace Clean", None);

    let workspace_config = load_workspace_config()?;

    // Determine which projects to clean
    let projects = match project {
        Some(proj) => {
            print_status(&format!("Cleaning specific project: {}", format_project_name(&proj)));
            vec![proj]
        },
        None => {
            print_status("Cleaning all workspace projects");
            workspace_config.workspace.projects
        }
    };

    // Create task list
    let mut task_list = TaskList::new(projects.clone());
    task_list.display();

    // Clean projects
    for (i, project_path) in projects.iter().enumerate() {
        task_list.start_task(i);

        let path = PathBuf::from(project_path);
        match load_project_config(Some(&path)) {
            Ok(config) => {
                if let Err(e) = clean_project(&config, &path, config_type, target) {
                    print_warning(&format!("Error cleaning project {}: {}", project_path, e), None);
                } else {
                    task_list.complete_task(i);
                }
            },
            Err(e) => {
                print_warning(&format!("Could not load config for {}: {}", project_path, e), None);
                print_warning("Skipping project", None);
            }
        }
    }

    print_success("Workspace clean completed", None);
    Ok(())
}

pub fn run_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    args: &[String]
) -> Result<(), Box<dyn std::error::Error>> {
    print_header("Workspace Run", None);

    let workspace_config = load_workspace_config()?;

    // Determine which project to run
    let project_path = match project {
        Some(proj) => proj,
        None => {
            if workspace_config.workspace.projects.is_empty() {
                return Err("No projects found in workspace".into());
            }

            // Check for default startup project
            if let Some(default_startup) = &workspace_config.workspace.default_startup_project {
                if workspace_config.workspace.projects.contains(default_startup) {
                    default_startup.clone()
                } else {
                    workspace_config.workspace.projects[0].clone()
                }
            } else {
                workspace_config.workspace.projects[0].clone()
            }
        }
    };

    print_status(&format!("Running project: {}", format_project_name(&project_path)));

    let path = PathBuf::from(&project_path);
    match load_project_config(Some(&path)) {
        Ok(config) => {
            run_project(&config, &path, config_type, variant_name, args, Some(&workspace_config))?;
        },
        Err(e) => {
            return Err(format!("Could not load config for {}: {}", project_path, e).into());
        }
    }

    Ok(())
}

pub fn test_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    variant: Option<&str>,
    filter: Option<&str>,
    label: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects,
    };

    for project_path in projects {
        println!("{}", format!("Testing project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        test_project(&config, &path, config_type, variant, filter)?;
    }

    println!("{}", "Workspace tests completed".green());
    Ok(())
}

pub fn install_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    prefix: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects,
    };

    for project_path in projects {
        println!("{}", format!("Installing project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        install_project(&config, &path, config_type, prefix)?;
    }

    println!("{}", "Workspace installation completed".green());
    Ok(())
}

pub fn install_workspace_deps(project: Option<String>, update: bool) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects,
    };

    for project_path in projects {
        println!("{}", format!("Installing dependencies for project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        install_dependencies(&config, &path, update)?;
    }

    println!("{}", "Workspace dependencies installed".green());
    Ok(())
}

pub fn run_workspace_script(name: String, project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let project_path = match project {
        Some(proj) => proj,
        None => {
            if workspace_config.workspace.projects.is_empty() {
                return Err("No projects found in workspace".into());
            }
            // Default to first project if none specified
            workspace_config.workspace.projects[0].clone()
        }
    };

    println!("{}", format!("Running script '{}' for project: {}", name, project_path).blue());
    let path = PathBuf::from(&project_path);
    let config = load_project_config(Some(&path))?;
    run_script(&config, &name, &path)?;

    Ok(())
}

pub fn generate_workspace_ide_files(ide_type: String, project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects.clone(),
    };

    for project_path in projects {
        println!("{}", format!("Generating IDE files for project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        generate_ide_files(&config, &path, &ide_type)?;
    }

    // Generate workspace-level IDE files if needed
    match ide_type.as_str() {
        "vscode" => {
            generate_vscode_workspace(&workspace_config)?;
        },
        "clion" => {
            generate_clion_workspace(&workspace_config)?;
        },
        _ => {
            println!("{}", format!("No workspace-level IDE files to generate for: {}", ide_type).yellow());
        }
    }

    println!("{}", "IDE files generation completed".green());
    Ok(())
}

pub fn package_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    package_type: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects,
    };

    for project_path in projects {
        println!("{}", format!("Packaging project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        package_project(&config, &path, config_type, package_type)?;
    }

    println!("{}", "Workspace packaging completed".green());
    Ok(())
}

pub fn list_workspace_items(what: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    println!("{}", "Workspace projects:".bold());
    for (i, project) in workspace_config.workspace.projects.iter().enumerate() {
        println!(" {}. {}", i + 1, project.green());
    }

    if workspace_config.workspace.projects.is_empty() {
        println!(" - No projects in workspace");
    } else if let Some(first_project) = workspace_config.workspace.projects.first() {
        // Show info about the first project
        let path = PathBuf::from(first_project);
        let config = load_project_config(Some(&path))?;

        println!("\n{}", "First project details:".bold());
        list_project_items(&config, what)?;
    }

    Ok(())
}

pub fn resolve_workspace_dependencies(
    config: &ProjectConfig,
    workspace_config: Option<&WorkspaceConfig>,
    project_path: &Path,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut cmake_options = Vec::new();

    if workspace_config.is_none() || config.dependencies.workspace.is_empty() {
        return Ok(cmake_options);
    }

    let workspace = workspace_config.unwrap();

    for dep in &config.dependencies.workspace {
        if !workspace.workspace.projects.contains(&dep.name) {
            print_warning(format!("Workspace dependency '{}' not found in workspace", dep.name).as_str(), None);
            continue;
        }

        // Get the absolute path to the dependency
        let dep_path = if Path::new(&dep.name).is_absolute() {
            PathBuf::from(&dep.name)
        } else if Path::new(&dep.name).exists() {
            fs::canonicalize(Path::new(&dep.name)).unwrap_or_else(|_| PathBuf::from(&dep.name))
        } else if Path::new("projects").join(&dep.name).exists() {
            fs::canonicalize(Path::new("projects").join(&dep.name))
                .unwrap_or_else(|_| PathBuf::from("projects").join(&dep.name))
        } else {
            PathBuf::from(&dep.name)
        };

        // First, try to load the dependency's config
        let dep_config = match load_project_config(Some(&dep_path)) {
            Ok(config) => config,
            Err(e) => {
                print_warning(format!("Could not load config for dependency '{}': {}", dep.name, e).as_str(), None);
                continue;
            }
        };

        // Check if the dependency has been built
        let dep_build_dir = dep_config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
        let dep_build_path = dep_path.join(dep_build_dir);

        if !dep_build_path.exists() {
            // Try to build the dependency
            let mut dep_conf = dep_config.clone();
            auto_adjust_config(&mut dep_conf)?;
            if let Err(e) = build_project(&dep_conf, &dep_path, None, None, None, Some(workspace)) {
                print_warning(format!("Failed to build dependency '{}': {}", dep.name, e).as_str(), None);;
            }
        }

        // Ensure the package config is generated
        if let Err(e) = generate_package_config(&dep_path, &dep.name) {
            print_warning( format!("Failed to generate package config for '{}': {}", dep.name, e).as_str(), None);
        }

        // Add the build directory to CMAKE_PREFIX_PATH
        let dep_build_dir = dep_config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
        let dep_build_path = dep_path.join(dep_build_dir);
        cmake_options.push(format!(
            "-DCMAKE_PREFIX_PATH={};${{CMAKE_PREFIX_PATH}}",
            dep_build_path.to_string_lossy().replace(r"\\?\", "")
        ));

        // Configure build dir
        cmake_options.push(format!(
            "-D{}_DIR={}",
            dep.name,
            dep_build_path.to_string_lossy().replace(r"\\?\", "")
        ));

        // Get the include directory
        let dep_include = dep_path.join("include");
        cmake_options.push(format!(
            "-D{}_INCLUDE_DIR={}",
            dep.name.to_uppercase(),
            dep_include.to_string_lossy().replace(r"\\?\", "")
        ));

        // Determine library name format based on compiler
        let compiler_label = get_effective_compiler_label(&dep_config);
        let is_msvc_style = matches!(compiler_label.to_lowercase().as_str(), "msvc" | "clang-cl");

        // Is this a shared library?
        let is_shared = dep_config.project.project_type == "shared-library";

        // Find all potential library files
        let lib_dir = dep_config.output.lib_dir.as_deref().unwrap_or("lib");

        // Expand the tokens for the actual configuration
        let build_type = get_build_type(&dep_config, None);
        let os_val = if cfg!(windows) { "windows" } else if cfg!(target_os = "macos") { "darwin" } else { "linux" };
        let arch_val = if cfg!(target_arch = "x86_64") { "x64" } else if cfg!(target_arch = "x86") { "x86" } else { "arm64" };

        let expanded_lib_dir = lib_dir
            .replace("${CONFIG}", &build_type)
            .replace("${OS}", os_val)
            .replace("${ARCH}", arch_val);

        // Build full library path - use an absolute path
        let lib_path = dep_path.join(&expanded_lib_dir);

        // Use enhanced library finding function but without verbose logging
        let found_libraries = find_library_files(&lib_path, &dep.name, is_shared, is_msvc_style);

        if !found_libraries.is_empty() {
            for (lib_file, filename) in &found_libraries {
                if !is_quiet() {
                    println!("{}", format!("Found library: {} ({})", lib_file.display(), filename).green());
                }

                // Add the library path to CMake variables
                cmake_options.push(format!(
                    "-D{}_LIBRARY={}",
                    dep.name.to_uppercase(),
                    lib_file.to_string_lossy().replace(r"\\?\", "")
                ));

                // Just use the first library file we find
                break;
            }
        } else {
            let found_libraries = find_library_files(&dep_path, &dep.name, is_shared, is_msvc_style);

            if !found_libraries.is_empty() {
                for (lib_file, filename) in &found_libraries {
                    // Add the library path to CMake variables
                    cmake_options.push(format!(
                        "-D{}_LIBRARY={}",
                        dep.name.to_uppercase(),
                        lib_file.to_string_lossy().replace(r"\\?\", "")
                    ));

                    // Just use the first library file we find
                    break;
                }
            } else {
                // Try to link directly to the library by name as a last resort
                cmake_options.push(format!(
                    "-DCMAKE_LIBRARY_PATH={}",
                    lib_path.to_string_lossy().replace(r"\\?\", "")
                ));

                // Try different prefix/suffix combinations
                cmake_options.push(format!(
                    "-DCMAKE_FIND_LIBRARY_PREFIXES=\"lib;\"",
                ));

                // Add all possible library extensions
                cmake_options.push(format!(
                    "-DCMAKE_FIND_LIBRARY_SUFFIXES=\".dll;.dll.a;.a;.lib;.so;.dylib\"",
                ));

                // Try to find the library by name
                cmake_options.push(format!(
                    "-D{}_LIBRARY_NAME={}",
                    dep.name.to_uppercase(),
                    dep.name
                ));
            }
        }
    }

    Ok(cmake_options)
}

pub fn build_dependency_graph(
    workspace_config: &WorkspaceConfig,
    project_paths: &[PathBuf]
) -> Result<HashMap<String, Vec<String>>, Box<dyn std::error::Error>> {
    let mut dependency_graph = HashMap::new();

    // Process each project to find its dependencies
    for project_path in project_paths {
        let project_name = project_path.file_name()
            .and_then(|name| name.to_str())
            .unwrap_or_default()
            .to_string();

        // Skip if the project isn't in the workspace
        if !workspace_config.workspace.projects.contains(&project_name) {
            continue;
        }

        // Try to load the project config to find dependencies
        if let Ok(config) = load_project_config(Some(project_path)) {
            // Extract workspace dependencies
            let deps: Vec<String> = config.dependencies.workspace.iter()
                .map(|dep| dep.name.clone())
                .collect();

            // Add to graph
            dependency_graph.insert(project_name, deps);
        } else {
            // If we can't load the config, assume no dependencies
            dependency_graph.insert(project_name, Vec::new());
        }
    }

    Ok(dependency_graph)
}

pub fn resolve_build_order(
    dependency_graph: &HashMap<String, Vec<String>>,
    projects: &[String]
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut result = Vec::new();
    let mut visited = HashSet::new();
    let mut temp_visited = HashSet::new();

    fn visit(
        project: &str,
        graph: &HashMap<String, Vec<String>>,
        visited: &mut HashSet<String>,
        temp_visited: &mut HashSet<String>,
        result: &mut Vec<String>
    ) -> Result<(), Box<dyn std::error::Error>> {
        if temp_visited.contains(project) {
            return Err(format!("Circular dependency detected involving project '{}'", project).into());
        }

        if visited.contains(project) {
            return Ok(());
        }

        temp_visited.insert(project.to_string());

        if let Some(deps) = graph.get(project) {
            for dep in deps {
                visit(dep, graph, visited, temp_visited, result)?;
            }
        }

        temp_visited.remove(project);
        visited.insert(project.to_string());
        result.push(project.to_string());

        Ok(())
    }

    for project in projects {
        if !visited.contains(project.as_str()) {
            visit(project, dependency_graph, &mut visited, &mut temp_visited, &mut result)?;
        }
    }

    Ok(result)
}

pub fn list_startup_projects(workspace_config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "Available startup projects:".bold());

    let default_startup = workspace_config.workspace.default_startup_project.as_deref();

    if let Some(startup_projects) = &workspace_config.workspace.startup_projects {
        for project in startup_projects {
            if Some(project.as_str()) == default_startup {
                print_status(format!("{} (default)", project).as_str());
            } else {
                print_status(format!("{}", project).as_str());
            }
        }
    } else {
        // If no specific startup projects, list all projects
        for project in &workspace_config.workspace.projects {
            if Some(project.as_str()) == default_startup {
                print_status(format!("{} (default)", project).as_str());
            } else {
                print_status(format!("{}", project).as_str());
            }
        }
    }

    Ok(())
}

pub fn set_startup_project(workspace_config: &mut WorkspaceConfig, project: &str) -> Result<(), Box<dyn std::error::Error>> {
    // Check if project exists in workspace
    if !workspace_config.workspace.projects.contains(&project.to_string()) {
        return Err(format!("Project '{}' not found in workspace", project).into());
    }

    // Set as default startup project
    workspace_config.workspace.default_startup_project = Some(project.to_string());

    // Add to startup projects list if not already there
    if let Some(startup_projects) = &mut workspace_config.workspace.startup_projects {
        if !startup_projects.contains(&project.to_string()) {
            startup_projects.push(project.to_string());
        }
    } else {
        workspace_config.workspace.startup_projects = Some(vec![project.to_string()]);
    }

    // Save updated config
    save_workspace_config(workspace_config)?;

    print_status(format!("Project '{}' set as default startup project", project).as_str());
    Ok(())
}

pub fn show_current_startup(workspace_config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    if let Some(default_startup) = &workspace_config.workspace.default_startup_project {
        print_status(format!("Current default startup project: {}", default_startup).as_str());
    } else {
        if !workspace_config.workspace.projects.is_empty() {
            print_status(format!("First project (default) is: {}", workspace_config.workspace.projects[0]).as_str());
        }
    }

    Ok(())
}
