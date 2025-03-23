use std::collections::HashMap;
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::Command;
use colored::Colorize;
use crate::{configure_project, count_project_source_files, ensure_build_tools, execute_build_with_progress, expand_output_tokens, get_active_variant, get_build_type, get_effective_compiler_label, is_executable, progress_bar, prompt, run_command, run_hooks, CFORGE_FILE, CMAKE_MIN_VERSION, DEFAULT_BIN_DIR, DEFAULT_BUILD_DIR, DEFAULT_LIB_DIR, WORKSPACE_FILE};
use crate::config::{create_default_config, create_header_only_config, create_library_config, load_project_config, load_workspace_config, save_project_config, save_workspace_config, ProjectConfig, WorkspaceConfig, WorkspaceWithProjects};
use crate::output_utils::{format_project_name, is_quiet, print_error, print_header, print_status, print_substep, print_success, print_warning, BuildProgress, ProgressBar};
use crate::tools::{is_msvc_style_for_config, parse_universal_flags};
use crate::workspace::build_dependency_graph;

pub fn init_project(path: Option<&Path>, template: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let project_path = path.unwrap_or_else(|| Path::new("."));

    let config_path = project_path.join(CFORGE_FILE);
    if config_path.exists() {
        let response = prompt("Project already exists. Overwrite? (y/N): ")?;
        if response.trim().to_lowercase() != "y" {
            println!("{}", "Initialization cancelled".blue());
            return Ok(());
        }
    }

    let config = match template {
        Some("lib") => create_library_config(),
        Some("header-only") => create_header_only_config(),
        Some("app") | _ => create_default_config(),
    };

    save_project_config(&config, project_path)?;

    // Create basic directory structure
    fs::create_dir_all(project_path.join("src"))?;
    fs::create_dir_all(project_path.join("include"))?;

    // Create a simple main.cpp file for executable projects
    if config.project.project_type == "executable" {
        let main_file = project_path.join("src").join("main.cpp");
        let mut file = File::create(main_file)?;
        file.write_all(b"#include <iostream>\n\nint main(int argc, char* argv[]) {\n    std::cout << \"Hello, world!\" << std::endl;\n    return 0;\n}\n")?;
    } else if config.project.project_type == "library" {
        // Create a simple header file
        let header_file = project_path.join("include").join(format!("{}.h", config.project.name));
        let mut file = File::create(header_file)?;
        file.write_all(format!("#pragma once\n\nnamespace {0} {{\n\n// Library interface\nclass Library {{\npublic:\n    int calculate(int a, int b);\n}};\n\n}} // namespace {0}\n", config.project.name).as_bytes())?;

        // Create a source file
        let source_file = project_path.join("src").join(format!("{}.cpp", config.project.name));
        let mut file = File::create(source_file)?;
        file.write_all(format!("#include \"{}.h\"\n\nnamespace {} {{\n\nint Library::calculate(int a, int b) {{\n    return a + b;\n}}\n\n}} // namespace {}\n", config.project.name, config.project.name, config.project.name).as_bytes())?;
    }

    println!("{}", "Project initialized successfully".green());

    // If this is a workspace project, add it to the workspace
    let workspace_file = Path::new(WORKSPACE_FILE);
    if workspace_file.exists() && path.is_some() {
        let mut workspace_config = load_workspace_config()?;
        let project_rel_path = path.unwrap().to_string_lossy().to_string();

        if !workspace_config.workspace.projects.contains(&project_rel_path) {
            workspace_config.workspace.projects.push(project_rel_path);
            save_workspace_config(&workspace_config)?;
        }
    }

    Ok(())
}

pub fn init_workspace() -> Result<(), Box<dyn std::error::Error>> {
    if Path::new(WORKSPACE_FILE).exists() {
        let response = prompt("Workspace already exists. Overwrite? (y/N): ")?;
        if response.trim().to_lowercase() != "y" {
            println!("{}", "Initialization cancelled".blue());
            return Ok(());
        }
    }

    let workspace_name = prompt("Workspace name: ")?;

    let workspace_config = WorkspaceConfig {
        workspace: WorkspaceWithProjects {
            name: workspace_name.trim().to_string(),
            projects: vec![],
            startup_projects: None,  // Initialize as None by default
            default_startup_project: None,  // Initialize as None by default
        },
    };

    save_workspace_config(&workspace_config)?;

    // Create basic workspace structure
    fs::create_dir_all("projects")?;

    println!("{}", "Workspace initialized successfully".green());
    println!("To add projects, run: {} in the projects directory", "cforge init".cyan());

    Ok(())
}

pub fn build_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    cross_target: Option<&str>,
    workspace_config: Option<&WorkspaceConfig>
) -> Result<(), Box<dyn std::error::Error>> {
    let project_name = &config.project.name;

    // Create a progress tracker for the build process
    let mut progress = BuildProgress::new(project_name, 4); // 4 main steps

    // Create a main progress bar for overall build progress
    let mut main_progress = ProgressBar::start(&format!("Building {}", project_name));

    // Step 1: Ensure tools (5% of progress)
    progress.next_step("Checking build tools");
    main_progress.update(0.05);
    ensure_build_tools(config)?;
    main_progress.update(0.1);

    // Calculate build paths
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = if let Some(target) = cross_target {
        project_path.join(format!("{}-{}", build_dir, target))
    } else {
        project_path.join(build_dir)
    };

    // Ensure the build directory exists
    fs::create_dir_all(&build_path)?;

    // Get the build type - make sure it matches configuration
    let build_type = get_build_type(config, config_type);

    // Print what build type is being used (for debugging)
    if !is_quiet() {
        print_substep(&format!("Using build configuration: {}", build_type));
    }

    // Check if we need to configure
    let needs_configure = !build_path.join("CMakeCache.txt").exists() ||
        (config.build.generator.as_deref().unwrap_or("") == "Ninja" &&
            !build_path.join("build.ninja").exists());

    // Step 2: Configure if needed (30% of progress)
    if needs_configure {
        progress.next_step("Configuring project");

        if !is_quiet() {
            print_status("Project not configured or build files missing, configuring...");
        }

        // Creating a progress bar specifically for configuration
        let mut config_progress = ProgressBar::start("Configuration");

        // Delegate to configure_project with the progress bar
        configure_project(
            config,
            project_path,
            Some(&build_type),  // Explicitly pass the build type
            variant_name,
            cross_target,
            workspace_config
        )?;

        config_progress.success();
        main_progress.update(0.3); // Configuration complete (30%)
    } else {
        progress.next_step("Project already configured");
        main_progress.update(0.3); // Configuration was already done
    }

    // Step 3: Pre-build hooks (35% of progress)
    progress.next_step("Running pre-build hooks");

    // Setup environment for hooks
    let mut hook_env = HashMap::new();
    hook_env.insert("PROJECT_PATH".to_string(), project_path.to_string_lossy().to_string());
    hook_env.insert("BUILD_PATH".to_string(), build_path.to_string_lossy().to_string());
    hook_env.insert("CONFIG_TYPE".to_string(), build_type.clone());

    if let Some(v) = variant_name {
        hook_env.insert("VARIANT".to_string(), v.to_string());
    }

    if let Some(t) = cross_target {
        hook_env.insert("TARGET".to_string(), t.to_string());
    }

    // Run pre-build hooks
    if let Some(hooks) = &config.hooks {
        if let Some(pre_hooks) = &hooks.pre_build {
            if !pre_hooks.is_empty() {
                print_substep("Running pre-build hooks");
                run_hooks(&Some(pre_hooks.clone()), project_path, Some(hook_env.clone()))?;
            }
        }
    }

    main_progress.update(0.35); // Pre-build hooks complete

    // Step 4: Build (35% to 95% of progress)
    progress.next_step("Building project");

    // Count source files to get a better estimate of build size
    let source_files_count = count_project_source_files(config, project_path)?;

    // Build using CMake
    let mut cmd = vec!["cmake".to_string(), "--build".to_string(), ".".to_string()];

    // Ensure we pass the correct build configuration
    cmd.push("--config".to_string());
    cmd.push(build_type.clone());

    // Add parallelism for faster builds
    let num_threads = num_cpus::get();
    cmd.push("--parallel".to_string());
    cmd.push(format!("{}", num_threads));

    // Build with simpler output
    if !is_quiet() {
        print_status(&format!("Building {} in {} configuration",
                              format_project_name(project_name),
                              build_type));
    }

    // Create build progress bar
    let build_progress = ProgressBar::start(&format!("Compiling {} source files", source_files_count));

    // Execute build command with progress tracking - using our enhanced function
    let build_result = execute_build_with_progress(
        cmd,
        &build_path,
        source_files_count,
        build_progress
    );

    if let Err(e) = build_result {
        main_progress.failure(&format!("Build failed: {}", e));
        print_error("Build failed. See above for details.", None, None);
        return Err(e);
    }

    main_progress.update(0.95); // Main build complete

    // Run post-build hooks (95% to 100%)
    if let Some(hooks) = &config.hooks {
        if let Some(post_hooks) = &hooks.post_build {
            if !post_hooks.is_empty() {
                print_substep("Running post-build hooks");
                run_hooks(&Some(post_hooks.clone()), project_path, Some(hook_env))?;
            }
        }
    }

    // Complete build
    main_progress.update(1.0); // Ensure we show 100%
    main_progress.success();
    progress.complete();

    Ok(())
}

pub fn run_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    args: &[String],
    workspace_config: Option<&WorkspaceConfig>
) -> Result<(), Box<dyn std::error::Error>> {
    // Calculate paths
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);
    let project_name = &config.project.name;
    let build_type = get_build_type(config, config_type);
    let bin_dir = config.output.bin_dir.as_deref().unwrap_or(DEFAULT_BIN_DIR);

    print_header(&format!("Running: {}", format_project_name(project_name)), None);

    // Print what build configuration we're using (for debugging)
    if !is_quiet() {
        print_substep(&format!("Using build configuration: {}", build_type));
    }

    // Make sure the project is built with the correct configuration
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        print_warning("Project not built yet. Building first...", None);
        build_project(config, project_path, Some(&build_type), variant_name, None, workspace_config)?;
    }

    // Generate all possible executable paths
    let spinner = progress_bar("Locating executable");

    let mut executable_paths = Vec::new();

    // Try with combined project_target pattern (new format)
    for target_name in config.targets.keys() {
        let combined_name = format!("{}_{}", project_name, target_name);

        // With .exe extension
        executable_paths.push(build_path.join(bin_dir).join(format!("{}.exe", combined_name)));
        // Without .exe extension
        executable_paths.push(build_path.join(bin_dir).join(&combined_name));

        // Add config-specific paths (e.g., bin/Release/executable)
        executable_paths.push(build_path.join(bin_dir).join(&build_type).join(format!("{}.exe", combined_name)));
        executable_paths.push(build_path.join(bin_dir).join(&build_type).join(&combined_name));

        // Standard CMake output directories
        executable_paths.push(build_path.join(&combined_name));
        executable_paths.push(build_path.join(format!("{}.exe", combined_name)));
        executable_paths.push(build_path.join(&build_type).join(&combined_name));
        executable_paths.push(build_path.join(&build_type).join(format!("{}.exe", combined_name)));
    }

    // For backward compatibility, also check old patterns
    // Check for project name
    executable_paths.push(build_path.join(bin_dir).join(format!("{}.exe", project_name)));
    executable_paths.push(build_path.join(bin_dir).join(project_name));
    executable_paths.push(build_path.join(project_name));
    executable_paths.push(build_path.join(format!("{}.exe", project_name)));
    executable_paths.push(build_path.join(&build_type).join(project_name));
    executable_paths.push(build_path.join(&build_type).join(format!("{}.exe", project_name)));

    // Check for target names
    for target_name in config.targets.keys() {
        executable_paths.push(build_path.join(bin_dir).join(format!("{}.exe", target_name)));
        executable_paths.push(build_path.join(bin_dir).join(target_name));
        executable_paths.push(build_path.join(target_name));
        executable_paths.push(build_path.join(format!("{}.exe", target_name)));
        executable_paths.push(build_path.join(&build_type).join(target_name));
        executable_paths.push(build_path.join(&build_type).join(format!("{}.exe", target_name)));
    }

    // Try with "default" as a fallback
    if project_name != "default" {
        executable_paths.push(build_path.join(bin_dir).join("default.exe"));
        executable_paths.push(build_path.join(bin_dir).join("default"));
        executable_paths.push(build_path.join("default"));
        executable_paths.push(build_path.join("default.exe"));
        executable_paths.push(build_path.join(&build_type).join("default"));
        executable_paths.push(build_path.join(&build_type).join("default.exe"));
    }

    // Find the first executable that exists
    let mut executable_path = None;
    for path in &executable_paths {
        if path.exists() && is_executable(path) {
            executable_path = Some(path.clone());
            break;
        }
    }

    // If still not found, look for any executable in bin_dir
    if executable_path.is_none() {
        let bin_path = build_path.join(bin_dir);
        if bin_path.exists() {
            if let Ok(entries) = fs::read_dir(&bin_path) {
                for entry in entries {
                    if let Ok(entry) = entry {
                        let path = entry.path();
                        if path.is_file() && is_executable(&path) {
                            executable_path = Some(path.clone());
                            break;
                        }
                    }
                }
            }
        }
    }

    // One last attempt - recursive search
    if executable_path.is_none() {
        let mut found = None;

        fn find_executables(dir: &Path, found: &mut Option<PathBuf>) {
            if let Ok(entries) = fs::read_dir(dir) {
                for entry in entries {
                    if let Ok(entry) = entry {
                        let path = entry.path();

                        if path.is_dir() {
                            find_executables(&path, found);
                        } else if path.is_file() && is_executable(&path) {
                            *found = Some(path.clone());
                            return;
                        }
                    }
                }
            }
        }

        find_executables(&build_path, &mut found);
        executable_path = found;
    }

    // Check if we found an executable
    let executable = match executable_path {
        Some(path) => {
            spinner.success();
            print_substep(&format!("Found executable: {}", path.display()));
            path
        },
        None => {
            spinner.failure("No executable found");
            return Err("Executable not found. Make sure the project is built successfully.".into());
        }
    };

    // Create environment for hooks
    let mut hook_env = HashMap::new();
    hook_env.insert("PROJECT_PATH".to_string(), project_path.to_string_lossy().to_string());
    hook_env.insert("BUILD_PATH".to_string(), build_path.to_string_lossy().to_string());
    hook_env.insert("CONFIG_TYPE".to_string(), build_type.clone());
    hook_env.insert("EXECUTABLE".to_string(), executable.to_string_lossy().to_string());

    if let Some(v) = variant_name {
        hook_env.insert("VARIANT".to_string(), v.to_string());
    }

    // Run pre-run hooks
    if let Some(hooks) = &config.hooks {
        if let Some(pre_hooks) = &hooks.pre_run {
            if !pre_hooks.is_empty() {
                print_substep("Running pre-run hooks");
                run_hooks(&Some(pre_hooks.clone()), project_path, Some(hook_env.clone()))?;
            }
        }
    }

    // Check executable one more time
    if !is_executable(&executable) {
        return Err(format!("No valid executable found at {}", executable.display()).into());
    }

    // Run the executable
    print_status(&format!("Running: {}", executable.display()));

    if !args.is_empty() {
        print_substep(&format!("Arguments: {}", args.join(" ")));
    }

    // Create and run command
    let mut command = Command::new(&executable);
    command.current_dir(project_path);

    if !args.is_empty() {
        command.args(args);
    }

    // Run directly showing full output
    print_header("Program Output", None);

    let status = command.status()?;

    if !status.success() {
        print_warning(&format!("Program exited with code {}", status.code().unwrap_or(-1)), None);
    } else {
        print_success("Program executed successfully", None);
    }

    // Run post-run hooks
    if let Some(hooks) = &config.hooks {
        if let Some(post_hooks) = &hooks.post_run {
            if !post_hooks.is_empty() {
                print_substep("Running post-run hooks");
                run_hooks(&Some(post_hooks.clone()), project_path, Some(hook_env))?;
            }
        }
    }

    Ok(())
}

pub fn clean_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    cross_target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    // Calculate paths
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = if let Some(target) = cross_target {
        project_path.join(format!("{}-{}", build_dir, target))
    } else {
        project_path.join(build_dir)
    };

    // Create environment for hooks
    let mut hook_env = HashMap::new();
    hook_env.insert("PROJECT_PATH".to_string(), project_path.to_string_lossy().to_string());
    hook_env.insert("BUILD_PATH".to_string(), build_path.to_string_lossy().to_string());

    if let Some(c) = config_type {
        hook_env.insert("CONFIG_TYPE".to_string(), c.to_string());
    }

    if let Some(t) = cross_target {
        hook_env.insert("TARGET".to_string(), t.to_string());
    }

    // Run pre-clean hooks
    if let Some(hooks) = &config.hooks {
        if let Some(pre_hooks) = &hooks.pre_clean {
            if !pre_hooks.is_empty() {
                print_header("Pre-clean Hooks", None);
                run_hooks(&Some(pre_hooks.clone()), project_path, Some(hook_env.clone()))?;
            }
        }
    }

    // Perform the clean operation
    if build_path.exists() {
        print_status(&format!("Cleaning project: {}", format_project_name(&config.project.name)));
        print_substep(&format!("Removing build directory: {}", build_path.display()));

        let spinner = progress_bar("Removing build files");
        match fs::remove_dir_all(&build_path) {
            Ok(_) => {
                spinner.success();
                print_success("Clean completed", None);
            },
            Err(e) => {
                spinner.failure(&e.to_string());
                return Err(format!("Failed to remove build directory: {}", e).into());
            }
        }
    } else {
        print_status(&format!("Project: {}", format_project_name(&config.project.name)));
        print_substep("Nothing to clean (build directory does not exist)");
    }

    // Run post-clean hooks
    if let Some(hooks) = &config.hooks {
        if let Some(post_hooks) = &hooks.post_clean {
            if !post_hooks.is_empty() {
                print_header("Post-clean Hooks", None);
                run_hooks(&Some(post_hooks.clone()), project_path, Some(hook_env))?;
            }
        }
    }

    Ok(())
}

pub fn test_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    filter: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);
    let build_type = get_build_type(config, config_type);

    // Check if tests directory exists
    if !project_path.join("tests").exists() {
        println!("{}", "No tests directory found. Skipping tests.".yellow());
        return Ok(());
    }

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path, config_type, variant_name, None, None)?;
    }

    // Run tests using CTest
    let mut cmd = vec!["ctest".to_string(), "--output-on-failure".to_string()];

    // Add configuration (Debug/Release)
    cmd.push("-C".to_string());
    cmd.push(build_type.clone());

    // Add filter if specified
    if let Some(f) = filter {
        cmd.push("-R".to_string());
        cmd.push(f.to_string());
    }

    // Run tests
    let result = run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None);

    match result {
        Ok(_) => {
            println!("{}", "All tests passed!".green());
        }
        Err(e) => {
            println!("{}", format!("Some tests failed: {}", e).red());
            return Err("Some tests failed. Check the output for details.".into());
        }
    }

    Ok(())
}

pub fn install_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    prefix: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    // Create a set of environment variables for hooks
    let mut hook_env = HashMap::new();
    hook_env.insert("PROJECT_PATH".to_string(), project_path.to_string_lossy().to_string());
    hook_env.insert("BUILD_PATH".to_string(), build_path.to_string_lossy().to_string());
    hook_env.insert("CONFIG_TYPE".to_string(), get_build_type(config, config_type));
    if let Some(p) = prefix {
        hook_env.insert("PREFIX".to_string(), p.to_string());
    }

    // Run pre-install hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.pre_install, project_path, Some(hook_env.clone()))?;
    }

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path, config_type, None, None, None)?;
    }

    // Run CMake install
    let mut cmd = vec!["cmake".to_string(), "--install".to_string(), ".".to_string()];

    // Add configuration (Debug/Release)
    let build_type = get_build_type(config, config_type);
    cmd.push("--config".to_string());
    cmd.push(build_type);

    // Add prefix if specified
    if let Some(p) = prefix {
        cmd.push("--prefix".to_string());
        cmd.push(p.to_string());
    }

    // Run install
    run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

    // Run post-install hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.post_install, project_path, Some(hook_env))?;
    }

    println!("{}", "Project installed successfully".green());
    Ok(())
}

pub fn package_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    package_type: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path, config_type, None, None, None)?;
    }

    // Run CPack to create package
    let mut cmd = vec!["cpack".to_string()];

    // Add configuration (Debug/Release)
    let build_type = get_build_type(config, config_type);
    cmd.push("-C".to_string());
    cmd.push(build_type);

    // Add package type if specified
    if let Some(pkg_type) = package_type {
        cmd.push("-G".to_string());
        cmd.push(pkg_type.to_string());
    }

    // Run cpack
    run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

    println!("{}", "Project packaged successfully".green());
    Ok(())
}

pub fn generate_package_config(project_path: &Path, project_name: &str) -> Result<(), Box<dyn std::error::Error>> {
    let config = load_project_config(Some(project_path))?;

    // Skip executable projects.
    if config.project.project_type == "executable" {
        return Ok(());
    }

    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);
    fs::create_dir_all(&build_path)?;

    // The package configuration file.
    let config_file = build_path.join(format!("{}Config.cmake", project_name));

    // Get the library output directory and expand tokens like ${CONFIG}, ${OS}, and ${ARCH}.
    let lib_dir = config.output.lib_dir.as_deref().unwrap_or(DEFAULT_LIB_DIR);
    let lib_dir_expanded = expand_output_tokens(lib_dir, &config);

    // Create absolute path without using canonicalize (which adds \\?\)
    let lib_path = if Path::new(&lib_dir_expanded).is_absolute() {
        PathBuf::from(&lib_dir_expanded)
    } else {
        project_path.join(&lib_dir_expanded)
    };

    // Removed the log that was here:
    // println!("{}", format!("Library path resolved to: {}", lib_path.display()).blue());

    // Determine the platform and compiler style to generate the correct library name format
    let compiler_label = get_effective_compiler_label(&config);
    let is_msvc_style = matches!(compiler_label.to_lowercase().as_str(), "msvc" | "clang-cl");

    // Check if this is a shared library
    let is_shared = config.project.project_type == "shared-library";

    // Generate a list of possible library filenames to try
    let mut possible_filenames = Vec::new();

    if is_msvc_style {
        // --- MSVC/clang-cl style ---
        if is_shared {
            // For a shared library with MSVC, you typically link against *.lib.
            // The .dll is needed at runtime but not for the link. We can look for both:
            possible_filenames.push(format!("{}.lib", project_name));
            possible_filenames.push(format!("lib{}.lib", project_name));
            // Optionally also see if we find the DLL (in case we want to copy it somewhere):
            possible_filenames.push(format!("{}.dll", project_name));
            possible_filenames.push(format!("lib{}.dll", project_name));
        } else {
            // For a static library on MSVC:
            possible_filenames.push(format!("{}.lib", project_name));
            possible_filenames.push(format!("lib{}.lib", project_name));
        }
    } else {
        // --- MinGW/GCC/clang (GNU driver) style ---
        if is_shared {
            // A shared library on MinGW typically has "libX.dll.a" as the import lib
            // and "X.dll" or "libX.dll" as the actual runtime
            possible_filenames.push(format!("lib{}.dll.a", project_name));
            possible_filenames.push(format!("{}.dll.a", project_name));
            // We can also look for the DLL in case we want to find it:
            possible_filenames.push(format!("lib{}.dll", project_name));
            possible_filenames.push(format!("{}.dll", project_name));
        } else {
            // Static library on MinGW: "libX.a" or "X.a"
            possible_filenames.push(format!("lib{}.a", project_name));
            possible_filenames.push(format!("{}.a", project_name));
        }
    }

    let mut lib_file = None;

    // Now loop over possible_filenames to see which actually exists:
    for filename in &possible_filenames {
        let candidate = lib_path.join(filename);
        // Removed the log that was here:
        // println!("Checking for: {}", candidate.display());
        if candidate.exists() {
            // Removed the log that was here:
            // println!("Found: {}", candidate.display());
            lib_file = Some(candidate);
            break;
        }
    }

    // If not found, check in the parent directory
    if lib_file.is_none() {
        let parent_dir = lib_path.parent().unwrap_or(&lib_path);
        for filename in &possible_filenames {
            let file_path = parent_dir.join(filename);
            if file_path.exists() {
                // Only keep this important notification about found files
                if !is_quiet() {
                    println!("{}", format!("Found library at: {}", file_path.display()).green());
                }
                lib_file = Some(file_path);
                break;
            }
        }
    }

    // If still not found, try to search the entire project directory
    if lib_file.is_none() {
        // Removed the log that was here:
        // println!("{}", "Searching project directory for library...".yellow());
        fn find_library(dir: &Path, patterns: &[String]) -> Option<PathBuf> {
            if let Ok(entries) = fs::read_dir(dir) {
                for entry in entries.filter_map(Result::ok) {
                    let path = entry.path();
                    if path.is_dir() {
                        if let Some(found) = find_library(&path, patterns) {
                            return Some(found);
                        }
                    } else if let Some(name) = path.file_name() {
                        let name_str = name.to_string_lossy();
                        for pattern in patterns {
                            if name_str == *pattern {
                                return Some(path);
                            }
                        }
                    }
                }
            }
            None
        }

        lib_file = find_library(project_path, &possible_filenames);
        if let Some(ref path) = lib_file {
            if !is_quiet() {
                println!("{}", format!("Found library by search: {}", path.display()).green());
            }
        }
    }

    let lib_file = match lib_file {
        Some(path) => path,
        None => {
            if !is_quiet() {
                println!("{}", format!("Warning: Library file not found for {}. Using placeholder.", project_name).yellow());
            }
            // Use a placeholder file that will be replaced at link time
            lib_path.join(if is_msvc_style {
                format!("{}.lib", project_name)
            } else if is_shared {
                format!("lib{}.dll.a", project_name)
            } else {
                format!("lib{}.a", project_name)
            })
        }
    };

    // Normalize the path for CMake
    let raw_lib_file = lib_file.to_string_lossy();
    let normalized_lib_file = if raw_lib_file.starts_with(r"\\?\") {
        raw_lib_file[4..].replace("\\", "/")
    } else {
        raw_lib_file.replace("\\", "/")
    };

    // Similarly, for the include path:
    let include_path = project_path.join("include");
    let raw_include = include_path.to_string_lossy();
    let normalized_include = if raw_include.starts_with(r"\\?\") {
        raw_include[4..].replace("\\", "/")
    } else {
        raw_include.replace("\\", "/")
    };

    // Choose the correct import target type
    let import_type = if is_shared { "SHARED" } else { "STATIC" };

    // Generate the config content with the correct library type
    let config_content = format!(r#"# Generated by cforge
# Config file for {} library

# Compute the installation prefix relative to this file
get_filename_component(SELF_DIR "${{CMAKE_CURRENT_LIST_FILE}}" PATH)

# Create imported target
if(NOT TARGET {}::{})
  add_library({}::{} {} IMPORTED)
  set_target_properties({}::{}
    PROPERTIES
    IMPORTED_LOCATION "{}"
    INTERFACE_INCLUDE_DIRECTORIES "{}"
  )
endif()

# Set variables for backward compatibility
set({}_LIBRARIES {}::{})
set({}_INCLUDE_DIRS "{}")
set({}_FOUND TRUE)
"#,
                                 project_name,
                                 project_name, project_name,
                                 project_name, project_name, import_type,
                                 project_name, project_name,
                                 normalized_lib_file, normalized_include,
                                 project_name.to_uppercase(), project_name, project_name,
                                 project_name.to_uppercase(), normalized_include,
                                 project_name.to_uppercase()
    );

    fs::write(&config_file, config_content)?;
    if !is_quiet() {
        println!("{}", format!("Generated package config at {}", config_file.display()).green());
    }

    // Also generate a version file for exact configuration matching.
    let version_file = build_path.join(format!("{}ConfigVersion.cmake", project_name));
    let version_content = format!(r#"# This is a basic version file for the Config-mode of find_package().
# It is used by find_package() to determine the compatibility of requested and found versions.

set(PACKAGE_VERSION "{}")
if(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
  if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)
    set(PACKAGE_VERSION_EXACT TRUE)
  endif()
endif()
"#, config.project.version);

    fs::write(&version_file, version_content)?;
    if !is_quiet() {
        println!("{}", format!("Generated version file at {}", version_file.display()).green());
    }

    Ok(())
}

pub fn generate_cmake_lists(config: &ProjectConfig, project_path: &Path, variant_name: Option<&str>, workspace_config: Option<&WorkspaceConfig>) -> Result<(), Box<dyn std::error::Error>> {
    let project_config = &config.project;
    let targets_config = &config.targets;
    let cmake_minimum = CMAKE_MIN_VERSION;

    // Create a map to store modified target names (when needed to avoid conflicts)
    let mut target_name_map: HashMap<String, String> = HashMap::new();

    let mut cmake_content = vec![
        format!("cmake_minimum_required(VERSION {})", cmake_minimum),
        format!("project({} VERSION {})", project_config.name, project_config.version),
        String::new(),
        "# Generated by cforge - Do not edit manually".to_string(),
        String::new(),
        // Helper function for globbing (include this as before)
        "# Helper function to expand glob patterns and verify sources exist".to_string(),
        "function(verify_sources_exist pattern_list output_var)".to_string(),
        "  set(expanded_sources)".to_string(),
        "  foreach(pattern ${pattern_list})".to_string(),
        "    file(GLOB_RECURSE matched_sources ${pattern})".to_string(),
        "    list(APPEND expanded_sources ${matched_sources})".to_string(),
        "  endforeach()".to_string(),
        "  if(NOT expanded_sources)".to_string(),
        "    message(WARNING \"No source files found matching patterns: ${pattern_list}\")".to_string(),
        "  endif()".to_string(),
        "  set(${output_var} ${expanded_sources} PARENT_SCOPE)".to_string(),
        "endfunction()".to_string(),
        String::new(),
    ];

    // Add pkg-config support
    cmake_content.push("# Find and configure pkg-config".to_string());
    cmake_content.push("find_package(PkgConfig QUIET)".to_string());
    cmake_content.push(String::new());

    // Export this package as a CMake package so other workspace projects can find it
    cmake_content.push("# Export package information".to_string());
    cmake_content.push("include(CMakePackageConfigHelpers)".to_string());
    cmake_content.push(format!("set(EXPORT_NAME {})", project_config.name));
    cmake_content.push(String::new());

    // Add support for install() commands to work correctly
    cmake_content.push("# Setup installation paths".to_string());
    cmake_content.push("if(WIN32 AND NOT DEFINED CMAKE_INSTALL_PREFIX)".to_string());
    cmake_content.push("  set(CMAKE_INSTALL_PREFIX \"$ENV{ProgramFiles}/${PROJECT_NAME}\")".to_string());
    cmake_content.push("endif()".to_string());
    cmake_content.push("include(GNUInstallDirs)".to_string());
    cmake_content.push(String::new());

    // Add configuration-specific preprocessor definitions from build.configs
    // This is a critical part of the fix
    if let Some(configs) = &config.build.configs {
        cmake_content.push("# Configuration-specific definitions".to_string());

        // Make sure debug info is output
        cmake_content.push("message(STATUS \"CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\")".to_string());

        // First create clear compiler flags for each configuration
        for (config_name, config_settings) in configs {
            let cmake_config_name = config_name.to_uppercase();

            // Initialize flags for this configuration
            cmake_content.push(format!("# {} configuration flags", config_name));

            // Clear any previous flags for this configuration
            cmake_content.push(format!("set(CMAKE_C_FLAGS_{} \"\")", cmake_config_name));
            cmake_content.push(format!("set(CMAKE_CXX_FLAGS_{} \"\")", cmake_config_name));

            // Add defines to compiler flags
            if let Some(defines) = &config_settings.defines {
                if !defines.is_empty() {
                    let define_flags = defines.iter()
                        .map(|d| if d.contains('=') { format!("-D{}", d) } else { format!("-D{}=1", d) })
                        .collect::<Vec<_>>()
                        .join(" ");

                    // Add defines directly to compiler flags
                    cmake_content.push(format!("set(CMAKE_C_FLAGS_{0} \"${{CMAKE_C_FLAGS_{0}}} {1}\")",
                                               cmake_config_name, define_flags));
                    cmake_content.push(format!("set(CMAKE_CXX_FLAGS_{0} \"${{CMAKE_CXX_FLAGS_{0}}} {1}\")",
                                               cmake_config_name, define_flags));
                }
            }

            // Add flags from the configuration
            if let Some(flags) = &config_settings.flags {
                if !flags.is_empty() {
                    let is_msvc_style = is_msvc_style_for_config(config);
                    let real_flags = parse_universal_flags(flags, is_msvc_style);
                    if !real_flags.is_empty() {
                        let flags_str = real_flags.join(" ");

                        // Add flags directly
                        cmake_content.push(format!("set(CMAKE_C_FLAGS_{0} \"${{CMAKE_C_FLAGS_{0}}} {1}\")",
                                                   cmake_config_name, flags_str));
                        cmake_content.push(format!("set(CMAKE_CXX_FLAGS_{0} \"${{CMAKE_CXX_FLAGS_{0}}} {1}\")",
                                                   cmake_config_name, flags_str));
                    }
                }
            }

            // Show flags for debugging - fixed the syntax error here
            cmake_content.push(format!("message(STATUS \"{} C flags: ${{CMAKE_C_FLAGS_{}}}\")", config_name, cmake_config_name));
            cmake_content.push(format!("message(STATUS \"{} CXX flags: ${{CMAKE_CXX_FLAGS_{}}}\")", config_name, cmake_config_name));
        }

        cmake_content.push(String::new());
    }

    // Add multi-config support
    cmake_content.push("# Enable multi-config support".to_string());
    cmake_content.push("if(NOT CMAKE_CONFIGURATION_TYPES)".to_string());
    cmake_content.push("  set(CMAKE_CONFIGURATION_TYPES \"Debug;Release\" CACHE STRING \"\" FORCE)".to_string());
    cmake_content.push("endif()".to_string());
    cmake_content.push(String::new());

    let vcpkg_config = &config.dependencies.vcpkg;
    if vcpkg_config.enabled && !vcpkg_config.packages.is_empty() {
        cmake_content.push("# vcpkg dependencies".to_string());

        // Process each package to create find_package calls
        for package in &vcpkg_config.packages {
            // Extract base package name without version or platform
            let base_package = package.split(':').next().unwrap_or(package);
            cmake_content.push(format!("find_package({} REQUIRED)", base_package));
        }
        cmake_content.push(String::new());
    }

    // For each target, we need to:
    // 1. create the target
    // 2. Set its properties
    // 3. Apply configuration-specific compile definitions

    // TARGET CREATION PHASE
    // Process each target and create them first
    for (target_name, target_config) in targets_config {
        let target_type = &project_config.project_type;
        let sources = &target_config.sources;

        // Handle source files using CMake's file globbing with our helper function
        cmake_content.push(format!("# Source patterns for target {}", target_name));
        cmake_content.push("# Debug output of source patterns for error diagnosis".to_string());
        cmake_content.push(format!("message(STATUS \"Source patterns for {}: ${{SOURCE_PATTERNS}}\")", target_name));

        // Convert source patterns to CMake list
        let source_patterns = sources.iter()
            .map(|s| format!("\"{}\"", s))
            .collect::<Vec<_>>()
            .join(" ");

        // Add default source if sources list is empty
        if sources.is_empty() {
            cmake_content.push(format!("set(SOURCE_PATTERNS \"src/main.cpp\")"));
        } else {
            cmake_content.push(format!("set(SOURCE_PATTERNS {})", source_patterns));
        }

        // Use our helper function to expand glob patterns
        cmake_content.push(format!("verify_sources_exist(\"${{SOURCE_PATTERNS}}\" {}_SOURCES)", target_name.to_uppercase()));

        cmake_content.push(format!("message(STATUS \"Found source files for {}: ${{{}}}\")",
                                   target_name, target_name.to_uppercase() + "_SOURCES"));

        // Add fallback for when no sources are found
        cmake_content.push(format!("if(NOT {}_SOURCES)", target_name.to_uppercase()));
        cmake_content.push(format!("  # No sources found, using default source"));
        cmake_content.push(format!("  file(WRITE \"${{CMAKE_CURRENT_BINARY_DIR}}/default_main.cpp\" \"#include <iostream>\\n\\nint main(int argc, char* argv[]) {{\\n    std::cout << \\\"Hello from {target_name}\\\" << std::endl;\\n    return 0;\\n}}\\n\")"));
        cmake_content.push(format!("  set({}_SOURCES \"${{CMAKE_CURRENT_BINARY_DIR}}/default_main.cpp\")", target_name.to_uppercase()));
        cmake_content.push(format!("  message(STATUS \"No source files found for {}, using default main.cpp\")", target_name));
        cmake_content.push(format!("endif()"));

        // Determine if we need a modified target name for CMake to avoid conflicts
        // This is crucial when project name and target name are identical
        let cmake_target_name = if project_config.name == *target_name {
            let modified_name = format!("{}_lib", target_name);
            target_name_map.insert(target_name.clone(), modified_name.clone());
            modified_name
        } else {
            target_name.clone()
        };

        // Create target using the expanded sources with the modified target name
        if target_type == "executable" {
            cmake_content.push(format!("add_executable({} ${{{}_SOURCES}})", cmake_target_name, target_name.to_uppercase()));

            // Set the output name to project_target pattern if they're different, otherwise just project name
            if project_config.name != *target_name {
                cmake_content.push(format!("set_target_properties({} PROPERTIES OUTPUT_NAME \"{}_{}\")",
                                           cmake_target_name, project_config.name, target_name));
            } else {
                cmake_content.push(format!("set_target_properties({} PROPERTIES OUTPUT_NAME \"{}\")",
                                           cmake_target_name, project_config.name));
            }
        } else if target_type == "shared-library" {
            cmake_content.push(format!("add_library({} SHARED ${{{}_SOURCES}})", cmake_target_name, target_name.to_uppercase()));

            // For libraries, set output name to just project name
            cmake_content.push(format!("set_target_properties({} PROPERTIES OUTPUT_NAME \"{}\")",
                                       cmake_target_name, project_config.name));
        } else if target_type == "static-library" {
            cmake_content.push(format!("add_library({} STATIC ${{{}_SOURCES}})", cmake_target_name, target_name.to_uppercase()));

            // For libraries, set output name to just project name
            cmake_content.push(format!("set_target_properties({} PROPERTIES OUTPUT_NAME \"{}\")",
                                       cmake_target_name, project_config.name));
        } else if target_type == "header-only" {
            cmake_content.push(format!("add_library({} INTERFACE)", cmake_target_name));
        }

        // Add configuration-specific preprocessor definitions to this target directly
        // This is the most important part to fix
        if let Some(configs) = &config.build.configs {
            cmake_content.push("# Apply configuration-specific defines to target".to_string());

            // Using generator expressions for multi-config support
            for (config_name, config_settings) in configs {
                if let Some(defines) = &config_settings.defines {
                    if !defines.is_empty() {
                        let cmake_config_name = config_name.to_uppercase();

                        let defines_str = defines.iter()
                            .map(|d| format!("$<$<CONFIG:{}>:{}>", cmake_config_name, d))
                            .collect::<Vec<_>>()
                            .join(";");

                        cmake_content.push(format!("target_compile_definitions({} PRIVATE {})",
                                                   cmake_target_name, defines_str));
                    }
                }
            }

            cmake_content.push(String::new());
        }
    }

    // PROPERTY SETTING PHASE
    // Now set properties on all targets
    for (target_name, target_config) in targets_config {
        let target_type = &project_config.project_type;
        let empty_vec = Vec::new();
        let include_dirs = target_config.include_dirs.as_ref().unwrap_or(&empty_vec);
        let defines = target_config.defines.as_ref().unwrap_or(&empty_vec);
        let links = target_config.links.as_ref().unwrap_or(&empty_vec);
        let mut all_links = Vec::new();

        // Get the actual target name (which may have been modified to avoid conflicts)
        let cmake_target_name = target_name_map.get(target_name).unwrap_or(&target_name).clone();

        // Enable PCH for this target if configured
        if let Some(pch_config) = &config.pch {
            if pch_config.enabled {
                cmake_content.push(format!("# Enable PCH for target {}", cmake_target_name));
                cmake_content.push(format!("target_enable_pch({})", cmake_target_name));
            }
        }

        if let Some(links) = &target_config.links {
            all_links.extend(links.clone());
        }

        // Add platform-specific links if applicable
        if let Some(platform_links) = &target_config.platform_links {
            let current_os = if cfg!(target_os = "windows") {
                "windows"
            } else if cfg!(target_os = "macos") {
                "darwin"
            } else {
                "linux"
            };

            if let Some(os_links) = platform_links.get(current_os) {
                all_links.extend(os_links.clone());
            }
        }

        // Add workspace dependencies as links
        for dep in &config.dependencies.workspace {
            // Add namespaced target
            let link_target = format!("{}::{}", dep.name, dep.name);

            // Only add if not already in links
            if !all_links.contains(&link_target) {
                all_links.push(link_target);
            }
        }

        // Include directories - use modified target name
        if !include_dirs.is_empty() {
            if target_type == "header-only" {
                // For header-only libraries
                cmake_content.push(format!("target_include_directories({} INTERFACE", cmake_target_name));
                for include_dir in include_dirs {
                    cmake_content.push(format!("  \"$<BUILD_INTERFACE:${{CMAKE_CURRENT_SOURCE_DIR}}/{}>\"", include_dir));
                    cmake_content.push(format!("  \"$<INSTALL_INTERFACE:include>\""));
                }
                cmake_content.push(")".to_string());
            } else if target_type == "static-library" || target_type == "shared-library" {
                // For libraries
                cmake_content.push(format!("target_include_directories({} PUBLIC", cmake_target_name));
                for include_dir in include_dirs {
                    cmake_content.push(format!("  \"$<BUILD_INTERFACE:${{CMAKE_CURRENT_SOURCE_DIR}}/{}>\"", include_dir));
                    cmake_content.push(format!("  \"$<INSTALL_INTERFACE:include>\""));
                }
                cmake_content.push(")".to_string());
            } else {
                // For executables
                let includes = include_dirs.iter()
                    .map(|s| format!("\"{}\"", s))
                    .collect::<Vec<_>>()
                    .join(" ");
                if !includes.is_empty() {
                    cmake_content.push(format!("target_include_directories({} PRIVATE {})", cmake_target_name, includes));
                }
            }
        }

        // Add target-specific defines - use modified target name
        if !defines.is_empty() {
            let defines_str = defines.iter()
                .map(|d| format!("\"{}\"", d))
                .collect::<Vec<_>>()
                .join(" ");

            if target_type == "header-only" {
                cmake_content.push(format!("target_compile_definitions({} INTERFACE {})", cmake_target_name, defines_str));
            } else {
                cmake_content.push(format!("target_compile_definitions({} PRIVATE {})", cmake_target_name, defines_str));
            }
        }

        // Link libraries - use modified target name
        if !all_links.is_empty() {
            let links_str = all_links.join(" ");
            if target_type == "header-only" {
                cmake_content.push(format!("target_link_libraries({} INTERFACE {})", cmake_target_name, links_str));
            } else {
                // Use PUBLIC instead of PRIVATE to propagate dependencies
                cmake_content.push(format!("target_link_libraries({} PUBLIC {})", cmake_target_name, links_str));
            }
        }

        // Export the target for libraries - use modified target name
        if target_type != "executable" {
            // Add export commands so this library can be used as a dependency
            cmake_content.push(String::new());
            cmake_content.push("# Export targets for use by other projects".to_string());

            // Create a namespaced alias that uses the project name
            // This is crucial - we need to make sure exported targets use the correct namespace
            cmake_content.push(format!("add_library({0}::{0} ALIAS {1})", project_config.name, cmake_target_name));

            // Export the target from the build tree
            cmake_content.push(format!("export(TARGETS {0} NAMESPACE {1}:: FILE {1}Config.cmake)",
                                       cmake_target_name, project_config.name));

            // Install the library
            cmake_content.push(format!("install(TARGETS {0} EXPORT {1}Targets",
                                       cmake_target_name, project_config.name));
            cmake_content.push(format!("  LIBRARY DESTINATION ${{CMAKE_INSTALL_LIBDIR}}"));
            cmake_content.push(format!("  ARCHIVE DESTINATION ${{CMAKE_INSTALL_LIBDIR}}"));
            cmake_content.push(format!("  RUNTIME DESTINATION ${{CMAKE_INSTALL_BINDIR}}"));
            cmake_content.push(format!("  INCLUDES DESTINATION ${{CMAKE_INSTALL_INCLUDEDIR}})"));

            // Install headers
            for include_dir in include_dirs {
                cmake_content.push(format!("install(DIRECTORY {} DESTINATION ${{CMAKE_INSTALL_INCLUDEDIR}})", include_dir));
            }

            // Export targets for installation - use project name for namespace
            cmake_content.push(format!("install(EXPORT {0}Targets NAMESPACE {0}:: DESTINATION ${{CMAKE_INSTALL_LIBDIR}}/cmake/{0})",
                                       project_config.name));

            // Write a basic config file directly into the build directory
            cmake_content.push(format!("# Write a basic config file for build tree usage"));
            cmake_content.push(format!("file(WRITE \"${{CMAKE_CURRENT_BINARY_DIR}}/{0}Config.cmake\"", project_config.name));
            cmake_content.push(format!("\"include(\\\"${{CMAKE_CURRENT_BINARY_DIR}}/{0}Targets.cmake\\\")\\n\"", project_config.name));
            cmake_content.push(format!("\"set({0}_INCLUDE_DIR \\\"${{CMAKE_CURRENT_SOURCE_DIR}}/include\\\")\\n\"",
                                       project_config.name.to_uppercase()));
            cmake_content.push(format!("\"set({0}_FOUND TRUE)\\n\")", project_config.name.to_uppercase()));

            // Export package for build tree - use project name
            cmake_content.push(format!("export(PACKAGE {})", project_config.name));
        }
    }

    // Add testing support if tests directory exists
    let tests_path = project_path.join("tests");
    if tests_path.exists() {
        cmake_content.push(String::new());
        cmake_content.push("# Testing support".to_string());
        cmake_content.push("enable_testing()".to_string());
        cmake_content.push("add_subdirectory(tests)".to_string());
        cmake_content.push(String::new());
    }

    // Add installation instructions if not already added
    let has_install_commands = cmake_content.iter().any(|line| line.contains("install("));
    if !has_install_commands && project_config.project_type == "executable" {
        cmake_content.push("# Installation".to_string());
        // Use the modified target names for installation
        for target_name in config.targets.keys() {
            let cmake_target_name = target_name_map.get(target_name).unwrap_or(target_name);
            cmake_content.push(format!("install(TARGETS {} DESTINATION bin)", cmake_target_name));
        }
        cmake_content.push(String::new());
    }

    // Add CPack configuration for packaging
    cmake_content.push("# Packaging with CPack".to_string());
    cmake_content.push("include(CPack)".to_string());
    cmake_content.push("set(CPACK_PACKAGE_NAME \"${PROJECT_NAME}\")".to_string());
    cmake_content.push("set(CPACK_PACKAGE_VERSION \"${PROJECT_VERSION}\")".to_string());
    cmake_content.push("set(CPACK_PACKAGE_VENDOR \"cforge User\")".to_string());
    cmake_content.push("set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_NAME} - ${PROJECT_DESCRIPTION}\")".to_string());

    // Write the CMakeLists.txt file
    let cmake_file = project_path.join("CMakeLists.txt");
    let mut file = File::create(cmake_file)?;
    file.write_all(cmake_content.join("\n").as_bytes())?;

    if !is_quiet() {
        println!("{}", "Generated CMakeLists.txt".green());
    }
    Ok(())
}

pub fn list_project_items(config: &ProjectConfig, what: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let item_type = what.unwrap_or("all");

    match item_type {
        "configs" => list_project_configs(config)?,
        "variants" => list_project_variants(config)?,
        "targets" => list_project_targets(config)?,
        "scripts" => list_project_scripts(config)?,
        "all" => {
            list_project_configs(config)?;
            list_project_variants(config)?;
            list_project_targets(config)?;
            list_project_scripts(config)?;
        },
        _ => {
            return Err(format!("Unknown item type: {}. Valid types are: configs, variants, targets, scripts, all", item_type).into());
        }
    }

    Ok(())
}

pub fn list_project_configs(config: &ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "Available configurations:".bold());

    if let Some(configs) = &config.build.configs {
        for (name, settings) in configs {
            // Fix: Store the joined string in a variable
            let flags_str = settings.flags.as_ref()
                .map(|f| f.join(" "))
                .unwrap_or_else(|| String::new());

            println!(" - {}: {}", name.green(), flags_str);
        }
    } else {
        println!(" - {}: Default debug build", "Debug".green());
        println!(" - {}: Optimized build", "Release".green());
    }

    println!();
    Ok(())
}

pub fn list_project_variants(config: &ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "Available build variants:".bold());

    if let Some(variants) = &config.variants {
        if let Some(default) = &variants.default {
            println!(" - {} (default)", default.green());
        }

        for (name, settings) in &variants.variants {
            if Some(name) != variants.default.as_ref() {
                let empty = String::new();
                let desc = settings.description.as_ref().unwrap_or(&empty);
                println!(" - {}: {}", name.green(), desc);
            }
        }
    } else {
        println!(" - {}: Standard build", "standard".green());
    }

    println!();
    Ok(())
}

pub fn list_project_targets(config: &ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "Available cross-compile targets:".bold());

    if let Some(cross) = &config.cross_compile {
        if cross.enabled {
            println!(" - {}: Configured in project", cross.target.green());
        }
    }

    // Show predefined targets
    println!(" - {}: Android ARM64 (NDK required)", "android-arm64".green());
    println!(" - {}: Android ARM (NDK required)", "android-arm".green());
    println!(" - {}: iOS ARM64 (Xcode required)", "ios".green());
    println!(" - {}: Raspberry Pi ARM (toolchain required)", "raspberry-pi".green());
    println!(" - {}: WebAssembly (Emscripten required)", "wasm".green());

    println!();
    Ok(())
}

pub fn list_project_scripts(config: &ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "Available scripts:".bold());

    if let Some(scripts) = &config.scripts {
        for (name, cmd) in &scripts.scripts {
            println!(" - {}: {}", name.green(), cmd);
        }
    } else {
        println!(" - No custom scripts defined");
    }

    println!();
    Ok(())
}

pub fn build_project_with_dependencies(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    cross_target: Option<&str>,
    workspace_config: Option<&WorkspaceConfig>,
    ensure_package_config: bool,
) -> Result<(), Box<dyn std::error::Error>> {
    // If we have a workspace with dependencies, ensure package configs exist
    if let Some(workspace) = workspace_config {
        if !config.dependencies.workspace.is_empty() && ensure_package_config {
            // First build dependency graph
            let mut project_paths = Vec::new();
            for project_name in &workspace.workspace.projects {
                let path = if Path::new(project_name).exists() {
                    PathBuf::from(project_name)
                } else if Path::new("projects").join(project_name).exists() {
                    PathBuf::from("projects").join(project_name)
                } else {
                    PathBuf::from(project_name)
                };

                project_paths.push(path);
            }

            let dep_graph = build_dependency_graph(workspace, &project_paths)?;

            // For each dependency, ensure package config exists
            for dep in &config.dependencies.workspace {
                // Check if we can find the dependency path
                let dep_path = if Path::new(&dep.name).exists() {
                    PathBuf::from(&dep.name)
                } else if Path::new("projects").join(&dep.name).exists() {
                    PathBuf::from("projects").join(&dep.name)
                } else {
                    PathBuf::from(&dep.name)
                };

                // Generate package config for each dependency
                generate_package_config(&dep_path, &dep.name)?;
            }
        }
    }

    // Now build the project with the normal build process
    build_project(config, project_path, config_type, variant_name, cross_target, workspace_config)
}