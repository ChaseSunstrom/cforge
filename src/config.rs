use std::collections::HashMap;
use std::{env, fmt, fs};
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use colored::Colorize;
use serde::{Deserialize, Serialize};
use crate::{detect_system_info, ensure_compiler_available, get_visual_studio_generator, has_command, is_workspace, parse_toml_error, CFORGE_FILE, DEFAULT_BIN_DIR, DEFAULT_BUILD_DIR, DEFAULT_LIB_DIR, DEFAULT_OBJ_DIR, VCPKG_DEFAULT_DIR, WORKSPACE_FILE};
use crate::errors::CforgeError;

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
pub struct TestConfig {
    // Directory where tests are located (default: "tests")
    pub directory: Option<String>,

    // Whether to enable testing (default: true)
    pub enabled: Option<bool>,

    // Default test timeout in seconds (default: 30)
    pub timeout: Option<u64>,

    // Default test labels
    pub labels: Option<Vec<String>>,

    // Test executables
    pub executables: Option<Vec<TestExecutable>>,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct TestExecutable {
    // Name of the test executable
    pub name: String,

    // Source files for this test executable
    pub sources: Vec<String>,

    // Include directories
    pub includes: Option<Vec<String>>,

    // Libraries to link against
    pub links: Option<Vec<String>>,

    // Preprocessor definitions
    pub defines: Option<Vec<String>>,

    // Command-line arguments to pass to the test
    pub args: Option<Vec<String>>,

    // Test timeout in seconds (overrides default)
    pub timeout: Option<u64>,

    // Test labels for filtering
    pub labels: Option<Vec<String>>,
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
    pub tests: TestConfig,
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
pub struct ErrorDiagnostic {
    pub file: String,
    pub line: usize,
    pub column: usize,
    pub level: String,    // "error", "warning", or "note"
    pub message: String,
    pub error_code: String,  // Generated error code (e.g. E4758)
    pub source_line: Option<String>,
    pub suggestion: Option<String>,
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


impl Default for DependenciesConfig {
    fn default() -> Self {
        DependenciesConfig {
            vcpkg: VcpkgConfig {
                enabled: false,
                path: None,  // Don't include this in default
                packages: Vec::new(),
            },
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
            path: None,  // Changed from Some(VCPKG_DEFAULT_DIR) to None
            packages: Vec::new(),
        }
    }
}


pub fn save_project_config(config: &ProjectConfig, path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    let config_path = path.join(CFORGE_FILE);

    // Convert to a string, but then manually clean up the output
    let mut toml_string = toml::to_string_pretty(config)?;

    // Clean up empty sections
    let sections_to_remove = [
        "\n[tests]\n\n",
        "\n[dependencies]\ncustom = []\ngit = []\nworkspace = []\n\n",
        "\n[dependencies.vcpkg]\nenabled = false\npackages = []\n\n",
        "\n[dependencies.conan]\nenabled = false\npackages = []\n\n",
    ];

    for section in sections_to_remove {
        toml_string = toml_string.replace(section, "\n");
    }

    // Remove empty platforms section
    if let Some(platforms) = &config.platforms {
        for (os_name, platform) in platforms {
            if platform.defines.is_none() && platform.flags.is_none() {
                let platform_section = format!("\n[platforms.{}]\ncompiler = \"{}\"\n\n",
                                               os_name, platform.compiler.as_ref().unwrap_or(&"".to_string()));
                toml_string = toml_string.replace(&platform_section, "\n");
            }
        }
    }

    // Remove sequential newlines to clean up the file
    while toml_string.contains("\n\n\n") {
        toml_string = toml_string.replace("\n\n\n", "\n\n");
    }

    let mut file = File::create(config_path)?;
    file.write_all(toml_string.as_bytes())?;
    println!("{}", format!("Configuration saved to {}", path.join(CFORGE_FILE).display()).green());
    Ok(())
}

pub fn load_project_config(path: Option<&Path>) -> Result<ProjectConfig, CforgeError> {
    let project_path = path.unwrap_or_else(|| Path::new("."));

    // When loading from a workspace dependency, we need to handle
    // both absolute paths and paths relative to the workspace root
    let config_path = if project_path.is_absolute() {
        project_path.join(CFORGE_FILE)
    } else if is_workspace() && !project_path.starts_with(".") {
        // For non-relative paths in a workspace, check if they should
        // be prefixed with "projects/"
        let with_projects = Path::new("projects").join(project_path);
        if with_projects.join(CFORGE_FILE).exists() {
            with_projects.join(CFORGE_FILE)
        } else {
            project_path.join(CFORGE_FILE)
        }
    } else {
        project_path.join(CFORGE_FILE)
    };

    if !config_path.exists() {
        return Err(CforgeError::new(&format!(
            "Configuration file '{}' not found. Run 'cforge init' to create one.",
            config_path.display()
        )));
    }

    let toml_str = match fs::read_to_string(&config_path) {
        Ok(content) => content,
        Err(e) => return Err(CforgeError::new(&format!(
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

pub fn save_workspace_config(config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    let toml_string = toml::to_string_pretty(config)?;
    let mut file = File::create(WORKSPACE_FILE)?;
    file.write_all(toml_string.as_bytes())?;
    println!("{}", format!("Configuration saved to {}", WORKSPACE_FILE).green());
    Ok(())
}

pub fn load_workspace_config() -> Result<WorkspaceConfig, CforgeError> {
    let workspace_path = Path::new(WORKSPACE_FILE);

    if !workspace_path.exists() {
        return Err(CforgeError::new(&format!(
            "Workspace file '{}' not found", WORKSPACE_FILE
        )));
    }

    let toml_str = match fs::read_to_string(workspace_path) {
        Ok(content) => content,
        Err(e) => return Err(CforgeError::new(&format!(
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

pub fn create_default_config() -> ProjectConfig {
    // Get system info for determining defaults
    let system_info = detect_system_info();

    // Create a simpler config settings map
    let mut configs = HashMap::new();

    // Debug configuration
    configs.insert("Debug".to_string(), ConfigSettings {
        defines: Some(vec!["DEBUG".to_string()]),
        flags: Some(vec!["DEBUG_INFO".to_string(), "NO_OPT".to_string()]),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    // Release configuration
    configs.insert("Release".to_string(), ConfigSettings {
        defines: Some(vec!["NDEBUG".to_string()]),
        flags: Some(vec!["OPTIMIZE".to_string()]),
        link_flags: None,
        output_dir_suffix: None,
        cmake_options: None,
    });

    // Basic platform config
    let mut platforms = HashMap::new();
    let os_name = if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "darwin"
    } else {
        "linux"
    };
    platforms.insert(os_name.to_string(), PlatformConfig {
        compiler: Some(system_info.compiler.clone()),
        defines: None,
        flags: None,
    });

    // Simple target config
    let mut targets = HashMap::new();
    targets.insert("default".to_string(), TargetConfig {
        sources: vec!["src/**.cpp".to_string(), "src/**.c".to_string()],
        include_dirs: Some(vec!["include".to_string()]),
        defines: None,
        links: None,
        platform_links: None,
    });

    // Create the full config
    ProjectConfig {
        project: ProjectInfo {
            name: env::current_dir()
                .unwrap_or_else(|_| PathBuf::from("my_project"))
                .file_name()
                .unwrap_or_else(|| std::ffi::OsStr::new("my_project"))
                .to_string_lossy()
                .to_string(),
            version: "0.1.0".to_string(),
            description: "A C/C++ project built with cforge".to_string(),
            project_type: "executable".to_string(),
            language: "c++".to_string(),
            standard: "c++17".to_string(),
        },
        build: BuildConfig {
            build_dir: Some(DEFAULT_BUILD_DIR.to_string()),
            generator: Some("default".to_string()),
            default_config: Some("Debug".to_string()),
            debug: Some(true),
            cmake_options: None,
            configs: Some(configs),
            compiler: Some(system_info.compiler.clone()),
        },
        // Empty defaults for unneeded sections
        dependencies: DependenciesConfig::default(),
        tests: TestConfig::default(),
        targets,
        platforms: Some(platforms),
        output: OutputConfig {
            bin_dir: Some(DEFAULT_BIN_DIR.to_string()),
            lib_dir: Some(DEFAULT_LIB_DIR.to_string()),
            obj_dir: Some(DEFAULT_OBJ_DIR.to_string()),
        },
        hooks: None,
        scripts: None,
        variants: None,
        cross_compile: None,
        pch: None,
    }
}

pub fn create_library_config() -> ProjectConfig {
    let mut config = create_default_config();
    config.project.project_type = "library".to_string();
    config.project.description = "A C++ library built with cforge".to_string();

    config
}

pub fn create_header_only_config() -> ProjectConfig {
    let mut config = create_default_config();
    config.project.project_type = "header-only".to_string();
    config.project.description = "A header-only C++ library built with cforge".to_string();

    // For header-only libraries, we don't need source files
    if let Some(target) = config.targets.get_mut("default") {
        target.sources = vec![];
    }

    config
}

pub fn auto_adjust_config(config: &mut ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
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

fn skip_empty_vec<T>(val: &[T]) -> bool {
    val.is_empty()
}

fn skip_empty_option<T>(val: &Option<T>) -> bool {
    val.is_none()
}

fn skip_false(val: &bool) -> bool {
    *val == false
}