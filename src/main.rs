use lazy_static::lazy_static;
use regex::Regex;
use std::collections::VecDeque;
use clap::{Parser, Subcommand};
use colored::*;
use regex;
use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    collections::HashSet,
    env,
    sync::Mutex,
    fmt,
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

lazy_static! {
    static ref EXECUTED_COMMANDS: Mutex<HashSet<String>> = Mutex::new(HashSet::new());
    static ref VERIFIED_TOOLS: Mutex<HashSet<String>> = Mutex::new(HashSet::new());
    static ref INSTALLED_PACKAGES: Mutex<HashSet<String>> = Mutex::new(HashSet::new());
    static ref CACHED_PATHS: Mutex<HashMap<String, String>> = Mutex::new(HashMap::new());
}

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

    #[clap(name = "startup")]
    Startup {
        /// Set a project as the default startup project
        #[clap(name = "project")]
        project: Option<String>,

        /// List all available startup projects
        #[clap(long)]
        list: bool,
    },

    /// Generate IDE project files
    #[clap(name = "ide")]
    Ide {
        /// IDE type (vscode, clion, vs, vs2022, vs2019, vs2017, etc.)
        #[clap(name = "type")]
        ide_type: String,

        /// Generate for specific project in a workspace
        #[clap(name = "project")]
        project: Option<String>,

        /// Target architecture (x64, Win32, ARM64, etc.) for Visual Studio
        #[clap(long)]
        arch: Option<String>,
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
    workspace: WorkspaceWithProjects,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct WorkspaceWithProjects {
    name: String,
    projects: Vec<String>,
    startup_projects: Option<Vec<String>>, // Projects that can be set as startup
    default_startup_project: Option<String>, // Default startup project
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
struct PCHConfig {
    enabled: bool,
    header: String,
    source: Option<String>,
    include_directories: Option<Vec<String>>,
    compiler_options: Option<Vec<String>>,  // Additional compiler options for PCH
    only_for_targets: Option<Vec<String>>,  // Apply PCH only to specific targets
    exclude_sources: Option<Vec<String>>,   // Sources to exclude from PCH
    disable_unity_build: Option<bool>,      // Disable unity build when using PCH (can cause conflicts)
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct ProjectConfig {
    project: ProjectInfo,
    build: BuildConfig,
    #[serde(default)]
    dependencies: DependenciesConfig,
    #[serde(default)]
    targets: HashMap<String, TargetConfig>,
    #[serde(default)]
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
    #[serde(default)]
    pch: Option<PCHConfig>,
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
    compiler: Option<String>,
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
    #[serde(default)]
    vcpkg: VcpkgConfig,
    system: Option<Vec<String>>,
    cmake: Option<Vec<String>>,
    #[serde(default)]
    conan: ConanConfig,       // Conan package manager support
    #[serde(default)]
    custom: Vec<CustomDependency>, // Custom dependencies
    #[serde(default)]
    git: Vec<GitDependency>,  // Git dependencies
    #[serde(default)]
    workspace: Vec<WorkspaceDependency>, // Dependencies on other workspace projects
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct WorkspaceDependency {
    name: String,             // Name of the project in workspace
    link_type: Option<String>, // "static", "shared", or "interface" (default: depends on target type)
    include_paths: Option<Vec<String>>, // Additional include paths relative to the dependency
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
    platform_links: Option<HashMap<String, Vec<String>>>, // Platform-specific links
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

#[derive(Debug, Clone)]
struct CompilerDiagnostic {
    file: String,
    line: usize,
    column: usize,
    level: String,    // "error", "warning", "note", etc.
    message: String,
    context: Vec<String>,
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

#[derive(Debug)]
pub struct CBuildError {
    message: String,
    file_path: Option<String>,
    line_number: Option<usize>,
    context: Option<String>,
}

impl CBuildError {
    pub fn new(message: &str) -> Self {
        CBuildError {
            message: message.to_string(),
            file_path: None,
            line_number: None,
            context: None,
        }
    }

    pub fn with_file(mut self, file_path: &str) -> Self {
        self.file_path = Some(file_path.to_string());
        self
    }

    pub fn with_line(mut self, line_number: usize) -> Self {
        self.line_number = Some(line_number);
        self
    }

    pub fn with_context(mut self, context: &str) -> Self {
        self.context = Some(context.to_string());
        self
    }
}

impl fmt::Display for CBuildError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "CBuild Error: {}", self.message)?;

        if let Some(file) = &self.file_path {
            write!(f, " in file '{}'", file)?;
        }

        if let Some(line) = self.line_number {
            write!(f, " at line {}", line)?;
        }

        if let Some(context) = &self.context {
            write!(f, "\nContext: {}", context)?;
        }

        Ok(())
    }
}

impl std::error::Error for CBuildError {}

// Now add this function to convert TOML errors to your custom error type
fn parse_toml_error(err: toml::de::Error, file_path: &str, file_content: &str) -> CBuildError {
    let message = err.to_string();

    // Try to extract line number from the error message
    let line_number = if let Some(span) = err.span() {
        // If we have a span, we can calculate the line number


        let content_up_to_error = &file_content[..span.start];
        Some(content_up_to_error.lines().count())
    } else {
        // Try to parse line number from error message (varies by TOML parser)
        message.lines()
            .find_map(|line| {
                if line.contains("line") {
                    line.split_whitespace()
                        .find_map(|word| {
                            if word.chars().all(|c| c.is_digit(10)) {
                                word.parse::<usize>().ok()
                            } else {
                                None
                            }
                        })
                } else {
                    None
                }
            })
    };

    // Extract context - the line with the error and a few lines before/after
    let context = if let Some(line_num) = line_number {
        let lines: Vec<&str> = file_content.lines().collect();
        let start = line_num.saturating_sub(2);
        let end = std::cmp::min(line_num + 2, lines.len());

        let mut result = String::new();
        for i in start..end {
            let line_prefix = if i == line_num - 1 { " -> " } else { "    " };
            if i < lines.len() {
                result.push_str(&format!("{}{}: {}\n", line_prefix, i + 1, lines[i]));
            }
        }
        Some(result)
    } else {
        None
    };

    // Find specific value that caused the error
    let problem_value = if let Some(span) = err.span() {
        if span.start < file_content.len() && span.end <= file_content.len() {
            Some(file_content[span.start..span.end].to_string())
        } else {
            None
        }
    } else {
        None
    };

    // Create detailed error message
    let detailed_message = if let Some(value) = problem_value {
        format!("{}\nProblem with value: '{}'", message, value)
    } else {
        message
    };

    let mut error = CBuildError::new(&detailed_message).with_file(file_path);

    if let Some(line) = line_number {
        error = error.with_line(line);
    }

    if let Some(ctx) = context {
        error = error.with_context(&ctx);
    }

    error
}
impl Default for DependenciesConfig {
    fn default() -> Self {
        DependenciesConfig {
            vcpkg: VcpkgConfig::default(),
            system: None,
            cmake: None,
            conan: ConanConfig::default(),
            custom: Vec::new(),
            git: Vec::new(),
            workspace: Vec::new(),
        }
    }
}

impl Default for VcpkgConfig {
    fn default() -> Self {
        VcpkgConfig {
            enabled: false,
            path: None,
            packages: Vec::new(),
        }
    }
}

fn run_command_once(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<HashMap<String, String>>,
    cache_key: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    // If a cache key is provided, check if we've already run this command
    if let Some(key) = cache_key {
        let mut cache = EXECUTED_COMMANDS.lock().unwrap();
        if cache.contains(key) {
            println!("{}", format!("Skipping already executed command: {}", key).blue());
            return Ok(());
        }
        cache.insert(key.to_string());
    }

    // Run the command
    run_command(cmd, cwd, env)
}

fn run_command_raw(command: &Commands) -> Result<(), Box<dyn std::error::Error>> {
    match command {
        Commands::Init { workspace, template } => {
            if *workspace {
                init_workspace()?;
            } else {
                init_project(None, template.as_deref())?;
            }
            Ok(())
        }
        Commands::Build { project, config, variant, target } => {
            if is_workspace() {
                // Use dependency-order aware build function
                build_workspace_with_dependency_order(project.clone(), config.as_deref(), variant.as_deref(), target.as_deref())?;
            } else {
                let mut proj_config = load_project_config(None)?;
                auto_adjust_config(&mut proj_config)?;
                build_project(&proj_config, &PathBuf::from("."), config.as_deref(), variant.as_deref(), target.as_deref(), None)?;
            }
            Ok(())
        },
        Commands::Clean { project, config, target } => {
            if is_workspace() {
                clean_workspace(project.clone(), config.as_deref(), target.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                clean_project(&proj_config, &PathBuf::from("."), config.as_deref(), target.as_deref())?;
            }
            Ok(())
        },
        Commands::Startup { project, list } => {
            if !is_workspace() {
                return Err("This command is only available in a workspace".into());
            }

            let mut workspace_config = load_workspace_config()?;

            if *list {
                list_startup_projects(&workspace_config)?;
            } else if let Some(proj) = project {
                set_startup_project(&mut workspace_config, &proj)?;
            } else {
                // If neither list nor project specified, show current startup project
                show_current_startup(&workspace_config)?;
            }
            Ok(())
        },
        Commands::Run { project, config, variant, args } => {
            if is_workspace() {
                run_workspace(project.clone(), config.as_deref(), variant.as_deref(), &args)?;
            } else {
                let proj_config = load_project_config(None)?;
                run_project(&proj_config, &PathBuf::from("."), config.as_deref(), variant.as_deref(), &args, None)?;
            }
            Ok(())
        }
        Commands::Test { project, config, variant, filter } => {
            if is_workspace() {
                test_workspace(project.clone(), config.as_deref(), variant.as_deref(), filter.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                test_project(&proj_config, &PathBuf::from("."), config.as_deref(), variant.as_deref(), filter.as_deref())?;
            }
            Ok(())
        }
        Commands::Install { project, config, prefix } => {
            if is_workspace() {
                install_workspace(project.clone(), config.as_deref(), prefix.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                install_project(&proj_config, &PathBuf::from("."), config.as_deref(), prefix.as_deref())?;
            }
            Ok(())
        }
        Commands::Deps { project, update } => {
            if is_workspace() {
                install_workspace_deps(project.clone(), *update)?;
            } else {
                let proj_config = load_project_config(None)?;
                install_dependencies(&proj_config, &PathBuf::from("."), *update)?;
            }
            Ok(())
        }
        Commands::Script { name, project } => {
            if is_workspace() {
                run_workspace_script(name.clone(), project.clone())?;
            } else {
                let proj_config = load_project_config(None)?;
                run_script(&proj_config, &name, &PathBuf::from("."))?;
            }
            Ok(())
        }
        Commands::Ide { ide_type, project, arch } => {
            // Format ide_type to include architecture if provided for VS
            let full_ide_type = match (ide_type.as_str(), arch) {
                (t, Some(a)) if t.starts_with("vs") => format!("{}:{}", t, a),
                _ => ide_type.to_string(),
            };

            if is_workspace() {
                generate_workspace_ide_files(full_ide_type, project.clone())?;
            } else {
                let proj_config = load_project_config(None)?;
                generate_ide_files(&proj_config, &PathBuf::from("."), &full_ide_type)?;
            }
            Ok(())
        }
        Commands::Package { project, config, type_ } => {
            if is_workspace() {
                package_workspace(project.clone(), config.as_deref(), type_.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                package_project(&proj_config, &PathBuf::from("."), config.as_deref(), type_.as_deref())?;
            }
            Ok(())
        }
        Commands::List { what } => {
            if is_workspace() {
                list_workspace_items(what.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                list_project_items(&proj_config, what.as_deref())?;
            }
            Ok(())
        }
    }
}
// Main implementation
fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    match run_command_raw(&cli.command) {
        Ok(()) => {
           Ok(())
        },
        Err(e) => {
            println!("{}", "Error:".red().bold());

            // Special formatting for CBuildError
            if let Some(cbuild_err) = e.downcast_ref::<CBuildError>() {
                println!("{}", cbuild_err.to_string().red());
            } else {
                // Regular error
                println!("{}", e.to_string().red());
            }

            std::process::exit(1);
        }
    }
}

// Check if current directory is a workspace
fn is_workspace() -> bool {
    Path::new(WORKSPACE_FILE).exists()
}

fn list_startup_projects(workspace_config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "Available startup projects:".bold());

    let default_startup = workspace_config.workspace.default_startup_project.as_deref();

    if let Some(startup_projects) = &workspace_config.workspace.startup_projects {
        for project in startup_projects {
            if Some(project.as_str()) == default_startup {
                println!(" * {} (default)", project.green());
            } else {
                println!(" - {}", project.green());
            }
        }
    } else {
        // If no specific startup projects, list all projects
        for project in &workspace_config.workspace.projects {
            if Some(project.as_str()) == default_startup {
                println!(" * {} (default)", project.green());
            } else {
                println!(" - {}", project.green());
            }
        }
    }

    Ok(())
}

fn set_startup_project(workspace_config: &mut WorkspaceConfig, project: &str) -> Result<(), Box<dyn std::error::Error>> {
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

    println!("{}", format!("Project '{}' set as default startup project", project).green());
    Ok(())
}

fn add_pch_support(
    cmake_content: &mut Vec<String>,
    config: &ProjectConfig,
    pch_config: &PCHConfig
) {
    if !pch_config.enabled {
        return;
    }

    cmake_content.push("# Precompiled header support".to_string());
    // Define PCH variables
    cmake_content.push(format!("set(PCH_HEADER \"{}\")", pch_config.header));

    if let Some(source) = &pch_config.source {
        cmake_content.push(format!("set(PCH_SOURCE \"{}\")", source));
    }

    // Add exclude patterns if specified
    if let Some(excludes) = &pch_config.exclude_sources {
        cmake_content.push("set(PCH_EXCLUDE_SOURCES".to_string());
        for exclude in excludes {
            cmake_content.push(format!("  \"{}\"", exclude));
        }
        cmake_content.push(")".to_string());
    }

    // Unity build option
    if let Some(disable_unity) = pch_config.disable_unity_build {
        if disable_unity {
            cmake_content.push("set(CMAKE_UNITY_BUILD OFF)".to_string());
        }
    }

    cmake_content.push("# PCH function for targets".to_string());
    cmake_content.push("function(target_enable_pch target_name)".to_string());

    // Check if target is in only_for_targets list
    if let Some(only_targets) = &pch_config.only_for_targets {
        cmake_content.push("  # Check if target is in the allowed targets list".to_string());
        cmake_content.push("  set(_apply_pch FALSE)".to_string());
        cmake_content.push("  foreach(_target IN ITEMS".to_string());
        for target in only_targets {
            cmake_content.push(format!("    \"{}\"", target));
        }
        cmake_content.push("  )".to_string());
        cmake_content.push("    if(\"${target_name}\" STREQUAL \"${_target}\")".to_string());
        cmake_content.push("      set(_apply_pch TRUE)".to_string());
        cmake_content.push("    endif()".to_string());
        cmake_content.push("  endforeach()".to_string());
        cmake_content.push("  if(NOT _apply_pch)".to_string());
        cmake_content.push("    message(STATUS \"Skipping PCH for ${target_name} (not in target list)\")".to_string());
        cmake_content.push("    return()".to_string());
        cmake_content.push("  endif()".to_string());
    }

    // Modern CMake PCH approach (CMake 3.16+)
    cmake_content.push("  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)".to_string());

    // If we have both header and source (for MSVC style)
    if pch_config.source.is_some() {
        cmake_content.push("    if(MSVC)".to_string());
        cmake_content.push("      target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());

        // Add exclude patterns if necessary
        if pch_config.exclude_sources.is_some() {
            cmake_content.push("      if(PCH_EXCLUDE_SOURCES)".to_string());
            cmake_content.push("        set_source_files_properties(${PCH_EXCLUDE_SOURCES}".to_string());
            cmake_content.push("          PROPERTIES SKIP_PRECOMPILE_HEADERS ON)".to_string());
            cmake_content.push("      endif()".to_string());
        }

        cmake_content.push("    else()".to_string());
        cmake_content.push("      target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());

        // Add exclude patterns for non-MSVC as well
        if pch_config.exclude_sources.is_some() {
            cmake_content.push("      if(PCH_EXCLUDE_SOURCES)".to_string());
            cmake_content.push("        set_source_files_properties(${PCH_EXCLUDE_SOURCES}".to_string());
            cmake_content.push("          PROPERTIES SKIP_PRECOMPILE_HEADERS ON)".to_string());
            cmake_content.push("      endif()".to_string());
        }

        cmake_content.push("    endif()".to_string());
    } else {
        // Header-only PCH
        cmake_content.push("    target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());

        // Add exclude patterns
        if pch_config.exclude_sources.is_some() {
            cmake_content.push("    if(PCH_EXCLUDE_SOURCES)".to_string());
            cmake_content.push("      set_source_files_properties(${PCH_EXCLUDE_SOURCES}".to_string());
            cmake_content.push("        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)".to_string());
            cmake_content.push("    endif()".to_string());
        }
    }

    // Add any additional compiler options
    if let Some(options) = &pch_config.compiler_options {
        if !options.is_empty() {
            cmake_content.push("    # Add additional compiler options for PCH".to_string());

            let opts_string = options.iter()
                .map(|opt| format!("      \"{}\"", opt))
                .collect::<Vec<_>>()
                .join("\n");

            cmake_content.push("    target_compile_options(${target_name} PRIVATE".to_string());
            cmake_content.push(opts_string);
            cmake_content.push("    )".to_string());
        }
    }

    // Fallback for older CMake versions
    cmake_content.push("  else()".to_string());
    cmake_content.push("    # Fallback for older CMake - compiler-specific flags".to_string());
    cmake_content.push("    if(MSVC)".to_string());
    cmake_content.push("      get_filename_component(PCH_NAME ${PCH_HEADER} NAME_WE)".to_string());
    cmake_content.push("      set(PCH_OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/${PCH_NAME}.pch\")".to_string());
    cmake_content.push("      # Set compiler flags for using PCH".to_string());
    cmake_content.push("      target_compile_options(${target_name} PRIVATE /Yu\"${PCH_HEADER}\" /Fp\"${PCH_OUTPUT}\")".to_string());

    // Handle separate source file for creating the PCH
    if let Some(_) = &pch_config.source {
        cmake_content.push("      # Set PCH creation flag on the source file that will create the PCH".to_string());
        cmake_content.push("      set_source_files_properties(${PCH_SOURCE} PROPERTIES COMPILE_FLAGS \"/Yc\\\"${PCH_HEADER}\\\" /Fp\\\"${PCH_OUTPUT}\\\"\")".to_string());
    }

    // Add excluded files for MSVC
    if pch_config.exclude_sources.is_some() {
        cmake_content.push("      # Exclude specified files from using PCH".to_string());
        cmake_content.push("      if(PCH_EXCLUDE_SOURCES)".to_string());
        cmake_content.push("        foreach(_src ${PCH_EXCLUDE_SOURCES})".to_string());
        cmake_content.push("          set_source_files_properties(${_src} PROPERTIES COMPILE_FLAGS \"/Y-\")".to_string());
        cmake_content.push("        endforeach()".to_string());
        cmake_content.push("      endif()".to_string());
    }

    // GCC/Clang fallback for older CMake
    cmake_content.push("    elseif(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES \"Clang\")".to_string());
    cmake_content.push("      # GCC/Clang PCH for older CMake requires custom command setup".to_string());
    cmake_content.push("      message(STATUS \"Using manual PCH setup for GCC/Clang with CMake < 3.16\")".to_string());

    // Attempt a basic setup for GCC in older CMake
    cmake_content.push("      if(CMAKE_COMPILER_IS_GNUCXX)".to_string());
    cmake_content.push("        # Create a precompiled header from the given header file".to_string());
    cmake_content.push("        get_filename_component(PCH_NAME ${PCH_HEADER} NAME)".to_string());
    cmake_content.push("        get_filename_component(PCH_DIR ${PCH_HEADER} DIRECTORY)".to_string());
    cmake_content.push("        set(PCH_BINARY \"${CMAKE_CURRENT_BINARY_DIR}/${PCH_NAME}.gch\")".to_string());

    // Add compile command for PCH
    cmake_content.push("        add_custom_command(OUTPUT ${PCH_BINARY}".to_string());
    cmake_content.push("          COMMAND ${CMAKE_CXX_COMPILER} -x c++-header ${CMAKE_CXX_FLAGS} -o ${PCH_BINARY} ${PCH_HEADER}".to_string());
    cmake_content.push("          DEPENDS ${PCH_HEADER})".to_string());

    // Add custom target for the PCH
    cmake_content.push("        add_custom_target(${target_name}_pch DEPENDS ${PCH_BINARY})".to_string());
    cmake_content.push("        add_dependencies(${target_name} ${target_name}_pch)".to_string());

    // Add include directory if needed
    cmake_content.push("        target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})".to_string());

    // Add compiler flags for using the PCH
    cmake_content.push("        target_compile_options(${target_name} PRIVATE -include ${PCH_NAME})".to_string());
    cmake_content.push("      endif()".to_string());

    cmake_content.push("    endif()".to_string());
    cmake_content.push("  endif()".to_string());
    cmake_content.push("endfunction()".to_string());

    cmake_content.push(String::new());
}

fn show_current_startup(workspace_config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    if let Some(default_startup) = &workspace_config.workspace.default_startup_project {
        println!("{}", format!("Current default startup project: {}", default_startup).green());
    } else {
        println!("{}", "No default startup project set. The first project will be used.".yellow());
        if !workspace_config.workspace.projects.is_empty() {
            println!("{}", format!("First project is: {}", workspace_config.workspace.projects[0]).blue());
        }
    }

    Ok(())
}

fn resolve_workspace_dependencies(
    config: &ProjectConfig,
    workspace_config: Option<&WorkspaceConfig>,
    project_path: &Path,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut cmake_options = Vec::new();

    if workspace_config.is_none() || config.dependencies.workspace.is_empty() {
        return Ok(cmake_options);
    }

    let workspace = workspace_config.unwrap();
    println!("{}", "Resolving workspace dependencies...".blue());

    for dep in &config.dependencies.workspace {
        if !workspace.workspace.projects.contains(&dep.name) {
            println!("{}", format!("Warning: Workspace dependency '{}' not found in workspace", dep.name).yellow());
            continue;
        }

        println!("{}", format!("Processing dependency: {}", dep.name).blue());

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
                println!("{}", format!("Warning: Could not load config for dependency '{}': {}", dep.name, e).yellow());
                continue;
            }
        };

        // Check if the dependency has been built
        let dep_build_dir = dep_config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
        let dep_build_path = dep_path.join(dep_build_dir);

        if !dep_build_path.exists() {
            println!("{}", format!("Dependency '{}' has not been built yet. Building it now...", dep.name).yellow());

            // Try to build the dependency
            let mut dep_conf = dep_config.clone();
            auto_adjust_config(&mut dep_conf)?;
            if let Err(e) = build_project(&dep_conf, &dep_path, None, None, None, Some(workspace)) {
                println!("{}", format!("Warning: Failed to build dependency '{}': {}", dep.name, e).red());
                println!("{}", "Continuing with dependency resolution anyway, but linking might fail.".yellow());
            }
        }

        // Ensure the package config is generated
        if let Err(e) = generate_package_config(&dep_path, &dep.name) {
            println!("{}", format!("Warning: Failed to generate package config for '{}': {}", dep.name, e).yellow());
            println!("{}", "Continuing with dependency resolution anyway, but linking might fail.".yellow());
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

        println!("{}", format!("Searching for library files for '{}' in: {}", dep.name, lib_path.display()).blue());

        // Use enhanced library finding function
        let found_libraries = find_library_files(&lib_path, &dep.name, is_shared, is_msvc_style);

        if !found_libraries.is_empty() {
            for (lib_file, filename) in &found_libraries {
                println!("{}", format!("Found library: {} ({})", lib_file.display(), filename).green());

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
            // If no libraries found, try to search the entire project directory
            println!("{}", format!("No libraries found in standard locations for '{}', performing deep search...", dep.name).yellow());

            let found_libraries = find_library_files(&dep_path, &dep.name, is_shared, is_msvc_style);

            if !found_libraries.is_empty() {
                for (lib_file, filename) in &found_libraries {
                    println!("{}", format!("Found library: {} ({})", lib_file.display(), filename).green());

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
                println!("{}", format!("Warning: No library files found for '{}'. Linking may fail.", dep.name).yellow());

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

    println!("{}", "Workspace dependency resolution completed.".green());
    Ok(cmake_options)
}

fn find_library_files(base_path: &Path, project_name: &str, is_shared: bool, is_msvc_style: bool) -> Vec<(PathBuf, String)> {
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

fn load_workspace_config() -> Result<WorkspaceConfig, CBuildError> {
    let workspace_path = Path::new(WORKSPACE_FILE);

    if !workspace_path.exists() {
        return Err(CBuildError::new(&format!(
            "Workspace file '{}' not found", WORKSPACE_FILE
        )));
    }

    let toml_str = match fs::read_to_string(workspace_path) {
        Ok(content) => content,
        Err(e) => return Err(CBuildError::new(&format!(
            "Failed to read {}: {}", WORKSPACE_FILE, e
        )).with_file(WORKSPACE_FILE)),
    };

    match toml::from_str::<WorkspaceConfig>(&toml_str) {
        Ok(config) => Ok(config),
        Err(e) => Err(parse_toml_error(
            e,
            WORKSPACE_FILE,
            &toml_str
        )),
    }
}

fn build_workspace_with_dependency_order(
    project: Option<String>,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects.clone(),
    };

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
    let dependency_graph = build_dependency_graph(&workspace_config, &project_paths)?;

    // Determine build order based on dependencies
    let build_order = resolve_build_order(&dependency_graph, &projects)?;

    println!("{}", format!("Build order: {}", build_order.join(" -> ")).blue());

    // Build projects in order, generating package configs as we go
    for project_name in &build_order {
        println!("{}", format!("Building project: {}", project_name).blue());
        let path = PathBuf::from(project_name);
        let mut config = load_project_config(Some(&path))?;

        auto_adjust_config(&mut config)?;

        // First, try the normal build process
        if let Err(e) = build_project(&config, &path, config_type, variant_name, target, Some(&workspace_config)) {
            // Print a clear error line in cbuild style, but continue with other projects
            println!("{}", format!("Warning: Building {} had issues, but continuing", project_name).yellow());
        }

        // Generate package config after build
        if let Err(e) = generate_package_config(&path, project_name) {
            println!("{}", format!("Error generating package config: {}", e).yellow());
        }
    }

    println!("{}", "Workspace build completed".green());
    Ok(())
}

fn get_possible_library_filenames(project_name: &str, is_shared: bool, is_msvc_style: bool) -> Vec<String> {
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

fn build_dependency_graph(workspace_config: &WorkspaceConfig,
                          project_paths: &[PathBuf])
                          -> Result<HashMap<String, Vec<String>>, Box<dyn std::error::Error>> {
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

// Helper function to ensure cmake directory exists with necessary files
fn ensure_cmake_directory(project_path: &Path, project_name: &str) -> Result<(), Box<dyn std::error::Error>> {
    let cmake_dir = project_path.join("cmake");

    // Create cmake directory if it doesn't exist
    if !cmake_dir.exists() {
        fs::create_dir_all(&cmake_dir)?;
    }

    // Create Config.cmake.in file if it doesn't exist
    let config_in_path = cmake_dir.join(format!("{}Config.cmake.in", project_name));
    if !config_in_path.exists() {
        let config_content = format!(r#"@PACKAGE_INIT@

include("${{CMAKE_CURRENT_LIST_DIR}}/{}Targets.cmake")

check_required_components({})
"#, project_name, project_name);

        fs::write(&config_in_path, config_content)?;
    }

    Ok(())
}

fn generate_package_config(project_path: &Path, project_name: &str) -> Result<(), Box<dyn std::error::Error>> {
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

    println!("{}", format!("Library path resolved to: {}", lib_path.display()).blue());

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
        let candidate = lib_path.join(filename); // Or whatever directory you want
        println!("Checking for: {}", candidate.display());
        if candidate.exists() {
            println!("Found: {}", candidate.display());
            // Do something with it, e.g. store it or set a CMake variable
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
                println!("{}", format!("Found library at: {}", file_path.display()).green());
                lib_file = Some(file_path);
                break;
            }
        }
    }

    // If still not found, try to search the entire project directory
    if lib_file.is_none() {
        println!("{}", "Searching project directory for library...".yellow());
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
            println!("{}", format!("Found library by search: {}", path.display()).green());
        }
    }

    let lib_file = match lib_file {
        Some(path) => path,
        None => {
            println!("{}", format!("Warning: Library file not found for {}. Using placeholder.", project_name).yellow());
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
    let config_content = format!(r#"# Generated by CBuild
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
    println!("{}", format!("Generated package config at {}", config_file.display()).green());

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
    println!("{}", format!("Generated version file at {}", version_file.display()).green());

    Ok(())
}
/// Helper function to expand tokens in output directory strings.
fn expand_output_tokens(s: &str, config: &ProjectConfig) -> String {
    let config_val = config.build.default_config.as_deref().unwrap_or("Debug");
    let os_val = if cfg!(windows) { "windows" } else if cfg!(target_os = "macos") { "darwin" } else { "linux" };
    let arch_val = if cfg!(target_arch = "x86_64") { "x64" } else if cfg!(target_arch = "x86") { "x86" } else { "arm64" };
    s.replace("${CONFIG}", config_val)
        .replace("${OS}", os_val)
        .replace("${ARCH}", arch_val)
}


fn build_project_with_dependencies(
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



fn build_workspace(
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

// Helper function to resolve build order based on dependencies
fn resolve_build_order(
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

fn clean_workspace(
    project: Option<String>,
    config_type: Option<&str>,
    target: Option<&str>
) -> Result<(), Box<dyn std::error::Error>> {
    let workspace_config = load_workspace_config()?;

    let projects = match project {
        Some(proj) => vec![proj],
        None => workspace_config.workspace.projects,
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
    variant_name: Option<&str>,
    args: &[String]
) -> Result<(), Box<dyn std::error::Error>> {
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
                    // Default to first project if specified default isn't valid
                    workspace_config.workspace.projects[0].clone()
                }
            } else {
                // No default specified, use first project
                workspace_config.workspace.projects[0].clone()
            }
        }
    };

    println!("{}", format!("Running project: {}", project_path).blue());
    let path = PathBuf::from(&project_path);
    let config = load_project_config(Some(&path))?;
    run_project(&config, &path, config_type, variant_name, args, Some(&workspace_config))?;

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

fn install_workspace(
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

fn install_workspace_deps(project: Option<String>, update: bool) -> Result<(), Box<dyn std::error::Error>> {
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

fn run_workspace_script(name: String, project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
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

fn generate_workspace_ide_files(ide_type: String, project: Option<String>) -> Result<(), Box<dyn std::error::Error>> {
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

fn package_workspace(
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

fn list_workspace_items(what: Option<&str>) -> Result<(), Box<dyn std::error::Error>> {
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

        if !workspace_config.workspace.projects.contains(&project_rel_path) {
            workspace_config.workspace.projects.push(project_rel_path);
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

fn get_effective_compiler_label(config: &ProjectConfig) -> String {
    if let Some(label) = &config.build.compiler {
        // If user explicitly set [build.compiler], use it
        return label.clone();
    }

    // Otherwise pick a default:
    if cfg!(target_os = "windows") {
        "msvc".to_string()      // Use cl.exe
    } else if cfg!(target_os = "macos") {
        "clang".to_string()     // Apple Clang
    } else {
        "gcc".to_string()       // Typical default on Linux
    }
}


fn create_default_config() -> ProjectConfig {
    // First, detect the system and compiler, though we won't directly
    // use it to pick slash/dash flags here. Instead, we store universal
    // tokens in the config; the actual slash/dash logic occurs later.
    let system_info = detect_system_info();

    // We’ll build up the config settings using universal tokens.
    // Then, at build time, your code calls map_token() / parse_universal_flags().
    let mut configs = HashMap::new();

    let default_compiler = if system_info.os == "windows" {
        // Typically you'd do "msvc" or "clang-cl"
        "clang-cl"
    } else if system_info.os == "darwin" {
        "clang"
    } else {
        "gcc"
    };

    //
    // 1) Debug
    //
    // Example universal tokens for a debug config:
    //  - NO_OPT => /Od or -O0
    //  - DEBUG_INFO => /Zi or -g
    //  - RTC1 => /RTC1 or (nothing) on GNU
    configs.insert("Debug".to_string(), ConfigSettings {
        defines: Some(vec!["DEBUG".to_string(), "_DEBUG".to_string()]),
        flags: Some(vec![
            "NO_OPT".to_string(),
            "DEBUG_INFO".to_string(),
            "RTC1".to_string(),
        ]),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    //
    // 2) Release
    //
    // Example tokens for a release config:
    //  - OPTIMIZE => /O2 or -O2
    //  - OB2 => /Ob2 or -finline-functions
    //  - DNDEBUG => /DNDEBUG or -DNDEBUG
    configs.insert("Release".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(vec![
            "OPTIMIZE".to_string(),
            "OB2".to_string(),
            "DNDEBUG".to_string(),
        ]),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    //
    // 3) RelWithDebInfo
    //
    // Example tokens:
    //  - OPTIMIZE => /O2 or -O2
    //  - OB1 => /Ob1 (MSVC) or skip on GNU
    //  - DNDEBUG => /DNDEBUG or -DNDEBUG
    //  - DEBUG_INFO => /Zi or -g
    configs.insert("RelWithDebInfo".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(vec![
            "OPTIMIZE".to_string(),
            "OB1".to_string(),
            "DNDEBUG".to_string(),
            "DEBUG_INFO".to_string(),
        ]),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    //
    // 4) MinSizeRel
    //
    // Example tokens:
    //  - MIN_SIZE => /O1 or -Os
    //  - DNDEBUG => /DNDEBUG or -DNDEBUG
    configs.insert("MinSizeRel".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(vec![
            "MIN_SIZE".to_string(),
            "DNDEBUG".to_string(),
        ]),
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
        platform_links: None, // Add this field
    });

    // Create default build variants
    let mut variants = HashMap::new();

    // Standard variant (default)
    variants.insert("standard".to_string(), VariantSettings {
        description: Some("Standard build with default settings".to_string()),
        defines: None,
        flags: None,  // No special tokens here
        dependencies: None,
        features: None,
        platforms: None,
        cmake_options: None,
    });

    // Memory safety variant
    // Store "MEMSAFE" as a token so the build can apply, e.g. /sdl /GS or -fsanitize=...
    variants.insert("memory_safety".to_string(), VariantSettings {
        description: Some("Build with memory safety checks".to_string()),
        defines: Some(vec!["ENABLE_MEMORY_SAFETY=1".to_string()]),
        flags: Some(vec![
            "MEMSAFE".to_string(),
        ]),
        dependencies: None,
        features: None,
        platforms: None,
        cmake_options: None,
    });

    // Performance variant
    // We store "OPTIMIZE", "LTO", "PARALLEL" so that for MSVC we get /O2 /GL /Qpar,
    variants.insert("performance".to_string(), VariantSettings {
        description: Some("Optimized for maximum performance".to_string()),
        defines: Some(vec!["OPTIMIZE_PERFORMANCE=1".to_string()]),
        flags: Some(vec![
            "OPTIMIZE".to_string(),
            "LTO".to_string(),
            "PARALLEL".to_string(),
        ]),
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
        "find src include -name '*.cpp' -o -name '*.h' | xargs clang-format -i".to_string(),
    );
    scripts.insert(
        "count_lines".to_string(),
        "find src include -name '*.cpp' -o -name '*.h' | xargs wc -l".to_string(),
    );
    scripts.insert(
        "clean_all".to_string(),
        if cfg!(target_os = "windows") {
            "rmdir /s /q build bin".to_string()
        } else {
            "rm -rf build bin".to_string()
        },
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

    // Finally, build and return the entire ProjectConfig
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
            compiler: Some(default_compiler.to_string()),
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
            workspace: vec![],
        },
        targets,
        platforms: Some(platforms),
        output: OutputConfig {
            bin_dir: Some(DEFAULT_BIN_DIR.to_string()),
            lib_dir: Some(DEFAULT_LIB_DIR.to_string()),
            obj_dir: Some(DEFAULT_OBJ_DIR.to_string()),
        },
        // If you want to retain the default hooks object, set it here:
        hooks: Some(hooks),
        scripts: Some(ScriptDefinitions { scripts }),
        variants: Some(BuildVariants {
            default: Some("standard".to_string()),
            variants,
        }),
        cross_compile: None,
        pch: None,
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

fn load_project_config(path: Option<&Path>) -> Result<ProjectConfig, CBuildError> {
    let project_path = path.unwrap_or_else(|| Path::new("."));

    // When loading from a workspace dependency, we need to handle
    // both absolute paths and paths relative to the workspace root
    let config_path = if project_path.is_absolute() {
        project_path.join(CBUILD_FILE)
    } else if is_workspace() && !project_path.starts_with(".") {
        // For non-relative paths in a workspace, check if they should
        // be prefixed with "projects/"
        let with_projects = Path::new("projects").join(project_path);
        if with_projects.join(CBUILD_FILE).exists() {
            with_projects.join(CBUILD_FILE)
        } else {
            project_path.join(CBUILD_FILE)
        }
    } else {
        project_path.join(CBUILD_FILE)
    };

    if !config_path.exists() {
        return Err(CBuildError::new(&format!(
            "Configuration file '{}' not found. Run 'cbuild init' to create one.",
            config_path.display()
        )));
    }

    let toml_str = match fs::read_to_string(&config_path) {
        Ok(content) => content,
        Err(e) => return Err(CBuildError::new(&format!(
            "Failed to read {}: {}", config_path.display(), e
        )).with_file(&config_path.to_string_lossy().to_string())),
    };

    match toml::from_str::<ProjectConfig>(&toml_str) {
        Ok(config) => Ok(config),
        Err(e) => Err(parse_toml_error(
            e,
            &config_path.to_string_lossy().to_string(),
            &toml_str
        )),
    }
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

    // Helper to see if a command is available
    fn has_command(cmd: &str) -> bool {
        let cmd_str = if cfg!(windows) && !cmd.ends_with(".exe") && !cmd.ends_with(".bat") && !cmd.ends_with(".cmd") {
            format!("{}.exe", cmd)
        } else {
            cmd.to_string()
        };

        // Try with --version flag first (most common)
        let version_result = Command::new(&cmd_str)
            .arg("--version")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_ok();

        if version_result {
            return true;
        }

        // Some tools don't support --version, so try with no arguments
        let no_args_result = Command::new(&cmd_str)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_ok();

        if no_args_result {
            return true;
        }

        // For MSVC cl.exe specially
        if cmd == "cl" {
            let cl_result = Command::new("cl")
                .arg("/?")
                .stdout(Stdio::null())
                .stderr(Stdio::null())
                .status()
                .is_ok();

            return cl_result;
        }

        false
    }

    // Windows logic:
    if cfg!(target_os = "windows") {
        // Check for cl.exe
        if has_command("cl") {
            return SystemInfo { os, arch, compiler: "msvc".to_string() };
        }
        // Check for clang-cl
        if has_command("clang-cl") {
            return SystemInfo { os, arch, compiler: "clang-cl".to_string() };
        }
        // Check for clang (GNU-style)
        if has_command("clang") {
            return SystemInfo { os, arch, compiler: "clang".to_string() };
        }
        // Check for gcc
        if has_command("gcc") {
            return SystemInfo { os, arch, compiler: "gcc".to_string() };
        }
        // Fallback
        return SystemInfo { os, arch, compiler: "default".to_string() };
    }

    // Non-Windows (macOS, Linux):
    if has_command("clang") {
        return SystemInfo { os, arch, compiler: "clang".to_string() };
    }
    if has_command("gcc") {
        return SystemInfo { os, arch, compiler: "gcc".to_string() };
    }

    // Fallback
    SystemInfo { os, arch, compiler: "default".to_string() }
}


fn setup_vcpkg(config: &ProjectConfig, project_path: &Path) -> Result<String, Box<dyn std::error::Error>> {
    // Skip if vcpkg is disabled
    let vcpkg_config = &config.dependencies.vcpkg;
    if !vcpkg_config.enabled {
        return Ok(String::new());
    }

    // Check if we've already set up vcpkg in this session
    let cache_key = "vcpkg_setup";
    {
        let installed = INSTALLED_PACKAGES.lock().unwrap();
        if installed.contains(cache_key) {
            let cached_path = get_cached_vcpkg_toolchain_path();
            if !cached_path.is_empty() {
                println!("{}", "Using previously set up vcpkg".blue());
                return Ok(cached_path);
            }
        }
    }

    // First, try the configured path
    let configured_path = vcpkg_config.path.as_deref().unwrap_or(VCPKG_DEFAULT_DIR);
    let configured_path = expand_tilde(configured_path);

    // Check if vcpkg is in PATH environment variable
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

    // Try to find vcpkg in common locations
    let mut potential_vcpkg_paths = vec![
        configured_path.clone(),
        // Add the path found in PATH if any
        vcpkg_in_path.map_or(String::new(), |p| p.to_string_lossy().to_string()),
        // Common installation locations
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
                None
            ) {
                Ok(_) => {},
                Err(e) => {
                    if vcpkg_dir.exists() {
                        println!("{}", format!("Git clone failed but directory exists: {}", e).yellow());
                    } else if !has_command("git") {
                        // Try to install git
                        println!("{}", "Git not found. Attempting to install git...".yellow());

                        let git_installed = if cfg!(windows) {
                            if has_command("winget") {
                                run_command(
                                    vec![
                                        "winget".to_string(),
                                        "install".to_string(),
                                        "--id".to_string(),
                                        "Git.Git".to_string()
                                    ],
                                    None,
                                    None
                                ).is_ok()
                            } else {
                                false
                            }
                        } else if cfg!(target_os = "macos") {
                            if has_command("brew") {
                                run_command(
                                    vec![
                                        "brew".to_string(),
                                        "install".to_string(),
                                        "git".to_string()
                                    ],
                                    None,
                                    None
                                ).is_ok()
                            } else {
                                false
                            }
                        } else { // Linux
                            run_command(
                                vec![
                                    "sudo".to_string(),
                                    "apt-get".to_string(),
                                    "update".to_string()
                                ],
                                None,
                                None
                            ).is_ok() &&
                                run_command(
                                    vec![
                                        "sudo".to_string(),
                                        "apt-get".to_string(),
                                        "install".to_string(),
                                        "-y".to_string(),
                                        "git".to_string()
                                    ],
                                    None,
                                    None
                                ).is_ok()
                        };

                        if git_installed && has_command("git") {
                            // Try cloning again
                            match run_command(
                                vec![
                                    String::from("git"),
                                    String::from("clone"),
                                    String::from("https://github.com/microsoft/vcpkg.git"),
                                    String::from(&configured_path)
                                ],
                                None,
                                None
                            ) {
                                Ok(_) => {},
                                Err(e) => return Err(format!("Failed to clone vcpkg repository: {}", e).into())
                            }
                        } else {
                            return Err("Git is required to set up vcpkg but could not be installed. Please install git manually.".into());
                        }
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

        // Check if we have the necessary compilers for bootstrapping
        if cfg!(windows) && !has_command("cl") && !has_command("g++") && !has_command("clang++") {
            println!("{}", "No C++ compiler found for bootstrapping vcpkg. Attempting to install a compiler...".yellow());
            ensure_compiler_available("msvc").or_else(|_| ensure_compiler_available("gcc"))?;
        } else if !cfg!(windows) && !has_command("g++") && !has_command("clang++") {
            println!("{}", "No C++ compiler found for bootstrapping vcpkg. Attempting to install a compiler...".yellow());
            ensure_compiler_available("gcc").or_else(|_| ensure_compiler_available("clang"))?;
        }

        match run_command(
            vec![String::from(bootstrap_script)],
            Some(&configured_path),
            None
        ) {
            Ok(_) => println!("{}", "vcpkg bootstrap completed successfully.".green()),
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
        println!("{}", "Installing dependencies with vcpkg...".blue());

        // First update vcpkg itself (quietly)
        let _ = Command::new(&vcpkg_exe)
            .arg("update")
            .current_dir(&vcpkg_path)
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();

        // Install packages with clean output
        run_vcpkg_install(&vcpkg_exe, &vcpkg_path, &vcpkg_config.packages)?;
    }

    // Return the path to vcpkg.cmake for CMake integration
    {
        let mut installed = INSTALLED_PACKAGES.lock().unwrap();
        installed.insert(cache_key.to_string());
    }

    let toolchain_file = PathBuf::from(&vcpkg_path)
        .join("scripts")
        .join("buildsystems")
        .join("vcpkg.cmake");

    if !toolchain_file.exists() {
        return Err(format!("vcpkg toolchain file not found at {}. This suggests a corrupt vcpkg installation.", toolchain_file.display()).into());
    }

    cache_vcpkg_toolchain_path(&toolchain_file);

    Ok(toolchain_file.to_string_lossy().to_string())
}

fn check_vcpkg_package_installed(vcpkg_path: &str, package: &str) -> bool {
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

fn run_vcpkg_install(
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
        println!("{}", "Packages already installed:".green());
        for pkg in &already_installed {
            println!("  - {}", pkg.green());
        }
        println!("{}", "vcpkg dependencies configured successfully".green());
        return Ok(());
    }

    // Otherwise, run the install command for new packages
    let mut cmd = Command::new(vcpkg_exe);
    cmd.arg("install");
    cmd.args(&to_install);
    cmd.current_dir(vcpkg_path);
    cmd.stdout(Stdio::piped());
    cmd.stderr(Stdio::piped());

    match cmd.output() {
        Ok(output) => {
            let status = output.status;

            if !status.success() {
                let stderr = String::from_utf8_lossy(&output.stderr);
                println!("{}", "Error installing vcpkg packages:".red());
                println!("{}", stderr);
                return Err("vcpkg install failed".into());
            }

            // Show the results
            if !already_installed.is_empty() {
                println!("{}", "Packages already installed:".green());
                for pkg in already_installed {
                    println!("  - {}", pkg.green());
                }
            }

            if !to_install.is_empty() {
                println!("{}", "Newly installed packages:".green());
                for pkg in to_install {
                    println!("  - {}", pkg.green());
                }
            }

            println!("{}", "vcpkg dependencies configured successfully".green());
            Ok(())
        },
        Err(e) => {
            println!("{}", format!("Error running vcpkg: {}", e).red());
            Err(e.into())
        }
    }
}

fn run_vcpkg_command(cmd: Vec<String>, cwd: &str) -> Result<String, Box<dyn std::error::Error>> {
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
        println!("{}", "Dependencies configured successfully".green());
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

    if let Some(configs) = &config.build.configs {
        if let Some(cfg_settings) = configs.get(build_type) {
            if let Some(defines) = &cfg_settings.defines {
                for define in defines {
                    options.push(format!("-D{}=1", define));
                }
            }

            // 2) parse universal tokens for flags
            let is_msvc_style = is_msvc_style_for_config(config);

            if let Some(token_list) = &cfg_settings.flags {
                let real_flags = parse_universal_flags(token_list, is_msvc_style);
                if !real_flags.is_empty() {
                    let joined = real_flags.join(" ");
                    if is_msvc_style {
                        options.push(format!(
                            "-DCMAKE_CXX_FLAGS_{}:STRING={}",
                            build_type.to_uppercase(),
                            joined
                        ));
                        options.push(format!(
                            "-DCMAKE_C_FLAGS_{}:STRING={}",
                            build_type.to_uppercase(),
                            joined
                        ));
                    } else {
                        options.push(format!(
                            "-DCMAKE_CXX_FLAGS_{}='{}'",
                            build_type.to_uppercase(),
                            joined
                        ));
                        options.push(format!(
                            "-DCMAKE_C_FLAGS_{}='{}'",
                            build_type.to_uppercase(),
                            joined
                        ));
                    }
                }
            }

            // 3) handle link_flags, cmake_options, etc. as before
            if let Some(link_flags) = &cfg_settings.link_flags {
                if !link_flags.is_empty() {
                    let link_str = link_flags.join(" ");
                    if cfg!(windows) {
                        options.push(format!(
                            "-DCMAKE_EXE_LINKER_FLAGS_{}=\"{}\"",
                            build_type.to_uppercase(),
                            link_str
                        ));
                        options.push(format!(
                            "-DCMAKE_SHARED_LINKER_FLAGS_{}=\"{}\"",
                            build_type.to_uppercase(),
                            link_str
                        ));
                    } else {
                        options.push(format!(
                            "-DCMAKE_EXE_LINKER_FLAGS_{}='{}'",
                            build_type.to_uppercase(),
                            link_str
                        ));
                        options.push(format!(
                            "-DCMAKE_SHARED_LINKER_FLAGS_{}='{}'",
                            build_type.to_uppercase(),
                            link_str
                        ));
                    }
                }
            }
            if let Some(cmake_opts) = &cfg_settings.cmake_options {
                options.extend(cmake_opts.clone());
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

fn apply_variant_settings(cmd: &mut Vec<String>, variant: &VariantSettings, config: &ProjectConfig) {
    let is_msvc_style = is_msvc_style_for_config(config);

    // The rest is the same
    if let Some(defines) = &variant.defines {
        for define in defines {
            cmd.push(format!("-D{}=1", define));
        }
    }

    if let Some(token_list) = &variant.flags {
        let real_flags = parse_universal_flags(token_list, is_msvc_style);
        if !real_flags.is_empty() {
            let joined = real_flags.join(" ");
            if is_msvc_style {
                cmd.push(format!("-DCMAKE_CXX_FLAGS:STRING={}", joined));
                cmd.push(format!("-DCMAKE_C_FLAGS:STRING={}", joined));
            } else {
                cmd.push(format!("-DCMAKE_CXX_FLAGS='{}'", joined));
                cmd.push(format!("-DCMAKE_C_FLAGS='{}'", joined));
            }
        }
    }

    // 4) apply any variant-level cmake_options
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

            // Hide detailed output
            command.stdout(Stdio::null());
            command.stderr(Stdio::piped());

            // Execute the command
            let output = command.output()?;

            // Only print errors
            if !output.status.success() {
                let stderr = String::from_utf8_lossy(&output.stderr);
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
fn generate_cmake_lists(config: &ProjectConfig, project_path: &Path, variant_name: Option<&str>, workspace_config: Option<&WorkspaceConfig>) -> Result<(), Box<dyn std::error::Error>> {
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

    if !config.dependencies.workspace.is_empty() && workspace_config.is_some() {
        cmake_content.push(String::new());
        cmake_content.push("# Workspace dependencies".to_string());
        for dep in &config.dependencies.workspace {
            let dep_name = &dep.name;
            cmake_content.push(format!("# Project dependency: {}", dep_name));
            cmake_content.push(format!("find_package({} CONFIG QUIET)", dep_name));
            cmake_content.push(format!("if(NOT {}_FOUND)", dep_name));
            cmake_content.push(format!("  message(WARNING \"Workspace dependency '{}' not found. Make sure to build dependencies first.\")", dep_name));
            // Only create a dummy imported target if manual paths are provided:
            cmake_content.push(format!("  if(DEFINED {0}_INCLUDE_DIR AND DEFINED {0}_LIBRARY)", dep_name.to_uppercase()));
            cmake_content.push(format!("    message(STATUS \"Using manual paths for {}\")", dep_name));
            cmake_content.push(format!("    add_library({0}::{0} STATIC IMPORTED)", dep_name));
            cmake_content.push(format!("    set_target_properties({0}::{0} PROPERTIES", dep_name));
            cmake_content.push(format!("      IMPORTED_LOCATION \"${{{0}_LIBRARY}}\"", dep_name.to_uppercase()));
            cmake_content.push(format!("      INTERFACE_INCLUDE_DIRECTORIES \"${{{0}_INCLUDE_DIR}}\"", dep_name.to_uppercase()));
            cmake_content.push("    )".to_string());
            cmake_content.push(format!("    set({}_FOUND TRUE)", dep_name));
            cmake_content.push("  endif()".to_string());
            cmake_content.push("else()".to_string());
            cmake_content.push(format!("  message(STATUS \"Workspace dependency '{}' found as imported target.\")", dep_name));
            cmake_content.push("endif()".to_string());
        }
        cmake_content.push(String::new());
    }

    // Set output directories based on config
    let output_config = &config.output;
    if let Some(bin_dir) = &output_config.bin_dir {
        // Process variables like ${CONFIG}, ${OS}, ${ARCH}
        let processed_bin_dir = bin_dir
            .replace("${CONFIG}", "${CMAKE_BUILD_TYPE}")
            .replace("${OS}", if cfg!(windows) { "windows" } else if cfg!(target_os = "macos") { "darwin" } else { "linux" })
            .replace("${ARCH}", if cfg!(target_arch = "x86_64") { "x64" } else if cfg!(target_arch = "x86") { "x86" } else { "arm64" });

        cmake_content.push(format!("set(CMAKE_RUNTIME_OUTPUT_DIRECTORY \"{}\")", processed_bin_dir));
    }

    if let Some(lib_dir) = &output_config.lib_dir {
        let processed_lib_dir = lib_dir
            .replace("${CONFIG}", "${CMAKE_BUILD_TYPE}")
            .replace("${OS}", if cfg!(windows) { "windows" } else if cfg!(target_os = "macos") { "darwin" } else { "linux" })
            .replace("${ARCH}", if cfg!(target_arch = "x86_64") { "x64" } else if cfg!(target_arch = "x86") { "x86" } else { "arm64" });

        cmake_content.push(format!("set(CMAKE_LIBRARY_OUTPUT_DIRECTORY \"{}\")", processed_lib_dir));
        cmake_content.push(format!("set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY \"{}\")", processed_lib_dir));
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

    // Add common preprocessor definitions
    cmake_content.push("# Add preprocessor definitions from build configuration".to_string());
    if let Some(configs) = &config.build.configs {
        if let Some(config_settings) = configs.get(&get_build_type(config, None)) {
            if let Some(defines) = &config_settings.defines {
                for define in defines {
                    cmake_content.push(format!("add_compile_definitions({})", define));
                }
            }
        }
    }
    cmake_content.push(String::new());

    // Add vcpkg dependencies - find all packages from vcpkg
    let vcpkg_config = &config.dependencies.vcpkg;
    if vcpkg_config.enabled && !vcpkg_config.packages.is_empty() {
        cmake_content.push("# vcpkg dependencies".to_string());
        for package in &vcpkg_config.packages {
            // Extract base package name without version or platform
            let base_package = package.split(':').next().unwrap_or(package);
            cmake_content.push(format!("find_package({} CONFIG REQUIRED)", base_package));
        }
        cmake_content.push(String::new());
    }

    // Add other dependencies
    if let Some(cmake_deps) = &config.dependencies.cmake {
        for dep in cmake_deps {
            cmake_content.push(format!("find_package({} REQUIRED)", dep));
        }
        if !cmake_deps.is_empty() {
            cmake_content.push(String::new());
        }
    }

    // Add build variant defines if a variant is specified
    if let Some(variant) = get_active_variant(config, variant_name) {
        if let Some(defines) = &variant.defines {
            cmake_content.push(String::new());
            cmake_content.push(format!("# Build variant: {}", variant_name.unwrap_or("default")).to_string());
            for define in defines {
                cmake_content.push(format!("add_compile_definitions({})", define));
            }
            cmake_content.push(String::new());
        }
    }

    // Add support for precompiled headers if enabled
    if let Some(pch_config) = &config.pch {
        if pch_config.enabled {
            cmake_content.push("# Precompiled header support".to_string());
            // Define PCH variables
            cmake_content.push(format!("set(PCH_HEADER \"{}\")", pch_config.header));

            if let Some(source) = &pch_config.source {
                cmake_content.push(format!("set(PCH_SOURCE \"{}\")", source));
            }

            cmake_content.push("# PCH function for targets".to_string());
            cmake_content.push("function(target_enable_pch target_name)".to_string());

            // Modern CMake PCH approach (CMake 3.16+)
            cmake_content.push("  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)".to_string());

            // If we have both header and source (for MSVC style)
            if pch_config.source.is_some() {
                cmake_content.push("    if(MSVC)".to_string());
                cmake_content.push("      target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());
                cmake_content.push("    else()".to_string());
                cmake_content.push("      target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());
                cmake_content.push("    endif()".to_string());
            } else {
                // Header-only PCH
                cmake_content.push("    target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());
            }

            // Fallback for older CMake versions
            cmake_content.push("  else()".to_string());
            cmake_content.push("    # Fallback for older CMake - compiler-specific flags".to_string());
            cmake_content.push("    if(MSVC)".to_string());
            cmake_content.push("      get_filename_component(PCH_NAME ${PCH_HEADER} NAME_WE)".to_string());
            cmake_content.push("      set(PCH_OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/${PCH_NAME}.pch\")".to_string());
            cmake_content.push("      # Set compiler flags for using PCH".to_string());
            cmake_content.push("      target_compile_options(${target_name} PRIVATE /Yu\"${PCH_HEADER}\" /Fp\"${PCH_OUTPUT}\")".to_string());

            // Handle separate source file for creating the PCH
            if let Some(_) = &pch_config.source {
                cmake_content.push("      # Set PCH creation flag on the source file that will create the PCH".to_string());
                cmake_content.push("      set_source_files_properties(${PCH_SOURCE} PROPERTIES COMPILE_FLAGS \"/Yc\\\"${PCH_HEADER}\\\" /Fp\\\"${PCH_OUTPUT}\\\"\")".to_string());
            }

            cmake_content.push("    elseif(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES \"Clang\")".to_string());
            cmake_content.push("      # GCC/Clang - we can't do much in older CMake".to_string());
            cmake_content.push("      message(STATUS \"PCH support for GCC/Clang requires CMake 3.16+. PCH disabled.\")".to_string());
            cmake_content.push("    endif()".to_string());
            cmake_content.push("  endif()".to_string());
            cmake_content.push("endfunction()".to_string());

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
        let mut all_links = Vec::new();

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

        // Handle PCH source if present - might need to separate it from compilation
        if let Some(pch_config) = &config.pch {
            if pch_config.enabled && pch_config.source.is_some() {
                let pch_source = pch_config.source.as_ref().unwrap();
                cmake_content.push(format!("# Handle PCH source separately"));
                cmake_content.push(format!("if(MSVC)"));
                cmake_content.push(format!("  # Find PCH source in sources list"));
                cmake_content.push(format!("  foreach(SRC ${{{}_SOURCES}})", target_name.to_uppercase()));
                cmake_content.push(format!("    if(SRC MATCHES \"{}\")", regex::escape(pch_source)));
                cmake_content.push(format!("      set(PCH_SOURCE \"${{SRC}}\")"));
                cmake_content.push(format!("      list(REMOVE_ITEM {}_SOURCES \"${{SRC}}\")", target_name.to_uppercase()));
                cmake_content.push(format!("    endif()"));
                cmake_content.push(format!("  endforeach()"));
                cmake_content.push(format!("  # Add PCH source back to sources"));
                cmake_content.push(format!("  list(APPEND {}_SOURCES \"${{PCH_SOURCE}}\")", target_name.to_uppercase()));
                cmake_content.push(format!("endif()"));
            }
        }

        // Create target using the expanded sources
        if target_type == "executable" {
            cmake_content.push(format!("add_executable({} ${{{}_SOURCES}})", target_name, target_name.to_uppercase()));
        } else if target_type == "shared-library" {
            cmake_content.push(format!("add_library({} SHARED ${{{}_SOURCES}})", target_name, target_name.to_uppercase()));
        } else if target_type == "static-library" {
            cmake_content.push(format!("add_library({} STATIC ${{{}_SOURCES}})", target_name, target_name.to_uppercase()));
        } else if target_type == "header-only" {
            cmake_content.push(format!("add_library({} INTERFACE)", target_name));
        }

        // Enable PCH for this target if configured
        if let Some(pch_config) = &config.pch {
            if pch_config.enabled {
                cmake_content.push(format!("# Enable PCH for target {}", target_name));
                cmake_content.push(format!("target_enable_pch({})", target_name));
            }
        }

        // Include directories
        if !include_dirs.is_empty() {
            let includes = include_dirs.iter()
                .map(|s| format!("\"{}\"", s))
                .collect::<Vec<_>>()
                .join(" ");

            if target_type == "header-only" {
                // For header-only libraries
                cmake_content.push(format!("target_include_directories({} INTERFACE", target_name));
                for include_dir in include_dirs {
                    cmake_content.push(format!("  \"$<BUILD_INTERFACE:${{CMAKE_CURRENT_SOURCE_DIR}}/{}>\"", include_dir));
                    cmake_content.push(format!("  \"$<INSTALL_INTERFACE:include>\""));
                }
                cmake_content.push(")".to_string());
            } else if target_type == "static-library" || target_type == "shared-library" {
                // For static libraries
                cmake_content.push(format!("target_include_directories({} PUBLIC", target_name));
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
                    cmake_content.push(format!("target_include_directories({} PRIVATE {})", target_name, includes));
                }
            }
        }

        // Add target-specific defines (as target_compile_definitions)
        if !defines.is_empty() {
            let defines_str = defines.iter()
                .map(|d| format!("\"{}\"", d))
                .collect::<Vec<_>>()
                .join(" ");

            if target_type == "header-only" {
                cmake_content.push(format!("target_compile_definitions({} INTERFACE {})", target_name, defines_str));
            } else {
                cmake_content.push(format!("target_compile_definitions({} PRIVATE {})", target_name, defines_str));
            }
        }

        // Link libraries
        if !all_links.is_empty() {
            let links_str = all_links.join(" ");
            if target_type == "header-only" {
                cmake_content.push(format!("target_link_libraries({} INTERFACE {})", target_name, links_str));
            } else {
                // Use PUBLIC instead of PRIVATE to propagate dependencies
                cmake_content.push(format!("target_link_libraries({} PUBLIC {})", target_name, links_str));
            }
        }

        // Export the target so other workspace projects can find it
        if target_type != "executable" {
            // Add export commands so this library can be used as a dependency
            cmake_content.push(String::new());
            cmake_content.push("# Export targets for use by other projects".to_string());

            // Create a namespaced alias and export it
            cmake_content.push(format!("add_library({0}::{0} ALIAS {0})", target_name));

            // Export the targets from the build tree
            cmake_content.push(format!("export(TARGETS {0} NAMESPACE {0}:: FILE {0}Config.cmake)", target_name));

            // Install the library
            cmake_content.push(format!("install(TARGETS {0} EXPORT {0}Targets", target_name));
            cmake_content.push(format!("  LIBRARY DESTINATION ${{CMAKE_INSTALL_LIBDIR}}"));
            cmake_content.push(format!("  ARCHIVE DESTINATION ${{CMAKE_INSTALL_LIBDIR}}"));
            cmake_content.push(format!("  RUNTIME DESTINATION ${{CMAKE_INSTALL_BINDIR}}"));
            cmake_content.push(format!("  INCLUDES DESTINATION ${{CMAKE_INSTALL_INCLUDEDIR}})"));

            // Install headers
            for include_dir in include_dirs {
                cmake_content.push(format!("install(DIRECTORY {} DESTINATION ${{CMAKE_INSTALL_INCLUDEDIR}})", include_dir));
            }

            // Export targets for installation
            cmake_content.push(format!("install(EXPORT {0}Targets NAMESPACE {0}:: DESTINATION ${{CMAKE_INSTALL_LIBDIR}}/cmake/{0})", target_name));

            // Write a basic config file directly into the build directory
            cmake_content.push(format!("# Write a basic config file for build tree usage"));
            cmake_content.push(format!("file(WRITE \"${{CMAKE_CURRENT_BINARY_DIR}}/{0}Config.cmake\"", target_name));
            cmake_content.push(format!("\"include(\\\"${{CMAKE_CURRENT_BINARY_DIR}}/{0}Targets.cmake\\\")\\n\"", target_name));
            cmake_content.push(format!("\"set({0}_INCLUDE_DIR \\\"${{CMAKE_CURRENT_SOURCE_DIR}}/include\\\")\\n\"", target_name.to_uppercase()));
            cmake_content.push(format!("\"set({0}_FOUND TRUE)\\n\")", target_name.to_uppercase()));

            // Export package for build tree
            cmake_content.push(format!("export(PACKAGE {})", target_name));
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

    // Add version header generation if source directory exists
    let src_path = project_path.join("src");
    if src_path.exists() {
        cmake_content.push("# Generate version information header".to_string());
        cmake_content.push("configure_file(".to_string());
        cmake_content.push("  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in".to_string());
        cmake_content.push("  ${CMAKE_CURRENT_BINARY_DIR}/include/version.h".to_string());
        cmake_content.push(")".to_string());
        cmake_content.push("include_directories(${CMAKE_CURRENT_BINARY_DIR}/include)".to_string());
        cmake_content.push(String::new());

        // Create version.h.in template if it doesn't exist
        let cmake_dir = project_path.join("cmake");
        if !cmake_dir.exists() {
            fs::create_dir_all(&cmake_dir)?;
        }

        let version_template_path = cmake_dir.join("version.h.in");
        if !version_template_path.exists() {
            let version_template = format!("#pragma once

// Auto-generated version header
#define {}_VERSION \"@PROJECT_VERSION@\"
#define {}_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define {}_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define {}_VERSION_PATCH @PROJECT_VERSION_PATCH@
",
                                           project_config.name.to_uppercase(),
                                           project_config.name.to_uppercase(),
                                           project_config.name.to_uppercase(),
                                           project_config.name.to_uppercase()
            );

            // Write version template file
            fs::write(&version_template_path, version_template)?;
        }
    }

    // Add installation instructions if not already added
    let has_install_commands = cmake_content.iter().any(|line| line.contains("install("));
    if !has_install_commands && project_config.project_type == "executable" {
        cmake_content.push("# Installation".to_string());
        cmake_content.push("install(TARGETS default DESTINATION bin)".to_string());
        cmake_content.push(String::new());
    }

    // Add CPack configuration for packaging
    cmake_content.push("# Packaging with CPack".to_string());
    cmake_content.push("include(CPack)".to_string());
    cmake_content.push("set(CPACK_PACKAGE_NAME \"${PROJECT_NAME}\")".to_string());
    cmake_content.push("set(CPACK_PACKAGE_VERSION \"${PROJECT_VERSION}\")".to_string());
    cmake_content.push("set(CPACK_PACKAGE_VENDOR \"CBuild User\")".to_string());
    cmake_content.push("set(CPACK_PACKAGE_DESCRIPTION_SUMMARY \"${PROJECT_NAME} - ${PROJECT_DESCRIPTION}\")".to_string());

    // OS-specific packaging options
    cmake_content.push("if(WIN32)".to_string());
    cmake_content.push("  set(CPACK_GENERATOR \"ZIP;NSIS\")".to_string());
    cmake_content.push("elseif(APPLE)".to_string());
    cmake_content.push("  set(CPACK_GENERATOR \"DragNDrop;TGZ\")".to_string());
    cmake_content.push("else()".to_string());
    cmake_content.push("  set(CPACK_GENERATOR \"TGZ;DEB\")".to_string());
    cmake_content.push("endif()".to_string());

    // Write the CMakeLists.txt file
    let cmake_file = project_path.join("CMakeLists.txt");
    let mut file = File::create(cmake_file)?;
    file.write_all(cmake_content.join("\n").as_bytes())?;

    println!("{}", "Generated CMakeLists.txt".green());
    Ok(())
}

// Helper function to check if a command is available in PATH
fn has_command(cmd: &str) -> bool {
    Command::new(cmd)
        .arg("--version")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .status()
        .is_ok()
}

fn ensure_build_tools(config: &ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "Checking for required build tools...".blue());

    // Create a key for this specific config to ensure tools match
    let config_key = format!("tools_for_{}",
                             config.build.compiler.as_deref().unwrap_or("default"));

    // Check if we've already verified tools for this config - but with a timeout
    {
        // Add a timeout to prevent deadlocks
        if let Ok(guard) = VERIFIED_TOOLS.try_lock() {
            if guard.contains(&config_key) {
                println!("{}", "Build tools already verified, skipping checks.".blue());
                return Ok(());
            }
        } else {
            println!("{}", "Warning: Could not acquire verification lock, continuing anyway.".yellow());
        }
    }

    // 1. First, ensure CMake is available - with timeout check
    let cmake_available = has_command_with_timeout("cmake", 5);
    if !cmake_available {
        println!("{}", "CMake not found. Attempting to install...".yellow());

        // Try to install CMake but with a timeout
        let (tx, rx) = std::sync::mpsc::channel();
        let handle = std::thread::spawn(move || {
            let result = ensure_compiler_available("cmake");
            let _ = tx.send(result.is_ok());
        });

        match rx.recv_timeout(std::time::Duration::from_secs(60)) {
            Ok(true) => {
                println!("{}", "CMake installed successfully.".green());
            },
            _ => {
                println!("{}", "CMake installation timed out or failed.".red());
                return Err("CMake is required but couldn't be installed automatically. Please install it manually.".into());
            }
        }

        let _ = handle.join();

        // Verify again after attempted install
        if !has_command_with_timeout("cmake", 5) {
            return Err("CMake is required but couldn't be installed automatically. Please install it manually.".into());
        }
    } else {
        println!("{}", "CMake: ✓".green());
    }

    // 2. Ensure the configured compiler is available - with safety
    let compiler_label = get_effective_compiler_label(config);
    let compilers_to_check = match compiler_label.as_str() {
        "msvc" => vec!["cl"],
        "gcc" => vec!["gcc", "g++"],
        "clang" => vec!["clang", "clang++"],
        "clang-cl" => vec!["clang-cl"],
        _ => vec![compiler_label.as_str()]
    };

    let mut compiler_found = true;
    for compiler in &compilers_to_check {
        if !has_command_with_timeout(compiler, 5) {
            compiler_found = false;
            println!("{}", format!("Compiler '{}' not found.", compiler).yellow());
            break;
        }
    }

    if !compiler_found {
        println!("{}", format!("Attempting to install compiler: {}", compiler_label).yellow());

        // Try to install the compiler with a timeout
        let (tx, rx) = std::sync::mpsc::channel();
        let compiler_to_install = compiler_label.clone();
        let handle = std::thread::spawn(move || {
            let result = ensure_compiler_available(&compiler_to_install);
            let _ = tx.send(result.is_ok());
        });

        match rx.recv_timeout(std::time::Duration::from_secs(120)) {
            Ok(true) => {
                println!("{}", format!("Compiler '{}' installed successfully.", compiler_label).green());
            },
            _ => {
                println!("{}", format!("Compiler '{}' installation timed out or failed.", compiler_label).red());
                return Err(format!("Required compiler '{}' could not be installed automatically.", compiler_label).into());
            }
        }

        let _ = handle.join();

        // Verify again after attempted install
        let mut success = true;
        for compiler in &compilers_to_check {
            if !has_command_with_timeout(compiler, 5) {
                success = false;
                println!("{}", format!("Compiler '{}' still not available after installation attempt.", compiler).red());
            }
        }

        if !success {
            return Err(format!("Required compiler '{}' could not be installed automatically.", compiler_label).into());
        }
    } else {
        println!("{}", format!("Compiler '{}': ✓", compiler_label).green());
    }

    // 3. Ensure a build generator is available - with safety
    let generator = config.build.generator.as_deref().unwrap_or("default");
    let generator_command = match generator {
        "Ninja" => Some("ninja"),
        "MinGW Makefiles" => Some("mingw32-make"),
        "NMake Makefiles" => Some("nmake"),
        "Unix Makefiles" => Some("make"),
        _ => None
    };

    if let Some(cmd) = generator_command {
        if !has_command_with_timeout(cmd, 5) {
            println!("{}", format!("Build generator '{}' not found. Attempting to install...", cmd).yellow());

            // Try to install the generator with a timeout
            let (tx, rx) = std::sync::mpsc::channel();
            let cmd_to_install = cmd.to_string();
            let handle = std::thread::spawn(move || {
                let install_success = match cmd_to_install.as_str() {
                    "ninja" => ensure_compiler_available("ninja").is_ok(),
                    "mingw32-make" => ensure_compiler_available("gcc").is_ok(), // MinGW includes make
                    "nmake" => ensure_compiler_available("msvc").is_ok(),       // MSVC includes nmake
                    "make" => ensure_compiler_available("gcc").is_ok(),         // gcc usually brings make
                    _ => false
                };
                let _ = tx.send(install_success);
            });

            match rx.recv_timeout(std::time::Duration::from_secs(60)) {
                Ok(true) => {
                    if has_command_with_timeout(cmd, 5) {
                        println!("{}", format!("Build generator '{}' installed successfully.", cmd).green());
                    } else {
                        println!("{}", format!("Could not install generator '{}', will try alternatives.", cmd).yellow());
                    }
                },
                _ => {
                    println!("{}", format!("Generator '{}' installation timed out or failed.", cmd).yellow());
                }
            }

            let _ = handle.join();
        } else {
            println!("{}", format!("Build generator '{}': ✓", cmd).green());
        }
    }

    // 4. Check if vcpkg is required but not available
    // We'll skip actual setup here, just do a basic check
    if config.dependencies.vcpkg.enabled {
        println!("{}", "vcpkg: ✓ (will be configured during build)".green());
    }

    // 5. Check if conan is required but not available - with timeout
    if config.dependencies.conan.enabled {
        if !has_command_with_timeout("conan", 5) {
            println!("{}", "Conan package manager not found. It will be installed during build if needed.".yellow());
        } else {
            println!("{}", "Conan package manager: ✓".green());
        }
    }

    // Mark this tool set as verified, but safely handle potential lock issues
    if let Ok(mut guard) = VERIFIED_TOOLS.try_lock() {
        guard.insert(config_key);
    } else {
        println!("{}", "Warning: Could not update verification cache, but continuing.".yellow());
    }

    println!("{}", "All required build tools are available.".green());

    Ok(())
}

// New helper function to check for commands with a timeout
fn has_command_with_timeout(cmd: &str, timeout_seconds: u64) -> bool {
    let (tx, rx) = std::sync::mpsc::channel();
    let cmd_string = cmd.to_string();
    let handle = std::thread::spawn(move || {
        let result = Command::new(&cmd_string)
            .arg("--version")
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status()
            .is_ok();
        let _ = tx.send(result);
    });

    match rx.recv_timeout(std::time::Duration::from_secs(timeout_seconds)) {
        Ok(result) => result,
        Err(_) => {
            println!("{}", format!("Command check for '{}' timed out.", cmd).yellow());
            false
        }
    }
}



// Ensure at least one generator is installed
fn ensure_generator_available() -> Result<String, Box<dyn std::error::Error>> {
    // Try to find an available generator in order of preference
    if has_command("ninja") {
        return Ok("Ninja".to_string());
    } else if has_command("mingw32-make") || has_command("make") {
        return Ok("MinGW Makefiles".to_string());
    } else if has_command("nmake") {
        return Ok("NMake Makefiles".to_string());
    }

    // No generator found, try to install one
    println!("{}", "No build generator found. Attempting to install one...".yellow());

    // Try to install Ninja (fastest and cross-platform)
    if ensure_compiler_available("ninja").is_ok() && has_command("ninja") {
        return Ok("Ninja".to_string());
    }

    // If on Windows, try to install MinGW
    if cfg!(target_os = "windows") && ensure_compiler_available("gcc").is_ok() && has_command("mingw32-make") {
        return Ok("MinGW Makefiles".to_string());
    }

    // As a last resort, if on Windows and Visual Studio tools are available
    if cfg!(target_os = "windows") && ensure_compiler_available("msvc").is_ok() && has_command("nmake") {
        return Ok("NMake Makefiles".to_string());
    }

    // If we got here, we couldn't install any generator
    Err("Could not find or install any build generator. Please install Ninja, MinGW, or Visual Studio Build Tools.".into())
}

// Simplified function to create a minimal template
fn create_simple_config() -> ProjectConfig {
    // Detect best available tools
    let system_info = detect_system_info();

    // Find best available generator
    let generator = if has_command("ninja") {
        "Ninja".to_string()
    } else if cfg!(target_os = "windows") && has_command("mingw32-make") {
        "MinGW Makefiles".to_string()
    } else if cfg!(target_os = "windows") && has_command("nmake") {
        "NMake Makefiles".to_string()
    } else {
        "Unix Makefiles".to_string()
    };

    // Project name from current directory
    let project_name = env::current_dir()
        .unwrap_or_else(|_| PathBuf::from("my_project"))
        .file_name()
        .unwrap_or_else(|| std::ffi::OsStr::new("my_project"))
        .to_string_lossy()
        .to_string();

    // Create minimal configs - just Debug and Release
    let mut configs = HashMap::new();
    configs.insert("Debug".to_string(), ConfigSettings {
        defines: Some(vec!["DEBUG".to_string()]),
        flags: Some(vec!["NO_OPT".to_string(), "DEBUG_INFO".to_string()]),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    configs.insert("Release".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(vec!["OPTIMIZE".to_string()]),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    // Create minimal platforms config
    let mut platforms = HashMap::new();
    platforms.insert(system_info.os.clone(), PlatformConfig {
        compiler: Some(system_info.compiler.clone()),
        defines: None,
        flags: None,
    });

    // Create minimal targets
    let mut targets = HashMap::new();
    targets.insert("default".to_string(), TargetConfig {
        sources: vec!["src/**/*.cpp".to_string(), "src/**/*.c".to_string()],
        include_dirs: Some(vec!["include".to_string()]),
        defines: Some(vec![]),
        links: Some(vec![]),
        platform_links: None, // Add this field
    });

    // Use the most basic vcpkg config
    let vcpkg_config = VcpkgConfig {
        enabled: false,  // Disabled by default for simplicity
        path: None,
        packages: vec![],
    };

    // Build the minimal ProjectConfig
    ProjectConfig {
        project: ProjectInfo {
            name: project_name,
            version: "0.1.0".to_string(),
            description: "A simple C++ project".to_string(),
            project_type: "executable".to_string(),
            language: "c++".to_string(),
            standard: "c++17".to_string(),
        },
        build: BuildConfig {
            build_dir: Some("build".to_string()),
            generator: Some(generator),
            default_config: Some("Debug".to_string()),
            debug: Some(true),
            cmake_options: None,
            configs: Some(configs),
            compiler: Some(system_info.compiler),
        },
        dependencies: DependenciesConfig {
            vcpkg: vcpkg_config,
            system: None,
            cmake: None,
            conan: ConanConfig::default(),
            custom: vec![],
            git: vec![],
            workspace: vec![]
        },
        targets,
        platforms: Some(platforms),
        output: OutputConfig::default(), // Use defaults
        hooks: None,          // No hooks
        scripts: None,        // No scripts
        variants: None,       // No variants
        cross_compile: None,  // No cross-compilation
        pch: None,
    }
}

fn get_cmake_generator(config: &ProjectConfig) -> Result<String, Box<dyn std::error::Error>> {
    let generator = config.build.generator.as_deref().unwrap_or("default");

    // If VS is explicitly requested with a version number
    if generator.starts_with("Visual Studio ") || generator.to_lowercase().starts_with("vs") {
        let requested_version = if generator.to_lowercase().starts_with("vs") {
            // Extract version from "vs2019" or similar format
            let version_str = generator.trim_start_matches("vs").trim_start_matches("VS");
            match version_str {
                "2022" => Some("17 2022"),
                "2019" => Some("16 2019"),
                "2017" => Some("15 2017"),
                "2015" => Some("14 2015"),
                "2013" => Some("12 2013"),
                _ => None,
            }
        } else {
            // Extract version from "Visual Studio XX YYYY" format
            Some(generator.trim_start_matches("Visual Studio "))
        };

        return Ok(get_visual_studio_generator(requested_version));
    }

    // Handle other specific generators
    if generator != "default" {
        // If a specific generator is requested, try to ensure its tools are available
        match generator {
            "Ninja" => {
                if !has_command("ninja") {
                    ensure_compiler_available("ninja")?;
                }
            },
            "NMake Makefiles" => {
                if !has_command("nmake") {
                    ensure_compiler_available("msvc")?;
                }
            },
            "MinGW Makefiles" => {
                if !has_command("mingw32-make") && !has_command("make") {
                    ensure_compiler_available("gcc")?;
                }
            },
            _ => {} // Other generators we don't try to auto-install
        }
        return Ok(generator.to_string());
    }

    // Auto-detect based on platform
    if cfg!(target_os = "windows") {
        // On Windows, prefer Visual Studio if available
        if Command::new("cl").arg("/?").stdout(Stdio::null()).stderr(Stdio::null()).status().is_ok() {
            // Try to determine VS version
            return Ok(get_visual_studio_generator(None));
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
    cross_target: Option<&str>,
    workspace_config: Option<&WorkspaceConfig>
) -> Result<(), Box<dyn std::error::Error>> {
    // First ensure all required build tools are available
    ensure_build_tools(config)?;

    let compiler_label = get_effective_compiler_label(config);

    let build_dir = config.build.build_dir.as_deref().unwrap_or("build");
    let build_path = project_path.join(build_dir);
    fs::create_dir_all(&build_path)?;

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
    generate_cmake_lists(config, project_path, variant_name, workspace_config)?;

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

    // Add compiler specification
    let is_msvc_style = matches!(compiler_label.to_lowercase().as_str(), "msvc" | "clang-cl");

    if let Some((c_comp, cxx_comp)) = map_compiler_label(&compiler_label) {
        cmd.push(format!("-DCMAKE_C_COMPILER={}", c_comp));
        cmd.push(format!("-DCMAKE_CXX_COMPILER={}", cxx_comp));
    }

    // Add platform-specific options
    let platform_options = get_platform_specific_options(config);
    cmd.extend(platform_options);

    // Add configuration-specific options
    let config_options = get_config_specific_options(config, &build_type);
    cmd.extend(config_options);

    // Add variant-specific options if a variant is active
    if let Some(variant) = get_active_variant(config, variant_name) {
        apply_variant_settings(&mut cmd, variant, config);
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

    // Add workspace dependency options
    if let Some(workspace) = workspace_config {
        let workspace_options = resolve_workspace_dependencies(config, Some(workspace), project_path)?;
        cmd.extend(workspace_options);
    }

    // Run CMake configuration
    let cmake_result = run_command(cmd, Some(&build_path.to_string_lossy().to_string()), env_vars.clone());

    if let Err(e) = cmake_result {
        println!("{}", format!("CMake configuration failed: {}", e).red());
        println!("{}", "Attempting to fix common issues and retry...".yellow());

        // Try to recover from common CMake errors
        if try_fix_cmake_errors(&build_path, is_msvc_style) {
            // If fixes were applied, try running CMake again
            let mut retry_cmd = vec!["cmake".to_string(), "..".to_string()];
            retry_cmd.push("-G".to_string());
            retry_cmd.push(generator.clone());
            retry_cmd.push(format!("-DCMAKE_BUILD_TYPE={}", build_type));

            // Add other essential options
            if !vcpkg_toolchain.is_empty() {
                retry_cmd.push(format!("-DCMAKE_TOOLCHAIN_FILE={}", vcpkg_toolchain));
            }

            // Retry with minimal options
            println!("{}", "Retrying CMake configuration with minimal options...".blue());
            run_command(retry_cmd, Some(&build_path.to_string_lossy().to_string()), env_vars.clone())?;
        } else {
            // If we couldn't fix it, return the original error
            return Err(e);
        }
    }

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

fn try_fix_cmake_errors(build_path: &Path, is_msvc_style: bool) -> bool {
    let mut fixed_something = false;

    // Check for CMakeCache.txt
    let cache_path = build_path.join("CMakeCache.txt");
    if cache_path.exists() {
        // Remove CMakeCache.txt to force CMake to reconfigure
        if let Err(e) = fs::remove_file(&cache_path) {
            println!("{}", format!("Warning: Could not remove CMakeCache.txt: {}", e).yellow());
        } else {
            println!("{}", "Removed CMakeCache.txt to force reconfiguration.".green());
            fixed_something = true;
        }
    }

    // Check for CMakeFiles directory
    let cmake_files_path = build_path.join("CMakeFiles");
    if cmake_files_path.exists() {
        // Remove CMakeFiles directory to force CMake to reconfigure
        if let Err(e) = fs::remove_dir_all(&cmake_files_path) {
            println!("{}", format!("Warning: Could not remove CMakeFiles directory: {}", e).yellow());
        } else {
            println!("{}", "Removed CMakeFiles directory to force reconfiguration.".green());
            fixed_something = true;
        }
    }

    // Create a minimal CMakeLists.txt in build directory to test CMake
    let test_cmake_path = build_path.join("test_cmake.txt");
    let test_content = "cmake_minimum_required(VERSION 3.10)\nproject(test_cmake)\n";
    if let Err(e) = fs::write(&test_cmake_path, test_content) {
        println!("{}", format!("Warning: Could not create test CMake file: {}", e).yellow());
    } else {
        println!("{}", "Created test CMake file to check configuration.".green());

        // Try to run a minimal CMake command to verify it works
        let mut test_cmd = vec!["cmake".to_string(), "-P".to_string(), "test_cmake.txt".to_string()];
        match Command::new(&test_cmd[0])
            .args(&test_cmd[1..])
            .current_dir(build_path)
            .output() {
            Ok(_) => {
                println!("{}", "CMake appears to be working.".green());
                fixed_something = true;
            },
            Err(e) => {
                println!("{}", format!("Warning: CMake test failed: {}", e).yellow());
            }
        }

        // Remove test file
        if let Err(e) = fs::remove_file(&test_cmake_path) {
            println!("{}", format!("Warning: Could not remove test CMake file: {}", e).yellow());
        }
    }

    // For MSVC, try to configure environment variables
    if is_msvc_style {
        println!("{}", "Trying to set up MSVC environment variables...".blue());

        // Try to find vcvarsall.bat
        let possible_paths = [
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat",
        ];

        for path in &possible_paths {
            if Path::new(path).exists() {
                println!("{}", format!("Found vcvarsall.bat at: {}", path).green());

                // Create a batch file to set up the environment
                let batch_path = build_path.join("setup_env.bat");
                let batch_content = format!(
                    "@echo off\n\"{}\" x64\necho Environment set up for MSVC\n",
                    path
                );

                if let Err(e) = fs::write(&batch_path, batch_content) {
                    println!("{}", format!("Warning: Could not create environment setup batch file: {}", e).yellow());
                } else {
                    println!("{}", "Created environment setup batch file. Please run it before building.".green());
                    fixed_something = true;
                }

                break;
            }
        }
    }

    fixed_something
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

/// Map a user-friendly compiler label (e.g. "gcc", "clang", "clang-cl", "msvc")
/// to a real pair of C/C++ compiler executables.
fn map_compiler_label(label: &str) -> Option<(String, String)> {
    match label.to_lowercase().as_str() {
        "gcc" => Some(("gcc".to_string(), "g++".to_string())),
        "clang" => Some(("clang".to_string(), "clang++".to_string())),
        "clang-cl" => {
            if cfg!(windows) {
                Some(("clang-cl.exe".to_string(), "clang-cl.exe".to_string()))
            } else {
                Some(("clang-cl".to_string(), "clang-cl".to_string()))
            }
        }
        "msvc" | "cl" => {
            Some(("cl.exe".to_string(), "cl.exe".to_string()))
        }
        _ => None,
    }
}



fn run_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    args: &[String],
    workspace_config: Option<&WorkspaceConfig>
) -> Result<(), Box<dyn std::error::Error>> {
    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = project_path.join(build_dir);
    let project_name = &config.project.name;
    let build_type = get_build_type(config, config_type);
    let bin_dir = config.output.bin_dir.as_deref().unwrap_or(DEFAULT_BIN_DIR);

    // Make sure the project is built
    if !build_path.exists() || !build_path.join("CMakeCache.txt").exists() {
        build_project(config, project_path, config_type, variant_name, None, workspace_config)?;
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

    if !is_executable(&executable) || executable.extension().map_or(false, |ext| ext == "txt") {
        return Err(format!("No valid executable found. Build the project first.").into());
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

fn cache_vcpkg_toolchain_path(path: &Path) {
    let path_str = path.to_string_lossy().to_string();
    let mut cache = CACHED_PATHS.lock().unwrap();
    cache.insert("vcpkg_toolchain".to_string(), path_str);
}

// Function to retrieve the cached vcpkg toolchain path
fn get_cached_vcpkg_toolchain_path() -> String {
    let cache = CACHED_PATHS.lock().unwrap();
    cache.get("vcpkg_toolchain")
        .cloned()
        .unwrap_or_default()
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

// Add this function to detect and potentially install compilers/tools
fn ensure_compiler_available(compiler_label: &str) -> Result<bool, Box<dyn std::error::Error>> {
    if has_command(compiler_label) {
        return Ok(true);
    }

    println!("{}", format!("Compiler '{}' not found. Attempting to install...", compiler_label).yellow());

    match compiler_label.to_lowercase().as_str() {
        "msvc" | "cl" => {
            // Try to install Visual Studio Build Tools
            if cfg!(target_os = "windows") {
                println!("Installing Visual Studio Build Tools...");

                // Use winget if available (Windows 10+)
                if has_command("winget") {
                    let install_cmd = vec![
                        "winget".to_string(),
                        "install".to_string(),
                        "--id".to_string(),
                        "Microsoft.VisualStudio.2022.BuildTools".to_string(),
                        "--silent".to_string(),
                        "--override".to_string(),
                        "--add Microsoft.VisualStudio.Workload.VCTools".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "Visual Studio Build Tools installed successfully.".green());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install automatically: {}", e).red());
                        }
                    }
                }

                // Manual instructions if automatic installation fails
                println!("{}", "Please install Visual Studio Build Tools manually:".yellow());
                println!("1. Download from https://visualstudio.microsoft.com/downloads/");
                println!("2. Select 'C++ build tools' during installation");
                println!("3. Restart your command prompt after installation");
            } else {
                println!("{}", "MSVC compiler is only available on Windows systems.".red());
            }
        },
        "gcc" | "g++" => {
            if cfg!(target_os = "windows") {
                println!("Installing MinGW-w64 (GCC for Windows)...");

                if has_command("winget") {
                    let install_cmd = vec![
                        "winget".to_string(),
                        "install".to_string(),
                        "--id".to_string(),
                        "GnuWin32.Make".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            // Now install MinGW
                            let mingw_cmd = vec![
                                "winget".to_string(),
                                "install".to_string(),
                                "--id".to_string(),
                                "MSYS2.MSYS2".to_string()
                            ];

                            match run_command(mingw_cmd, None, None) {
                                Ok(_) => {
                                    println!("{}", "MSYS2 installed. Installing MinGW toolchain...".blue());

                                    // Install MinGW toolchain through MSYS2
                                    let toolchain_cmd = vec![
                                        "C:\\msys64\\usr\\bin\\bash.exe".to_string(),
                                        "-c".to_string(),
                                        "pacman -S --noconfirm mingw-w64-x86_64-toolchain".to_string()
                                    ];

                                    match run_command(toolchain_cmd, None, None) {
                                        Ok(_) => {
                                            println!("{}", "MinGW toolchain installed successfully.".green());
                                            println!("{}", "Please add C:\\msys64\\mingw64\\bin to your PATH and restart your command prompt.".yellow());
                                            return Ok(true);
                                        },
                                        Err(e) => {
                                            println!("{}", format!("Failed to install MinGW toolchain: {}", e).red());
                                        }
                                    }
                                },
                                Err(e) => {
                                    println!("{}", format!("Failed to install MSYS2: {}", e).red());
                                }
                            }
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install Make: {}", e).red());
                        }
                    }
                }

                // Manual instructions
                println!("{}", "Please install MinGW-w64 manually:".yellow());
                println!("1. Download from https://www.mingw-w64.org/downloads/");
                println!("2. Add the bin directory to your PATH environment variable");
                println!("3. Restart your command prompt after installation");
            } else if cfg!(target_os = "macos") {
                println!("Installing GCC via Homebrew...");

                if has_command("brew") {
                    let install_cmd = vec![
                        "brew".to_string(),
                        "install".to_string(),
                        "gcc".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "GCC installed successfully.".green());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install GCC: {}", e).red());
                        }
                    }
                } else {
                    println!("{}", "Homebrew not found. Please install Homebrew first:".yellow());
                    println!("/bin/bash -c \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\"");
                }
            } else {
                // Linux
                println!("Installing GCC via apt...");

                let install_cmd = vec![
                    "sudo".to_string(),
                    "apt".to_string(),
                    "update".to_string()
                ];

                match run_command(install_cmd, None, None) {
                    Ok(_) => {
                        let gcc_cmd = vec![
                            "sudo".to_string(),
                            "apt".to_string(),
                            "install".to_string(),
                            "-y".to_string(),
                            "build-essential".to_string()
                        ];

                        match run_command(gcc_cmd, None, None) {
                            Ok(_) => {
                                println!("{}", "GCC installed successfully.".green());
                                return Ok(true);
                            },
                            Err(e) => {
                                println!("{}", format!("Failed to install GCC: {}", e).red());
                            }
                        }
                    },
                    Err(e) => {
                        println!("{}", format!("Failed to update package lists: {}", e).red());
                    }
                }
            }
        },
        "clang" | "clang++" => {
            if cfg!(target_os = "windows") {
                println!("Installing LLVM/Clang...");

                if has_command("winget") {
                    let install_cmd = vec![
                        "winget".to_string(),
                        "install".to_string(),
                        "--id".to_string(),
                        "LLVM.LLVM".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "LLVM/Clang installed successfully.".green());
                            println!("{}", "Please restart your command prompt to update your PATH.".yellow());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install LLVM/Clang: {}", e).red());
                        }
                    }
                }
            } else if cfg!(target_os = "macos") {
                println!("Installing Clang via Homebrew...");

                if has_command("brew") {
                    let install_cmd = vec![
                        "brew".to_string(),
                        "install".to_string(),
                        "llvm".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "LLVM/Clang installed successfully.".green());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install LLVM/Clang: {}", e).red());
                        }
                    }
                }
            } else {
                // Linux
                println!("Installing Clang via apt...");

                let install_cmd = vec![
                    "sudo".to_string(),
                    "apt".to_string(),
                    "update".to_string()
                ];

                match run_command(install_cmd, None, None) {
                    Ok(_) => {
                        let clang_cmd = vec![
                            "sudo".to_string(),
                            "apt".to_string(),
                            "install".to_string(),
                            "-y".to_string(),
                            "clang".to_string()
                        ];

                        match run_command(clang_cmd, None, None) {
                            Ok(_) => {
                                println!("{}", "Clang installed successfully.".green());
                                return Ok(true);
                            },
                            Err(e) => {
                                println!("{}", format!("Failed to install Clang: {}", e).red());
                            }
                        }
                    },
                    Err(e) => {
                        println!("{}", format!("Failed to update package lists: {}", e).red());
                    }
                }
            }
        },
        "ninja" => {
            if cfg!(target_os = "windows") {
                println!("Installing Ninja build system...");

                if has_command("winget") {
                    let install_cmd = vec![
                        "winget".to_string(),
                        "install".to_string(),
                        "--id".to_string(),
                        "Ninja-build.Ninja".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "Ninja build system installed successfully.".green());
                            println!("{}", "Please restart your command prompt to update your PATH.".yellow());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install Ninja: {}", e).red());
                        }
                    }
                }
            } else if cfg!(target_os = "macos") {
                println!("Installing Ninja via Homebrew...");

                if has_command("brew") {
                    let install_cmd = vec![
                        "brew".to_string(),
                        "install".to_string(),
                        "ninja".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "Ninja build system installed successfully.".green());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install Ninja: {}", e).red());
                        }
                    }
                }
            } else {
                // Linux
                println!("Installing Ninja via apt...");

                let install_cmd = vec![
                    "sudo".to_string(),
                    "apt".to_string(),
                    "update".to_string()
                ];

                match run_command(install_cmd, None, None) {
                    Ok(_) => {
                        let ninja_cmd = vec![
                            "sudo".to_string(),
                            "apt".to_string(),
                            "install".to_string(),
                            "-y".to_string(),
                            "ninja-build".to_string()
                        ];

                        match run_command(ninja_cmd, None, None) {
                            Ok(_) => {
                                println!("{}", "Ninja build system installed successfully.".green());
                                return Ok(true);
                            },
                            Err(e) => {
                                println!("{}", format!("Failed to install Ninja: {}", e).red());
                            }
                        }
                    },
                    Err(e) => {
                        println!("{}", format!("Failed to update package lists: {}", e).red());
                    }
                }
            }
        },
        "cmake" => {
            if cfg!(target_os = "windows") {
                println!("Installing CMake...");

                if has_command("winget") {
                    let install_cmd = vec![
                        "winget".to_string(),
                        "install".to_string(),
                        "--id".to_string(),
                        "Kitware.CMake".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "CMake installed successfully.".green());
                            println!("{}", "Please restart your command prompt to update your PATH.".yellow());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install CMake: {}", e).red());
                        }
                    }
                }
            } else if cfg!(target_os = "macos") {
                println!("Installing CMake via Homebrew...");

                if has_command("brew") {
                    let install_cmd = vec![
                        "brew".to_string(),
                        "install".to_string(),
                        "cmake".to_string()
                    ];

                    match run_command(install_cmd, None, None) {
                        Ok(_) => {
                            println!("{}", "CMake installed successfully.".green());
                            return Ok(true);
                        },
                        Err(e) => {
                            println!("{}", format!("Failed to install CMake: {}", e).red());
                        }
                    }
                }
            } else {
                // Linux
                println!("Installing CMake via apt...");

                let install_cmd = vec![
                    "sudo".to_string(),
                    "apt".to_string(),
                    "update".to_string()
                ];

                match run_command(install_cmd, None, None) {
                    Ok(_) => {
                        let cmake_cmd = vec![
                            "sudo".to_string(),
                            "apt".to_string(),
                            "install".to_string(),
                            "-y".to_string(),
                            "cmake".to_string()
                        ];

                        match run_command(cmake_cmd, None, None) {
                            Ok(_) => {
                                println!("{}", "CMake installed successfully.".green());
                                return Ok(true);
                            },
                            Err(e) => {
                                println!("{}", format!("Failed to install CMake: {}", e).red());
                            }
                        }
                    },
                    Err(e) => {
                        println!("{}", format!("Failed to update package lists: {}", e).red());
                    }
                }
            }
        },
        _ => {
            println!("{}", format!("Don't know how to install compiler/tool: {}", compiler_label).red());
        }
    }

    println!("{}", "Please install the required tools manually and try again.".yellow());
    Ok(false)
}

fn auto_adjust_config(config: &mut ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
    // Auto-detect best available compiler/generator combo
    let has_msvc = has_command("cl");
    let has_gcc = has_command("gcc") || has_command("g++");
    let has_clang = has_command("clang") || has_command("clang++");
    let has_ninja = has_command("ninja");
    let has_mingw_make = has_command("mingw32-make");
    let has_nmake = has_command("nmake");
    let has_make = has_command("make");

    // First ensure we have a compiler configured
    if config.build.compiler.is_none() {
        if cfg!(target_os = "windows") {
            if has_msvc {
                config.build.compiler = Some("msvc".to_string());
            } else if has_clang {
                config.build.compiler = Some("clang".to_string());
            } else if has_gcc {
                config.build.compiler = Some("gcc".to_string());
            } else {
                // Try to install a compiler, with preference for MSVC on Windows
                if ensure_compiler_available("msvc").is_ok() && has_command("cl") {
                    config.build.compiler = Some("msvc".to_string());
                } else if ensure_compiler_available("gcc").is_ok() && (has_command("gcc") || has_command("g++")) {
                    config.build.compiler = Some("gcc".to_string());
                } else if ensure_compiler_available("clang").is_ok() && (has_command("clang") || has_command("clang++")) {
                    config.build.compiler = Some("clang".to_string());
                } else {
                    return Err("Could not find or install any C++ compiler. Please install one manually.".into());
                }
            }
        } else if cfg!(target_os = "macos") {
            if has_clang {
                config.build.compiler = Some("clang".to_string());
            } else if has_gcc {
                config.build.compiler = Some("gcc".to_string());
            } else {
                // Try to install Xcode command line tools (comes with clang)
                if ensure_compiler_available("clang").is_ok() && has_command("clang") {
                    config.build.compiler = Some("clang".to_string());
                } else {
                    return Err("Could not find or install Clang compiler. Please install Xcode Command Line Tools.".into());
                }
            }
        } else {
            // Linux - prefer GCC, fallback to Clang
            if has_gcc {
                config.build.compiler = Some("gcc".to_string());
            } else if has_clang {
                config.build.compiler = Some("clang".to_string());
            } else {
                // Try to install GCC
                if ensure_compiler_available("gcc").is_ok() && (has_command("gcc") || has_command("g++")) {
                    config.build.compiler = Some("gcc".to_string());
                } else if ensure_compiler_available("clang").is_ok() && (has_command("clang") || has_command("clang++")) {
                    config.build.compiler = Some("clang".to_string());
                } else {
                    return Err("Could not find or install any C++ compiler. Please install GCC or Clang manually.".into());
                }
            }
        }
    }

    // Now ensure we have a generator
    if config.build.generator.is_none() || config.build.generator.as_deref().unwrap_or("") == "default" {
        // Pick best generator based on what's available
        if has_ninja {
            config.build.generator = Some("Ninja".to_string());
        } else if cfg!(target_os = "windows") {
            if has_msvc {
                // Visual Studio generator
                let vs_version = get_visual_studio_generator(None);
                config.build.generator = Some(vs_version);
            } else if has_mingw_make {
                config.build.generator = Some("MinGW Makefiles".to_string());
            } else if has_nmake {
                config.build.generator = Some("NMake Makefiles".to_string());
            } else {
                // Try to install Ninja
                if ensure_compiler_available("ninja").is_ok() && has_command("ninja") {
                    config.build.generator = Some("Ninja".to_string());
                } else {
                    // Use a Visual Studio generator even if not installed
                    // CMake will try to locate it
                    let vs_version = get_visual_studio_generator(None);
                    config.build.generator = Some(vs_version);
                }
            }
        } else {
            // macOS or Linux
            if has_ninja {
                config.build.generator = Some("Ninja".to_string());
            } else if has_make {
                config.build.generator = Some("Unix Makefiles".to_string());
            } else if cfg!(target_os = "macos") {
                config.build.generator = Some("Xcode".to_string());
            } else {
                // Try to install a generator
                if ensure_compiler_available("ninja").is_ok() && has_command("ninja") {
                    config.build.generator = Some("Ninja".to_string());
                } else {
                    // Default fallback
                    config.build.generator = Some("Unix Makefiles".to_string());
                }
            }
        }
    }

    // Update platform config to match compiler
    if let Some(platforms) = &mut config.platforms {
        let current_os = if cfg!(target_os = "windows") {
            "windows"
        } else if cfg!(target_os = "macos") {
            "darwin"
        } else {
            "linux"
        };

        if let Some(platform_config) = platforms.get_mut(current_os) {
            platform_config.compiler = config.build.compiler.clone();
        } else {
            // Create platform config if it doesn't exist
            platforms.insert(current_os.to_string(), PlatformConfig {
                compiler: config.build.compiler.clone(),
                defines: Some(vec![]),
                flags: Some(vec![]),
            });
        }
    } else {
        // Create platforms map if it doesn't exist
        let mut platforms = HashMap::new();
        let current_os = if cfg!(target_os = "windows") {
            "windows"
        } else if cfg!(target_os = "macos") {
            "darwin"
        } else {
            "linux"
        };

        platforms.insert(current_os.to_string(), PlatformConfig {
            compiler: config.build.compiler.clone(),
            defines: Some(vec![]),
            flags: Some(vec![]),
        });

        config.platforms = Some(platforms);
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
    println!("IDE Type requested: {}", ide_type);

    // Parse IDE type with possible version specification
    let parts: Vec<&str> = ide_type.split(':').collect();
    let (base_ide_type, version) = if parts.len() > 1 {
        (parts[0], Some(parts[1]))
    } else {
        (ide_type, None)
    };

    println!("Base IDE type: {}, Version: {:?}", base_ide_type, version);

    match base_ide_type {
        "vscode" => generate_vscode_files(config, project_path)?,
        "clion" => {
            // CLion doesn't need special files, just CMake project
            build_project(config, project_path, None, None, None, None)?;
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
        "vs" | "vs2022" | "vs2019" | "vs2017" | "vs2015" | "vs2013" => {
            // Generate Visual Studio project using CMake
            if cfg!(target_os = "windows") {
                let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);

                // Derive VS version from the ide_type or from the version parameter
                let vs_version_hint = if base_ide_type != "vs" {
                    // Extract version from "vs20XX" format
                    Some(base_ide_type.trim_start_matches("vs"))
                } else {
                    version
                };

                println!("VS version hint: {:?}", vs_version_hint);

                // Map year to generator version
                let vs_version_str = match vs_version_hint {
                    Some("2022") => Some("17 2022"),
                    Some("2019") => Some("16 2019"),
                    Some("2017") => Some("15 2017"),
                    Some("2015") => Some("14 2015"),
                    Some("2013") => Some("12 2013"),
                    _ => None,
                };

                println!("Looking for VS generator with string: {:?}", vs_version_str);

                // Get appropriate VS generator
                let vs_generator = get_visual_studio_generator(vs_version_str);
                println!("Selected VS generator: {}", vs_generator);

                // Create suffix based on VS version
                let vs_suffix = vs_generator.replace("Visual Studio ", "vs").replace(" ", "");
                let vs_dir = format!("{}-{}", build_dir, vs_suffix.to_lowercase());
                let vs_path = project_path.join(&vs_dir);
                fs::create_dir_all(&vs_path)?;

                println!("Creating VS project in directory: {}", vs_dir);

                let mut cmd = vec![
                    "cmake".to_string(),
                    "..".to_string(),
                    "-G".to_string(),
                    vs_generator.clone(),
                ];

                // Add platform architecture parameter for VS 2019+
                // Modern VS generators require the -A parameter
                if vs_generator.contains("16 2019") || vs_generator.contains("17 2022") {
                    cmd.push("-A".to_string());

                    // Use arch from CLI if provided
                    let arch = match version {
                        Some(arch) if arch == "x86" || arch == "Win32" => "Win32",
                        Some(arch) if arch == "x64" => "x64",
                        Some(arch) if arch == "ARM" => "ARM",
                        Some(arch) if arch == "ARM64" => "ARM64",
                        _ => "x64" // Default to x64
                    };

                    cmd.push(arch.to_string());
                    println!("Adding architecture parameter: {}", arch);
                }

                println!("Final CMake command: {}", cmd.join(" "));

                run_command(cmd, Some(&vs_path.to_string_lossy().to_string()), None)?;
                println!("{}", format!("{} project generated in {}", vs_generator, vs_dir).green());
            } else {
                return Err("Visual Studio is only available on Windows".into());
            }
        },
        _ => {
            return Err(format!("Unsupported IDE type: {}. Supported types are: vscode, clion, xcode, vs, vs2022, vs2019, vs2017, vs2015, vs2013", ide_type).into());
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
    for project in &config.workspace.projects {
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
// Enhanced run_command function with Rust-style C++ error formatting
fn run_command(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<HashMap<String, String>>
) -> Result<(), Box<dyn std::error::Error>> {
    if cmd.is_empty() {
        return Err("Cannot run empty command".into());
    }

    // Check if this is a CMake command or other verbose compiler command
    let is_cmake = cmd[0].contains("cmake");
    let is_compiler_command = cmd[0].contains("cl") || cmd[0].contains("clang") || cmd[0].contains("gcc");
    let is_verbose_command = is_cmake || is_compiler_command;
    let is_build_command = is_cmake && cmd.len() > 1 && cmd.contains(&"--build".to_string());
    let show_output = false;

    let is_vcpkg_install = cmd.len() >= 2 &&
        (cmd[0].contains("vcpkg") ||
            cmd[0].ends_with("vcpkg.exe")) &&
        cmd[1] == "install";

    if is_vcpkg_install {
        // For vcpkg install, we'll run the command but handle output specially
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

        command.stdout(Stdio::piped());
        command.stderr(Stdio::piped());

        let output = command.output()?;
        let status = output.status;

        // If command failed, show error
        if !status.success() {
            let stderr = String::from_utf8_lossy(&output.stderr).to_string();
            println!("{}", "vcpkg install failed:".red());
            println!("{}", stderr);
            return Err("vcpkg install command failed".into());
        }

        // Process the packages mentioned in the command
        let packages: Vec<&str> = cmd.iter()
            .skip(2) // Skip "vcpkg install"
            .map(|s| s.as_str())
            .collect();

        println!("{}", "Packages already installed:".green());
        for pkg in packages {
            println!("  - {}", pkg.green());
        }

        println!("{}", "vcpkg dependencies configured successfully".green());
        return Ok(());
    }

    // Always capture output
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

    // Always pipe output so we can process it
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    // For verbose commands, just show "executing" and status
    if is_verbose_command && !show_output {
        if is_build_command {
            println!("{}", "Building...".blue());
        } else {
            println!("{}", format!("Executing: {}...", cmd[0]).blue());
        }
    } else {
        println!("{}", format!("Executing: {}", cmd.join(" ")).blue());
    }



    let output = command.output()?;
    let stdout = String::from_utf8_lossy(&output.stdout).to_string();
    let stderr = String::from_utf8_lossy(&output.stderr).to_string();

    // If the command failed, format errors in cbuild style directly in console
    if !output.status.success() {
        // For build failures, extract and format key errors
        if is_cmake || is_compiler_command {
            // Write full error output to a log file for reference
            let log_file = format!("cbuild_error_{}.log", chrono::Local::now().format("%Y%m%d_%H%M%S"));
            if let Ok(mut file) = File::create(&log_file) {
                let _ = file.write_all(format!("STDOUT:\n{}\n\nSTDERR:\n{}", stdout, stderr).as_bytes());
            }

            // Extract the main error files and patterns
            println!("{}", "Build failed with the following errors:".red().bold());

            // Extract key errors from the build output
            let error_count = display_syntax_errors(&stdout, &stderr);

            // If no errors were found using syntax patterns, show the raw error output
            if error_count == 0 {
                display_raw_errors(&stdout, &stderr);
            }

            println!("{}", format!("Full error details written to {}", log_file).yellow());
        } else {
            // For other commands, print the regular output
            println!("{}", stderr);
        }
        return Err("Command failed".into());
    }

    // Print output only when explicitly asked or for non-verbose commands
    if show_output || (!is_verbose_command && !stdout.is_empty()) {
        println!("{}", stdout);
    } else if !is_verbose_command {
        println!("{}", format!("Command '{}' completed successfully.", cmd[0]).green());
    }

    Ok(())
}


// Function to display syntax errors in a rust-like format directly in the console
fn display_syntax_errors(stdout: &str, stderr: &str) -> usize {
    // Use regex to extract errors from stdout and stderr
    let error_patterns = [
        // Clang/GCC style errors with file:line:col: error: message
        (regex::Regex::new(r"(?m)(.*?):(\d+):(\d+):\s+(error|warning|note):\s+(.*)").unwrap(), true),

        // MSVC style errors
        (regex::Regex::new(r"(?m)(.*?)\((\d+),(\d+)\):\s+(error|warning|note):\s+(.*)").unwrap(), true),

        // Rust-style errors
        (regex::Regex::new(r"(?m)^.*error(?:\[E\d+\])?:\s+(.*)$").unwrap(), true),

        // Generic errors
        (regex::Regex::new(r"(?m)^.*Error:\s+(.*)$").unwrap(), true),

        // Ninja errors
        (regex::Regex::new(r"(?m)^ninja:\s+error:\s+(.*)$").unwrap(), true),

        // CMake errors
        (regex::Regex::new(r"(?m)^CMake\s+Error:\s+(.*)$").unwrap(), true),

        // Missing file errors
        (regex::Regex::new(r"(?m).*(?:No such file or directory|cannot find|not found).*").unwrap(), false),
    ];

    // Used to track unique errors to avoid duplicates
    let mut seen_errors = HashSet::new();
    let mut displayed_error_count = 0;
    let max_errors_to_display = 10;

    // Look in stderr first (more likely to contain actual errors)
    for (pattern, is_important) in &error_patterns {
        for cap in pattern.captures_iter(stderr) {
            // If we already have enough errors, stop
            if displayed_error_count >= max_errors_to_display {
                break;
            }

            // Format depends on the pattern matched
            let error_text = if cap.len() >= 6 {
                // clang-style error with file, line, column
                let file = &cap[1];
                let line = &cap[2];
                let column = &cap[3];
                let error_type = &cap[4];
                let message = &cap[5];

                format!("{}:{}:{}: {}: {}", file, line, column, error_type, message)
            } else if cap.len() >= 2 {
                // Error message without file location
                cap[0].to_string()
            } else {
                continue;
            };

            // Deduplicate errors
            let error_key = error_text.chars()
                .filter(|c| !c.is_whitespace())
                .collect::<String>();

            if !seen_errors.contains(&error_key) {
                seen_errors.insert(error_key);

                // Format the error nicely
                println!("  {}", error_text.red());

                // Try to extract the source code line if it exists
                if cap.len() >= 6 {
                    let file = &cap[1];
                    let line_num = cap[2].parse::<usize>().unwrap_or(0);

                    // Try to find the corresponding code in the error output
                    if let Some(code_line) = extract_source_line(stdout, stderr, file, line_num) {
                        println!("      | {}", code_line.trim());

                        // Try to create the caret line showing the error position
                        let column = cap[3].parse::<usize>().unwrap_or(0);
                        let caret_line = create_caret_line(column, &cap[5]);
                        println!("      | {}", caret_line.red());
                    }
                }

                displayed_error_count += 1;
            }
        }

        // Also check stdout for errors (some tools output errors to stdout)
        for cap in pattern.captures_iter(stdout) {
            // If we already have enough errors, stop
            if displayed_error_count >= max_errors_to_display {
                break;
            }

            // Format depends on the pattern matched
            let error_text = if cap.len() >= 6 {
                // clang-style error with file, line, column
                let file = &cap[1];
                let line = &cap[2];
                let column = &cap[3];
                let error_type = &cap[4];
                let message = &cap[5];

                format!("{}:{}:{}: {}: {}", file, line, column, error_type, message)
            } else if cap.len() >= 2 {
                // Error message without file location
                cap[0].to_string()
            } else {
                continue;
            };

            // Deduplicate errors
            let error_key = error_text.chars()
                .filter(|c| !c.is_whitespace())
                .collect::<String>();

            if !seen_errors.contains(&error_key) {
                seen_errors.insert(error_key);

                // Format the error nicely
                println!("  {}", error_text.red());

                // Try to extract the source code line if it exists
                if cap.len() >= 6 {
                    let file = &cap[1];
                    let line_num = cap[2].parse::<usize>().unwrap_or(0);

                    // Try to find the corresponding code in the error output
                    if let Some(code_line) = extract_source_line(stdout, stderr, file, line_num) {
                        println!("      | {}", code_line.trim());

                        // Try to create the caret line showing the error position
                        let column = cap[3].parse::<usize>().unwrap_or(0);
                        let caret_line = create_caret_line(column, &cap[5]);
                        println!("      | {}", caret_line.red());
                    }
                }

                displayed_error_count += 1;
            }
        }
    }

    displayed_error_count
}

// Display raw errors when no structured errors could be found
fn display_raw_errors(stdout: &str, stderr: &str) {
    let mut error_lines = Vec::new();

    // First check stderr
    for line in stderr.lines() {
        if line.contains("error") || line.contains("Error") || line.contains("failed") ||
            line.contains("Failed") || line.contains("missing") {
            error_lines.push(line);
        }
    }

    // If no errors found in stderr, check stdout
    if error_lines.is_empty() {
        for line in stdout.lines() {
            if line.contains("error") || line.contains("Error") || line.contains("failed") ||
                line.contains("Failed") || line.contains("missing") {
                error_lines.push(line);
            }
        }
    }

    // Display the errors (up to 10)
    let max_errors = 10;
    for (i, line) in error_lines.iter().take(max_errors).enumerate() {
        // Clean up the line by removing ANSI escape codes and trimming
        let clean_line = line.trim();
        println!("  {}: {}", format!("Error {}", i+1).red().bold(), clean_line.red());
    }

    // Show how many more errors there are
    if error_lines.len() > max_errors {
        println!("  ... and {} more errors", error_lines.len() - max_errors);
    }

    // If we found no errors at all, show a generic message
    if error_lines.is_empty() {
        println!("  {}: Build command failed but no specific errors were found", "Error".red().bold());

        // Try to show the last few lines of stderr or stdout as context
        if !stderr.is_empty() {
            let last_lines: Vec<&str> = stderr.lines().rev().take(5).collect();
            println!("  Last few lines of stderr:");
            for line in last_lines.iter().rev() {
                println!("    {}", line);
            }
        } else if !stdout.is_empty() {
            let last_lines: Vec<&str> = stdout.lines().rev().take(5).collect();
            println!("  Last few lines of stdout:");
            for line in last_lines.iter().rev() {
                println!("    {}", line);
            }
        }
    }
}

// Try to extract the source code line that caused the error
fn extract_source_line(stdout: &str, stderr: &str, file: &str, line_num: usize) -> Option<String> {
    // Try to find patterns like "line_num |   code..." in the output
    let pattern = format!(r"(?m)^.*\s*{}\s*\|\s*(.*)$", line_num);
    let line_pattern = regex::Regex::new(&pattern).ok()?;

    // Check stderr first
    if let Some(cap) = line_pattern.captures(stderr) {
        if cap.len() >= 2 {
            return Some(cap[1].to_string());
        }
    }

    // Then check stdout
    if let Some(cap) = line_pattern.captures(stdout) {
        if cap.len() >= 2 {
            return Some(cap[1].to_string());
        }
    }

    // If we couldn't find the line in the build output, try to read from the file directly
    // (Only do this for safe paths)
    if !file.contains("..") && Path::new(file).exists() {
        if let Ok(content) = fs::read_to_string(file) {
            let lines: Vec<&str> = content.lines().collect();
            if line_num > 0 && line_num <= lines.len() {
                return Some(lines[line_num - 1].to_string());
            }
        }
    }

    None
}

// Create a caret line with the appropriate positioning for the error
fn create_caret_line(column: usize, message: &str) -> String {
    let mut caret_line = String::new();

    // Add spaces until we reach the column
    for _ in 0..column.saturating_sub(1) {
        caret_line.push(' ');
    }

    // Add the caret
    caret_line.push('^');

    // Add a few squiggly lines
    let squiggle_length = message.len().min(30);
    for _ in 0..squiggle_length.saturating_sub(1) {
        caret_line.push('~');
    }

    caret_line
}

fn extract_build_errors(stdout: &str, stderr: &str) -> Vec<String> {
    let mut errors = Vec::new();

    // Common error patterns to look for
    let error_patterns = [
        (regex::Regex::new(r"(?m)^(?:.*?)error(?::|\[)[^\n]*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*Error:.*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*fatal error:.*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*undefined reference to.*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*cannot find.*$").unwrap(), false),
        (regex::Regex::new(r"(?m)^.*No such file or directory.*$").unwrap(), false),
    ];

    // Search for errors in stderr first (more likely to contain actual errors)
    for (pattern, is_important) in &error_patterns {
        for cap in pattern.captures_iter(stderr) {
            let error = cap[0].to_string();
            if *is_important || errors.len() < 10 {  // Only include less important errors if we don't have many
                errors.push(error);
            }
        }
    }

    // If we didn't find any errors in stderr, check stdout too
    if errors.is_empty() {
        for (pattern, is_important) in &error_patterns {
            for cap in pattern.captures_iter(stdout) {
                let error = cap[0].to_string();
                if *is_important || errors.len() < 10 {
                    errors.push(error);
                }
            }
        }
    }

    // Sort errors by importance and uniqueness
    errors.sort_by_key(|e| (!e.contains("error:"), !e.contains("Error:"), e.clone()));

    // Remove duplicates
    errors.dedup();

    // To make the error list more manageable, group similar errors
    let mut grouped_errors = Vec::new();
    let mut seen_patterns = HashSet::new();

    for error in errors {
        // Try to extract just the key error message without file/line details
        if let Some(idx) = error.find(':') {
            let error_type = &error[idx+1..];
            // Get a simplified version for deduplication
            let simple = error_type.trim()
                .chars()
                .filter(|c| !c.is_whitespace())
                .collect::<String>();

            if simple.len() > 10 && !seen_patterns.contains(&simple) {
                seen_patterns.insert(simple);
                grouped_errors.push(error);
            }
        } else {
            grouped_errors.push(error);
        }
    }

    // If we have too many errors, just take the most important ones
    if grouped_errors.len() > 20 {
        grouped_errors.truncate(20);
    }

    grouped_errors
}


// Helper function to check if a command is a build command

fn is_build_command(cmd: &[String]) -> bool {
    if cmd.is_empty() {
        return false;
    }

    // Check if it's a CMake build command
    if cmd[0] == "cmake" && cmd.len() > 1 {
        return cmd.iter().any(|arg| arg == "--build");
    }

    // Check for other build commands
    matches!(cmd[0].as_str(), "make" | "ninja" | "cl" | "clang" | "g++" | "gcc" | "MSBuild.exe")
}



// Format C++ errors in a Rust-like style
fn format_cpp_errors_rust_style(output: &str) -> Vec<String> {
    let mut results = Vec::new();
    let mut diagnostics = Vec::new();

    // We use these regexes for capturing:
    let clang_style = Regex::new(r"(?m)(.*?):(\d+):(\d+): (error|warning|note): (.*)").unwrap();
    let msvc_style  = Regex::new(r"(?m)(.*?)\((\d+),(\d+)\): (error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap();
    // MSVC sometimes omits the column:
    let msvc_style_alt = Regex::new(r"(?m)(.*?)\((\d+)\): (error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap();

    // This will help gather lines that appear immediately after an error line for context
    let lines: Vec<&str> = output.lines().collect();

    // We'll store all matched diagnostics in a Vec, then we’ll add context.
    // clang/gcc pattern first:
    for cap in clang_style.captures_iter(output) {
        let file    = cap[1].to_string();
        let line    = cap[2].parse::<usize>().unwrap_or(0);
        let column  = cap[3].parse::<usize>().unwrap_or(0);
        let level   = cap[4].to_string();
        let message = cap[5].to_string();

        diagnostics.push(CompilerDiagnostic {
            file, line, column,
            level,
            message,
            context: vec![],
        });
    }

    // Next, MSVC pattern (with column):
    for cap in msvc_style.captures_iter(output) {
        let file    = cap[1].to_string();
        let line    = cap[2].parse::<usize>().unwrap_or(0);
        let column  = cap[3].parse::<usize>().unwrap_or(0);
        let level   = cap[4].to_string();
        let message = cap[5].to_string();

        diagnostics.push(CompilerDiagnostic {
            file, line, column,
            level,
            message,
            context: vec![],
        });
    }

    // Next, MSVC pattern (no column):
    for cap in msvc_style_alt.captures_iter(output) {
        let file    = cap[1].to_string();
        let line    = cap[2].parse::<usize>().unwrap_or(0);
        let level   = cap[3].to_string();
        let message = cap[4].to_string();
        // We guess a column of 1
        diagnostics.push(CompilerDiagnostic {
            file,
            line,
            column: 1,
            level,
            message,
            context: vec![],
        });
    }

    // Now we add some snippet context for each error line. We'll look up to ~2 lines around.
    // We do this by scanning through `lines` to find occurrences of e.g. "file:line"
    // But let's do a simpler approach: we have the file + line in diagnostics;
    // we won't open that file from disk — we'll just try to glean lines from the *compiler output itself.*
    // For truly Rust-like snippet context, you'd read the actual file from disk. But below uses the raw compiler lines.
    //
    // For big/bulky logs, you might want a more advanced approach. For now, we do a best-effort gather.

    // We'll just do the "In file included from..." lines or the next 1–3 lines if they appear to be code context.
    // For example, clang often shows something like:
    //   <source>:20:10: error: ...
    //   20 |     int x = ...
    //      |          ^
    //   ...
    // We'll collect those lines.

    // We'll do a simpler approach: if the line has "line:col: error", the next 2 lines might be code context
    for diag in &mut diagnostics {
        // We'll try to find the exact line text in the compiler output that has the caret afterward.
        // That usually is the next line after the line that matched. Let’s find that index:
        let re_for_search = format!("{}:{}:{}:", diag.file, diag.line, diag.column);
        let pos = lines.iter().position(|ln| ln.contains(&re_for_search));
        if let Some(idx) = pos {
            // Next lines might contain code context or caret:
            // We'll gather up to 3 lines after that
            for offset in 1..=3 {
                if idx + offset < lines.len() {
                    let possible_code_line = lines[idx + offset];
                    // If it looks like an error or warning line, we stop:
                    if possible_code_line.contains("error:") || possible_code_line.contains("warning:") {
                        break;
                    }
                    diag.context.push(possible_code_line.to_string());
                }
            }
        }
    }

    // Deduplicate
    let mut seen = HashSet::new();
    let mut unique_diagnostics = Vec::new();
    for d in &diagnostics {
        let key = format!("{}:{}:{}:{}:{}",
                          d.file, d.line, d.column, d.level, d.message
        );
        if !seen.contains(&key) {
            seen.insert(key);
            unique_diagnostics.push(d.clone());
        }
    }

    // Sort by level (error first, warning, then note) and by file + line
    unique_diagnostics.sort_by(|a, b| {
        let order_level = cmp_level(&a.level).cmp(&cmp_level(&b.level));
        if order_level != std::cmp::Ordering::Equal {
            return order_level;
        }
        let order_file = a.file.cmp(&b.file);
        if order_file != std::cmp::Ordering::Equal {
            return order_file;
        }
        a.line.cmp(&b.line)
    });

    // Now we create fancy multiline strings
    for diag in &mut unique_diagnostics {
        // Make a Rust-like header with error code
        let error_code = if diag.level == "error" { "E0001" }
        else if diag.level == "warning" { "W0001" }
        else { "N0001" };

        let color = match diag.level.as_str() {
            "error"   => Color::Red,
            "warning" => Color::Yellow,
            "note"    => Color::BrightBlue,
            _         => Color::White,
        };

        // Create a nicer header with error code and file location
        let header = format!(
            "{}[{}]: {}",
            format!("{}{}", diag.level.to_uppercase(), if diag.level == "error" { format!("[{}]", error_code) } else { String::new() }),
            short_path(&diag.file),
            diag.message
        )
            .color(color)
            .bold()
            .to_string();

        // Add line/column indicator like Rust
        let location = format!(
            " --> {}:{}:{}",
            diag.file,
            diag.line,
            diag.column
        )
            .color(color)
            .to_string();

        // Create a nicer code snippet with line numbers
        let mut snippet_lines = Vec::new();

        // Add line number for context
        snippet_lines.push(format!("{} |", diag.line.to_string().blue().bold()));

        // Add the code line if available
        if !diag.context.is_empty() {
            snippet_lines.push(format!("{} | {}", " ".blue().bold(), diag.context[0]));

            // Add caret line with appropriate spacing
            let mut indicator = String::new();
            for _ in 0..(diag.column.saturating_sub(1)) {
                indicator.push(' ');
            }
            indicator.push('^');
            for _ in 0..diag.message.len().min(3) {
                indicator.push('~');
            }

            snippet_lines.push(format!("{} | {}", " ".blue().bold(), indicator.color(color).bold()));
        }

        // Add a help message for common errors
        let help_message = get_help_for_error(&diag.message);
        if !help_message.is_empty() {
            snippet_lines.push(format!("{}: {}", "help".green().bold(), help_message));
        }

        results.push(header);
        results.push(location);
        results.push(String::new());  // Empty line for spacing
        results.extend(snippet_lines);
        results.push(String::new());  // Empty line for spacing
    }

    results
}

fn get_help_for_error(error_msg: &str) -> String {
    if error_msg.contains("undeclared identifier") {
        return "make sure the variable or function is declared before use".to_string();
    } else if error_msg.contains("template parameter pack") {
        return "variadic template parameters must be the last parameter in the template list".to_string();
    } else if error_msg.contains("no member named") {
        return "check for typos or ensure the class defines this member".to_string();
    } else if error_msg.contains("constexpr") && error_msg.contains("not a literal type") {
        return "add a constexpr constructor to the class or remove constexpr from the function".to_string();
    }

    String::new()
}

fn cmp_level(lv: &str) -> u8 {
    match lv {
        "error"   => 0,
        "warning" => 1,
        "note"    => 2,
        _         => 3,
    }
}

fn short_path(path: &str) -> String {
    // e.g. "C:/Arcnum/Spark/include\spark_event.hpp" => "include/spark_event.hpp"
    let p = std::path::Path::new(path);
    if let Some(fname) = p.file_name() {
        let fname_s = fname.to_string_lossy();
        // Also try to grab one directory level up:
        if let Some(parent) = p.parent() {
            if let Some(pdir) = parent.file_name() {
                return format!("{}/{}", pdir.to_string_lossy(), fname_s);
            }
        }
        fname_s.into_owned()
    } else {
        path.to_string()
    }
}

// Helper to process and format an error with its context
fn process_error_context(
    formatted_errors: &mut Vec<String>,
    seen: &mut HashSet<String>,
    file: &str,
    context: &[String],
    line_num: usize,
    column: usize,
    message: &str,
    error_type: &str
) {
    if file.is_empty() || line_num == 0 || context.is_empty() {
        return;
    }

    // Create a unique key for the error
    let key = format!("{}:{}:{}:{}", file, line_num, column, message);
    if seen.contains(&key) {
        return; // Skip duplicate error
    }
    seen.insert(key);

    let error_color = if error_type == "error" {
        "red"
    } else if error_type == "warning" {
        "yellow"
    } else {
        "blue"
    };

    let formatted_file = format_file_path(file);
    let error_header = format!("{}[{}]: {}", error_type.to_uppercase(), formatted_file, message);
    formatted_errors.push(format!("{}", error_header.color(error_color).bold()));

    // Pick a representative line from the context (e.g. second line if available)
    let error_line = if context.len() >= 3 {
        context.get(1).unwrap_or(&context[0]).clone()
    } else {
        context[0].clone()
    };

    formatted_errors.push(format!("   {} |", line_num.to_string().blue().bold()));
    formatted_errors.push(format!("   {} | {}", " ".blue().bold(), error_line));

    // Create an indicator line with a caret at the error column
    let mut indicator = String::new();
    for _ in 0..(column.saturating_sub(1)) {
        indicator.push(' ');
    }
    indicator.push('^');
    for _ in 0..message.len().min(3) {
        indicator.push('~');
    }
    formatted_errors.push(format!("   {} | {}", " ".blue().bold(), indicator.color(error_color).bold()));
    formatted_errors.push(String::new());
}

// Helper to format file paths for display
fn format_file_path(path: &str) -> String {
    // Extract just the filename and a bit of path context
    let path_obj = std::path::Path::new(path);
    if let Some(filename) = path_obj.file_name() {


        let filename_str = filename.to_string_lossy();

        // Try to include the parent directory for context
        if let Some(parent) = path_obj.parent() {
            if let Some(dirname) = parent.file_name() {
                return format!("{}/{}", dirname.to_string_lossy(), filename_str);
            }
        }

        return filename_str.to_string();
    }

    path.to_string()
}

// Analyze C++ errors and provide specific suggestions
fn analyze_cpp_errors(error_output: &str) -> Vec<String> {
    let mut suggestions = Vec::new();

    // Common C++ error patterns and their solutions
    if error_output.contains("constexpr function's return type") && error_output.contains("is not a literal type") {
        suggestions.push("Add a constexpr constructor to the class or remove constexpr from the function".to_string());
        suggestions.push("Example: 'constexpr VertexLayout() = default;' in your class definition".to_string());
    }

    if error_output.contains("template parameter pack must be the last template parameter") {
        suggestions.push("Move the variadic template parameter to the end of the parameter list".to_string());
        suggestions.push("Example: Change 'template <typename... As, typename... Bs>' to 'template <typename A, typename... Bs>'".to_string());
    }

    if error_output.contains("use of undeclared identifier 'AllIn'") {
        suggestions.push("Define the 'AllIn' template concept before using it".to_string());
        suggestions.push("Example: 'template<typename T, typename... Types> concept AllIn = (std::is_same_v<T, Types> || ...);'".to_string());
    }

    if error_output.contains("member initializer") && error_output.contains("does not name a non-static data member") {
        suggestions.push("Declare the member variable in your class before initializing it in the constructor".to_string());
        suggestions.push("Example: Add 'VariantT m_variant;' to your class definition".to_string());
    }

    if error_output.contains("No such file or directory") || error_output.contains("cannot open include file") {
        suggestions.push("Check that the include path is correct and the file exists".to_string());
        suggestions.push("Make sure all dependencies are installed with 'cbuild deps'".to_string());
    }

    if error_output.contains("undefined reference to") || error_output.contains("unresolved external symbol") {
        suggestions.push("Ensure the required library is linked in your cbuild.toml".to_string());
        suggestions.push("Check that the function/symbol is defined in the linked libraries".to_string());
    }

    if error_output.contains("error[E0282]") || error_output.contains("type annotations needed") {
        suggestions.push("Add explicit type annotations to ambiguous variable declarations".to_string());
        suggestions.push("Example: Change 'let x = func();' to 'let x: ReturnType = func();'".to_string());
    }

    // Return only unique suggestions
    suggestions.sort();
    suggestions.dedup();
    suggestions
}

// Enhanced build_project function with cleaner error reporting
fn build_project(
    config: &ProjectConfig,
    project_path: &Path,
    config_type: Option<&str>,
    variant_name: Option<&str>,
    cross_target: Option<&str>,
    workspace_config: Option<&WorkspaceConfig>
) -> Result<(), Box<dyn std::error::Error>> {
    // First, ensure all required tools are installed
    ensure_build_tools(config)?;

    let build_dir = config.build.build_dir.as_deref().unwrap_or(DEFAULT_BUILD_DIR);
    let build_path = if let Some(target) = cross_target {
        project_path.join(format!("{}-{}", build_dir, target))
    } else {
        project_path.join(build_dir)
    };

    // Ensure the build directory exists.
    fs::create_dir_all(&build_path)?;

    // Determine the generator being used.
    let generator = get_cmake_generator(config)?;

    // Check if we need to re-run configuration:
    // For example, if there's no CMakeCache.txt or, when using Ninja, build.ninja is missing.
    let needs_configure = {
        if !build_path.join("CMakeCache.txt").exists() {
            true
        } else if generator == "Ninja" && !build_path.join("build.ninja").exists() {
            true
        } else {
            false
        }
    };

    if needs_configure {
        println!("{}", "Project not configured yet or build files missing, configuring...".blue());
        configure_project(config, project_path, config_type, variant_name, cross_target, workspace_config)?;
    }

    // Run pre-build hooks
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
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.pre_build, project_path, Some(hook_env.clone()))?;
    }

    // Build using CMake
    let mut cmd = vec!["cmake".to_string(), "--build".to_string(), ".".to_string()];
    let build_type = get_build_type(config, config_type);
    cmd.push("--config".to_string());
    cmd.push(build_type.clone());

    // Add parallelism for faster builds
    let num_threads = num_cpus::get();
    cmd.push("--parallel".to_string());
    cmd.push(format!("{}", num_threads));

    // Try to build with quiet output
    println!("{}", format!("Building {} in {} configuration...", config.project.name, build_type).blue());
    let build_result = run_command(cmd, Some(&build_path.to_string_lossy().to_string()), None);

    if let Err(e) = build_result {
        println!("{}", "Build failed with errors. See above for details.".red());
        return Err(e);
    }

    // Run post-build hooks
    if let Some(hooks) = &config.hooks {
        run_hooks(&hooks.post_build, project_path, Some(hook_env))?;
    }

    println!("{}", format!("Build completed successfully ({} configuration)", build_type).green());
    Ok(())
}


fn analyze_build_error(error_output: &str) -> Vec<String> {
    let mut suggestions = Vec::new();

    // Common C++ build errors and their solutions
    if error_output.contains("No such file or directory") || error_output.contains("cannot open include file") {
        suggestions.push("Missing header file. Check that all dependencies are installed.".to_string());
        suggestions.push("Verify include paths are correct in your cbuild.toml.".to_string());
    }

    if error_output.contains("undefined reference to") || error_output.contains("unresolved external symbol") {
        suggestions.push("Missing library or object file. Check that all dependencies are installed.".to_string());
        suggestions.push("Verify library paths and link options in your cbuild.toml.".to_string());
        suggestions.push("Make sure the library was built with the same compiler/settings.".to_string());
    }

    if error_output.contains("incompatible types") || error_output.contains("cannot convert") {
        suggestions.push("Type mismatch error. This might be caused by using different compiler flags or standard versions.".to_string());
        suggestions.push("Make sure all libraries and your code use compatible C++ standards.".to_string());
    }

    if error_output.contains("permission denied") {
        suggestions.push("Permission error. Try running the command with administrative privileges.".to_string());
        suggestions.push("Check if the files or directories are read-only or locked by another process.".to_string());
    }

    if error_output.contains("vcpkg") && error_output.contains("not found") {
        suggestions.push("vcpkg issue. Make sure vcpkg is properly installed.".to_string());
        suggestions.push("Check if the vcpkg path in cbuild.toml is correct.".to_string());
        suggestions.push("Try running 'cbuild deps' to install dependencies first.".to_string());
    }

    if error_output.contains("cl.exe") && error_output.contains("not recognized") {
        suggestions.push("Visual Studio tools not found. Make sure Visual Studio or Build Tools are installed.".to_string());
        suggestions.push("Try opening a Developer Command Prompt or run from Visual Studio Command Prompt.".to_string());
        suggestions.push("Make sure the environment variables are set correctly.".to_string());
    }

    if error_output.contains("CMake Error") {
        if error_output.contains("generator") {
            suggestions.push("CMake generator issue. Make sure the requested generator is installed.".to_string());
            suggestions.push("Try using a different generator in cbuild.toml or let CBuild auto-detect.".to_string());
        }

        if error_output.contains("Could not find") {
            suggestions.push("CMake dependency issue. Make sure all required packages are installed.".to_string());
            suggestions.push("Run 'cbuild deps' to install dependencies.".to_string());
        }
    }

    // If no specific issues found, provide general suggestions
    if suggestions.is_empty() {
        suggestions.push("Check if all build tools are installed and available in PATH.".to_string());
        suggestions.push("Make sure all dependencies are installed with 'cbuild deps'.".to_string());
        suggestions.push("Try using a different compiler or generator.".to_string());
        suggestions.push("Run 'cbuild clean' and then try building again.".to_string());
    }

    suggestions
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

/// Map a single "universal" token to either MSVC/clang-cl flags (slash-based)
/// or GCC/Clang (GNU driver) flags (dash-based). Returns a Vec of strings
/// because one token might expand to multiple real flags.
fn map_token(token: &str, msvc_style: bool) -> Vec<String> {
    match token {
        // --- Common optimization levels ---
        "NO_OPT" => {
            // /Od vs -O0
            if msvc_style {
                vec!["/Od".to_string()]
            } else {
                vec!["-O0".to_string()]
            }
        }
        "NO_WARNINGS" => {
            if msvc_style {
                // MSVC/clang-cl
                vec!["/W0".to_string()] // or "/W0"
            } else {
                // GCC/Clang (GNU driver)
                vec!["-w".to_string()]
            }
        }
        "OPTIMIZE" => {
            // /O2 vs -O2
            if msvc_style {
                vec!["/O2".to_string()]
            } else {
                vec!["-O2".to_string()]
            }
        }
        "OPTIMIZE_MAX" => {
            // /O2 vs -O2
            if msvc_style {
                vec!["/O3".to_string()]
            } else {
                vec!["-O3".to_string()]
            }
        }
        "MIN_SIZE" => {
            // /O1 vs -Os
            if msvc_style {
                vec!["/O1".to_string()]
            } else {
                vec!["-Os".to_string()]
            }
        }
        "OB1" => {
            // /Ob1 vs maybe -finline-limit=... or we skip
            if msvc_style {
                vec!["/Ob1".to_string()]
            } else {
                // There's no direct single GCC/Clang equivalent to /Ob1
                // so either skip or guess
                vec![]
            }
        }
        "OB2" => {
            // /Ob2 vs maybe -finline-functions
            if msvc_style {
                vec!["/Ob2".to_string()]
            } else {
                vec!["-finline-functions".to_string()]
            }
        }

        // --- Debug info ---
        "DEBUG_INFO" => {
            // /Zi vs -g
            if msvc_style {
                vec!["/Zi".to_string()]
            } else {
                vec!["-g".to_string()]
            }
        }
        "RTC1" => {
            // /RTC1 is a run-time check in MSVC, not in GCC/Clang.
            // There's no perfect equivalent. We can skip or do -fstack-protector
            if msvc_style {
                vec!["/RTC1".to_string()]
            } else {
                vec![]
            }
        }

        // --- Link-time optimization ---
        "LTO" => {
            // /GL vs -flto
            if msvc_style {
                vec!["/GL".to_string()]
            } else {
                vec!["-flto".to_string()]
            }
        }

        // --- Parallel or auto-parallel ---
        "PARALLEL" => {
            // /Qpar vs maybe -fopenmp
            if msvc_style {
                vec!["/Qpar".to_string()]
            } else {
                // Up to you if you want -fopenmp or skip
                vec!["-fopenmp".to_string()]
            }
        }

        // --- Memory safety checks ---
        "MEMSAFE" => {
            // /sdl and /GS vs e.g. -fsanitize=address etc.
            if msvc_style {
                vec!["/sdl".to_string(), "/GS".to_string()]
            } else {
                vec!["-fsanitize=address".to_string(), "-fno-omit-frame-pointer".to_string()]
            }
        }

        // If user wrote DNDEBUG or some define as a "flag," we can handle it here:
        "DNDEBUG" => {
            // Typically you'd do -DNDEBUG as a define in your code, but here's how if you want it in flags
            if msvc_style {
                // Actually MSVC style doesn't do /D NDEBUG? We might do /DNDEBUG
                vec!["/DNDEBUG".to_string()]
            } else {
                vec!["-DNDEBUG".to_string()]
            }
        }

        // If there's some leftover or unknown token
        _ => {
            println!("Warning: unrecognized token `{}`, passing unchanged.", token);
            vec![token.to_string()]
        }
    }
}

/// Convert a list of universal tokens into real slash-based or dash-based flags
/// depending on `is_msvc_style`.
fn parse_universal_flags(tokens: &[String], is_msvc_style: bool) -> Vec<String> {
    let mut result = Vec::new();
    for t in tokens {
        let expanded = map_token(t, is_msvc_style);
        result.extend(expanded);
    }
    result
}

/// Decide slash or dash based on the user-chosen compiler label
fn is_msvc_style_for_config(config: &ProjectConfig) -> bool {
    let label = get_effective_compiler_label(config).to_lowercase();
    // If user says "msvc" or "clang-cl", do slash-based flags
    matches!(label.as_str(), "msvc" | "clang-cl")
}

// Helper function to detect available Visual Studio versions
fn detect_visual_studio_versions() -> Vec<(String, String)> {
    let mut versions = Vec::new();

    // First, try vswhere (most reliable on modern Windows)
    let vswhere_success = if has_command("vswhere") {
        if let Ok(output) = Command::new("vswhere")
            .arg("-latest")
            .arg("-products")
            .arg("*")
            .arg("-requires")
            .arg("Microsoft.Component.MSBuild")
            .arg("-property")
            .arg("installationVersion")
            .output()
        {
            let version_str = String::from_utf8_lossy(&output.stdout).trim().to_string();
            if !version_str.is_empty() {
                // Parse major version
                if let Some(major) = version_str.split('.').next() {
                    let vs_name = match major {
                        "17" => "Visual Studio 17 2022",
                        "16" => "Visual Studio 16 2019",
                        "15" => "Visual Studio 15 2017",
                        "14" => "Visual Studio 14 2015",
                        "12" => "Visual Studio 12 2013",
                        _ => "Unknown Visual Studio",
                    };

                    versions.push((vs_name.to_string(), format!("{}.0", major)));
                    true
                } else {
                    false
                }
            } else {
                false
            }
        } else {
            false
        }
    } else {
        false
    };

    // If vswhere didn't work, try registry lookups
    if !vswhere_success {
        for (version, reg_key, generator) in &[
            ("17.0", "17.0", "Visual Studio 17 2022"),
            ("16.0", "16.0", "Visual Studio 16 2019"),
            ("15.0", "15.0", "Visual Studio 15 2017"),
            ("14.0", "14.0", "Visual Studio 14 2015"),
            ("12.0", "12.0", "Visual Studio 12 2013")
        ] {
            if let Ok(output) = Command::new("powershell")
                .arg("-Command")
                .arg(format!("(Get-ItemProperty -Path 'HKLM:\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7' -Name '{}' -ErrorAction SilentlyContinue).'{}'", reg_key, reg_key))
                .output()
            {
                if !output.stdout.is_empty() {
                    versions.push((generator.to_string(), version.to_string()));
                }
            }
        }

        // Try to find Build Tools instead of full VS
        if versions.is_empty() {
            if let Ok(output) = Command::new("powershell")
                .arg("-Command")
                .arg("Get-ChildItem 'HKLM:\\SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7'")
                .output()
            {
                let result = String::from_utf8_lossy(&output.stdout);

                // Very simple check - look for a common version number
                if result.contains("17.0") {
                    versions.push(("Visual Studio 17 2022".to_string(), "17.0".to_string()));
                } else if result.contains("16.0") {
                    versions.push(("Visual Studio 16 2019".to_string(), "16.0".to_string()));
                } else if result.contains("15.0") {
                    versions.push(("Visual Studio 15 2017".to_string(), "15.0".to_string()));
                }
            }
        }
    }

    // Try more direct detection: If we have cl.exe, try to determine its version
    if versions.is_empty() && has_command("cl") {
        if let Ok(output) = Command::new("cl").output() {
            let cl_version = String::from_utf8_lossy(&output.stderr);
            if cl_version.contains("19.30") || cl_version.contains("19.3") {
                versions.push(("Visual Studio 17 2022".to_string(), "17.0".to_string()));
            } else if cl_version.contains("19.20") || cl_version.contains("19.2") {
                versions.push(("Visual Studio 16 2019".to_string(), "16.0".to_string()));
            } else if cl_version.contains("19.1") {
                versions.push(("Visual Studio 15 2017".to_string(), "15.0".to_string()));
            }
        }
    }

    // If no version detected, provide modern fallbacks
    if versions.is_empty() {
        if has_command("cl") {
            // If cl.exe exists but we couldn't determine version, default to 2022
            versions.push(("Visual Studio 17 2022".to_string(), "17.0".to_string()));
        } else {
            // No VS detected but will still try to use a modern version
            versions.push(("Visual Studio 17 2022".to_string(), "17.0".to_string()));
        }
    }

    // Sort by version (newest first)
    versions.sort_by(|a, b| {
        let a_ver = a.1.split('.').next().unwrap_or("0").parse::<i32>().unwrap_or(0);
        let b_ver = b.1.split('.').next().unwrap_or("0").parse::<i32>().unwrap_or(0);
        b_ver.cmp(&a_ver)
    });

    versions
}

// Simplified version
fn get_visual_studio_generator(requested_version: Option<&str>) -> String {
    println!("Detecting Visual Studio versions...");
    let versions = detect_visual_studio_versions();

    println!("Available Visual Studio versions: {:?}", versions
        .iter()
        .map(|(name, version)| format!("{} ({})", name, version))
        .collect::<Vec<_>>());

    println!("Requested version: {:?}", requested_version);

    // If a specific version is requested, try to find it
    if let Some(requested) = requested_version {
        for (name, _) in &versions {
            if name.to_lowercase().contains(&requested.to_lowercase()) {
                println!("Found matching VS version: {}", name);
                return name.clone();
            }
        }
        println!("No exact match for '{}', falling back to latest", requested);
    }

    // Otherwise return the latest (first in the list)
    if let Some((name, _)) = versions.first() {
        println!("Using latest VS version: {}", name);
        name.clone()
    } else {
        // Modern fallback
        println!("No VS versions detected, using fallback: Visual Studio 17 2022");


        "Visual Studio 17 2022".to_string()
    }
}
