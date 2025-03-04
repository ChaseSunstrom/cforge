use clap::{Parser, Subcommand};
use colored::*;
use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    env,
    fs::{self, File},
    io::Write,
    path::{Path, PathBuf},
    process::{Command, Stdio},
};

// Constants
const CBUILD_FILE: &str = "cbuild.toml";
const WORKSPACE_FILE: &str = "cbuild-workspace.toml";
const DEFAULT_BUILD_DIR: &str = "build";
const DEFAULT_BIN_DIR: &str = "bin";
const DEFAULT_LIB_DIR: &str = "lib";
const DEFAULT_OBJ_DIR: &str = "obj";
const VCPKG_DEFAULT_DIR: &str = "~/.vcpkg";
const CMAKE_MIN_VERSION: &str = "3.15";

// CLI Commands
#[derive(Debug, Parser)]
#[clap(
    name = "cbuild",
    about = "A TOML-based build system for C/C++ with CMake and vcpkg integration"
)]
struct Cli {
    #[clap(subcommand)]
    command: Commands,
}

#[derive(Debug, Subcommand)]
enum Commands {
    /// Initialize a new project or workspace
    #[clap(name = "init")]
    Init {
        /// Create a workspace instead of a single project
        #[clap(long)]
        workspace: bool,
    },

    /// Build the project or workspace
    #[clap(name = "build")]
    Build {
        /// Build specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },

    /// Clean build artifacts
    #[clap(name = "clean")]
    Clean {
        /// Clean specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },

    /// Run the built executable
    #[clap(name = "run")]
    Run {
        /// Run a specific project in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },

    /// Run tests
    #[clap(name = "test")]
    Test {
        /// Test specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },

    /// Install the built project
    #[clap(name = "install")]
    Install {
        /// Install specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },

    /// Install dependencies
    #[clap(name = "deps")]
    Deps {
        /// Install dependencies for specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },
}

// Configuration Models
#[derive(Debug, Serialize, Deserialize, Clone)]
struct WorkspaceConfig {
    workspace: WorkspaceInfo,
    projects: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct WorkspaceInfo {
    name: String,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct ProjectConfig {
    project: ProjectInfo,
    build: BuildConfig,
    dependencies: DependenciesConfig,
    targets: HashMap<String, TargetConfig>,
    platforms: Option<HashMap<String, PlatformConfig>>,
    #[serde(default)]
    output: OutputConfig,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct ProjectInfo {
    name: String,
    version: String,
    description: String,
    #[serde(rename = "type")]
    project_type: String, // executable, library, static-library, header-only
    language: String,     // c, c++
    standard: String,     // c11, c++17, etc.
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct BuildConfig {
    build_dir: Option<String>,
    generator: Option<String>,
    debug: Option<bool>,
    cmake_options: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct DependenciesConfig {
    vcpkg: VcpkgConfig,
    system: Option<Vec<String>>,
    cmake: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct VcpkgConfig {
    enabled: bool,
    path: Option<String>,
    packages: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct TargetConfig {
    sources: Vec<String>,
    include_dirs: Option<Vec<String>>,
    defines: Option<Vec<String>>,
    links: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct PlatformConfig {
    compiler: Option<String>,
    defines: Option<Vec<String>>,
    flags: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct OutputConfig {
    bin_dir: Option<String>,
    lib_dir: Option<String>,
    obj_dir: Option<String>,
}

// System Detection
struct SystemInfo {
    os: String,
    arch: String,
    compiler: String,
}

// Main implementation
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    match cli.command {
        Commands::Init { workspace } => {
            if workspace {
                init_workspace()?;
            } else {
                init_project(None)?;
            }
        }
        Commands::Build { project } => {
            if is_workspace() {
                build_workspace(project)?;
            } else {
                let config = load_project_config(None)?;
                build_project(&config, &PathBuf::from("."))?;
            }
        }
        Commands::Clean { project } => {
            if is_workspace() {
                clean_workspace(project)?;
            } else {
                let config = load_project_config(None)?;
                clean_project(&config, &PathBuf::from("."))?;
            }
        }
        Commands::Run { project } => {
            if is_workspace() {
                run_workspace(project)?;
            } else {
                let config = load_project_config(None)?;
                run_project(&config, &PathBuf::from("."))?;
            }
        }
        Commands::Test { project } => {
            if is_workspace() {
                test_workspace(project)?;
            } else {
                let config = load_project_config(None)?;
                test_project(&config, &PathBuf::from("."))?;
            }
        }
        Commands::Install { project } => {
            if is_workspace() {
                install_workspace(project)?;
            } else {
                let config = load_project_config(None)?;
                install_project(&config, &PathBuf::from("."))?;
            }
        }
        Commands::Deps { project } => {
            if is_workspace() {
                install_workspace_deps(project)?;
            } else {
                let config = load_project_config(None)?;
                install_dependencies(&config, &PathBuf::from("."))?;
            }
        }
    }

    Ok(())
}

// Check if current directory is a workspace
fn is_workspace() -> bool {
    Path::new(WORKSPACE_FILE).exists()
}

// Workspace functions
fn init_workspace() -> Result<(), Box<dyn std::error::Error>> {
    if Path::new(WORKSPACE_FILE).exists() {
        let response = prompt("Workspace already exists. Overwrite? (y/N): ")?;
        if response.trim().to_lowercase() != "y" {
            println!("{}", "Initialization cancelled".blue());
            return Ok(());
        }
    }

    let workspace_name = prompt("Workspace name: ")?;

    let workspace_config = WorkspaceConfig {
        workspace: WorkspaceInfo {
            name: workspace_name.trim().to_string(),
        },
        projects: Vec::new(),
    };

    save_workspace_config(&workspace_config)?;

    // Create basic workspace structure
    fs::create_dir_all("projects")?;

    println!("{}", "Workspace initialized successfully".green());
    println!("To add projects, run: {} in the projects directory", "cbuild init".cyan());

    Ok(())
}

fn save_workspace_config(config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    let toml_string = toml::to_string_pretty(config)?;
    let mut file = File::create(WORKSPACE_FILE)?;
    file.write_all(toml_string.as_bytes())?;
    println!("{}", format!("Configuration saved to {}", WORKSPACE_FILE).green());
    Ok(())
}

fn load_workspace_config() -> Result<WorkspaceConfig, Box<dyn std::error::Error>> {
    if !Path::new(WORKSPACE_FILE).exists() {
        return Err(format!("Workspace file '{}' not found", WORKSPACE_FILE).into());
    }

    let toml_str = fs::read_to_string(WORKSPACE_FILE)?;
    let config: WorkspaceConfig = toml::from_str(&toml_str)?;
    Ok(config)
}

fn build_workspace(project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
    };

    for project_path in projects {
        println!("{}", format!("Building project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        build_project(&config, &path)?;
    }

    println!("{}", "Workspace build completed".green());
    Ok(())
}

fn clean_workspace(project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
    };

    for project_path in projects {
        println!("{}", format!("Cleaning project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        clean_project(&config, &path)?;
    }

    println!("{}", "Workspace clean completed".green());
    Ok(())
}

fn run_workspace(project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let project_path = match project {
        Some(proj) => proj,
        None => {
            if workspace_config.projects.is_empty() {
                return Err("No projects found in workspace".into());
            }
            // Default to first project if none specified
            workspace_config.projects[0].clone()
        }
    };

    println!("{}", format!("Running project: {}", project_path).blue());
    let path = PathBuf::from(&project_path);
    let config = load_project_config(Some(&path))?;
    run_project(&config, &path)?;

    Ok(())
}

fn test_workspace(project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
    };

    for project_path in projects {
        println!("{}", format!("Testing project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        test_project(&config, &path)?;
    }

    println!("{}", "Workspace tests completed".green());
    Ok(())
}

fn install_workspace(project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
    };

    for project_path in projects {
        println!("{}", format!("Installing project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        install_project(&config, &path)?;
    }

    println!("{}", "Workspace installation completed".green());
    Ok(())
}

fn install_workspace_deps(project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
    };

    for project_path in projects {
        println!("{}", format!("Installing dependencies for project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        install_dependencies(&config, &path)?;
    }

    println!("{}", "Workspace dependencies installed".green());
    Ok(())
}

// Project functions
fn init_project(path: Option<&Path>) -> Result<(), Box<dyn std::error::Error>> {
    let project_path = path.unwrap_or_else(|| Path::new("."));

    let config_path = project_path.join(CBUILD_FILE);
    if config_path.exists() {
        let response = prompt("Project already exists. Overwrite? (y/N): ")?;
        if response.trim().to_lowercase() != "y" {
            println!("{}", "Initialization cancelled".blue());
            return Ok(());
        }
    }

    let config = create_default_config();
    save_project_config(&config, project_path)?;

    // Create basic directory structure
    fs::create_dir_all(project_path.join("src"))?;
    fs::create_dir_all(project_path.join("include"))?;

    // Create a simple main.cpp file for executable projects
    if config.project.project_type == "executable" {
        let main_file = project_path.join("src").join("main.cpp");
        let mut file = File::create(main_file)?;
        file.write_all(b"#include <iostream>\n\nint main(int argc, char* argv[]) {\n    std::cout << \"Hello, CBuild!\" << std::endl;\n    return 0;\n}\n")?;
    }

    println!("{}", "Project initialized successfully".green());

    // If this is a workspace project, add it to the workspace
    let workspace_file = Path::new(WORKSPACE_FILE);
    if workspace_file.exists() && path.is_some() {
        let mut workspace_config = load_workspace_config()?;
        let project_rel_path = path.unwrap().to_string_lossy().to_string();

        if !workspace_config.projects.contains(&project_rel_path) {
            workspace_config.projects.push(project_rel_path);
            save_workspace_config(&workspace_config)?;
        }
    }

    Ok(())
}

fn create_default_config() -> ProjectConfig {
    let system_info = detect_system_info();

    // Create a default platform config
    let mut platforms = HashMap::new();
    platforms.insert(system_info.os.clone(), PlatformConfig {
        compiler: Some(system_info.compiler.clone()),
        defines: Some(vec![]),
        flags: Some(vec![]),
    });

    // Create a default target config
    let mut targets = HashMap::new();
    targets.insert("default".to_string(), TargetConfig {
        sources: vec!["src/**/*.cpp".to_string(), "src/**/*.c".to_string()],
        include_dirs: Some(vec!["include".to_string()]),
        defines: Some(vec![]),
        links: Some(vec![]),
    });

    ProjectConfig {
        project: ProjectInfo {
            name: env::current_dir()
                .unwrap_or_else(|_| PathBuf::from("my_project"))
                .file_name()
                .unwrap_or_else(|| std::ffi::OsStr::new("my_project"))
                .to_string_lossy()
                .to_string(),
            version: "0.1.0".to_string(),
            description: "A C/C++ project built with CBuild".to_string(),
            project_type: "executable".to_string(),
            language: "c++".to_string(),
            standard: "c++17".to_string(),
        },
        build: BuildConfig {
            build_dir: Some(DEFAULT_BUILD_DIR.to_string()),
            generator: Some("default".to_string()),
            debug: Some(true),
            cmake_options: Some(vec![]),
        },
        dependencies: DependenciesConfig {
            vcpkg: VcpkgConfig {
                enabled: true,
                path: Some(VCPKG_DEFAULT_DIR.to_string()),
                packages: vec![],
            },
            system: Some(vec![]),
            cmake: Some(vec![]),
        },
        targets,
        platforms: Some(platforms),
        output: OutputConfig {
            bin_dir: Some(DEFAULT_BIN_DIR.to_string()),
            lib_dir: Some(DEFAULT_LIB_DIR.to_string()),
            obj_dir: Some(DEFAULT_OBJ_DIR.to_string()),
        },
    }
}

fn save_project_config(config: &ProjectConfig, path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let config_path = path.join(CBUILD_FILE);
    let toml_string = toml::to_string_pretty(config)?;
    let mut file = File::create(config_path)?;
    file.write_all(toml_string.as_bytes())?;
    println!("{}", format!("Configuration saved to {}", path.join(CBUILD_FILE).display()).green());
    Ok(())
}

fn load_project_config(path: Option<&Path>) -> Result<ProjectConfig, Box<dyn std::error::Error>> {
    let project_path = path.unwrap_or_else(|| Path::new("."));
    let config_path = project_path.join(CBUILD_FILE);

    if !config_path.exists() {
        return Err(format!("Configuration file '{}' not found. Run 'cbuild init' to create one.", config_path.display()).into());
    }

    let toml_str = fs::read_to_string(config_path)?;
    let config: ProjectConfig = toml::from_str(&toml_str)?;
    Ok(config)
}

fn detect_system_info() -> SystemInfo {
    let os = if cfg!(target_os = "windows") {
        "windows".to_string()
    } else if cfg!(target_os = "macos") {
        "darwin".to_string()
    } else {
        "linux".to_string()
    };

    let arch = if cfg!(target_arch = "x86_64") {
        "x86_64".to_string()
    } else if cfg!(target_arch = "x86") {
        "x86".to_string()
    } else if cfg!(target_arch = "aarch64") {
        "aarch64".to_string()
    } else {
        "unknown".to_string()
    };

    // Detect compiler based on platform
    let compiler = if cfg!(target_os = "windows") {
        // Try to find MSVC
        if Command::new("cl").arg("/?").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            "msvc".to_string()
        } else if Command::new("gcc").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            "gcc".to_string()
        } else {
            "default".to_string()
        }
    } else {
        // Unix-like OS - check for clang first, then gcc
        if Command::new("clang").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            "clang".to_string()
        } else if Command::new("gcc").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            "gcc".to_string()
        } else {
            "default".to_string()
        }
    };

    SystemInfo { os, arch, compiler }
}

fn setup_vcpkg(config: &ProjectConfig, project_path: &Path) -> Result<String, Box<dyn std::error::Error>> {
    let vcpkg_config = &config.dependencies.vcpkg;
    if !vcpkg_config.enabled {
        return Ok(String::new());
    }

    // First, try the configured path
    let configured_path = vcpkg_config.path.as_deref().unwrap_or(VCPKG_DEFAULT_DIR);
    let configured_path = expand_tilde(configured_path);

    // Try to find vcpkg in common locations
    let mut potential_vcpkg_paths = vec![
        configured_path.clone(),
        // Common installation locations
        String::from("C:/vcpkg"),
        String::from("C:/dev/vcpkg"),
        String::from("C:/tools/vcpkg"),
        String::from("/usr/local/vcpkg"),
        String::from("/opt/vcpkg"),
        expand_tilde("~/vcpkg"),
    ];

    // Also check PATH environment variable
    if let Ok(path_var) = env::var("PATH") {
        for path in path_var.split(if cfg!(windows) { ';' } else { ':' }) {
            let path_buf = PathBuf::from(path);
            let vcpkg_in_path = if cfg!(windows) {
                path_buf.join("vcpkg.exe")
            } else {
                path_buf.join("vcpkg")
            };

            if vcpkg_in_path.exists() {
                // Add the parent directory (vcpkg root)
                if let Some(parent) = vcpkg_in_path.parent() {
                    potential_vcpkg_paths.push(parent.to_string_lossy().to_string());
                }
            }
        }
    }

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
        println!("{}", format!("Found existing vcpkg installation at {}", path).green());
        path
    } else {
        println!("{}", format!("vcpkg not found, attempting to set up at {}", configured_path).blue());

        // Create parent directory if it doesn't exist
        let vcpkg_parent_dir = Path::new(&configured_path).parent().unwrap_or_else(|| Path::new(&configured_path));
        fs::create_dir_all(vcpkg_parent_dir)?;

        // Check if the directory exists but is not a proper vcpkg installation
        let vcpkg_dir = Path::new(&configured_path);
        if vcpkg_dir.exists() {
            println!("{}", format!("Directory {} exists but does not contain a valid vcpkg installation. Attempting to bootstrap...", configured_path).yellow());
        } else {
            // Try to clone vcpkg repository
            match run_command(
                vec![
                    String::from("git"),
                    String::from("clone"),
                    String::from("https://github.com/microsoft/vcpkg.git"),
                    String::from(&configured_path)
                ],
                None,
                None,
            ) {
                Ok(_) => {},
                Err(e) => {
                    // If clone failed because directory exists, continue anyway
                    if vcpkg_dir.exists() {
                        println!("{}", format!("Git clone failed but directory exists: {}", e).yellow());
                    } else {
                        return Err(e);
                    }
                }
            }
        }

        // Try to bootstrap vcpkg
        let bootstrap_script = if cfg!(windows) {
            "bootstrap-vcpkg.bat"
        } else {
            "./bootstrap-vcpkg.sh"
        };

        match run_command(
            vec![String::from(bootstrap_script)],
            Some(&configured_path),
            None,
        ) {
            Ok(_) => {},
            Err(e) => {
                println!("{}", format!("Bootstrapping vcpkg failed: {}", e).yellow());
                println!("{}", "Will try to use vcpkg anyway if it exists.".yellow());
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
        return Err(format!("vcpkg executable not found at {}. Please install vcpkg manually.", vcpkg_exe.display()).into());
    }

    // Install configured packages
    if !vcpkg_config.packages.is_empty() {
        println!("{}", "Installing vcpkg packages...".blue());
        let mut cmd = vec![vcpkg_exe.to_string_lossy().to_string(), "install".to_string()];
        cmd.extend(vcpkg_config.packages.clone());

        run_command(cmd, Some(&vcpkg_path), None)?;
    }

    // Return the path to vcpkg.cmake for CMake integration
    let toolchain_file = PathBuf::from(&vcpkg_path)
        .join("scripts")
        .join("buildsystems")
        .join("vcpkg.cmake");

    if !toolchain_file.exists() {
        return Err(format!("vcpkg toolchain file not found at {}. This suggests a corrupt vcpkg installation.", toolchain_file.display()).into());
    }

    Ok(toolchain_file.to_string_lossy().to_string())
}
fn generate_cmake_lists(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let project_config = &config.project;
    let targets_config = &config.targets;
    let cmake_minimum = CMAKE_MIN_VERSION;

    let mut cmake_content = vec![
        format!("cmake_minimum_required(VERSION {})", cmake_minimum),
        format!("project({} VERSION {})", project_config.name, project_config.version),
        String::new(),
        "# Generated by CBuild - Do not edit manually".to_string(),
        String::new(),
    ];

    // Set output directories based on config
    let output_config = &config.output;
    if let Some(bin_dir) = &output_config.bin_dir {
        cmake_content.push(format!("set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/{})", bin_dir));
    }
    if let Some(lib_dir) = &output_config.lib_dir {
        cmake_content.push(format!("set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/{})", lib_dir));
        cmake_content.push(format!("set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${{CMAKE_BINARY_DIR}}/{})", lib_dir));
    }

    // Set C/C++ standard
    let language = &project_config.language;
    if language == "c++" {
        let standard = project_config.standard.replace("c++", "");
        cmake_content.push(format!("set(CMAKE_CXX_STANDARD {})", standard));
        cmake_content.push("set(CMAKE_CXX_STANDARD_REQUIRED ON)".to_string());
        cmake_content.push("set(CMAKE_CXX_EXTENSIONS OFF)".to_string());
    } else if language == "c" {
        let standard = project_config.standard.replace("c", "");
        cmake_content.push(format!("set(CMAKE_C_STANDARD {})", standard));
        cmake_content.push("set(CMAKE_C_STANDARD_REQUIRED ON)".to_string());
        cmake_content.push("set(CMAKE_C_EXTENSIONS OFF)".to_string());
    }

    // Add dependencies
    if let Some(cmake_deps) = &config.dependencies.cmake {
        for dep in cmake_deps {
            cmake_content.push(format!("find_package({} REQUIRED)", dep));
        }
    }

    // Add targets
    for (target_name, target_config) in targets_config {
        let target_type = &project_config.project_type;
        let sources = &target_config.sources;
        let empty_vec = Vec::new();
        let include_dirs = target_config.include_dirs.as_ref().unwrap_or(&empty_vec);
        let defines = target_config.defines.as_ref().unwrap_or(&empty_vec);
        let links = target_config.links.as_ref().unwrap_or(&empty_vec);

        // Check if we have any explicit source files (not globs)
        let mut has_explicit_sources = false;
        let mut explicit_sources = Vec::new();

        for source in sources {
            if !source.contains("*") {
                has_explicit_sources = true;
                explicit_sources.push(source.clone());
            }
        }

        // Convert glob patterns to file_glob commands
        let mut source_vars = Vec::new();
        for (idx, pattern) in sources.iter().enumerate() {
            // Only use glob for patterns containing wildcards
            if pattern.contains("*") {
                let var_name = format!("{}_SOURCES_{}", target_name.to_uppercase(), idx);
                cmake_content.push(format!("file(GLOB_RECURSE {} {})", var_name, pattern));
                source_vars.push(format!("${{{}}}", var_name));
            }
        }

        // If we don't have any explicit sources or globs, add a check to create a default source file
        if !has_explicit_sources && source_vars.is_empty() {
            // Create a default main.cpp file to avoid CMake errors
            let main_file = project_path.join("src").join("main.cpp");
            if !main_file.exists() {
                fs::create_dir_all(project_path.join("src"))?;
                let mut file = File::create(&main_file)?;
                file.write_all(b"#include <iostream>\n\nint main(int argc, char* argv[]) {\n    std::cout << \"Hello, CBuild!\" << std::endl;\n    return 0;\n}\n")?;
                println!("{}", format!("Created default source file at {}", main_file.display()).green());
            }
            explicit_sources.push("src/main.cpp".to_string());
            has_explicit_sources = true;
        }

        // Combine explicit sources and globbed sources
        let mut all_sources = Vec::new();
        all_sources.extend(explicit_sources);
        all_sources.extend(source_vars);

        let sources_joined = all_sources.join(" ");

        // Create target
        if target_type == "executable" {
            cmake_content.push(format!("add_executable({} {})", target_name, sources_joined));
        } else if target_type == "library" {
            cmake_content.push(format!("add_library({} SHARED {})", target_name, sources_joined));
        } else if target_type == "static-library" {
            cmake_content.push(format!("add_library({} STATIC {})", target_name, sources_joined));
        } else if target_type == "header-only" {
            cmake_content.push(format!("add_library({} INTERFACE)", target_name));
        }

        // Include directories
        if !include_dirs.is_empty() {
            if target_type == "header-only" {
                cmake_content.push(format!("target_include_directories({} INTERFACE {})", target_name, include_dirs.join(" ")));
            } else {
                cmake_content.push(format!("target_include_directories({} PRIVATE {})", target_name, include_dirs.join(" ")));
            }
        }

        // Add defines
        if !defines.is_empty() {
            let defines_str = defines.iter().map(|d| format!("\"{}\"", d)).collect::<Vec<_>>().join(" ");
            if target_type == "header-only" {
                cmake_content.push(format!("target_compile_definitions({} INTERFACE {})", target_name, defines_str));
            } else {
                cmake_content.push(format!("target_compile_definitions({} PRIVATE {})", target_name, defines_str));
            }
        }

        // Link libraries
        if !links.is_empty() {
            if target_type == "header-only" {
                cmake_content.push(format!("target_link_libraries({} INTERFACE {})", target_name, links.join(" ")));
            } else {
                cmake_content.push(format!("target_link_libraries({} PRIVATE {})", target_name, links.join(" ")));
            }
        }

        // Installation rules
        if target_type != "header-only" {
            if target_type == "executable" {
                cmake_content.push(format!("install(TARGETS {} DESTINATION bin)", target_name));
            } else {
                cmake_content.push(format!("install(TARGETS {} DESTINATION lib)", target_name));
                // Install header files for libraries
                for include_dir in include_dirs {
                    cmake_content.push(format!("install(DIRECTORY {}/ DESTINATION include)", include_dir));
                }
            }
        }
    }

    // Write the CMakeLists.txt file
    let cmake_file = project_path.join("CMakeLists.txt");
    let mut file = File::create(cmake_file)?;
    file.write_all(cmake_content.join("\n").as_bytes())?;

    println!("{}", "Generated CMakeLists.txt".green());
    Ok(())
}

fn get_cmake_generator(config: &ProjectConfig) -> Result<String, Box<dyn std::error::Error>> {
    let generator = config.build.generator.as_deref().unwrap_or("default");
    if generator != "default" {
        return Ok(generator.to_string());
    }

    if cfg!(target_os = "windows") {
        // On Windows, prefer Visual Studio if available
        if Command::new("cl").arg("/?").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            // Try to determine VS version
            if Command::new("msbuild").arg("/?").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
                // Use vswhere to find VS version if available
                if let Ok(output) = Command::new("powershell")
                    .arg("-Command")
                    .arg("(Get-ItemProperty -Path 'HKLM:\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7' -Name '17.0' -ErrorAction SilentlyContinue).'17.0'")
                    .output()
                {
                    if !output.stdout.is_empty() {
                        return Ok("Visual Studio 17 2022".to_string());
                    }
                }

                if let Ok(output) = Command::new("powershell")
                    .arg("-Command")
                    .arg("(Get-ItemProperty -Path 'HKLM:\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7' -Name '16.0' -ErrorAction SilentlyContinue).'16.0'")
                    .output()
                {
                    if !output.stdout.is_empty() {
                        return Ok("Visual Studio 16 2019".to_string());
                    }
                }

                return Ok("Visual Studio 15 2017".to_string());
            }
        }

        // Fallback to Ninja if available
        if Command::new("ninja").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            return Ok("Ninja".to_string());
        }

        // Default to NMake
        return Ok("NMake Makefiles".to_string());
    } else if cfg!(target_os = "macos") {
        // macOS - prefer Ninja, fallback to Xcode
        if Command::new("ninja").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            return Ok("Ninja".to_string());
        }
        return Ok("Xcode".to_string());
    } else {
        // Linux - prefer Ninja, fallback to Unix Makefiles
        if Command::new("ninja").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            return Ok("Ninja".to_string());
        }
        return Ok("Unix Makefiles".to_string());
    }
}

fn get_build_type(config: &ProjectConfig) -> String {
    let debug = config.build.debug.unwrap_or(true);
    if debug { "Debug".to_string() } else { "Release".to_string() }
}

fn get_platform_specific_options(config: &ProjectConfig) -> Vec<String> {
    let current_os = if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "darwin"
    } else {
        "linux"
    };

    let mut options = Vec::new();

    if let Some(platforms) = &config.platforms {
        if let Some(platform_config) = platforms.get(current_os) {
            // Add platform-specific defines
            if let Some(defines) = &platform_config.defines {
                for define in defines {
                    options.push(format!("-D{}", define));
                }
            }

            // Add platform-specific flags
            if let Some(flags) = &platform_config.flags {
                if !flags.is_empty() {
                    let flags_str = flags.join(" ");
                    if cfg!(target_os = "windows") {
                        options.push(format!("-DCMAKE_CXX_FLAGS=\"{}\"", flags_str));
                        options.push(format!("-DCMAKE_C_FLAGS=\"{}\"", flags_str));
                    } else {
                        options.push(format!("-DCMAKE_CXX_FLAGS='{}'", flags_str));
                        options.push(format!("-DCMAKE_C_FLAGS='{}'", flags_str));
                    }
                }
            }
        }
    }

    options
}

fn configure_project(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);
    fs::create_dir_all(&build_path)?;

    // Get CMake generator
    let generator = get_cmake_generator(config)?;

    // Get build type
    let build_type = get_build_type(config);

    // Set up vcpkg if enabled
    let vcpkg_toolchain = setup_vcpkg(config, project_path)?;

    // Generate CMakeLists.txt
    generate_cmake_lists(config, project_path)?;

    // Build CMake command
    let mut cmd = vec!["cmake".to_string(), "..".to_string()];

    // Add generator
    cmd.push("-G".to_string());
    cmd.push(generator.clone());

    // Add build type
    cmd.push(format!("-DCMAKE_BUILD_TYPE={}", build_type));

    // Add vcpkg toolchain if available
    if !vcpkg_toolchain.is_empty() {
        cmd.push(format!("-DCMAKE_TOOLCHAIN_FILE={}", vcpkg_toolchain));
    }

    // Add platform-specific options
    let platform_options = get_platform_specific_options(config);
    cmd.extend(platform_options);

    // Add custom CMake options
    if let Some(cmake_options) = &config.build.cmake_options {
        cmd.extend(cmake_options.clone());
    }

    // Run CMake configuration
    run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

    println!("{}", format!("Project configured with generator: {}", generator).green());
    Ok(())
}

fn build_project(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    if !build_path.join("CMakeCache.txt").exists() {
        println!("{}", "Project not configured yet, configuring...".blue());
        configure_project(config, project_path)?;
    }

    // Build using CMake
    let mut cmd = vec!["cmake".to_string(), "--build".to_string(), ".".to_string()];

    // Add configuration (Debug/Release)
    let build_type = get_build_type(config);
    cmd.push("--config".to_string());
    cmd.push(build_type.clone());

    // Run build
    run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

    println!("{}", format!("Build completed successfully ({} configuration)", build_type).green());
    Ok(())
}

fn clean_project(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    if build_path.exists() {
        println!("{}", format!("Removing build directory: {}", build_path.display()).blue());
        fs::remove_dir_all(&build_path)?;
        println!("{}", "Clean completed".green());
    } else {
        println!("{}", "Nothing to clean".blue());
    }

    Ok(())
}

fn run_project(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);
    let project_name = &config.project.name;
    let build_type = get_build_type(config);

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path)?;
    }

    // Find the executable
    let executable_paths = [
        // Standard CMake output directories
        build_path.join(project_name),                    // Unix Makefiles, Ninja
        build_path.join(&build_type).join(format!("{}.exe", project_name)),  // Visual Studio, Windows
        build_path.join(&build_type).join(project_name),        // Visual Studio, non-Windows
        build_path.join(format!("{}.exe", project_name)),       // NMake, Windows
        // Xcode output
        build_path.join(&build_type).join(project_name),        // Xcode
        // Custom binary directories
        if let Some(bin_dir) = &config.output.bin_dir {
            build_path.join(bin_dir).join(project_name)
        } else {
            build_path.join(project_name)
        },
        if let Some(bin_dir) = &config.output.bin_dir {
            build_path.join(bin_dir).join(&build_type).join(project_name)
        } else {
            build_path.join(&build_type).join(project_name)
        },
    ];

    let mut executable_path = None;
    for path in &executable_paths {
        if path.exists() && is_executable(path) {
            executable_path = Some(path.clone());
            break;
        }

        // Check for Windows .exe extension if not already included
        if cfg!(target_os = "windows") && !path.to_string_lossy().ends_with(".exe") {
            let exe_path = PathBuf::from(format!("{}.exe", path.display()));
            if exe_path.exists() {
                executable_path = Some(exe_path);
                break;
            }
        }
    }

    let executable = executable_path.ok_or_else(|| format!("Executable not found. Make sure the project is built successfully."))?;

    println!("{}", format!("Running: {}", executable.display()).blue());
    let status = Command::new(executable)
        .current_dir(project_path)
        .status()?;

    if !status.success() {
        println!("{}", format!("Program exited with code {}", status.code().unwrap_or(-1)).yellow());
    } else {
        println!("{}", "Program executed successfully".green());
    }

    Ok(())
}

fn test_project(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    // Check if tests directory exists
    if !project_path.join("tests").exists() {
        println!("{}", "No tests directory found. Skipping tests.".yellow());
        return Ok(());
    }

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path)?;
    }

    // Run tests using CTest
    let mut cmd = vec!["ctest".to_string(), "--output-on-failure".to_string()];

    // Add configuration (Debug/Release)
    let build_type = get_build_type(config);
    cmd.push("-C".to_string());
    cmd.push(build_type);

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

fn install_project(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path)?;
    }

    // Run CMake install
    let mut cmd = vec!["cmake".to_string(), "--install".to_string(), ".".to_string()];

    // Add configuration (Debug/Release)
    let build_type = get_build_type(config);
    cmd.push("--config".to_string());
    cmd.push(build_type);

    // Run install
    run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

    println!("{}", "Project installed successfully".green());
    Ok(())
}

fn install_dependencies(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    // Set up vcpkg dependencies
    let vcpkg_toolchain = setup_vcpkg(config, project_path)?;

    if !vcpkg_toolchain.is_empty() {
        println!("{}", "Dependencies installed successfully".green());
    } else {
        println!("{}", "No dependencies configured or vcpkg is disabled".blue());
    }

    Ok(())
}

// Utility functions
fn run_command(cmd: Vec<String>, cwd: Option<&str>, env: Option<HashMap<String, String>>) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", format!("Running: {}", cmd.join(" ")).blue());

    let mut command = Command::new(&cmd[0]);
    command.args(&cmd[1..]);

    if let Some(dir) = cwd {
        command.current_dir(dir);
    }

    if let Some(env_vars) = env {
        for (key, value) in env_vars {
            command.env(key, value);
        }
    }

    let output = match command.output() {
        Ok(output) => output,
        Err(e) => {
            println!("{}", format!("Failed to execute command: {}", e).red());
            return Err(Box::new(e));
        }
    };

    // Capture stdout and stderr
    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let stderr = String::from_utf8_lossy(&output.stderr).to_string();

    // Print command output regardless of success/failure
    if !stdout.is_empty() {
        println!("{}", stdout);
    }

    if !output.status.success() {
        println!("{}", "Command failed with error:".red());
        if !stderr.is_empty() {
            eprintln!("{}", stderr);
        }

        return Err(format!("Command failed with exit code: {}", output.status).into());
    }

    Ok(())
}

fn prompt(message: &str) -> Result<String, Box<dyn std::error::Error>> {
    print!("{}", message);
    std::io::stdout().flush()?;

    let mut input = String::new();
    std::io::stdin().read_line(&mut input)?;

    Ok(input)
}

fn expand_tilde(path: &str) -> String {
    if path.starts_with("~/") {
        if let Some(home) = dirs::home_dir() {
            return home.join(path.strip_prefix("~/").unwrap()).to_string_lossy().to_string();
        }
    }
    path.to_string()
}

fn is_executable(path: &Path) -> bool {
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(metadata) = fs::metadata(path) {
            return metadata.permissions().mode() & 0o111 != 0;
        }
        false
    }

    #[cfg(not(unix))]
    {
        // On Windows and other platforms, we consider all files executable if they exist
        path.exists()
    }
}