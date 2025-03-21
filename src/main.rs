#![allow(warnings)]
mod output_utils;
mod cli;
mod config;
mod project;
mod workspace;
mod dependencies;
mod build;
mod tools;
mod ide;
mod commands;
mod errors;
mod utils;
mod cross_compile;
mod ctest;

use crate::{output_utils::*, cli::*, config::*, project::*, workspace::*, build::*, commands::*, dependencies::*, errors::*, ide::*, tools::*, utils::*};
use lazy_static::lazy_static;
use clap::{Parser, Subcommand};
use colored::*;
use regex;
use serde::{Deserialize, Serialize};
use std::{collections::HashMap, collections::HashSet, env, sync::Mutex, fmt, fs::{self, File}, io::{Write, BufRead, BufReader}, path::{Path, PathBuf}, process::{Command, Stdio}, thread};
use regex::Regex;

// Constants
const CFORGE_FILE: &str = "cforge.toml";
const DEFAULT_BUILD_DIR: &str = "build";
const DEFAULT_BIN_DIR: &str = "bin";
const DEFAULT_LIB_DIR: &str = "lib";
const DEFAULT_OBJ_DIR: &str = "obj";
const VCPKG_DEFAULT_DIR: &str = "~/.vcpkg";
const CMAKE_MIN_VERSION: &str = "3.15";
const WORKSPACE_FILE: &str = "cforge-workspace.toml";

lazy_static! {
    static ref EXECUTED_COMMANDS: Mutex<HashSet<String>> = Mutex::new(HashSet::new());
    static ref VERIFIED_TOOLS: Mutex<HashSet<String>> = Mutex::new(HashSet::new());
    static ref INSTALLED_PACKAGES: Mutex<HashSet<String>> = Mutex::new(HashSet::new());
    static ref CACHED_PATHS: Mutex<HashMap<String, String>> = Mutex::new(HashMap::new());
    static ref CLANG_STYLE: Regex = Regex::new(r"(?m)(.*?):(\d+):(\d+):\s+(error|warning|note):\s+(.*)").unwrap();
    static ref MSVC_STYLE: Regex = Regex::new(r"(?m)(.*?)\((\d+),(\d+)\):\s+(error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap();
    static ref MSVC_SIMPLE: Regex = Regex::new(r"(?m)(.*?)\((\d+)\):\s+(error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap();
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cli = Cli::parse();

    // Set verbosity level from command line or environment
    if let Ok(val) = env::var("CFORGE_VERBOSE") {
        if val == "1" || val.to_lowercase() == "true" {
            set_verbosity("verbose");
        }
    } else if let Ok(val) = env::var("CFORGE_QUIET") {
        if val == "1" || val.to_lowercase() == "true" {
            set_verbosity("quiet");
        }
    } else {
        // Set from CLI argument if provided
        if let Some(verbosity) = cli.verbosity.as_deref() {
            set_verbosity(verbosity);
        }
    }

    // Show header only if not in quiet mode
    if !is_quiet() {
        println!("┌{:─^50}┐", "");
        println!("│{:^50}│", "cforge - C/C++ Build System".bold());
        println!("│{:^50}│", format!("v{}", env!("CARGO_PKG_VERSION")));
        println!("└{:─^50}┘", "");
        println!();
    }

    match run_command_raw(&cli.command) {
        Ok(()) => {
            if !is_quiet() {
                println!();
                print_success("Command completed successfully", None);
            }
            Ok(())
        },
        Err(e) => {
            // Format and display the error
            println!();

            // Special formatting for CforgeError
            if let Some(cforge_err) = e.downcast_ref::<CforgeError>() {
                print_error(&format!("cforge Error: {}", cforge_err.message), None, None);

                if let Some(file_path) = &cforge_err.file_path {
                    print_substep(&format!("File: {}", file_path));
                }

                if let Some(line_number) = cforge_err.line_number {
                    print_substep(&format!("Line: {}", line_number));
                }

                if let Some(context) = &cforge_err.context {
                    print_substep("Context:");
                    for line in context.lines() {
                        println!("    {}", line);
                    }
                }
            } else {
                // Regular error
                print_error(&e.to_string(), None, None);
            }

            std::process::exit(1);
        }
    }
}
