use std::process::{Command, Stdio};
use colored::Colorize;
use crate::config::{ProjectConfig, SystemInfo};
use crate::{run_command, VERIFIED_TOOLS};

pub fn ensure_build_tools(config: &ProjectConfig) -> Result<(), Box<dyn std::error::Error>> {
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

pub fn ensure_compiler_available(compiler_label: &str) -> Result<bool, Box<dyn std::error::Error>> {
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

pub fn ensure_generator_available() -> Result<String, Box<dyn std::error::Error>> {
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

pub fn detect_system_info() -> SystemInfo {
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


    // Windows logic:
    if cfg!(target_os = "windows") {
        // Check for cl.exe
        if crate::has_command("cl") {
            return SystemInfo { os, arch, compiler: "msvc".to_string() };
        }
        // Check for clang-cl
        if crate::has_command("clang-cl") {
            return SystemInfo { os, arch, compiler: "clang-cl".to_string() };
        }
        // Check for clang (GNU-style)
        if crate::has_command("clang") {
            return SystemInfo { os, arch, compiler: "clang".to_string() };
        }
        // Check for gcc
        if crate::has_command("gcc") {
            return SystemInfo { os, arch, compiler: "gcc".to_string() };
        }
        // Fallback
        return SystemInfo { os, arch, compiler: "default".to_string() };
    }

    // Non-Windows (macOS, Linux):
    if crate::has_command("clang") {
        return SystemInfo { os, arch, compiler: "clang".to_string() };
    }
    if crate::has_command("gcc") {
        return SystemInfo { os, arch, compiler: "gcc".to_string() };
    }

    // Fallback
    SystemInfo { os, arch, compiler: "default".to_string() }
}

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

// New helper function to check for commands with a timeout
pub fn has_command_with_timeout(cmd: &str, timeout_seconds: u64) -> bool {
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

pub fn get_effective_compiler_label(config: &ProjectConfig) -> String {
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

pub fn is_msvc_style_for_config(config: &ProjectConfig) -> bool {
    let label = get_effective_compiler_label(config).to_lowercase();
    // If user says "msvc" or "clang-cl", do slash-based flags
    matches!(label.as_str(), "msvc" | "clang-cl")
}

pub fn map_token(token: &str, msvc_style: bool) -> Vec<String> {
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

pub fn map_compiler_label(label: &str) -> Option<(String, String)> {
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

pub fn parse_universal_flags(tokens: &[String], is_msvc_style: bool) -> Vec<String> {
    let mut result = Vec::new();
    for t in tokens {
        let expanded = map_token(t, is_msvc_style);
        result.extend(expanded);
    }
    result
}
