use clap::{Parser, Subcommand};
use colored::*;
use regex;
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
    about = "A TOML-based build system for C/C++ with CMake and vcpkg integration",
    version = "0.2.0",
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

        /// Create a project with a specific template
        #[clap(long)]
        template: Option<String>,
    },

    /// Build the project or workspace
    #[clap(name = "build")]
    Build {
        /// Build specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Build with specific configuration (Debug, Release, etc.)
        #[clap(long)]
        config: Option<String>,

        /// Build with specific variant
        #[clap(long)]
        variant: Option<String>,

        /// Cross-compile for specified target
        #[clap(long)]
        target: Option<String>,
    },

    /// Clean build artifacts
    #[clap(name = "clean")]
    Clean {
        /// Clean specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Clean specific configuration
        #[clap(long)]
        config: Option<String>,

        /// Clean specific target
        #[clap(long)]
        target: Option<String>,
    },

    /// Run the built executable
    #[clap(name = "run")]
    Run {
        /// Run a specific project in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Run with specific configuration
        #[clap(long)]
        config: Option<String>,

        /// Run with specific variant
        #[clap(long)]
        variant: Option<String>,

        /// Arguments to pass to the executable
        #[clap(trailing_var_arg = true)]
        args: Vec<String>,
    },

    /// Run tests
    #[clap(name = "test")]
    Test {
        /// Test specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Test with specific configuration
        #[clap(long)]
        config: Option<String>,

        /// Test with specific variant
        #[clap(long)]
        variant: Option<String>,

        /// Test filter pattern
        #[clap(long)]
        filter: Option<String>,
    },

    /// Install the built project
    #[clap(name = "install")]
    Install {
        /// Install specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Install with specific configuration
        #[clap(long)]
        config: Option<String>,

        /// Install prefix
        #[clap(long)]
        prefix: Option<String>,
    },

    /// Install dependencies
    #[clap(name = "deps")]
    Deps {
        /// Install dependencies for specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Update existing dependencies
        #[clap(long)]
        update: bool,
    },

    /// Run a custom script
    #[clap(name = "script")]
    Script {
        /// Script name to run
        #[clap(name = "name")]
        name: String,

        /// Run script for a specific project in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },

    /// Generate IDE project files
    #[clap(name = "ide")]
    Ide {
        /// IDE type (vscode, clion, etc.)
        #[clap(name = "type")]
        ide_type: String,

        /// Generate for specific project in a workspace
        #[clap(name = "project")]
        project: Option<String>,
    },

    /// Package the project
    #[clap(name = "package")]
    Package {
        /// Package specific project(s) in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Package with specific configuration
        #[clap(long)]
        config: Option<String>,

        /// Package type (deb, rpm, zip, etc.)
        #[clap(long)]
        type_: Option<String>,
    },

    /// List available configurations, variants and targets
    #[clap(name = "list")]
    List {
        /// List specific items (configs, variants, targets, scripts)
        #[clap(name = "what")]
        what: Option<String>,
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
    #[serde(default)]
    hooks: Option<BuildHooks>,
    #[serde(default)]
    scripts: Option<ScriptDefinitions>,
    #[serde(default)]
    variants: Option<BuildVariants>,
    #[serde(default)]
    cross_compile: Option<CrossCompileConfig>,
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
    default_config: Option<String>,  // Default configuration to use (Debug, Release, etc.)
    debug: Option<bool>,             // Legacy option, kept for backwards compatibility
    cmake_options: Option<Vec<String>>,
    configs: Option<HashMap<String, ConfigSettings>>,  // Configuration-specific settings
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct ConfigSettings {
    defines: Option<Vec<String>>,
    flags: Option<Vec<String>>,
    link_flags: Option<Vec<String>>,
    output_dir_suffix: Option<String>,  // Optional suffix for output directories
    cmake_options: Option<Vec<String>>,  // Configuration-specific CMake options
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct DependenciesConfig {
    vcpkg: VcpkgConfig,
    system: Option<Vec<String>>,
    cmake: Option<Vec<String>>,
    #[serde(default)]
    conan: ConanConfig,       // Conan package manager support
    #[serde(default)]
    custom: Vec<CustomDependency>, // Custom dependencies
    #[serde(default)]
    git: Vec<GitDependency>,  // Git dependencies
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct VcpkgConfig {
    enabled: bool,
    path: Option<String>,
    packages: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct ConanConfig {
    enabled: bool,
    packages: Vec<String>,
    options: Option<HashMap<String, String>>,
    generators: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct CustomDependency {
    name: String,
    url: String,
    version: Option<String>,
    cmake_options: Option<Vec<String>>,
    build_command: Option<String>,
    install_command: Option<String>,
    include_path: Option<String>,
    library_path: Option<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct GitDependency {
    name: String,
    url: String,
    branch: Option<String>,
    tag: Option<String>,
    commit: Option<String>,
    cmake_options: Option<Vec<String>>,
    shallow: Option<bool>,
    update: Option<bool>,
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

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct BuildHooks {
    pre_configure: Option<Vec<String>>,
    post_configure: Option<Vec<String>>,
    pre_build: Option<Vec<String>>,
    post_build: Option<Vec<String>>,
    pre_clean: Option<Vec<String>>,
    post_clean: Option<Vec<String>>,
    pre_install: Option<Vec<String>>,
    post_install: Option<Vec<String>>,
    pre_run: Option<Vec<String>>,
    post_run: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct ScriptDefinitions {
    scripts: HashMap<String, String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct BuildVariants {
    default: Option<String>,
    variants: HashMap<String, VariantSettings>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct VariantSettings {
    description: Option<String>,
    defines: Option<Vec<String>>,
    flags: Option<Vec<String>>,
    dependencies: Option<Vec<String>>,
    features: Option<Vec<String>>,
    platforms: Option<Vec<String>>,  // Platforms this variant is valid for
    cmake_options: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct CrossCompileConfig {
    enabled: bool,
    target: String,
    toolchain: Option<String>,
    sysroot: Option<String>,
    cmake_toolchain_file: Option<String>,
    define_prefix: Option<String>,  // Prefix for CMake defines (e.g., "ANDROID")
    flags: Option<Vec<String>>,
    env_vars: Option<HashMap<String, String>>,
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
        Commands::Init { workspace, template } => {
            if workspace {
                init_workspace()?;
            } else {
                init_project(None, template.as_deref())?;
            }
        }
        Commands::Build { project, config, variant, target } => {
            if is_workspace() {
                build_workspace(project, config.as_deref(), variant.as_deref(), target.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                build_project(&proj_config, &PathBuf::from("."), config.as_deref(), variant.as_deref(), target.as_deref())?;
            }
        }
        Commands::Clean { project, config, target } => {
            if is_workspace() {
                clean_workspace(project, config.as_deref(), target.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                clean_project(&proj_config, &PathBuf::from("."), config.as_deref(), target.as_deref())?;
            }
        }
        Commands::Run { project, config, variant, args } => {
            if is_workspace() {
                run_workspace(project, config.as_deref(), variant.as_deref(), &args)?;
            } else {
                let proj_config = load_project_config(None)?;
                run_project(&proj_config, &PathBuf::from("."), config.as_deref(), variant.as_deref(), &args)?;
            }
        }
        Commands::Test { project, config, variant, filter } => {
            if is_workspace() {
                test_workspace(project, config.as_deref(), variant.as_deref(), filter.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                test_project(&proj_config, &PathBuf::from("."), config.as_deref(), variant.as_deref(), filter.as_deref())?;
            }
        }
        Commands::Install { project, config, prefix } => {
            if is_workspace() {
                install_workspace(project, config.as_deref(), prefix.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                install_project(&proj_config, &PathBuf::from("."), config.as_deref(), prefix.as_deref())?;
            }
        }
        Commands::Deps { project, update } => {
            if is_workspace() {
                install_workspace_deps(project, update)?;
            } else {
                let proj_config = load_project_config(None)?;
                install_dependencies(&proj_config, &PathBuf::from("."), update)?;
            }
        }
        Commands::Script { name, project } => {
            if is_workspace() {
                run_workspace_script(name, project)?;
            } else {
                let proj_config = load_project_config(None)?;
                run_script(&proj_config, &name, &PathBuf::from("."))?;
            }
        }
        Commands::Ide { ide_type, project } => {
            if is_workspace() {
                generate_workspace_ide_files(ide_type, project)?;
            } else {
                let proj_config = load_project_config(None)?;
                generate_ide_files(&proj_config, &PathBuf::from("."), &ide_type)?;
            }
        }
        Commands::Package { project, config, type_ } => {
            if is_workspace() {
                package_workspace(project, config.as_deref(), type_.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                package_project(&proj_config, &PathBuf::from("."), config.as_deref(), type_.as_deref())?;
            }
        }
        Commands::List { what } => {
            if is_workspace() {
                list_workspace_items(what.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                list_project_items(&proj_config, what.as_deref())?;
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

fn build_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    variant: Option<&str>,
    target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
    };

    for project_path in projects {
        println!("{}", format!("Building project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        build_project(&config, &path, config_type, variant, target)?;
    }

    println!("{}", "Workspace build completed".green());
    Ok(())
}

fn clean_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
    };

    for project_path in projects {
        println!("{}", format!("Cleaning project: {}", project_path).blue());
        let path = PathBuf::from(&project_path);
        let config = load_project_config(Some(&path))?;
        clean_project(&config, &path, config_type, target)?;
    }

    println!("{}", "Workspace clean completed".green());
    Ok(())
}

fn run_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    variant: Option<&str>,
    args: &[String]
) -> Result<(), Box<dyn std::error::Error>> {
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
    run_project(&config, &path, config_type, variant, args)?;

    Ok(())
}

fn test_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    variant: Option<&str>,
    filter: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
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

fn install_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    prefix: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
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

fn install_workspace_deps(project: Option<String>, update: bool) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
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

fn run_workspace_script(name: String, project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
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

    println!("{}", format!("Running script '{}' for project: {}", name, project_path).blue());
    let path = PathBuf::from(&project_path);
    let config = load_project_config(Some(&path))?;
    run_script(&config, &name, &path)?;

    Ok(())
}

fn generate_workspace_ide_files(ide_type: String, project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects.clone(),
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

fn package_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    package_type: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.projects,
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

fn list_workspace_items(what: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    println!("{}", "Workspace projects:".bold());
    for (i, project) in workspace_config.projects.iter().enumerate() {
        println!(" {}. {}", i + 1, project.green());
    }

    if workspace_config.projects.is_empty() {
        println!(" - No projects in workspace");
    } else if let Some(first_project) = workspace_config.projects.first() {
        // Show info about the first project
        let path = PathBuf::from(first_project);
        let config = load_project_config(Some(&path))?;

        println!("\n{}", "First project details:".bold());
        list_project_items(&config, what)?;
    }

    Ok(())
}

// Project functions
fn init_project(path: Option<&Path>, template: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let project_path = path.unwrap_or_else(|| Path::new("."));

    let config_path = project_path.join(CBUILD_FILE);
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
    fs::create_dir_all(project_path.join("scripts"))?;

    // Create a simple main.cpp file for executable projects
    if config.project.project_type == "executable" {
        let main_file = project_path.join("src").join("main.cpp");
        let mut file = File::create(main_file)?;
        file.write_all(b"#include <iostream>\n\nint main(int argc, char* argv[]) {\n    std::cout << \"Hello, CBuild!\" << std::endl;\n    return 0;\n}\n")?;
    } else if config.project.project_type == "library" {
        // Create a header file
        let header_file = project_path.join("include").join(format!("{}.h", config.project.name));
        let mut file = File::create(header_file)?;
        file.write_all(format!("#pragma once\n\nnamespace {0} {{\n\n// Library interface\nclass Library {{\npublic:\n    Library();\n    ~Library();\n    \n    int calculate(int a, int b);\n}};\n\n}} // namespace {0}\n", config.project.name).as_bytes())?;

        // Create a source file
        let source_file = project_path.join("src").join(format!("{}.cpp", config.project.name));
        let mut file = File::create(source_file)?;
        file.write_all(format!("#include \"{}.h\"\n\nnamespace {} {{\n\nLibrary::Library() {{\n}}\n\nLibrary::~Library() {{\n}}\n\nint Library::calculate(int a, int b) {{\n    return a + b;\n}}\n\n}} // namespace {}\n", config.project.name, config.project.name, config.project.name).as_bytes())?;
    }

    // Create a version script
    let version_script = project_path.join("scripts").join("version_gen.py");
    let mut file = File::create(version_script)?;
    file.write_all(b"#!/usr/bin/env python3\n\n# Generate version information\nimport os\nimport datetime\n\ndef main():\n    version_file = 'src/version.h'\n    \n    with open(version_file, 'w') as f:\n        f.write('#pragma once\\n\\n')\n        f.write(f'#define PROJECT_VERSION \"{config.project.version}\"\\n')\n        f.write(f'#define BUILD_TIMESTAMP \"{datetime.datetime.now().strftime(\"%Y-%m-%d %H:%M:%S\")}\"\\n')\n\nif __name__ == '__main__':\n    main()\n")?;

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

fn create_library_config() -> ProjectConfig {
    let mut config = create_default_config();
    config.project.project_type = "library".to_string();
    config.project.description = "A C++ library built with CBuild".to_string();

    config
}

fn create_header_only_config() -> ProjectConfig {
    let mut config = create_default_config();
    config.project.project_type = "header-only".to_string();
    config.project.description = "A header-only C++ library built with CBuild".to_string();

    // For header-only libraries, we don't need source files
    if let Some(target) = config.targets.get_mut("default") {
        target.sources = vec![];
    }

    config
}

fn create_default_config() -> ProjectConfig {
    let system_info = detect_system_info();

    // Create default configurations
    let mut configs = HashMap::new();

    // Debug configuration
    configs.insert("Debug".to_string(), ConfigSettings {
        defines: Some(vec!["DEBUG".to_string(), "_DEBUG".to_string()]),
        flags: Some(if cfg!(target_os = "windows") {
            vec!["/Od".to_string(), "/Zi".to_string(), "/RTC1".to_string()]
        } else {
            vec!["-O0".to_string(), "-g".to_string()]
        }),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    // Release configuration
    configs.insert("Release".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(if cfg!(target_os = "windows") {
            vec!["/O2".to_string(), "/Ob2".to_string(), "/DNDEBUG".to_string()]
        } else {
            vec!["-O3".to_string(), "-DNDEBUG".to_string()]
        }),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    // RelWithDebInfo configuration
    configs.insert("RelWithDebInfo".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(if cfg!(target_os = "windows") {
            vec!["/O2".to_string(), "/Ob1".to_string(), "/DNDEBUG".to_string(), "/Zi".to_string()]
        } else {
            vec!["-O2".to_string(), "-g".to_string(), "-DNDEBUG".to_string()]
        }),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    // MinSizeRel configuration
    configs.insert("MinSizeRel".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(if cfg!(target_os = "windows") {
            vec!["/O1".to_string(), "/Ob1".to_string(), "/DNDEBUG".to_string()]
        } else {
            vec!["-Os".to_string(), "-DNDEBUG".to_string()]
        }),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

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

    // Create default build variants
    let mut variants = HashMap::new();

    // Standard variant (default)
    variants.insert("standard".to_string(), VariantSettings {
        description: Some("Standard build with default settings".to_string()),
        defines: None,
        flags: None,
        dependencies: None,
        features: None,
        platforms: None,
        cmake_options: None,
    });

    // Memory safety variant
    variants.insert("memory_safety".to_string(), VariantSettings {
        description: Some("Build with memory safety checks".to_string()),
        defines: Some(vec!["ENABLE_MEMORY_SAFETY=1".to_string()]),
        flags: Some(if cfg!(target_os = "windows") {
            vec!["/sdl".to_string(), "/GS".to_string()]
        } else {
            vec!["-fsanitize=address".to_string(), "-fsanitize=undefined".to_string()]
        }),
        dependencies: None,
        features: None,
        platforms: None,
        cmake_options: None,
    });

    // Performance variant
    variants.insert("performance".to_string(), VariantSettings {
        description: Some("Optimized for maximum performance".to_string()),
        defines: Some(vec!["OPTIMIZE_PERFORMANCE=1".to_string()]),
        flags: Some(if cfg!(target_os = "windows") {
            vec!["/GL".to_string(), "/Qpar".to_string(), "/O2".to_string()]
        } else {
            vec!["-O3".to_string(), "-march=native".to_string(), "-flto".to_string()]
        }),
        dependencies: None,
        features: None,
        platforms: None,
        cmake_options: None,
    });

    // Create default build hooks
    let hooks = BuildHooks {
        pre_configure: Some(vec![
            "echo Running pre-configure hook...".to_string(),
        ]),
        post_configure: Some(vec![
            "echo Configuration completed.".to_string(),
        ]),
        pre_build: Some(vec![
            "echo Starting build process...".to_string(),
        ]),
        post_build: Some(vec![
            "echo Build completed successfully.".to_string(),
        ]),
        pre_clean: None,
        post_clean: None,
        pre_install: None,
        post_install: None,
        pre_run: None,
        post_run: None,
    };

    // Create default scripts
    let mut scripts = HashMap::new();

    scripts.insert(
        "format".to_string(),
        "find src include -name '*.cpp' -o -name '*.h' | xargs clang-format -i".to_string()
    );

    scripts.insert(
        "count_lines".to_string(),
        "find src include -name '*.cpp' -o -name '*.h' | xargs wc -l".to_string()
    );

    scripts.insert(
        "clean_all".to_string(),
        if cfg!(target_os = "windows") {
            "rmdir /s /q build bin".to_string()
        } else {
            "rm -rf build bin".to_string()
        }
    );

    // Create default conan config
    let conan_config = ConanConfig {
        enabled: false,
        packages: vec![],
        options: Some(HashMap::new()),
        generators: Some(vec![
            "cmake".to_string(),
            "cmake_find_package".to_string(),
        ]),
    };

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
            default_config: Some("Debug".to_string()),
            debug: Some(true), // Legacy option, kept for backwards compatibility
            cmake_options: Some(vec![]),
            configs: Some(configs),
        },
        dependencies: DependenciesConfig {
            vcpkg: VcpkgConfig {
                enabled: true,
                path: Some(VCPKG_DEFAULT_DIR.to_string()),
                packages: vec![],
            },
            system: Some(vec![]),
            cmake: Some(vec![]),
            conan: conan_config,
            custom: vec![],
            git: vec![],
        },
        targets,
        platforms: Some(platforms),
        output: OutputConfig {
            bin_dir: Some(DEFAULT_BIN_DIR.to_string()),
            lib_dir: Some(DEFAULT_LIB_DIR.to_string()),
            obj_dir: Some(DEFAULT_OBJ_DIR.to_string()),
        },
        hooks: None,
        scripts: Some(ScriptDefinitions { scripts }),
        variants: Some(BuildVariants {
            default: Some("standard".to_string()),
            variants,
        }),
        cross_compile: None,
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

// Setup Conan packages
fn setup_conan(config: &ProjectConfig, project_path: &Path) -> Result<String, Box<dyn std::error::Error>> {
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

// Setup git dependencies
fn setup_git_dependencies(config: &ProjectConfig, project_path: &Path) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let git_deps = &config.dependencies.git;
    if git_deps.is_empty() {
        return Ok(Vec::new());
    }

    // Check if git is installed
    if Command::new("git").arg("--version").stdout(Stdio::null()).stderr(Stdio::null()).status().is_err() {
        return Err("Git not found. Please install git first.".into());
    }

    // Create deps directory
    let deps_dir = project_path.join("deps");
    fs::create_dir_all(&deps_dir)?;

    let mut cmake_include_paths = Vec::new();

    for dep in git_deps {
        let dep_path = deps_dir.join(&dep.name);

        if dep_path.exists() {
            // If update is requested, pull latest changes
            if dep.update.unwrap_or(false) {
                println!("{}", format!("Updating git dependency: {}", dep.name).blue());

                let mut cmd = vec![
                    "git".to_string(),
                    "pull".to_string(),
                ];

                run_command(cmd, Some(&dep_path.to_string_lossy().to_string()), None)?;
            } else {
                println!("{}", format!("Git dependency already exists: {}", dep.name).green());
            }
        } else {
            println!("{}", format!("Cloning git dependency: {}", dep.name).blue());

            // Build git clone command
            let mut cmd = vec![
                "git".to_string(),
                "clone".to_string(),
            ];

            // Add shallow clone option if requested
            if dep.shallow.unwrap_or(false) {
                cmd.push("--depth=1".to_string());
            }

            // Add branch/tag/commit if specified
            if let Some(branch) = &dep.branch {
                cmd.push("--branch".to_string());
                cmd.push(branch.clone());
            } else if let Some(tag) = &dep.tag {
                cmd.push("--branch".to_string());
                cmd.push(tag.clone());
            }

            // Add URL and target directory
            cmd.push(dep.url.clone());
            cmd.push(dep_path.to_string_lossy().to_string());

            // Clone the repository
            run_command(cmd, Some(&deps_dir.to_string_lossy().to_string()), None)?;

            // Checkout specific commit if requested
            if let Some(commit) = &dep.commit {
                let mut cmd = vec![
                    "git".to_string(),
                    "checkout".to_string(),
                    commit.clone(),
                ];

                run_command(cmd, Some(&dep_path.to_string_lossy().to_string()), None)?;
            }
        }

        // Build dependency if it has a CMakeLists.txt
        if dep_path.join("CMakeLists.txt").exists() {
            let build_path = dep_path.join("build");
            fs::create_dir_all(&build_path)?;

            // Configure with CMake
            let mut cmd = vec![
                "cmake".to_string(),
                "..".to_string(),
            ];

            // Add dependency-specific CMake options
            if let Some(cmake_options) = &dep.cmake_options {
                cmd.extend(cmake_options.clone());
            }

            run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

            // Build
            let mut cmd = vec![
                "cmake".to_string(),
                "--build".to_string(),
                ".".to_string(),
            ];

            run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

            // Add dependency path to CMake include paths
            cmake_include_paths.push(format!("{}/build", dep_path.to_string_lossy()));
        }
    }

    Ok(cmake_include_paths)
}

// Setup custom dependencies
fn setup_custom_dependencies(config: &ProjectConfig, project_path: &Path) -> Result<Vec<String>, Box<dyn std::error::Error>> {
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

// Enhanced install_dependencies function
fn install_dependencies(config: &ProjectConfig, project_path: &Path, update: bool) -> Result<HashMap<String, String>, Box<dyn std::error::Error>> {
    let mut dependencies_info = HashMap::new();

    // Set up vcpkg dependencies
    let vcpkg_toolchain = setup_vcpkg(config, project_path)?;
    if !vcpkg_toolchain.is_empty() {
        dependencies_info.insert("vcpkg_toolchain".to_string(), vcpkg_toolchain);
    }

    // Set up conan dependencies
    let conan_cmake = setup_conan(config, project_path)?;
    if !conan_cmake.is_empty() {
        dependencies_info.insert("conan_cmake".to_string(), conan_cmake);
    }

    // Set up git dependencies
    let git_includes = setup_git_dependencies(config, project_path)?;
    if !git_includes.is_empty() {
        dependencies_info.insert("git_includes".to_string(), git_includes.join(";"));
    }

    // Set up custom dependencies
    let custom_includes = setup_custom_dependencies(config, project_path)?;
    if !custom_includes.is_empty() {
        dependencies_info.insert("custom_includes".to_string(), custom_includes.join(";"));
    }

    if !dependencies_info.is_empty() {
        println!("{}", "Dependencies installed successfully".green());
    } else {
        println!("{}", "No dependencies configured or all dependencies are disabled".blue());
    }

    Ok(dependencies_info)
}

// Predefined cross-compilation targets
fn get_predefined_cross_target(target_name: &str) -> Option<CrossCompileConfig> {
    match target_name {
        "android-arm64" => Some(CrossCompileConfig {
            enabled: true,
            target: "aarch64-linux-android".to_string(),
            toolchain: None,
            sysroot: None,
            cmake_toolchain_file: Some("$ANDROID_NDK/build/cmake/android.toolchain.cmake".to_string()),
            define_prefix: Some("ANDROID".to_string()),
            flags: Some(vec![
                "-DANDROID_ABI=arm64-v8a".to_string(),
                "-DANDROID_PLATFORM=android-24".to_string(),
                "-DANDROID_STL=c++_shared".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "android-arm" => Some(CrossCompileConfig {
            enabled: true,
            target: "armv7a-linux-androideabi".to_string(),
            toolchain: None,
            sysroot: None,
            cmake_toolchain_file: Some("$ANDROID_NDK/build/cmake/android.toolchain.cmake".to_string()),
            define_prefix: Some("ANDROID".to_string()),
            flags: Some(vec![
                "-DANDROID_ABI=armeabi-v7a".to_string(),
                "-DANDROID_PLATFORM=android-19".to_string(),
                "-DANDROID_STL=c++_shared".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "ios" => Some(CrossCompileConfig {
            enabled: true,
            target: "arm64-apple-ios".to_string(),
            toolchain: None,
            sysroot: None,
            cmake_toolchain_file: None,
            define_prefix: Some("IOS".to_string()),
            flags: Some(vec![
                "-DCMAKE_SYSTEM_NAME=iOS".to_string(),
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0".to_string(),
                "-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO".to_string(),
                "-DCMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE=YES".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "raspberry-pi" => Some(CrossCompileConfig {
            enabled: true,
            target: "arm-linux-gnueabihf".to_string(),
            toolchain: Some("arm-linux-gnueabihf".to_string()),
            sysroot: None,
            cmake_toolchain_file: None,
            define_prefix: Some("RASPBERRY_PI".to_string()),
            flags: Some(vec![
                "-DCMAKE_SYSTEM_NAME=Linux".to_string(),
                "-DCMAKE_SYSTEM_PROCESSOR=arm".to_string(),
                "-DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc".to_string(),
                "-DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "wasm" => Some(CrossCompileConfig {
            enabled: true,
            target: "wasm32-unknown-emscripten".to_string(),
            toolchain: Some("emscripten".to_string()),
            sysroot: None,
            cmake_toolchain_file: Some("$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake".to_string()),
            define_prefix: Some("WASM".to_string()),
            flags: Some(vec![
                "-DCMAKE_SYSTEM_NAME=Emscripten".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        _ => None,
    }
}

// Function to set up cross-compilation environment
fn setup_cross_compilation(config: &ProjectConfig, cross_config: &CrossCompileConfig) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut cmake_options = Vec::new();

    // Add toolchain file if provided
    if let Some(toolchain_file) = &cross_config.cmake_toolchain_file {
        // Expand environment variables in toolchain file path
        let expanded_path = expand_env_vars(toolchain_file);
        cmake_options.push(format!("-DCMAKE_TOOLCHAIN_FILE={}", expanded_path));
    }

    // Add target-specific flags
    if let Some(flags) = &cross_config.flags {
        cmake_options.extend(flags.clone());
    }

    // Add sysroot if provided
    if let Some(sysroot) = &cross_config.sysroot {
        // Expand environment variables in sysroot path
        let expanded_sysroot = expand_env_vars(sysroot);
        cmake_options.push(format!("-DCMAKE_SYSROOT={}", expanded_sysroot));
    }

    // Add define prefix if provided (useful for conditional compilation)
    if let Some(prefix) = &cross_config.define_prefix {
        cmake_options.push(format!("-D{}=1", prefix));
    }

    // If no toolchain file is provided but toolchain name is, set explicit compiler paths
    if cross_config.cmake_toolchain_file.is_none() && cross_config.toolchain.is_some() {
        let toolchain = cross_config.toolchain.as_ref().unwrap();

        // Set C and C++ compilers
        cmake_options.push(format!("-DCMAKE_C_COMPILER={}-gcc", toolchain));
        cmake_options.push(format!("-DCMAKE_CXX_COMPILER={}-g++", toolchain));

        // Set other tools
        cmake_options.push(format!("-DCMAKE_AR={}-ar", toolchain));
        cmake_options.push(format!("-DCMAKE_RANLIB={}-ranlib", toolchain));
        cmake_options.push(format!("-DCMAKE_STRIP={}-strip", toolchain));
    }

    // Set system name and processor if not already set by flags
    if !cmake_options.iter().any(|opt| opt.starts_with("-DCMAKE_SYSTEM_NAME=")) {
        let target_parts: Vec<&str> = cross_config.target.split('-').collect();
        if target_parts.len() >= 3 {
            let arch = target_parts[0];
            let vendor = target_parts[1];
            let sys = target_parts[2];

            let system_name = match sys {
                "windows" => "Windows",
                "darwin" | "ios" | "macos" => "Darwin",
                "android" => "Android",
                _ => "Linux",
            };

            cmake_options.push(format!("-DCMAKE_SYSTEM_NAME={}", system_name));
            cmake_options.push(format!("-DCMAKE_SYSTEM_PROCESSOR={}", arch));
        }
    }

    Ok(cmake_options)
}

// Helper function to expand environment variables in a string
fn expand_env_vars(input: &str) -> String {
    let mut result = input.to_string();

    // Find environment variables in the format $VAR or ${VAR}
    let env_var_regex = regex::Regex::new(r"\$\{?([a-zA-Z_][a-zA-Z0-9_]*)\}?").unwrap();

    for cap in env_var_regex.captures_iter(input) {
        let var_name = &cap[1];
        let var_pattern = &cap[0];

        if let Ok(var_value) = env::var(var_name) {
            result = result.replace(var_pattern, &var_value);
        }
    }

    result
}

// Function to get environment variables for cross-compilation
fn get_cross_compilation_env(cross_config: &CrossCompileConfig) -> HashMap<String, String> {
    let mut env_vars = HashMap::new();

    // Add environment variables specified in config
    if let Some(vars) = &cross_config.env_vars {
        for (key, value) in vars {
            env_vars.insert(key.clone(), expand_env_vars(value));
        }
    }

    // Add common environment variables based on target
    match cross_config.target.as_str() {
        "aarch64-linux-android" | "armv7a-linux-androideabi" => {
            // Add Android SDK/NDK environment variables if not already set
            if !env_vars.contains_key("ANDROID_NDK") && env::var("ANDROID_NDK").is_err() {
                // Try to find Android NDK in common locations
                for path in &[
                    "$ANDROID_HOME/ndk-bundle",
                    "$ANDROID_HOME/ndk/latest",
                    "$ANDROID_HOME/ndk/21.0.6113669", // Common version
                ] {
                    let expanded_path = expand_env_vars(path);
                    if Path::new(&expanded_path).exists() {
                        env_vars.insert("ANDROID_NDK".to_string(), expanded_path);
                        break;
                    }
                }
            }
        },
        "wasm32-unknown-emscripten" => {
            // Add Emscripten environment variables if not already set
            if !env_vars.contains_key("EMSCRIPTEN") && env::var("EMSCRIPTEN").is_err() {
                // Try to find Emscripten in common locations
                for path in &[
                    "$HOME/emsdk/upstream/emscripten",
                    "/usr/local/emsdk/upstream/emscripten",
                    "C:/emsdk/upstream/emscripten",
                ] {
                    let expanded_path = expand_env_vars(path);
                    if Path::new(&expanded_path).exists() {
                        env_vars.insert("EMSCRIPTEN".to_string(), expanded_path);
                        break;
                    }
                }
            }
        },
        _ => {},
    }

    env_vars
}

// Update get_build_type function to handle custom configurations
fn get_build_type(config: &ProjectConfig, requested_config: Option<&str>) -> String {
    // If a specific configuration was requested, use that
    if let Some(requested) = requested_config {
        return requested.to_string();
    }

    // Otherwise use the default configuration from the config file
    if let Some(default_config) = &config.build.default_config {
        return default_config.clone();
    }

    // Fallback to traditional debug/release
    if config.build.debug.unwrap_or(true) {
        "Debug".to_string()
    } else {
        "Release".to_string()
    }
}

// Get configuration-specific options
fn get_config_specific_options(config: &ProjectConfig, build_type: &str) -> Vec<String> {
    let mut options = Vec::new();

    // Add configuration-specific options if available
    if let Some(configs) = &config.build.configs {
        if let Some(config_settings) = configs.get(build_type) {
            // Add configuration-specific defines - use =1 format
            if let Some(defines) = &config_settings.defines {
                for define in defines {
                    options.push(format!("-D{}=1", define));
                }
            }

            // Add configuration-specific flags - handle Windows specially
            if let Some(flags) = &config_settings.flags {
                if !flags.is_empty() {
                    if cfg!(windows) {
                        // On Windows, join flags and specify STRING type to avoid quoting issues
                        let flags_str = flags.join(" ");
                        options.push(format!("-DCMAKE_CXX_FLAGS_{}:STRING={}",
                                             build_type.to_uppercase(), flags_str));
                        options.push(format!("-DCMAKE_C_FLAGS_{}:STRING={}",
                                             build_type.to_uppercase(), flags_str));
                    } else {
                        // On Unix, we can join with spaces and use single quotes
                        let flags_str = flags.join(" ");
                        options.push(format!("-DCMAKE_CXX_FLAGS_{}='{}'",
                                             build_type.to_uppercase(), flags_str));
                        options.push(format!("-DCMAKE_C_FLAGS_{}='{}'",
                                             build_type.to_uppercase(), flags_str));
                    }
                }
            }

            if let Some(link_flags) = &config_settings.link_flags {
                if !link_flags.is_empty() {
                    let link_flags_str = link_flags.join(" ");
                    if cfg!(target_os = "windows") {
                        options.push(format!("-DCMAKE_EXE_LINKER_FLAGS_{}=\"{}\"", build_type.to_uppercase(), link_flags_str));
                        options.push(format!("-DCMAKE_SHARED_LINKER_FLAGS_{}=\"{}\"", build_type.to_uppercase(), link_flags_str));
                    } else {
                        options.push(format!("-DCMAKE_EXE_LINKER_FLAGS_{}='{}'", build_type.to_uppercase(), link_flags_str));
                        options.push(format!("-DCMAKE_SHARED_LINKER_FLAGS_{}='{}'", build_type.to_uppercase(), link_flags_str));
                    }
                }
            }

            // Add configuration-specific CMake options
            if let Some(cmake_options) = &config_settings.cmake_options {
                options.extend(cmake_options.clone());
            }
        }
    }

    options
}
// Functions to handle build variants
fn get_active_variant<'a>(config: &'a ProjectConfig, requested_variant: Option<&str>) -> Option<&'a VariantSettings> {
    if let Some(variants) = &config.variants {
        // If a specific variant was requested, use that
        if let Some(requested) = requested_variant {
            return variants.variants.get(requested);
        }

        // Otherwise use the default variant from the config file
        if let Some(default_variant) = &variants.default {
            return variants.variants.get(default_variant);
        }
    }

    None
}

fn apply_variant_settings(cmd: &mut Vec<String>, variant: &VariantSettings) {
    // Add variant-specific defines with =1 format
    if let Some(defines) = &variant.defines {
        for define in defines {
            cmd.push(format!("-D{}=1", define));
        }
    }

    // Add variant-specific flags with proper quoting
    if let Some(flags) = &variant.flags {
        if !flags.is_empty() {
            if cfg!(windows) {
                // On Windows, join flags and use STRING type
                let flags_str = flags.join(" ");
                cmd.push(format!("-DCMAKE_CXX_FLAGS:STRING={}", flags_str));
                cmd.push(format!("-DCMAKE_C_FLAGS:STRING={}", flags_str));
            } else {
                // On Unix, use single quotes
                let flags_str = flags.join(" ");
                cmd.push(format!("-DCMAKE_CXX_FLAGS='{}'", flags_str));
                cmd.push(format!("-DCMAKE_C_FLAGS='{}'", flags_str));
            }
        }
    }

    // Add variant-specific CMake options
    if let Some(cmake_options) = &variant.cmake_options {
        cmd.extend(cmake_options.clone());
    }
}
// Function to run build hooks
fn run_hooks(hooks: &Option<Vec<String>>, project_path: &Path, env_vars: Option<HashMap<String, String>>) -> Result<(), Box<dyn std::error::Error>> {
    if let Some(commands) = hooks {
        for cmd_str in commands {
            println!("{}", format!("Running hook: {}", cmd_str).blue());

            // Use the system shell instead of direct command execution
            let (shell, shell_arg) = if cfg!(windows) {
                ("cmd", "/C")
            } else {
                ("sh", "-c")
            };

            // Create command
            let mut command = Command::new(shell);
            command.arg(shell_arg).arg(cmd_str);
            command.current_dir(project_path);

            // Add environment variables if provided
            if let Some(env) = &env_vars {
                for (key, value) in env {
                    command.env(key, value);
                }
            }

            // Execute the command
            let output = command.output()?;

            // Print output
            let stdout = String::from_utf8_lossy(&output.stdout);
            let stderr = String::from_utf8_lossy(&output.stderr);

            if !stdout.is_empty() {
                println!("{}", stdout);
            }

            if !output.status.success() {
                println!("{}", "Hook command failed:".red());
                if !stderr.is_empty() {
                    eprintln!("{}", stderr);
                }

                return Err(format!("Hook failed with exit code: {}", output.status).into());
            }
        }
    }

    Ok(())
}
fn generate_cmake_lists(config: &ProjectConfig, project_path: &Path, variant_name: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let project_config = &config.project;
    let targets_config = &config.targets;
    let cmake_minimum = CMAKE_MIN_VERSION;

    let mut cmake_content = vec![
        format!("cmake_minimum_required(VERSION {})", cmake_minimum),
        format!("project({} VERSION {})", project_config.name, project_config.version),
        String::new(),
        "# Generated by CBuild - Do not edit manually".to_string(),
        String::new(),
        // Add helper function to expand glob patterns at configure time
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

    // Add Conan support if enabled
    if config.dependencies.conan.enabled {
        // Include Conan-generated file if it exists
        cmake_content.push(String::new());
        cmake_content.push("# Conan package manager integration".to_string());
        cmake_content.push("if(EXISTS \"${CMAKE_BINARY_DIR}/conan/conanbuildinfo.cmake\")".to_string());
        cmake_content.push("  include(${CMAKE_BINARY_DIR}/conan/conanbuildinfo.cmake)".to_string());
        cmake_content.push("  conan_basic_setup()".to_string());
        cmake_content.push("endif()".to_string());
        cmake_content.push(String::new());
    }

    // Add dependencies
    if let Some(cmake_deps) = &config.dependencies.cmake {
        for dep in cmake_deps {
            cmake_content.push(format!("find_package({} REQUIRED)", dep));
        }
    }

    // Create a default source file if needed
    let default_source_path = project_path.join("src").join("main.cpp");
    let default_source_exists = default_source_path.exists();

    if !default_source_exists && project_config.project_type == "executable" {
        fs::create_dir_all(project_path.join("src"))?;
        let mut file = File::create(&default_source_path)?;
        file.write_all(b"#include <iostream>\n\nint main(int argc, char* argv[]) {\n    std::cout << \"Hello, CBuild!\" << std::endl;\n    return 0;\n}\n")?;
        println!("{}", format!("Created default source file: {}", default_source_path.display()).green());
    }

    // Add build variant defines if a variant is specified
    if let Some(variant) = get_active_variant(config, variant_name) {
        if let Some(defines) = &variant.defines {
            cmake_content.push(String::new());
            cmake_content.push(format!("# Build variant: {}", variant_name.unwrap_or("default")).to_string());
            for define in defines {
                cmake_content.push(format!("add_definitions(-D{})", define));
            }
            cmake_content.push(String::new());
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

        // Handle source files using CMake's file globbing with our helper function
        cmake_content.push(format!("# Source patterns for target {}", target_name));

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

        // Add fallback for when no sources are found
        cmake_content.push(format!("if(NOT {}_SOURCES)", target_name.to_uppercase()));
        cmake_content.push(format!("  # No sources found, using default source"));
        cmake_content.push(format!("  set({}_SOURCES \"${{CMAKE_SOURCE_DIR}}/src/main.cpp\")", target_name.to_uppercase()));
        cmake_content.push(format!("endif()"));

        // Create target using the expanded sources
        if target_type == "executable" {
            cmake_content.push(format!("add_executable({} ${{{}_SOURCES}})", target_name, target_name.to_uppercase()));
        } else if target_type == "library" {
            cmake_content.push(format!("add_library({} SHARED ${{{}_SOURCES}})", target_name, target_name.to_uppercase()));
        } else if target_type == "static-library" {
            cmake_content.push(format!("add_library({} STATIC ${{{}_SOURCES}})", target_name, target_name.to_uppercase()));
        } else if target_type == "header-only" {
            cmake_content.push(format!("add_library({} INTERFACE)", target_name));
        }

        // Include directories
        if !include_dirs.is_empty() {
            let includes = include_dirs.iter()
                .map(|s| format!("\"{}\"", s))
                .collect::<Vec<_>>()
                .join(" ");

            if target_type == "header-only" {
                cmake_content.push(format!("target_include_directories({} INTERFACE {})", target_name, includes));
            } else {
                cmake_content.push(format!("target_include_directories({} PRIVATE {})", target_name, includes));
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
            let links_str = links.join(" ");
            if target_type == "header-only" {
                cmake_content.push(format!("target_link_libraries({} INTERFACE {})", target_name, links_str));
            } else {
                cmake_content.push(format!("target_link_libraries({} PRIVATE {})", target_name, links_str));
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
            // Add platform-specific defines with =1 format
            if let Some(defines) = &platform_config.defines {
                for define in defines {
                    options.push(format!("-D{}=1", define));
                }
            }

            // Add platform-specific flags with proper quoting
            if let Some(flags) = &platform_config.flags {
                if !flags.is_empty() {
                    if cfg!(windows) {
                        // On Windows, join flags and use STRING type
                        let flags_str = flags.join(" ");
                        options.push(format!("-DCMAKE_CXX_FLAGS:STRING={}", flags_str));
                        options.push(format!("-DCMAKE_C_FLAGS:STRING={}", flags_str));
                    } else {
                        // On Unix, use single quotes
                        let flags_str = flags.join(" ");
                        options.push(format!("-DCMAKE_CXX_FLAGS='{}'", flags_str));
                        options.push(format!("-DCMAKE_C_FLAGS='{}'", flags_str));
                    }
                }
            }
        }
    }

    options
}
fn configure_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    cross_target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    // Create a set of environment variables for hooks
    let mut hook_env = HashMap::new();
    hook_env.insert("PROJECT_PATH".to_string(), project_path.to_string_lossy().to_string());
    hook_env.insert("BUILD_PATH".to_string(), build_path.to_string_lossy().to_string());
    hook_env.insert("CONFIG_TYPE".to_string(), get_build_type(config, config_type));
    if let Some(v) = variant_name {
        hook_env.insert("VARIANT".to_string(), v.to_string());
    }

    // Run pre-configure hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.pre_configure, project_path, Some(hook_env.clone()))?;
    }

    // Determine cross-compilation configuration
    let cross_config = if let Some(target) = cross_target {
        if let Some(predefined) = get_predefined_cross_target(target) {
            Some(predefined)
        } else if let Some(config_cross) = &config.cross_compile {
            if config_cross.enabled && config_cross.target == target {
                Some(config_cross.clone())
            } else {
                None
            }
        } else {
            None
        }
    } else if let Some(config_cross) = &config.cross_compile {
        if config_cross.enabled {
            Some(config_cross.clone())
        } else {
            None
        }
    } else {
        None
    };

    // If cross-compiling, use a subdirectory for the build
    let build_path = if let Some(cross_config) = &cross_config {
        let target_build_dir = format!("{}-{}", build_dir, cross_config.target);
        let target_build_path = project_path.join(&target_build_dir);
        fs::create_dir_all(&target_build_path)?;
        target_build_path
    } else {
        fs::create_dir_all(&build_path)?;
        build_path
    };

    // Get CMake generator
    let generator = get_cmake_generator(config)?;

    // Get build type
    let build_type = get_build_type(config, config_type);

    // Set up dependencies
    let deps_result = install_dependencies(config, project_path, false)?;
    let vcpkg_toolchain = deps_result.get("vcpkg_toolchain").cloned().unwrap_or_default();
    let conan_cmake = deps_result.get("conan_cmake").cloned().unwrap_or_default();

    // Generate CMakeLists.txt
    generate_cmake_lists(config, project_path, variant_name)?;

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

    // Add configuration-specific options
    let config_options = get_config_specific_options(config, &build_type);
    cmd.extend(config_options);

    // Add variant-specific options if a variant is active
    if let Some(variant) = get_active_variant(config, variant_name) {
        apply_variant_settings(&mut cmd, variant);
    }

    // Add cross-compilation options
    let mut env_vars = None;
    if let Some(cross_config) = &cross_config {
        println!("{}", format!("Configuring for cross-compilation target: {}", cross_config.target).blue());

        // Get cross-compilation CMake options
        let cross_options = setup_cross_compilation(config, cross_config)?;
        cmd.extend(cross_options);

        // Get environment variables for cross-compilation
        let cross_env = get_cross_compilation_env(cross_config);
        if !cross_env.is_empty() {
            let mut all_env = cross_env;
            for (k, v) in hook_env {
                all_env.insert(k, v);
            }
            env_vars = Some(all_env);
        } else {
            env_vars = Some(hook_env);
        }
    } else {
        env_vars = Some(hook_env);
    }

    // Add custom CMake options
    if let Some(cmake_options) = &config.build.cmake_options {
        cmd.extend(cmake_options.clone());
    }

    // Run CMake configuration
    run_command(cmd, Some(&build_path.to_string_lossy().to_string()), env_vars.clone())?;

    // Run post-configure hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.post_configure, project_path, env_vars)?;
    }

    println!("{}", format!("Project configured with generator: {} ({})", generator, build_type).green());
    if let Some(variant_name) = variant_name {
        println!("{}", format!("Using build variant: {}", variant_name).green());
    }
    if let Some(cross_config) = &cross_config {
        println!("{}", format!("Cross-compilation target: {}", cross_config.target).green());
    }

    Ok(())
}

fn build_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    cross_target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = if let Some(target) = cross_target {
        project_path.join(format!("{}-{}", build_dir, target))
    } else {
        project_path.join(build_dir)
    };

    // Create a set of environment variables for hooks
    let mut hook_env = HashMap::new();
    hook_env.insert("PROJECT_PATH".to_string(), project_path.to_string_lossy().to_string());
    hook_env.insert("BUILD_PATH".to_string(), build_path.to_string_lossy().to_string());
    hook_env.insert("CONFIG_TYPE".to_string(), get_build_type(config, config_type));
    if let Some(v) = variant_name {
        hook_env.insert("VARIANT".to_string(), v.to_string());
    }
    if let Some(t) = cross_target {
        hook_env.insert("TARGET".to_string(), t.to_string());
    }

    if !build_path.join("CMakeCache.txt").exists() {
        println!("{}", "Project not configured yet, configuring...".blue());
        configure_project(config, project_path, config_type, variant_name, cross_target)?;
    }

    // Run pre-build hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.pre_build, project_path, Some(hook_env.clone()))?;
    }

    // Build using CMake
    let mut cmd = vec!["cmake".to_string(), "--build".to_string(), ".".to_string()];

    // Add configuration (Debug/Release, etc.)
    let build_type = get_build_type(config, config_type);
    cmd.push("--config".to_string());
    cmd.push(build_type.clone());

    // Run build
    run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None)?;

    // Run post-build hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.post_build, project_path, Some(hook_env))?;
    }

    println!("{}", format!("Build completed successfully ({} configuration)", build_type).green());
    Ok(())
}

fn clean_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    cross_target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = if let Some(target) = cross_target {
        project_path.join(format!("{}-{}", build_dir, target))
    } else {
        project_path.join(build_dir)
    };

    // Create a set of environment variables for hooks
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
        run_hooks(&hooks.pre_clean, project_path, Some(hook_env.clone()))?;
    }

    if build_path.exists() {
        println!("{}", format!("Removing build directory: {}", build_path.display()).blue());
        fs::remove_dir_all(&build_path)?;
        println!("{}", "Clean completed".green());
    } else {
        println!("{}", "Nothing to clean".blue());
    }

    // Run post-clean hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.post_clean, project_path, Some(hook_env))?;
    }

    Ok(())
}

fn run_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    args: &[String]
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);
    let project_name = &config.project.name;
    let build_type = get_build_type(config, config_type);
    let bin_dir = config.output.bin_dir.as_deref().unwrap_or(DEFAULT_BIN_DIR);

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path, config_type, variant_name, None)?;
    }

    // Generate all possible executable paths
    let mut executable_paths = Vec::new();

    // Path from build logs
    let bin_exe_path = build_path.join(bin_dir).join(format!("{}.exe", project_name));
    executable_paths.push(bin_exe_path.clone());

    // Path without .exe extension
    executable_paths.push(build_path.join(bin_dir).join(project_name));

    // Standard CMake output directories (VS, Xcode, etc.)
    executable_paths.push(build_path.join(project_name));
    executable_paths.push(build_path.join(format!("{}.exe", project_name)));
    executable_paths.push(build_path.join(&build_type).join(project_name));
    executable_paths.push(build_path.join(&build_type).join(format!("{}.exe", project_name)));

    // Check for target name other than project name (default)
    if config.targets.contains_key("default") && project_name != "default" {
        executable_paths.push(build_path.join(bin_dir).join("default.exe"));
        executable_paths.push(build_path.join(bin_dir).join("default"));
        executable_paths.push(build_path.join("default"));
        executable_paths.push(build_path.join("default.exe"));
        executable_paths.push(build_path.join(&build_type).join("default"));
        executable_paths.push(build_path.join(&build_type).join("default.exe"));
    }

    // Debug: List all checked paths
    println!("{}", "Searching for executable in:".blue());
    for path in &executable_paths {
        println!("  - {} {}", path.display(), if path.exists() { "✓".green() } else { "✗".red() });
    }

    // Find the first executable that exists
    let mut executable_path = None;
    for path in &executable_paths {
        if path.exists() {
            if is_executable(path) {
                executable_path = Some(path.clone());
                println!("{}", format!("Found executable: {}", path.display()).green());
                break;
            } else {
                println!("{}", format!("Found file but not executable: {}", path.display()).yellow());
            }
        }
    }

    // If still not found, look for any executable in bin_dir
    if executable_path.is_none() {
        let bin_path = build_path.join(bin_dir);
        if bin_path.exists() {
            println!("{}", format!("Searching for any executable in {}", bin_path.display()).blue());

            // Look for any .exe file in the bin directory
            if let Ok(entries) = fs::read_dir(&bin_path) {
                for entry in entries {
                    if let Ok(entry) = entry {
                        let path = entry.path();
                        if path.is_file() && path.extension().map_or(false, |ext| ext == "exe") {
                            executable_path = Some(path.clone());
                            println!("{}", format!("Found executable: {}", path.display()).green());
                            break;
                        }
                    }
                }
            }
        }
    }

    let executable = match executable_path {
        Some(path) => path,
        None => {
            // One last attempt - find any executable in the build directory
            let mut found = None;

            // Walk the build directory recursively
            fn find_executables(dir: &Path, found: &mut Option<PathBuf>) {
                if let Ok(entries) = fs::read_dir(dir) {
                    for entry in entries {
                        if let Ok(entry) = entry {
                            let path = entry.path();

                            if path.is_dir() {
                                find_executables(&path, found);
                            } else if path.is_file() &&
                                (path.extension().map_or(false, |ext| ext == "exe") || is_executable(&path)) {
                                *found = Some(path.clone());
                                return;
                            }
                        }
                    }
                }
            }

            find_executables(&build_path, &mut found);

            match found {
                Some(path) => {
                    println!("{}", format!("Found executable by recursive search: {}", path.display()).green());
                    path
                },
                None => return Err(format!("Executable not found. Make sure the project is built successfully.").into())
            }
        }
    };

    // Create a set of environment variables for hooks
    let mut hook_env = HashMap::new();
    hook_env.insert("PROJECT_PATH".to_string(), project_path.to_string_lossy().to_string());
    hook_env.insert("BUILD_PATH".to_string(), build_path.to_string_lossy().to_string());
    hook_env.insert("CONFIG_TYPE".to_string(), build_type);
    hook_env.insert("EXECUTABLE".to_string(), executable.to_string_lossy().to_string());
    if let Some(v) = variant_name {
        hook_env.insert("VARIANT".to_string(), v.to_string());
    }

    // Run pre-run hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.pre_run, project_path, Some(hook_env.clone()))?;
    }

    println!("{}", format!("Running: {}", executable.display()).blue());

    // Create command with passed arguments
    let mut command = Command::new(&executable);
    command.current_dir(project_path);
    if !args.is_empty() {
        command.args(args);
        println!("{}", format!("Arguments: {}", args.join(" ")).blue());
    }

    let status = command.status()?;

    if !status.success() {
        println!("{}", format!("Program exited with code {}", status.code().unwrap_or(-1)).yellow());
    } else {
        println!("{}", "Program executed successfully".green());
    }

    // Run post-run hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.post_run, project_path, Some(hook_env))?;
    }

    Ok(())
}

fn test_project(
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
        build_project(config, project_path, config_type, variant_name, None)?;
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

fn install_project(
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
        build_project(config, project_path, config_type, None, None)?;
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

fn run_script(config: &ProjectConfig, script_name: &str, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    if let Some(scripts) = &config.scripts {
        if let Some(script_command) = scripts.scripts.get(script_name) {
            println!("{}", format!("Running script: {}", script_name).blue());

            // Create command
            let shell = if cfg!(target_os = "windows") { "cmd" } else { "sh" };
            let shell_arg = if cfg!(target_os = "windows") { "/C" } else { "-c" };

            let mut command = Command::new(shell);
            command.arg(shell_arg).arg(script_command);
            command.current_dir(project_path);

            // Execute the command
            let output = command.output()?;

            // Print output
            let stdout = String::from_utf8_lossy(&output.stdout);
            let stderr = String::from_utf8_lossy(&output.stderr);

            if !stdout.is_empty() {
                println!("{}", stdout);
            }

            if !output.status.success() {
                println!("{}", format!("Script '{}' failed:", script_name).red());
                if !stderr.is_empty() {
                    eprintln!("{}", stderr);
                }

                return Err(format!("Script failed with exit code: {}", output.status).into());
            }

            println!("{}", format!("Script '{}' completed successfully.", script_name).green());
            return Ok(());
        } else {
            return Err(format!("Script '{}' not found in configuration.", script_name).into());
        }
    }

    Err(format!("No scripts defined in configuration.").into())
}

fn generate_ide_files(config: &ProjectConfig, project_path: &Path, ide_type: &str) -> Result<(), Box<dyn std::error::Error>> {
    match ide_type {
        "vscode" => generate_vscode_files(config, project_path)?,
        "clion" => {
            // CLion doesn't need special files, just CMake project
            configure_project(config, project_path, None, None, None)?;
            println!("{}", "Project is ready for CLion. Open the directory in CLion.".green());
        },
        "xcode" => {
            // Generate Xcode project using CMake
            if cfg!(target_os = "macos") {
                let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
                let xcode_dir = format!("{}-xcode", build_dir);
                let xcode_path = project_path.join(&xcode_dir);
                fs::create_dir_all(&xcode_path)?;

                let mut cmd = vec![
                    "cmake".to_string(),
                    "..".to_string(),
                    "-G".to_string(),
                    "Xcode".to_string(),
                ];

                run_command(cmd, Some(&xcode_path.to_string_lossy().to_string()), None)?;
                println!("{}", format!("Xcode project generated in {}", xcode_dir).green());
            } else {
                return Err("Xcode is only available on macOS".into());
            }
        },
        "vs" => {
            // Generate Visual Studio project using CMake
            if cfg!(target_os = "windows") {
                let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
                let vs_dir = format!("{}-vs", build_dir);
                let vs_path = project_path.join(&vs_dir);
                fs::create_dir_all(&vs_path)?;

                let mut cmd = vec![
                    "cmake".to_string(),
                    "..".to_string(),
                    "-G".to_string(),
                ];

                // Determine Visual Studio version
                let vs_version = if Command::new("msbuild").arg("/?").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
                    if let Ok(output) = Command::new("powershell")
                        .arg("-Command")
                        .arg("(Get-ItemProperty -Path 'HKLM:\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7' -Name '17.0' -ErrorAction SilentlyContinue).'17.0'")
                        .output()
                    {
                        if !output.stdout.is_empty() {
                            "Visual Studio 17 2022".to_string()
                        } else if let Ok(output) = Command::new("powershell")
                            .arg("-Command")
                            .arg("(Get-ItemProperty -Path 'HKLM:\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7' -Name '16.0' -ErrorAction SilentlyContinue).'16.0'")
                            .output()
                        {
                            if !output.stdout.is_empty() {
                                "Visual Studio 16 2019".to_string()
                            } else {
                                "Visual Studio 15 2017".to_string()
                            }
                        } else {
                            "Visual Studio 15 2017".to_string()
                        }
                    } else {
                        "Visual Studio 15 2017".to_string()
                    }
                } else {
                    "Visual Studio 15 2017".to_string()
                };

                cmd.push(vs_version.clone());

                run_command(cmd, Some(&vs_path.to_string_lossy().to_string()), None)?;
                println!("{}", format!("{} project generated in {}", vs_version, vs_dir).green());
            } else {
                return Err("Visual Studio is only available on Windows".into());
            }
        },
        _ => {
            return Err(format!("Unsupported IDE type: {}. Supported types are: vscode, clion, xcode, vs", ide_type).into());
        }
    }

    Ok(())
}

fn generate_vscode_files(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    // Create .vscode directory
    let vscode_dir = project_path.join(".vscode");
    fs::create_dir_all(&vscode_dir)?;

    // Create c_cpp_properties.json
    let mut intellisense_defines = Vec::new();
    let mut include_paths = Vec::<String>::new();  // <-- Change to Vec<String>

    // Add standard include paths
    include_paths.push("${workspaceFolder}/**".to_string());  // <-- Add .to_string()

    // Add include paths from targets
    for (_, target) in &config.targets {
        if let Some(includes) = &target.include_dirs {
            for include in includes {
                include_paths.push(format!("${{workspaceFolder}}/{}", include));
            }
        }

        if let Some(defines) = &target.defines {
            intellisense_defines.extend(defines.clone());
        }
    }

    // Add platform-specific defines
    let current_os = if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "darwin"
    } else {
        "linux"
    };

    if let Some(platforms) = &config.platforms {
        if let Some(platform) = platforms.get(current_os) {
            if let Some(defines) = &platform.defines {
                intellisense_defines.extend(defines.clone());
            }
        }
    }

    // Create properties object
    let mut properties = serde_json::json!({
        "configurations": [
            {
                "name": "CBuild",
                "includePath": include_paths,
                "defines": intellisense_defines,
                "compilerPath": "/usr/bin/g++",
                "cStandard": "c11",
                "cppStandard": "c++17",
                "intelliSenseMode": "gcc-x64"
            }
        ],
        "version": 4
    });

    // Adjust compiler path and IntelliSense mode based on platform
    if let Some(platforms) = &config.platforms {
        if let Some(platform) = platforms.get(current_os) {
            if let Some(compiler) = &platform.compiler {
                let (compiler_path, intellisense_mode) = match (current_os, compiler.as_str()) {
                    ("windows", "msvc") => ("C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/MSVC/14.29.30133/bin/Hostx64/x64/cl.exe", "msvc-x64"),
                    ("windows", "gcc") => ("C:/msys64/mingw64/bin/g++.exe", "gcc-x64"),
                    ("windows", "clang") => ("C:/Program Files/LLVM/bin/clang++.exe", "clang-x64"),
                    ("darwin", "clang") => ("/usr/bin/clang++", "clang-x64"),
                    ("linux", "gcc") => ("/usr/bin/g++", "gcc-x64"),
                    ("linux", "clang") => ("/usr/bin/clang++", "clang-x64"),
                    _ => ("/usr/bin/g++", "gcc-x64")
                };

                if let Some(conf) = properties["configurations"].as_array_mut().and_then(|a| a.get_mut(0)) {
                    conf["compilerPath"] = serde_json::json!(compiler_path);
                    conf["intelliSenseMode"] = serde_json::json!(intellisense_mode);
                }
            }
        }
    }

    // Write c_cpp_properties.json
    let properties_path = vscode_dir.join("c_cpp_properties.json");
    fs::write(&properties_path, serde_json::to_string_pretty(&properties)?)?;

    // Create launch.json for debugging
    let launch = serde_json::json!({
        "version": "0.2.0",
        "configurations": [
            {
                "name": "Debug",
                "type": "cppdbg",
                "request": "launch",
                "program": "${workspaceFolder}/build/bin/${workspaceFolderBasename}",
                "args": [],
                "stopAtEntry": false,
                "cwd": "${workspaceFolder}",
                "environment": [],
                "externalConsole": false,
                "MIMode": "gdb",
                "setupCommands": [
                    {
                        "description": "Enable pretty-printing for gdb",
                        "text": "-enable-pretty-printing",
                        "ignoreFailures": true
                    }
                ],
                "preLaunchTask": "build"
            }
        ]
    });

    // Write launch.json
    let launch_path = vscode_dir.join("launch.json");
    fs::write(&launch_path, serde_json::to_string_pretty(&launch)?)?;

    // Create tasks.json for building
    let tasks = serde_json::json!({
        "version": "2.0.0",
        "tasks": [
            {
                "label": "build",
                "type": "shell",
                "command": "cbuild",
                "args": ["build"],
                "group": {
                    "kind": "build",
                    "isDefault": true
                },
                "problemMatcher": ["$gcc"]
            },
            {
                "label": "clean",
                "type": "shell",
                "command": "cbuild",
                "args": ["clean"],
                "problemMatcher": []
            },
            {
                "label": "run",
                "type": "shell",
                "command": "cbuild",
                "args": ["run"],
                "problemMatcher": []
            },
            {
                "label": "test",
                "type": "shell",
                "command": "cbuild",
                "args": ["test"],
                "problemMatcher": ["$gcc"]
            }
        ]
    });

    // Write tasks.json
    let tasks_path = vscode_dir.join("tasks.json");
    fs::write(&tasks_path, serde_json::to_string_pretty(&tasks)?)?;

    println!("{}", "VS Code configuration files generated".green());
    Ok(())
}

fn generate_vscode_workspace(config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    // Create workspace file (.code-workspace)
    let workspace_file = PathBuf::from(format!("{}.code-workspace", config.workspace.name));

    // Build list of folders
    let mut folders = Vec::new();
    for project in &config.projects {
        folders.push(serde_json::json!({
            "path": project
        }));
    }

    // Create workspace object
    let workspace_content = serde_json::json!({
        "folders": folders,
        "settings": {
            "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
            "cmake.configureOnOpen": true
        },
        "extensions": {
            "recommendations": [
                "ms-vscode.cpptools",
                "ms-vscode.cmake-tools",
                "twxs.cmake"
            ]
        }
    });

    // Write workspace file
    fs::write(&workspace_file, serde_json::to_string_pretty(&workspace_content)?)?;

    println!("{}", format!("VS Code workspace file generated: {}", workspace_file.display()).green());
    Ok(())
}

fn generate_clion_workspace(config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "CLion workspace doesn't require special files.".green());
    println!("{}", "Open the workspace directory in CLion and it will detect all CMake projects.".green());
    Ok(())
}

fn package_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    package_type: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path, config_type, None, None)?;
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

// List available configurations, variants, etc.
fn list_project_items(config: &ProjectConfig, what: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
    let item_type = what.unwrap_or("all");

    match item_type {
        "configs" | "all" => {
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

            if item_type == "all" {
                println!();
            }
        },
        "variants" | "all" => {
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

            if item_type == "all" {
                println!();
            }
        },
        "targets" | "all" => {
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

            if item_type == "all" {
                println!();
            }
        },
        "scripts" | "all" => {
            println!("{}", "Available scripts:".bold());

            if let Some(scripts) = &config.scripts {
                for (name, cmd) in &scripts.scripts {
                    println!(" - {}: {}", name.green(), cmd);
                }
            } else {
                println!(" - No custom scripts defined");
            }
        },
        _ => {
            return Err(format!("Unknown item type: {}. Valid types are: configs, variants, targets, scripts, all", item_type).into());
        }
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