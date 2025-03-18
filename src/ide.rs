use std::fs;
use std::path::{Path, PathBuf};
use colored::Colorize;
use crate::config::{ProjectConfig, WorkspaceConfig};
use crate::{run_command, DEFAULT_BUILD_DIR};
use crate::build::get_visual_studio_generator;
use crate::project::build_project;

pub fn generate_ide_files(config: &ProjectConfig, project_path: &Path, ide_type: &str) -> Result<(), Box<dyn std::error::Error>> {
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

pub fn generate_vscode_files(config: &ProjectConfig, project_path: &Path) -> Result<(), Box<dyn std::error::Error>> {
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
                "name": "cforge",
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
                "command": "cforge",
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
                "command": "cforge",
                "args": ["clean"],
                "problemMatcher": []
            },
            {
                "label": "run",
                "type": "shell",
                "command": "cforge",
                "args": ["run"],
                "problemMatcher": []
            },
            {
                "label": "test",
                "type": "shell",
                "command": "cforge",
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

pub fn generate_vscode_workspace(config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
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

pub fn generate_clion_workspace(config: &WorkspaceConfig) -> Result<(), Box<dyn std::error::Error>> {
    println!("{}", "CLion workspace doesn't require special files.".green());
    println!("{}", "Open the workspace directory in CLion and it will detect all CMake projects.".green());
    Ok(())
}