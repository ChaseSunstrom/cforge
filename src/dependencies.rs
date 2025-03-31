use std::collections::{HashMap, HashSet, VecDeque};
use std::{env, fs};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use colored::Colorize;
use crate::config::{PackageInstallState, ProjectConfig};
use crate::output_utils::{is_quiet, is_verbose, print_check_item, print_detailed, print_error, print_status, print_step, print_substep, print_success, print_warning, SpinningWheel};
use crate::{ensure_compiler_available, has_command, progress_bar, run_command, run_command_with_pattern_tracking, run_command_with_timeout, CACHED_PATHS, DEFAULT_BUILD_DIR, INSTALLED_PACKAGES, VCPKG_DEFAULT_DIR};
use crate::errors::expand_tilde;

pub fn install_dependencies(
    config: &ProjectConfig,
    project_path: &Path,
    update: bool
) -> Result<HashMap<String, String>, Box<dyn std::error::Error>> {
    let mut dependencies_info = HashMap::new();

    // Set up vcpkg dependencies
    if config.dependencies.vcpkg.enabled {
        print_status("\nSetting up vcpkg dependencies");

        let spinner = progress_bar("Configuring vcpkg");
        match setup_vcpkg(config, project_path) {
            Ok(toolchain) => {
                if !toolchain.is_empty() {
                    dependencies_info.insert("vcpkg_toolchain".to_string(), toolchain);
                    spinner.success();
                } else {
                    spinner.success(); // Still success but empty result
                }
            },
            Err(e) => {
                spinner.failure(&e.to_string());
                return Err(e);
            }
        }
    }

    // Set up conan dependencies
    if config.dependencies.conan.enabled && !config.dependencies.conan.packages.is_empty() {
        print_status("Setting up Conan dependencies");

        let spinner = progress_bar("Configuring Conan");
        match setup_conan(config, project_path) {
            Ok(cmake_file) => {
                if !cmake_file.is_empty() {
                    dependencies_info.insert("conan_cmake".to_string(), cmake_file);
                    spinner.success();
                } else {
                    spinner.success(); // Still success but empty result
                }
            },
            Err(e) => {
                spinner.failure(&e.to_string());
                return Err(e);
            }
        }
    }

    // Set up git dependencies - WITHOUT a progress bar wrapper
    if !config.dependencies.git.is_empty() {
        print_status("Setting up git dependencies");

        // Remove the spinner/progress bar - each individual git command will show its own progress
        match setup_git_dependencies(config, project_path) {
            Ok(includes) => {
                if !includes.is_empty() {
                    dependencies_info.insert("git_includes".to_string(), includes.join(";"));
                }
            },
            Err(e) => {
                print_error(&format!("Failed to set up git dependencies: {}", e), None, None);
                return Err(e);
            }
        }
    }

    // Set up custom dependencies
    if !config.dependencies.custom.is_empty() {
        print_status("Setting up custom dependencies");

        let spinner = progress_bar("Configuring custom dependencies");
        match setup_custom_dependencies(config, project_path) {
            Ok(includes) => {
                if !includes.is_empty() {
                    dependencies_info.insert("custom_includes".to_string(), includes.join(";"));
                    spinner.success();
                } else {
                    spinner.success(); // Still success but empty result
                }
            },
            Err(e) => {
                spinner.failure(&e.to_string());
                return Err(e);
            }
        }
    }

    if !dependencies_info.is_empty() {
        print_check_item("Dependencies configured successfully", None);
    } else {
        print_check_item("Dependencies status", Some("No dependencies configured or all dependencies are disabled"));
    }

    Ok(dependencies_info)
}
pub fn setup_vcpkg(
    config: &ProjectConfig,
    project_path: &Path
) -> Result<String, Box<dyn std::error::Error>> {

    // Skip if vcpkg is disabled
    let vcpkg_config = &config.dependencies.vcpkg;
    if !vcpkg_config.enabled {
        if !is_quiet() {
            print_substep("vcpkg is disabled in configuration, skipping setup");
        }
        return Ok(String::new());
    }

    // Skip if no packages are defined
    if vcpkg_config.packages.is_empty() {
        if !is_quiet() {
            print_substep("No vcpkg packages defined, skipping setup");
        }
        return Ok(String::new());
    }

    // Create a progress bar for overall vcpkg setup
    let mut vcpkg_progress = SpinningWheel::start("Setting up vcpkg");

    // Check if we've already set up vcpkg in this session
    let cache_key = "vcpkg_setup";
    let should_setup = {
        let installed = INSTALLED_PACKAGES.lock().unwrap();
        if installed.contains(cache_key) {
            let cached_path = get_cached_vcpkg_toolchain_path();
            if !cached_path.is_empty() {
                if !is_quiet() {
                    print_detailed("Using previously set up vcpkg");
                }
                vcpkg_progress.success();
                return Ok(cached_path);
            }
            false
        } else {
            true
        }
    };

    if !should_setup {
        vcpkg_progress.success();
        return Ok(String::new());
    }

    // First, try the configured path
    let configured_path = vcpkg_config.path.as_deref().unwrap_or(VCPKG_DEFAULT_DIR);
    let configured_path = expand_tilde(configured_path);

    // Try to find vcpkg location - 10% progress
    if !is_quiet() {
        print_substep("Locating vcpkg");
    }

    // Check if vcpkg is in PATH
    let mut vcpkg_in_path = None;
    if let Ok(path_var) = env::var("PATH") {
        for path in path_var.split(if cfg!(windows) { ';' } else { ':' }) {
            let vcpkg_exe = if cfg!(windows) {
                PathBuf::from(path).join("vcpkg.exe")
            } else {
                PathBuf::from(path).join("vcpkg")
            };

            if vcpkg_exe.exists() {
                vcpkg_in_path = Some(vcpkg_exe.parent().unwrap().to_path_buf());
                break;
            }
        }
    }

    // Try common locations
    let mut potential_vcpkg_paths = vec![
        configured_path.clone(),
        vcpkg_in_path.map_or(String::new(), |p| p.to_string_lossy().to_string()),
        String::from("C:/vcpkg"),
        String::from("C:/dev/vcpkg"),
        String::from("C:/tools/vcpkg"),
        String::from("/usr/local/vcpkg"),
        String::from("/opt/vcpkg"),
        expand_tilde("~/vcpkg"),
    ];

    // Filter out empty strings
    potential_vcpkg_paths.retain(|s| !s.is_empty());

    // Find the first valid vcpkg installation
    let mut found_vcpkg_path = None;
    let mut found_exe_path = None;

    for path in potential_vcpkg_paths {
        let vcpkg_exe = if cfg!(windows) {
            PathBuf::from(&path).join("vcpkg.exe")
        } else {
            PathBuf::from(&path).join("vcpkg")
        };

        if vcpkg_exe.exists() {
            // Check if this is a valid vcpkg installation with the toolchain file
            let toolchain_file = PathBuf::from(&path)
                .join("scripts")
                .join("buildsystems")
                .join("vcpkg.cmake");

            if toolchain_file.exists() {
                found_vcpkg_path = Some(path);
                found_exe_path = Some(vcpkg_exe);
                break;
            }
        }
    }

    // If vcpkg wasn't found anywhere, try to set it up in the configured path
    let vcpkg_path = if let Some(path) = found_vcpkg_path {
        if !is_quiet() {
            print_substep(&format!("Found existing vcpkg installation at {}", path));
        }
        path
    } else {
        if !is_quiet() {
            print_substep(&format!("vcpkg not found, attempting to set up at {}", configured_path));
        }

        // Create parent directory if it doesn't exist
        let vcpkg_parent_dir = Path::new(&configured_path).parent().unwrap_or_else(|| Path::new(&configured_path));
        fs::create_dir_all(vcpkg_parent_dir)?;

        // Check if the directory exists but is not a proper vcpkg installation
        let vcpkg_dir = Path::new(&configured_path);
        if vcpkg_dir.exists() {
            print_warning(&format!("Directory {} exists but does not contain a valid vcpkg installation", configured_path), None);
        }

        // Try to clone vcpkg repository - 20% to 40% progress
        let mut clone_progress = SpinningWheel::start("Cloning vcpkg repository");
        match run_command_with_timeout(
            vec![
                String::from("git"),
                String::from("clone"),
                String::from("https://github.com/microsoft/vcpkg.git"),
                String::from(&configured_path)
            ],
            None,
            None,
            180,
            None
        ) {
            Ok(_) => {
                clone_progress.success();
            },
            Err(e) => {
                clone_progress.failure(&e.to_string());

                if vcpkg_dir.exists() {
                    print_warning("Git clone failed but directory exists", None);
                } else if !has_command("git") {
                    // Try to install git
                    print_warning("Git not found. Attempting to install git...", None);

                    let mut git_install_progress = SpinningWheel::start("Installing git");

                    let git_installed = if cfg!(windows) {
                        if has_command("winget") {
                            run_command_with_timeout(
                                vec![
                                    "winget".to_string(),
                                    "install".to_string(),
                                    "--id".to_string(),
                                    "Git.Git".to_string()
                                ],
                                None,
                                None,
                                120,
                                None
                            ).is_ok()
                        } else {
                            false
                        }
                    } else if cfg!(target_os = "macos") {
                        if has_command("brew") {
                            run_command_with_timeout(
                                vec![
                                    "brew".to_string(),
                                    "install".to_string(),
                                    "git".to_string()
                                ],
                                None,
                                None,
                                180,
                                None
                            ).is_ok()
                        } else {
                            false
                        }
                    } else { // Linux
                        run_command_with_timeout(
                            vec![
                                "sudo".to_string(),
                                "apt-get".to_string(),
                                "update".to_string()
                            ],
                            None,
                            None,
                            60,
                            None
                        ).is_ok() &&
                            run_command_with_timeout(
                                vec![
                                    "sudo".to_string(),
                                    "apt-get".to_string(),
                                    "install".to_string(),
                                    "-y".to_string(),
                                    "git".to_string()
                                ],
                                None,
                                None,
                                120,
                                None
                            ).is_ok()
                    };

                    if git_installed && has_command("git") {
                        git_install_progress.success();

                        // Try cloning again
                        let mut retry_progress = SpinningWheel::start("Retrying vcpkg clone");
                        match run_command_with_timeout(
                            vec![
                                String::from("git"),
                                String::from("clone"),
                                String::from("https://github.com/microsoft/vcpkg.git"),
                                String::from(&configured_path)
                            ],
                            None,
                            None,
                            180,
                            None
                        ) {
                            Ok(_) => {
                                retry_progress.success();
                            },
                            Err(e) => {
                                retry_progress.failure(&e.to_string());
                                vcpkg_progress.failure("Failed to clone vcpkg repository");
                                return Err("Failed to clone vcpkg repository".into());
                            }
                        }
                    } else {
                        git_install_progress.failure("Failed to install git");
                        vcpkg_progress.failure("Git is required to set up vcpkg");
                        return Err("Git is required to set up vcpkg but could not be installed".into());
                    }
                } else {
                    vcpkg_progress.failure(&e.to_string());
                    return Err(e);
                }
            }
        }

        let bootstrap_script = if cfg!(windows) {
            "bootstrap-vcpkg.bat"
        } else {
            "./bootstrap-vcpkg.sh"
        };

        // Check if compilers are available for bootstrapping
        if cfg!(windows) && !has_command("cl") && !has_command("g++") && !has_command("clang++") {
            print_warning("No C++ compiler found for bootstrapping vcpkg. Attempting to install...", None);

            let mut compiler_progress = SpinningWheel::start("Installing C++ compiler");
            match ensure_compiler_available("msvc").or_else(|_| ensure_compiler_available("gcc")) {
                Ok(_) => compiler_progress.success(),
                Err(e) => {
                    compiler_progress.failure(&e.to_string());
                    vcpkg_progress.failure("No compiler available for bootstrapping vcpkg");
                    return Err(e);
                }
            }
        } else if !cfg!(windows) && !has_command("g++") && !has_command("clang++") {
            print_warning("No C++ compiler found for bootstrapping vcpkg. Attempting to install...", None);

            let mut compiler_progress = SpinningWheel::start("Installing C++ compiler");
            match ensure_compiler_available("gcc").or_else(|_| ensure_compiler_available("clang")) {
                Ok(_) => compiler_progress.success(),
                Err(e) => {
                    compiler_progress.failure(&e.to_string());
                    vcpkg_progress.failure("No compiler available for bootstrapping vcpkg");
                    return Err(e);
                }
            }
        }

        let mut bootstrap_progress = SpinningWheel::start("Bootstrapping vcpkg");
        match run_command_with_timeout(
            vec![String::from(bootstrap_script)],
            Some(&configured_path),
            None,
            300,
            None
        ) {
            Ok(_) => {
                bootstrap_progress.success();
            },
            Err(e) => {
                bootstrap_progress.failure(&e.to_string());
                print_warning("Bootstrapping failed or timed out", Some("Will try to use existing vcpkg if available"));
            }
        }

        configured_path
    };

    // Get the vcpkg executable path
    let vcpkg_exe = if let Some(exe) = found_exe_path {
        exe
    } else {
        if cfg!(windows) {
            PathBuf::from(&vcpkg_path).join("vcpkg.exe")
        } else {
            PathBuf::from(&vcpkg_path).join("vcpkg")
        }
    };

    // Verify vcpkg executable exists
    if !vcpkg_exe.exists() {
        vcpkg_progress.failure(&format!("vcpkg executable not found at {}", vcpkg_exe.display()));
        return Err(format!("vcpkg executable not found at {}. Please install vcpkg manually.", vcpkg_exe.display()).into());
    }

    if !vcpkg_config.packages.is_empty() {
        if !is_quiet() {
            print_substep("Installing dependencies with vcpkg");
        }

        if !is_quiet() {
            print_substep("Checking package installation status");
        }

        // First update vcpkg with a dedicated progress bar
        let mut update_progress = SpinningWheel::start("Updating vcpkg");
        let update_result = run_command_with_timeout(
            vec![
                vcpkg_exe.to_string_lossy().to_string(),
                "update".to_string()
            ],
            Some(&vcpkg_path),
            None,
            120,
            None
        );

        if update_result.is_ok() {
            update_progress.success();
        } else {
            update_progress.failure("Update timed out or failed");
            print_warning("vcpkg update failed or timed out", Some("Continuing with package installation"));
        }

        // Install packages with dedicated progress bar
        let mut install_progress = SpinningWheel::start("Installing packages");
        match run_vcpkg_install_with_progress(&vcpkg_exe, &vcpkg_path, &vcpkg_config.packages, install_progress.clone()) {
            Ok(_) => {
                install_progress.success();
            },
            Err(e) => {
                install_progress.failure(&e.to_string());
                vcpkg_progress.failure(&e.to_string());
                return Err(e);
            }
        }

    }


    {
        let mut installed = INSTALLED_PACKAGES.lock().unwrap();
        installed.insert(cache_key.to_string());
    }

    let toolchain_file = PathBuf::from(&vcpkg_path)
        .join("scripts")
        .join("buildsystems")
        .join("vcpkg.cmake");

    if !toolchain_file.exists() {
        vcpkg_progress.failure(&format!("vcpkg toolchain file not found at {}", toolchain_file.display()));
        return Err(format!("vcpkg toolchain file not found at {}. This suggests a corrupt vcpkg installation.", toolchain_file.display()).into());
    }

    cache_vcpkg_toolchain_path(&toolchain_file);

    // Complete progress
    vcpkg_progress.success();

    Ok(toolchain_file.to_string_lossy().to_string())
}

pub fn setup_conan(config: &ProjectConfig, project_path: &Path) -> Result<String, Box<dyn std::error::Error>> {
    let conan_config = &config.dependencies.conan;
    if !conan_config.enabled || conan_config.packages.is_empty() {
        return Ok(String::new());
    }

    // Check if conan is installed
    if Command::new("conan").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_err() {
        return Err("Conan package manager not found. Please install it first.".into());
    }

    // Create conan directory in build
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let conan_dir = project_path.join(build_dir).join("conan");
    fs::create_dir_all(&conan_dir)?;

    // Create conanfile.txt
    let mut conanfile_content = String::from("[requires]\n");
    for package in &conan_config.packages {
        conanfile_content.push_str(&format!("{}\n", package));
    }

    // Add options if provided
    if let Some(options) = &conan_config.options {
        conanfile_content.push_str("\n[options]\n");
        for (option, value) in options {
            conanfile_content.push_str(&format!("{}={}\n", option, value));
        }
    }

    // Add generators
    conanfile_content.push_str("\n[generators]\n");
    if let Some(generators) = &conan_config.generators {
        for generator in generators {
            conanfile_content.push_str(&format!("{}\n", generator));
        }
    } else {
        // Default generators
        conanfile_content.push_str("cmake\n");
        conanfile_content.push_str("cmake_find_package\n");
    }

    // Write conanfile.txt
    let conanfile_path = conan_dir.join("conanfile.txt");
    fs::write(&conanfile_path, conanfile_content)?;

    // Run conan install
    let mut cmd = vec![
        "conan".to_string(),
        "install".to_string(),
        conanfile_path.to_string_lossy().to_string(),
        "--build=missing".to_string(),
    ];

    run_command(cmd, Some(&conan_dir.to_string_lossy().to_string()), None)?;

    // Return the path to conan's generated CMake file
    let cmake_file = conan_dir.join("conanbuildinfo.cmake");
    if !cmake_file.exists() {
        return Err("Conan failed to generate CMake integration files.".into());
    }

    Ok(cmake_file.to_string_lossy().to_string())
}

pub fn setup_git_dependencies(config: &ProjectConfig, project_path: &Path) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let git_deps = &config.dependencies.git;
    if git_deps.is_empty() {
        return Ok(Vec::new());
    }

    // Check if git is installed
    if Command::new("git").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_err() {
        return Err("Git not found. Please install git first.".into());
    }

    // Create deps directory
    println!("Project path: {}", project_path.display());
    let deps_dir = project_path.join("deps");
    println!("Deps directory: {}", deps_dir.display());
    fs::create_dir_all(&deps_dir)?;

    let mut cmake_include_paths = Vec::new();

    for dep in git_deps {
        let dep_path = deps_dir.join(&dep.name);
        println!("Checking for dependency: {}", dep.name);


        if dep_path.exists() {
            println!("{}", format!("Directory for dependency {} already exists at {}",
                                   dep.name, dep_path.display()).green());
            println!("{}", "Skipping clone operation, using existing directory.".green());

            // If the directory is a git repo and update is requested, try to pull
            let git_dir = dep_path.join(".git");
            if git_dir.exists() && dep.update.unwrap_or(false) {
                println!("Attempting to update existing git repository...");
                let status = Command::new("git")
                    .arg("pull")
                    .current_dir(&dep_path)
                    .status();

                if let Ok(status) = status {
                    if status.success() {
                        println!("Successfully updated repository.");
                    } else {
                        println!("Failed to update repository, continuing with existing files.");
                    }
                }
            }

            // Skip to next dependency
            continue;
        }

        // Only if the directory doesn't exist, clone the repository
        println!("{}", format!("Cloning git dependency: {}", dep.name).blue());

        // Build git clone command - very simple approach here
        let output = Command::new("git")
            .arg("clone")
            .arg(if dep.shallow.unwrap_or(true) { "--depth=1" } else { "--progress" })
            .args(if let Some(tag) = &dep.tag {
                vec!["--branch", tag]
            } else if let Some(branch) = &dep.branch {
                vec!["--branch", branch]
            } else {
                vec![]
            })
            .arg(&dep.url)
            .arg(&dep_path)
            .output();

        match output {
            Ok(output) => {
                if output.status.success() {
                    println!("{}", format!("Successfully cloned git dependency: {}", dep.name).green());

                    // Checkout specific commit if requested
                    if let Some(commit) = &dep.commit {
                        println!("{}", format!("Checking out commit: {}", commit).blue());

                        let checkout = Command::new("git")
                            .arg("checkout")
                            .arg(commit)
                            .current_dir(&dep_path)
                            .output();

                        if let Ok(checkout) = checkout {
                            if !checkout.status.success() {
                                println!("{}", format!("Failed to checkout commit: {}",
                                                       String::from_utf8_lossy(&checkout.stderr)).yellow());
                            }
                        }
                    }
                } else {
                    // Clone failed
                    let stderr = String::from_utf8_lossy(&output.stderr);
                    println!("{}", format!("Git clone failed: {}", stderr).red());

                    // If it's just the "directory exists" error, we can continue
                    if stderr.contains("already exists and is not") {
                        println!("{}", "Directory already exists, continuing with existing files.".yellow());
                    } else {
                        return Err(format!("Failed to clone git dependency {}: {}", dep.name, stderr).into());
                    }
                }
            },
            Err(e) => {
                println!("{}", format!("Failed to execute git command: {}", e).red());
                return Err(format!("Failed to clone git dependency {}: {}", dep.name, e).into());
            }
        }
    }

    Ok(cmake_include_paths)
}

// Custom function to run git commands with progress tracking
fn run_git_command_with_progress(args: Vec<String>, cwd: &Path, description: &str) -> Result<(), Box<dyn std::error::Error>> {
    use std::io::{BufRead, BufReader};
    use std::process::{Command, Stdio};
    use regex::Regex;

    let mut cmd = Command::new("git");
    cmd.args(&args);
    cmd.current_dir(cwd);

    // Make sure we capture stderr where git sends progress information
    cmd.stderr(Stdio::piped());
    cmd.stdout(Stdio::piped());

    println!("{}", format!("Running git {}", args.join(" ")).blue());

    let mut child = cmd.spawn()?;

    // Get handles to the stdout and stderr
    let stderr = child.stderr.take().ok_or("Failed to capture stderr")?;

    // Set up a progress tracking regex for git output
    // This regex matches git's progress output format (e.g., "Receiving objects: 45% (1234/2345)")
    let progress_regex = Regex::new(r"(?i)(?:Receiving|Resolving|Checking out files|remote: Counting|Writing|Checking out|Enumerating|Compressing)(?:[^:]+): +(\d+)%").unwrap();

    // Create the progress bar
    let mut progress_bar = SpinningWheel::start(description);
    let mut current_progress = 0.0;

    // Read and process stderr lines
    let stderr_reader = BufReader::new(stderr);
    for line in stderr_reader.lines() {
        if let Ok(line) = line {
            // Check for progress information
            if let Some(captures) = progress_regex.captures(&line) {
                if let Some(percent_str) = captures.get(1) {
                    if let Ok(percent) = percent_str.as_str().parse::<f32>() {
                        let new_progress = percent / 100.0;
                        if new_progress > current_progress {
                            current_progress = new_progress;
                        }
                    }
                }
            }

            // For verbose mode, show all output
            if is_verbose() {
                eprintln!("GIT> {}", line);
            }
        }
    }

    // Wait for the command to finish
    let status = child.wait()?;

    // Complete the progress bar
    if status.success() {
        progress_bar.success();
        Ok(())
    } else {
        progress_bar.failure(&format!("Git exited with code: {}", status.code().unwrap_or(-1)));
        Err(format!("Git command failed with exit code: {}", status.code().unwrap_or(-1)).into())
    }
}
// Setup custom dependencies
pub fn setup_custom_dependencies(config: &ProjectConfig, project_path: &Path) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let custom_deps = &config.dependencies.custom;
    if custom_deps.is_empty() {
        return Ok(Vec::new());
    }

    // Create deps directory
    let deps_dir = project_path.join("deps");
    fs::create_dir_all(&deps_dir)?;

    let mut cmake_include_paths = Vec::new();

    for dep in custom_deps {
        let dep_path = deps_dir.join(&dep.name);

        if !dep_path.exists() {
            println!("{}", format!("Downloading custom dependency: {}", dep.name).blue());

            // Create download directory
            fs::create_dir_all(&dep_path)?;

            // Download the dependency
            let url = &dep.url;
            let output_file = if url.ends_with(".zip") || url.ends_with(".tar.gz") || url.ends_with(".tgz") {
                format!("{}.{}", dep.name, url.split('.').last().unwrap_or("zip"))
            } else {
                format!("{}.zip", dep.name)
            };

            let output_path = deps_dir.join(&output_file);

            let mut cmd = if cfg!(target_os = "windows") {
                vec![
                    "powershell".to_string(),
                    "-Command".to_string(),
                    format!("Invoke-WebRequest -Uri '{}' -OutFile '{}'", url, output_path.to_string_lossy()),
                ]
            } else {
                vec![
                    "curl".to_string(),
                    "-L".to_string(),
                    "--output".to_string(),
                    output_path.to_string_lossy().to_string(),
                    url.clone(),
                ]
            };

            run_command(cmd, Some(&deps_dir.to_string_lossy().to_string()), None)?;

            // Extract archive
            let mut cmd = if output_path.to_string_lossy().ends_with(".zip") {
                if cfg!(target_os = "windows") {
                    vec![
                        "powershell".to_string(),
                        "-Command".to_string(),
                        format!("Expand-Archive -Path '{}' -DestinationPath '{}'",
                                output_path.to_string_lossy(), dep_path.to_string_lossy()),
                    ]
                } else {
                    vec![
                        "unzip".to_string(),
                        output_path.to_string_lossy().to_string(),
                        "-d".to_string(),
                        dep_path.to_string_lossy().to_string(),
                    ]
                }
            } else if output_path.to_string_lossy().ends_with(".tar.gz") || output_path.to_string_lossy().ends_with(".tgz") {
                if cfg!(target_os = "windows") {
                    vec![
                        "tar".to_string(),
                        "-xzf".to_string(),
                        output_path.to_string_lossy().to_string(),
                        "-C".to_string(),
                        dep_path.to_string_lossy().to_string(),
                    ]
                } else {
                    vec![
                        "tar".to_string(),
                        "-xzf".to_string(),
                        output_path.to_string_lossy().to_string(),
                        "-C".to_string(),
                        dep_path.to_string_lossy().to_string(),
                    ]
                }
            } else {
                return Err(format!("Unsupported archive format for dependency: {}", dep.name).into());
            };

            run_command(cmd, Some(&deps_dir.to_string_lossy().to_string()), None)?;

            // Build if a build command is provided
            if let Some(build_cmd) = &dep.build_command {
                let shell = if cfg!(target_os = "windows") { "cmd" } else { "sh" };
                let shell_arg = if cfg!(target_os = "windows") { "/C" } else { "-c" };

                let mut cmd = vec![
                    shell.to_string(),
                    shell_arg.to_string(),
                    build_cmd.clone(),
                ];

                run_command(cmd, Some(&dep_path.to_string_lossy().to_string()), None)?;
            }

            // Install if an install command is provided
            if let Some(install_cmd) = &dep.install_command {
                let shell = if cfg!(target_os = "windows") { "cmd" } else { "sh" };
                let shell_arg = if cfg!(target_os = "windows") { "/C" } else { "-c" };

                let mut cmd = vec![
                    shell.to_string(),
                    shell_arg.to_string(),
                    install_cmd.clone(),
                ];

                run_command(cmd, Some(&dep_path.to_string_lossy().to_string()), None)?;
            }
        } else {
            println!("{}", format!("Custom dependency already exists: {}", dep.name).green());
        }

        // Add include and library paths to CMake include paths
        if let Some(include_path) = &dep.include_path {
            let full_include_path = dep_path.join(include_path);
            cmake_include_paths.push(format!("-DCMAKE_INCLUDE_PATH={}", full_include_path.to_string_lossy()));
        }

        if let Some(library_path) = &dep.library_path {
            let full_library_path = dep_path.join(library_path);
            cmake_include_paths.push(format!("-DCMAKE_LIBRARY_PATH={}", full_library_path.to_string_lossy()));
        }
    }

    Ok(cmake_include_paths)
}

pub fn run_vcpkg_install_with_timeout(
    vcpkg_exe: &Path,
    vcpkg_path: &str,
    packages: &[String],
    timeout_seconds: u64
) -> Result<(), Box<dyn std::error::Error>> {
    // First check which packages are already installed
    let mut already_installed = Vec::new();
    let mut to_install = Vec::new();

    for pkg in packages {
        if check_vcpkg_package_installed(vcpkg_path, pkg) {
            already_installed.push(pkg.clone());
        } else {
            to_install.push(pkg.clone());
        }
    }

    // If all packages are already installed, just show that
    if to_install.is_empty() {
        if !is_quiet() {
            print_step("Packages already installed:", "  ");
            for pkg in &already_installed {
                print_substep(&format!("{}", pkg));
            }
        }
        return Ok(());
    }

    // Otherwise, run the install command for new packages
    let mut cmd = vec![
        vcpkg_exe.to_string_lossy().to_string(),
        "install".to_string(),
    ];

    // Add all packages to install at once
    for pkg in &to_install {
        cmd.push(pkg.clone());
    }

    // Store the count for later use (avoid borrowing moved value)
    let to_install_count = to_install.len();

    // Use the timeout version
    match run_command_with_timeout(cmd, Some(vcpkg_path), None, timeout_seconds, None) {
        Ok(_) => {
            // Show the results
            if !already_installed.is_empty() && !is_quiet() {
                print_step("Packages already installed:", "  ");
                for pkg in already_installed {
                    print_substep(&format!("{}", pkg));
                }
            }

            if !to_install.is_empty() && !is_quiet() {
                print_step("Newly installed packages:", "  ");
                for pkg in &to_install {
                    print_substep(&format!("{}", pkg));
                }
            }

            Ok(())
        },
        Err(e) => {
            print_error(&format!("Error running vcpkg: {}", e), None, None);

            // Try installing packages one by one as a fallback
            print_warning("Batch installation failed, trying one package at a time", None);

            let mut success_count = 0;
            let mut failed_pkgs = Vec::new();

            // Use a reference to avoid moving to_install
            for pkg in &to_install {
                let package_spinner = progress_bar(&format!("Installing package {}", pkg));

                let single_cmd = vec![
                    vcpkg_exe.to_string_lossy().to_string(),
                    "install".to_string(),
                    pkg.clone(),
                ];

                match run_command_with_timeout(single_cmd, Some(vcpkg_path), None, 300, None) { // 5 minute timeout per package
                    Ok(_) => {
                        package_spinner.success();
                        success_count += 1;
                    },
                    Err(e) => {
                        package_spinner.failure(&e.to_string());
                        failed_pkgs.push(pkg.clone());
                    }
                }
            }

            if success_count > 0 {
                print_substep(&format!("Successfully installed {} out of {} packages",
                                       success_count, to_install_count));

                if !failed_pkgs.is_empty() {
                    print_warning(&format!("Failed to install packages: {}", failed_pkgs.join(", ")),
                                  Some("You may need to install these manually"));
                }

                // If we got at least some packages, consider it a partial success
                if success_count > failed_pkgs.len() {
                    return Ok(());
                }
            }

            Err(e)
        }
    }
}

pub fn check_vcpkg_package_installed(vcpkg_path: &str, package: &str) -> bool {
    let vcpkg_exe = if cfg!(windows) {
        PathBuf::from(vcpkg_path).join("vcpkg.exe")
    } else {
        PathBuf::from(vcpkg_path).join("vcpkg")
    };

    // Run vcpkg list to check if the package is installed
    let output = Command::new(vcpkg_exe)
        .arg("list")
        .arg(package)
        .current_dir(vcpkg_path)
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .output();

    match output {
        Ok(output) => {
            let stdout = String::from_utf8_lossy(&output.stdout);
            stdout.contains(package)
        },
        Err(_) => false
    }
}

pub fn run_vcpkg_install(
    vcpkg_exe: &Path,
    vcpkg_path: &str,
    packages: &[String]
) -> Result<(), Box<dyn std::error::Error>> {
    // First check which packages are already installed
    let mut already_installed = Vec::new();
    let mut to_install = Vec::new();

    for pkg in packages {
        if check_vcpkg_package_installed(vcpkg_path, pkg) {
            already_installed.push(pkg.clone());
        } else {
            to_install.push(pkg.clone());
        }
    }

    // If all packages are already installed, just show that
    if to_install.is_empty() {
        if !is_quiet() {
            print_step("Packages already installed:", "  ");
            for pkg in &already_installed {
                print_substep(&format!("{}", pkg));
            }
        }
        return Ok(());
    }

    // Otherwise, run the install command for new packages
    let mut cmd = vec![
        vcpkg_exe.to_string_lossy().to_string(),
        "install".to_string()
    ];
    cmd.extend(to_install.clone());

    // Use the timeout version with a reasonable timeout (10 minutes)
    match run_command_with_timeout(cmd, Some(vcpkg_path), None, 600, None) {
        Ok(_) => {
            // Show the results
            if !already_installed.is_empty() && !is_quiet() {
                print_step("Packages already installed:", "  ");
                for pkg in already_installed {
                    print_substep(&format!("{}", pkg));
                }
            }

            if !to_install.is_empty() && !is_quiet() {
                print_step("Newly installed packages:", "  ");
                for pkg in to_install {
                    print_substep(&format!("{}", pkg));
                }
            }

            Ok(())
        },
        Err(e) => {
            print_error(&format!("Error running vcpkg: {}", e), None, None);
            Err(e)
        }
    }
}

pub fn setup_conan_with_progress(
    config: &ProjectConfig,
    project_path: &Path,
    progress: &mut SpinningWheel
) -> Result<String, Box<dyn std::error::Error>> {
    let conan_config = &config.dependencies.conan;
    if !conan_config.enabled || conan_config.packages.is_empty() {
        return Ok(String::new());
    }

    // Check if conan is installed
    if Command::new("conan").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_err() {

        // Try to install Conan
        print_warning("Conan package manager not found. Attempting to install...", None);

        let mut install_progress = SpinningWheel::start("Installing Conan");
        let install_result = if cfg!(windows) {
            if has_command("pip") {
                run_command_with_timeout(
                    vec!["pip".to_string(), "install".to_string(), "conan".to_string()],
                    None,
                    None,
                    180,
                    None
                )
            } else {
                Err("pip not found".into())
            }
        } else if cfg!(target_os = "macos") {
            if has_command("brew") {
                run_command_with_timeout(
                    vec!["brew".to_string(), "install".to_string(), "conan".to_string()],
                    None,
                    None,
                    180,
                    None
                )
            } else {
                Err("Homebrew not found".into())
            }
        } else {
            if has_command("pip") || has_command("pip3") {
                let pip_cmd = if has_command("pip3") { "pip3" } else { "pip" };
                run_command_with_timeout(
                    vec![pip_cmd.to_string(), "install".to_string(), "conan".to_string()],
                    None,
                    None,
                    180,
                    None
                )
            } else {
                Err("pip not found".into())
            }
        };

        if install_result.is_err() {
            install_progress.failure("Failed to install Conan");
            progress.clone().failure("Failed to install Conan package manager");
            return Err("Conan package manager not found and could not be installed. Please install it first.".into());
        }

        install_progress.success();
    }

    // Create conan directory in build
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let conan_dir = project_path.join(build_dir).join("conan");
    fs::create_dir_all(&conan_dir)?;

    // Create conanfile.txt
    let mut conanfile_content = String::from("[requires]\n");
    for package in &conan_config.packages {
        conanfile_content.push_str(&format!("{}\n", package));
    }

    // Add options if provided
    if let Some(options) = &conan_config.options {
        conanfile_content.push_str("\n[options]\n");
        for (option, value) in options {
            conanfile_content.push_str(&format!("{}={}\n", option, value));
        }
    }

    // Add generators
    conanfile_content.push_str("\n[generators]\n");
    if let Some(generators) = &conan_config.generators {
        for generator in generators {
            conanfile_content.push_str(&format!("{}\n", generator));
        }
    } else {
        // Default generators
        conanfile_content.push_str("cmake\n");
        conanfile_content.push_str("cmake_find_package\n");
    }

    // Write conanfile.txt
    let conanfile_path = conan_dir.join("conanfile.txt");
    fs::write(&conanfile_path, conanfile_content)?;

    // Run conan install with a dedicated progress bar
    let mut install_progress = SpinningWheel::start("Installing Conan packages");

    // Run conan install
    let mut cmd = vec![
        "conan".to_string(),
        "install".to_string(),
        conanfile_path.to_string_lossy().to_string(),
        "--build=missing".to_string(),
    ];

    // Track installation progress by parsing output
    let result = run_command_with_pattern_tracking(
        cmd,
        Some(&conan_dir.to_string_lossy().to_string()),
        None,
        install_progress.clone(),
        vec![
            ("Configuring".to_string(), 0.3),
            ("Downloading".to_string(), 0.4),
            ("Downloading".to_string(), 0.5),
            ("Generating".to_string(), 0.7),
            ("Building".to_string(), 0.8),
            ("Generator".to_string(), 0.9),
        ]
    );

    match result {
        Ok(_) => install_progress.success(),
        Err(e) => {
            install_progress.failure(&e.to_string());
            progress.clone().failure("Conan installation failed");
            return Err(e);
        }
    }

    // Return the path to conan's generated CMake file
    let cmake_file = conan_dir.join("conanbuildinfo.cmake");
    if !cmake_file.exists() {
        progress.clone().failure("Conan failed to generate CMake integration files");
        return Err("Conan failed to generate CMake integration files.".into());
    }

    Ok(cmake_file.to_string_lossy().to_string())
}

pub fn setup_git_dependencies_with_progress(
    config: &ProjectConfig,
    project_path: &Path,
    progress: &mut SpinningWheel
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    // Just call the main function - the internal progress tracking will handle it
    let result = setup_git_dependencies(config, project_path);

    result
}

// Setup custom dependencies with progress tracking
pub fn setup_custom_dependencies_with_progress(
    config: &ProjectConfig,
    project_path: &Path,
    progress: &mut SpinningWheel
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let custom_deps = &config.dependencies.custom;
    if custom_deps.is_empty() {
        return Ok(Vec::new());
    }

    // Create deps directory
    let deps_dir = project_path.join("deps");
    fs::create_dir_all(&deps_dir)?;

    let mut cmake_include_paths = Vec::new();

    // Calculate progress steps for each dependency
    let total_deps = custom_deps.len();

    // Process each custom dependency
    for (i, dep) in custom_deps.iter().enumerate() {
        let dep_path = deps_dir.join(&dep.name);

        if !is_quiet() {
            print_substep(&format!("Processing custom dependency: {}", dep.name));
        }

        if !dep_path.exists() {
            if !is_quiet() {
                print_substep(&format!("Downloading custom dependency: {}", dep.name));
            }

            // Create download directory
            fs::create_dir_all(&dep_path)?;

            // Download the dependency
            let url = &dep.url;
            let output_file = if url.ends_with(".zip") || url.ends_with(".tar.gz") || url.ends_with(".tgz") {
                format!("{}.{}", dep.name, url.split('.').last().unwrap_or("zip"))
            } else {
                format!("{}.zip", dep.name)
            };

            let output_path = deps_dir.join(&output_file);

            // Download with a dedicated progress bar
            let mut download_progress = SpinningWheel::start(&format!("Downloading {}", dep.name));

            let mut cmd = if cfg!(target_os = "windows") {
                vec![
                    "powershell".to_string(),
                    "-Command".to_string(),
                    format!("Invoke-WebRequest -Uri '{}' -OutFile '{}'", url, output_path.to_string_lossy()),
                ]
            } else {
                vec![
                    "curl".to_string(),
                    "-L".to_string(),
                    "--output".to_string(),
                    output_path.to_string_lossy().to_string(),
                    url.clone(),
                ]
            };

            match run_command(cmd, Some(&deps_dir.to_string_lossy().to_string()), None) {
                Ok(_) => download_progress.success(),
                Err(e) => {
                    download_progress.failure(&e.to_string());
                    progress.clone().failure(&format!("Failed to download {}: {}", dep.name, e));
                    return Err(format!("Failed to download custom dependency {}: {}", dep.name, e).into());
                }
            }

            // Extract archive with a dedicated progress bar
            let mut extract_progress = SpinningWheel::start(&format!("Extracting {}", dep.name));

            let mut cmd = if output_path.to_string_lossy().ends_with(".zip") {
                if cfg!(target_os = "windows") {
                    vec![
                        "powershell".to_string(),
                        "-Command".to_string(),
                        format!("Expand-Archive -Path '{}' -DestinationPath '{}'",
                                output_path.to_string_lossy(), dep_path.to_string_lossy()),
                    ]
                } else {
                    vec![
                        "unzip".to_string(),
                        output_path.to_string_lossy().to_string(),
                        "-d".to_string(),
                        dep_path.to_string_lossy().to_string(),
                    ]
                }
            } else if output_path.to_string_lossy().ends_with(".tar.gz") || output_path.to_string_lossy().ends_with(".tgz") {
                if cfg!(target_os = "windows") {
                    vec![
                        "tar".to_string(),
                        "-xzf".to_string(),
                        output_path.to_string_lossy().to_string(),
                        "-C".to_string(),
                        dep_path.to_string_lossy().to_string(),
                    ]
                } else {
                    vec![
                        "tar".to_string(),
                        "-xzf".to_string(),
                        output_path.to_string_lossy().to_string(),
                        "-C".to_string(),
                        dep_path.to_string_lossy().to_string(),
                    ]
                }
            } else {
                extract_progress.failure("Unsupported archive format");
                progress.clone().failure(&format!("Unsupported archive format for dependency: {}", dep.name));
                return Err(format!("Unsupported archive format for dependency: {}", dep.name).into());
            };

            match run_command(cmd, Some(&deps_dir.to_string_lossy().to_string()), None) {
                Ok(_) => extract_progress.success(),
                Err(e) => {
                    extract_progress.failure(&e.to_string());
                    progress.clone().failure(&format!("Failed to extract {}: {}", dep.name, e));
                    return Err(format!("Failed to extract custom dependency {}: {}", dep.name, e).into());
                }
            }

            // Build if a build command is provided
            if let Some(build_cmd) = &dep.build_command {
                let mut build_progress = SpinningWheel::start(&format!("Building {}", dep.name));

                let shell = if cfg!(target_os = "windows") { "cmd" } else { "sh" };
                let shell_arg = if cfg!(target_os = "windows") { "/C" } else { "-c" };

                let mut cmd = vec![
                    shell.to_string(),
                    shell_arg.to_string(),
                    build_cmd.clone(),
                ];

                match run_command(cmd, Some(&dep_path.to_string_lossy().to_string()), None) {
                    Ok(_) => build_progress.success(),
                    Err(e) => {
                        build_progress.failure(&e.to_string());
                        print_warning(&format!("Failed to build {}: {}", dep.name, e), Some("Will try to continue"));
                        // Continue anyway, build might not be essential
                    }
                }

            }

            // Install if an install command is provided
            if let Some(install_cmd) = &dep.install_command {
                let mut install_progress = SpinningWheel::start(&format!("Installing {}", dep.name));

                let shell = if cfg!(target_os = "windows") { "cmd" } else { "sh" };
                let shell_arg = if cfg!(target_os = "windows") { "/C" } else { "-c" };

                let mut cmd = vec![
                    shell.to_string(),
                    shell_arg.to_string(),
                    install_cmd.clone(),
                ];

                match run_command(cmd, Some(&dep_path.to_string_lossy().to_string()), None) {
                    Ok(_) => install_progress.success(),
                    Err(e) => {
                        install_progress.failure(&e.to_string());
                        print_warning(&format!("Failed to install {}: {}", dep.name, e), Some("Will try to continue"));
                        // Continue anyway, install might not be essential
                    }
                }
            }
        } else if !is_quiet() {
            print_substep(&format!("Custom dependency already exists: {}", dep.name));
        }

        // Add include and library paths to CMake include paths
        if let Some(include_path) = &dep.include_path {
            let full_include_path = dep_path.join(include_path);
            cmake_include_paths.push(format!("-DCMAKE_INCLUDE_PATH={}", full_include_path.to_string_lossy()));
        }

        if let Some(library_path) = &dep.library_path {
            let full_library_path = dep_path.join(library_path);
            cmake_include_paths.push(format!("-DCMAKE_LIBRARY_PATH={}", full_library_path.to_string_lossy()));
        }

        // Update progress after processing this dependency
    }

    Ok(cmake_include_paths)
}

pub fn find_vcpkg_executable(search_path: &str) -> Option<PathBuf> {
    let vcpkg_exe = if cfg!(windows) {
        PathBuf::from(search_path).join("vcpkg.exe")
    } else {
        PathBuf::from(search_path).join("vcpkg")
    };

    if vcpkg_exe.exists() {
        Some(vcpkg_exe)
    } else {
        None
    }
}

pub fn run_vcpkg_command(cmd: Vec<String>, cwd: &str) -> Result<String, Box<dyn std::error::Error>> {
    let mut command = Command::new(&cmd[0]);
    command.args(&cmd[1..]);
    command.current_dir(cwd);
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    let output = command.output()?;
    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let stderr = String::from_utf8_lossy(&output.stderr).to_string();

    if !output.status.success() {
        return Err(format!("vcpkg command failed: {}", stderr).into());
    }

    Ok(stdout)
}

pub fn find_library_files(base_path: &Path, project_name: &str, is_shared: bool, is_msvc_style: bool) -> Vec<(PathBuf, String)> {
    let mut result = Vec::new();
    let possible_filenames = get_possible_library_filenames(project_name, is_shared, is_msvc_style);

    // First look directly in the specified path
    for filename in &possible_filenames {
        let lib_path = base_path.join(filename);
        if lib_path.exists() {
            result.push((lib_path, filename.clone()));
        }
    }

    // If nothing found, try common subdirectories
    if result.is_empty() {
        for subdir in &["lib", "libs", "bin", "build/lib", "build/bin"] {
            let subdir_path = base_path.join(subdir);
            if subdir_path.exists() {
                for filename in &possible_filenames {
                    let lib_path = subdir_path.join(filename);
                    if lib_path.exists() {
                        result.push((lib_path, filename.clone()));
                    }
                }
            }
        }

        // Also search for configuration-specific directories
        for config in &["Debug", "Release", "RelWithDebInfo", "MinSizeRel"] {
            let config_path = base_path.join(config);
            if config_path.exists() {
                for filename in &possible_filenames {
                    let lib_path = config_path.join(filename);
                    if lib_path.exists() {
                        result.push((lib_path, filename.clone()));
                    }
                }
            }
        }
    }

    // If still empty, do a more exhaustive search (breadth-first to avoid going too deep)
    if result.is_empty() {
        let mut queue = VecDeque::new();
        queue.push_back(base_path.to_path_buf());
        let mut visited = HashSet::new();

        while let Some(dir) = queue.pop_front() {
            if visited.contains(&dir) {
                continue;
            }
            visited.insert(dir.clone());

            // Don't go more than 3 levels deep to avoid excessive searching
            let current_depth = dir.strip_prefix(base_path).map_or(0, |p| p.components().count());
            if current_depth > 3 {
                continue;
            }

            // Check for library files in this directory
            for filename in &possible_filenames {
                let lib_path = dir.join(filename);
                if lib_path.exists() {
                    result.push((lib_path, filename.clone()));
                }
            }

            // Add subdirectories to the queue
            if let Ok(entries) = fs::read_dir(&dir) {
                for entry in entries.filter_map(Result::ok) {
                    let path = entry.path();
                    if path.is_dir() {
                        queue.push_back(path);
                    }
                }
            }
        }
    }

    result
}

pub fn get_possible_library_filenames(project_name: &str, is_shared: bool, is_msvc_style: bool) -> Vec<String> {
    let mut filenames = Vec::new();

    if is_msvc_style {
        // MSVC-style libraries
        if is_shared {
            // Import libraries
            filenames.push(format!("{}.lib", project_name));
            filenames.push(format!("lib{}.lib", project_name));

            // DLLs
            filenames.push(format!("{}.dll", project_name));
            filenames.push(format!("lib{}.dll", project_name));
        } else {
            // Static libraries
            filenames.push(format!("{}.lib", project_name));
            filenames.push(format!("lib{}.lib", project_name));
        }
    } else {
        // GCC/Clang-style libraries
        if is_shared {
            // Import libraries
            filenames.push(format!("lib{}.dll.a", project_name));
            filenames.push(format!("{}.dll.a", project_name));

            // Shared objects
            filenames.push(format!("lib{}.so", project_name));
            filenames.push(format!("lib{}.dylib", project_name));

            // DLLs
            filenames.push(format!("lib{}.dll", project_name));
            filenames.push(format!("{}.dll", project_name));
        } else {
            // Static libraries
            filenames.push(format!("lib{}.a", project_name));
            filenames.push(format!("{}.a", project_name));
        }
    }

    filenames
}

pub fn get_cached_vcpkg_toolchain_path() -> String {
    let cache = CACHED_PATHS.lock().unwrap();
    cache.get("vcpkg_toolchain")
        .cloned()
        .unwrap_or_default()
}

pub fn cache_vcpkg_toolchain_path(path: &Path) {
    let path_str = path.to_string_lossy().to_string();
    let mut cache = CACHED_PATHS.lock().unwrap();
    cache.insert("vcpkg_toolchain".to_string(), path_str);
}

pub fn run_vcpkg_install_with_progress(
    vcpkg_exe: &Path,
    vcpkg_path: &str,
    packages: &[String],
    mut progress: SpinningWheel
) -> Result<(), Box<dyn std::error::Error>> {
    // First check which packages are already installed - update progress to 10%

    let mut already_installed = Vec::new();
    let mut to_install = Vec::new();

    // Calculate total packages and set initial progress
    let total_packages = packages.len();
    let mut checked_count = 0;

    for pkg in packages {
        // Update progress as we check each package
        checked_count += 1;

        if check_vcpkg_package_installed(vcpkg_path, pkg) {
            already_installed.push(pkg.clone());
        } else {
            to_install.push(pkg.clone());
        }
    }

    // If all packages are already installed, just show that - complete progress
    if to_install.is_empty() {
        if !is_quiet() {
            print_step("Packages already installed:", "  ");
            for pkg in &already_installed {
                print_substep(&format!("{}", pkg));
            }
        }

        // Complete progress since all packages are already installed
        progress.success();
        return Ok(());
    }

    // Otherwise, run the install command for new packages
    let mut cmd = vec![
        vcpkg_exe.to_string_lossy().to_string(),
        "install".to_string(),
    ];

    // Add all packages to install at once
    for pkg in &to_install {
        cmd.push(pkg.clone());
    }

    // Update progress to show we're starting installation (20%)

    if !is_quiet() {
        print_substep(&format!("Installing {} packages", to_install.len()));
    }

    // Create shared state to track installation progress
    let package_state = Arc::new(Mutex::new(PackageInstallState {
        current_package: String::new(),
        current_percentage: 0.0,
        packages_completed: 0,
        total_packages: to_install.len(),
    }));

    // Use improved command execution with better progress tracking
    let result = run_command_with_pattern_tracking(
        cmd.clone(),
        Some(vcpkg_path),
        None,
        progress.clone(),
        vec![
            ("Downloading".to_string(), 0.3),
            ("Extracting".to_string(), 0.4),
            ("Configuring".to_string(), 0.5),
            ("Building".to_string(), 0.7),
            ("Installing".to_string(), 0.9),
        ]
    );

    // Handle result
    match result {
        Ok(_) => {
            // Show results
            if !already_installed.is_empty() && !is_quiet() {
                print_step("Packages already installed:", "  ");
                for pkg in already_installed {
                    print_substep(&format!("{}", pkg)); // Changed from print_detailed
                }
            }

            if !to_install.is_empty() && !is_quiet() {
                print_step("Newly installed packages:", "  ");
                for pkg in &to_install {
                    print_substep(&format!("{}", pkg)); // Changed from print_detailed
                }
            }

            // Final progress update
            progress.success();
            println!();
            Ok(())
        },
        Err(e) => {
            // If batch installation failed, try individually - update to 30%
            print_warning(&format!("Error running vcpkg: {}", e), None);
            print_warning("Batch installation failed, trying one package at a time", None);

            let mut success_count = 0;
            let mut failed_pkgs = Vec::new();

            // Try each package individually
            for (i, pkg) in to_install.iter().enumerate() {
                let mut package_progress_bar = SpinningWheel::start(&format!("Installing package {}", pkg));

                let single_cmd = vec![
                    vcpkg_exe.to_string_lossy().to_string(),
                    "install".to_string(),
                    pkg.clone(),
                ];

                match run_command_with_timeout(single_cmd, Some(vcpkg_path), None, 300, None) { // 5 minute timeout per package
                    Ok(_) => {
                        package_progress_bar.success();
                        success_count += 1;
                    },
                    Err(e) => {
                        package_progress_bar.failure(&e.to_string());
                        failed_pkgs.push(pkg.clone());
                    }
                }
            }

            // Determine overall success based on how many packages succeeded
            if success_count > 0 {
                print_substep(&format!("Successfully installed {} out of {} packages",
                                       success_count, to_install.len()));

                if !failed_pkgs.is_empty() {
                    print_warning(&format!("Failed to install packages: {}", failed_pkgs.join(", ")),
                                  Some("You may need to install these manually"));
                }

                // Update progress to show partial success
                progress.clone().success();

                // If we got at least some packages, consider it a partial success
                if success_count > failed_pkgs.len() {
                    return Ok(());
                }
            }

            progress.failure(&e.to_string());
            Err(e.into())
        }
    }
}
