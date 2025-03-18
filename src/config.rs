use std::collections::HashMap;
use std::fmt;
use std::fs::File;
use std::io::Write;
use std::path::Path;
use colored::Colorize;
use serde::{Deserialize, Serialize};
use crate::CFORGE_FILE;

pub struct CommandProgressData {
    pub lines_processed: usize,
    pub percentage_markers: Vec<f32>,
    pub last_reported_progress: f32,
    pub completed: bool,
    pub error_encountered: bool,
    pub total_lines_estimate: usize,
}

pub struct BuildProgressState {
    pub compiled_files: usize,
    pub total_files: usize,
    pub current_percentage: f32,
    pub errors: Vec<String>,
    pub is_linking: bool,
}

pub struct PackageInstallState {
    pub current_package: String,
    pub current_percentage: f32,
    pub packages_completed: usize,
    pub total_packages: usize,
}


// Configuration Models
#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct WorkspaceConfig {
    pub workspace: WorkspaceWithProjects,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct WorkspaceWithProjects {
    pub name: String,
    pub projects: Vec<String>,
    pub startup_projects: Option<Vec<String>>, // Projects that can be set as startup
    pub default_startup_project: Option<String>, // Default startup project
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct PCHConfig {
    pub enabled: bool,
    pub header: String,
    pub source: Option<String>,
    pub include_directories: Option<Vec<String>>,
    pub compiler_options: Option<Vec<String>>,  // Additional compiler options for PCH
    pub only_for_targets: Option<Vec<String>>,  // Apply PCH only to specific targets
    pub exclude_sources: Option<Vec<String>>,   // Sources to exclude from PCH
    pub disable_unity_build: Option<bool>,      // Disable unity build when using PCH (can cause conflicts)
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ProjectConfig {
    pub project: ProjectInfo,
    pub build: BuildConfig,
    #[serde(default)]
    pub dependencies: DependenciesConfig,
    #[serde(default)]
    pub targets: HashMap<String, TargetConfig>,
    #[serde(default)]
    pub platforms: Option<HashMap<String, PlatformConfig>>,
    #[serde(default)]
    pub output: OutputConfig,
    #[serde(default)]
    pub hooks: Option<BuildHooks>,
    #[serde(default)]
    pub scripts: Option<ScriptDefinitions>,
    #[serde(default)]
    pub variants: Option<BuildVariants>,
    #[serde(default)]
    pub cross_compile: Option<CrossCompileConfig>,
    #[serde(default)]
    pub pch: Option<PCHConfig>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct ProjectInfo {
    pub name: String,
    pub version: String,
    pub description: String,
    #[serde(rename = "type")]
    pub project_type: String, // executable, library, static-library, header-only
    pub language: String,     // c, c++
    pub standard: String,     // c11, c++17, etc.
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct BuildConfig {
    pub build_dir: Option<String>,
    pub generator: Option<String>,
    pub default_config: Option<String>,  // Default configuration to use (Debug, Release, etc.)
    pub debug: Option<bool>,             // Legacy option, kept for backwards compatibility
    pub cmake_options: Option<Vec<String>>,
    pub configs: Option<HashMap<String, ConfigSettings>>,  // Configuration-specific settings
    pub compiler: Option<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct ConfigSettings {
    pub defines: Option<Vec<String>>,
    pub flags: Option<Vec<String>>,
    pub link_flags: Option<Vec<String>>,
    pub output_dir_suffix: Option<String>,  // Optional suffix for output directories
    pub cmake_options: Option<Vec<String>>,  // Configuration-specific CMake options
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct DependenciesConfig {
    #[serde(default)]
    pub vcpkg: VcpkgConfig,
    pub system: Option<Vec<String>>,
    pub cmake: Option<Vec<String>>,
    #[serde(default)]
    pub conan: ConanConfig,       // Conan package manager support
    #[serde(default)]
    pub custom: Vec<CustomDependency>, // Custom dependencies
    #[serde(default)]
    pub git: Vec<GitDependency>,  // Git dependencies
    #[serde(default)]
    pub workspace: Vec<WorkspaceDependency>, // Dependencies on other workspace projects
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct WorkspaceDependency {
    pub name: String,             // Name of the project in workspace
    pub link_type: Option<String>, // "static", "shared", or "interface" (default: depends on target type)
    pub include_paths: Option<Vec<String>>, // Additional include paths relative to the dependency
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct VcpkgConfig {
    pub enabled: bool,
    pub path: Option<String>,
    pub packages: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct ConanConfig {
    pub enabled: bool,
    pub packages: Vec<String>,
    pub options: Option<HashMap<String, String>>,
    pub generators: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct CustomDependency {
    pub name: String,
    pub url: String,
    pub version: Option<String>,
    pub cmake_options: Option<Vec<String>>,
    pub build_command: Option<String>,
    pub install_command: Option<String>,
    pub include_path: Option<String>,
    pub library_path: Option<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct GitDependency {
    pub name: String,
    pub url: String,
    pub branch: Option<String>,
    pub tag: Option<String>,
    pub commit: Option<String>,
    pub cmake_options: Option<Vec<String>>,
    pub shallow: Option<bool>,
    pub update: Option<bool>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct TargetConfig {
    pub sources: Vec<String>,
    pub include_dirs: Option<Vec<String>>,
    pub defines: Option<Vec<String>>,
    pub links: Option<Vec<String>>,
    pub platform_links: Option<HashMap<String, Vec<String>>>, // Platform-specific links
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct PlatformConfig {
    pub compiler: Option<String>,
    pub defines: Option<Vec<String>>,
    pub flags: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct OutputConfig {
    pub bin_dir: Option<String>,
    pub lib_dir: Option<String>,
    pub obj_dir: Option<String>,
}

#[derive(Debug, Clone)]
pub struct CompilerDiagnostic {
    pub file: String,
    pub line: usize,
    pub column: usize,
    pub level: String,    // "error", "warning", "note", etc.
    pub message: String,
    pub context: Vec<String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct BuildHooks {
    pub pre_configure: Option<Vec<String>>,
    pub post_configure: Option<Vec<String>>,
    pub pre_build: Option<Vec<String>>,
    pub post_build: Option<Vec<String>>,
    pub pre_clean: Option<Vec<String>>,
    pub post_clean: Option<Vec<String>>,
    pub pre_install: Option<Vec<String>>,
    pub post_install: Option<Vec<String>>,
    pub pre_run: Option<Vec<String>>,
    pub post_run: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct ScriptDefinitions {
    pub scripts: HashMap<String, String>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct BuildVariants {
    pub default: Option<String>,
    pub variants: HashMap<String, VariantSettings>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct VariantSettings {
    pub description: Option<String>,
    pub defines: Option<Vec<String>>,
    pub flags: Option<Vec<String>>,
    pub dependencies: Option<Vec<String>>,
    pub features: Option<Vec<String>>,
    pub platforms: Option<Vec<String>>,  // Platforms this variant is valid for
    pub cmake_options: Option<Vec<String>>,
}

#[derive(Debug, Serialize, Deserialize, Clone, Default)]
pub struct CrossCompileConfig {
    pub enabled: bool,
    pub target: String,
    pub toolchain: Option<String>,
    pub sysroot: Option<String>,
    pub cmake_toolchain_file: Option<String>,
    pub define_prefix: Option<String>,  // Prefix for CMake defines (e.g., "ANDROID")
    pub flags: Option<Vec<String>>,
    pub env_vars: Option<HashMap<String, String>>,
}

// System Detection
pub struct SystemInfo {
    pub os: String,
    pub arch: String,
    pub compiler: String,
}

#[derive(Debug)]
pub struct CforgeError {
    pub message: String,
    pub file_path: Option<String>,
    pub line_number: Option<usize>,
    pub context: Option<String>,
}

impl CforgeError {
    pub fn new(message: &str) -> Self {
        CforgeError {
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

impl fmt::Display for CforgeError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "cforge Error: {}", self.message)?;

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

impl std::error::Error for CforgeError {}

pub fn save_project_config(config: &ProjectConfig, path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let config_path = path.join(CFORGE_FILE);
    let toml_string = toml::to_string_pretty(config)?;
    let mut file = File::create(config_path)?;
    file.write_all(toml_string.as_bytes())?;
    println!("{}", format!("Configuration saved to {}", path.join(CFORGE_FILE).display()).green());
    Ok(())
}