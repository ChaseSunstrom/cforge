use clap::{Parser, Subcommand};

#[derive(Debug, Parser)]
#[clap(
    name = "cforge",
    about = "A TOML-based build system for C/C++ with CMake and vcpkg integration",
    version = env!("CARGO_PKG_VERSION"),
)]
pub struct Cli {
    #[clap(subcommand)]
    pub command: Commands,

    /// Set verbosity level (quiet, normal, verbose)
    #[clap(long, global = true)]
    pub verbosity: Option<String>,
}

#[derive(Debug, Subcommand)]
pub enum Commands {
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
