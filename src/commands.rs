use std::collections::HashMap;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use std::sync::mpsc::channel;
use std::thread;
use std::time::Duration;
use colored::Colorize;
use crate::ctest::*;
use crate::config::*;
use crate::output_utils::*;
use crate::{display_syntax_errors, list_project_targets, progress_bar, EXECUTED_COMMANDS};
use crate::build::run_script;
use crate::cli::Commands;
use crate::config::{auto_adjust_config, load_project_config, load_workspace_config};
use crate::dependencies::install_dependencies;
use crate::errors::format_cpp_errors_rust_style;
use crate::ide::generate_ide_files;
use crate::output_utils::{is_quiet, is_verbose, print_detailed, print_error, print_step, print_substep, print_warning, ProgressBar};
use crate::project::{build_project, clean_project, init_project, init_workspace, install_project, list_project_items, package_project, run_project, test_project};
use crate::workspace::{build_workspace_with_dependency_order, clean_workspace, generate_workspace_ide_files, install_workspace, install_workspace_deps, is_workspace, list_startup_projects, list_workspace_items, package_workspace, run_workspace, run_workspace_script, set_startup_project, show_current_startup, test_workspace};

pub struct CommandProgressData {
    pub lines_processed: usize,
    pub percentage_markers: Vec<f32>,
    pub last_reported_progress: f32,
    pub completed: bool,
    pub error_encountered: bool,
    pub total_lines_estimate: usize,
}

pub fn run_command_with_pattern_tracking(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<HashMap<String, String>>,
    progress: ProgressBar,
    patterns: Vec<(String, f32)>
) -> Result<(), Box<dyn std::error::Error>> {
    use std::process::{Command, Stdio};
    use std::io::{BufRead, BufReader};
    use std::sync::mpsc::channel;
    use std::time::Duration;

    // Build the command
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

    // Pipe stdout and stderr
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(e) => return Err(format!("Failed to execute command: {}", e).into()),
    };

    // Clone progress for threads
    let progress_stdout = progress.clone();
    let progress_stderr = progress.clone();

    // Take ownership of stdout/stderr handles
    let stdout = child.stdout.take()
        .ok_or("Failed to capture stdout")?;
    let stderr = child.stderr.take()
        .ok_or("Failed to capture stderr")?;

    // Clone patterns for threads
    let patterns_stdout = patterns.clone();
    let patterns_stderr = patterns.clone();

    // Stdout reader thread
    let stdout_handle = thread::spawn(move || {
        let reader = BufReader::new(stdout);
        for line in reader.lines().filter_map(Result::ok) {
            // Check for progress patterns
            for (pattern, value) in &patterns_stdout {
                if line.contains(pattern) {
                    progress_stdout.update(*value);
                    break;
                }
            }

            // Only show important output
            if is_verbose() {
                println!("{}", line);
            }
        }
    });

    // Stderr reader thread
    let stderr_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);
        for line in reader.lines().filter_map(Result::ok) {
            // Check for progress patterns
            for (pattern, value) in &patterns_stderr {
                if line.contains(pattern) {
                    progress_stderr.update(*value);
                    break;
                }
            }

            // Only show errors
            if is_verbose() ||
                line.contains("error:") || line.contains("Error:") {
                eprintln!("{}", line.red());
            }
        }
    });

    // Wait for the command to complete with timeout
    let (tx, rx) = channel();
    let wait_handle = thread::spawn(move || {
        let status = child.wait();
        let _ = tx.send(status);
    });

    // Wait with a reasonable timeout
    match rx.recv_timeout(Duration::from_secs(600)) {
        Ok(status_result) => {
            match status_result {
                Ok(status) => {
                    if !status.success() {
                        return Err(format!("Command failed with exit code: {}",
                                           status.code().unwrap_or(-1)).into());
                    }
                },
                Err(e) => return Err(format!("Command error: {}", e).into()),
            }
        },
        Err(_) => {
            return Err("Command timed out after 10 minutes".into());
        }
    }

    // Wait for IO threads to finish
    let _ = stdout_handle.join();
    let _ = stderr_handle.join();

    Ok(())
}

pub fn run_command_once(
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

pub fn run_command_raw(command: &Commands) -> Result<(), Box<dyn std::error::Error>> {
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
        Commands::Test { project, config, variant, filter, label, report, discover, init } => {
            if *init {
                // Initialize test environment
                if is_workspace() {
                    init_workspace_tests(project.clone())?;
                } else {
                    let proj_config = load_project_config(None)?;
                    init_test_directory(&proj_config, &PathBuf::from("."))?;
                    generate_test_cmakelists(&proj_config, &PathBuf::from("."))?;
                }
                return Ok(());
            }

            if *discover {
                // Discover tests and update config
                if is_workspace() {
                    discover_workspace_tests(project.clone())?;
                } else {
                    let mut proj_config = load_project_config(None)?;
                    let discovered_tests = discover_tests(&proj_config, &PathBuf::from("."))?;

                    if !discovered_tests.is_empty() {
                        print_status(&format!("Discovered {} tests", discovered_tests.len()));
                        update_config_with_tests(&mut proj_config, discovered_tests)?;
                        save_project_config(&proj_config, &PathBuf::from("."))?;
                    } else {
                        print_warning("No tests discovered", None);
                    }
                }
                return Ok(());
            }

            if report.is_some() {
                // Generate test reports
                if is_workspace() {
                    generate_workspace_test_reports(project.clone(), config.as_deref(), report.as_deref())?;
                } else {
                    let proj_config = load_project_config(None)?;
                    generate_test_reports(&proj_config, &PathBuf::from("."), config.as_deref(), report.as_deref())?;
                }
                return Ok(());
            }

            // Run tests normally
            if is_workspace() {
                test_workspace(project.clone(), config.as_deref(), variant.as_deref(), filter.as_deref(), label.as_deref())?;
            } else {
                let proj_config = load_project_config(None)?;
                run_tests(&proj_config, &PathBuf::from("."), config.as_deref(), variant.as_deref(), filter.as_deref(), label.as_deref())?;
            }
            Ok(())
        },
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

pub fn run_command_with_timeout(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<HashMap<String, String>>,
    timeout_seconds: u64
) -> Result<(), Box<dyn std::error::Error>> {
    // Build the Command as before
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

    // Pipe stdout and stderr so we can read them
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    // Spawn the command
    let child = command.spawn()
        .map_err(|e| format!("Failed to execute command: {}", e))?;

    // Wrap the Child in Arc<Mutex<>> so multiple threads can access
    let child_arc = Arc::new(Mutex::new(child));

    // Take ownership of stdout/stderr handles *before* the wait thread
    let stdout = child_arc
        .lock()
        .unwrap()
        .stdout
        .take()
        .ok_or("Failed to capture child stdout")?;
    let stderr = child_arc
        .lock()
        .unwrap()
        .stderr
        .take()
        .ok_or("Failed to capture child stderr")?;

    // Spawn a thread to continuously read from stdout
    let child_arc_out = Arc::clone(&child_arc);
    let out_handle = thread::spawn(move || {
        let reader = BufReader::new(stdout);
        for line in reader.lines() {
            /*
            if let Ok(line) = line {
                println!("{}", line);
            }
            */
        }
        drop(child_arc_out); // not strictly necessary, but can be explicit
    });

    // Spawn a thread to continuously read from stderr
    let child_arc_err = Arc::clone(&child_arc);
    let err_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);
        for line in reader.lines() {
            if let Ok(line) = line {
                eprintln!("STDERR> {}", line);
            }
        }
        drop(child_arc_err);
    });

    // We also need a channel to receive when the process completes
    let (tx, rx) = channel();
    let child_arc_wait = Arc::clone(&child_arc);

    // Thread to wait on the child's exit
    thread::spawn(move || {
        // Wait for the child to exit
        let status = child_arc_wait.lock().unwrap().wait();
        // Send the result back so we know the command is done
        let _ = tx.send(status);
    });

    // Now we wait up to `timeout_seconds` for the child to finish
    match rx.recv_timeout(Duration::from_secs(timeout_seconds)) {
        Ok(status_result) => {
            // The child exited (we got a status). Join reading threads
            out_handle.join().ok();
            err_handle.join().ok();

            match status_result {
                Ok(status) => {
                    if status.success() {
                        Ok(())
                    } else {
                        Err(format!("Command failed with exit code: {}",
                                    status.code().unwrap_or(-1)).into())
                    }
                },
                Err(e) => Err(format!("Command error: {}", e).into()),
            }
        },
        Err(_) => {
            // Timeout occurred: kill the child
            eprintln!(
                "Command timed out after {} seconds: {}",
                timeout_seconds,
                cmd.join(" ")
            );

            // Because we have Arc<Mutex<Child>>, we can kill safely
            let mut child = child_arc.lock().unwrap();
            let _ = child.kill();

            // Optionally wait() again to reap the process
            let _ = child.wait();

            Err(format!(
                "Command timed out after {} seconds: {}",
                timeout_seconds,
                cmd.join(" ")
            )
                .into())
        }
    }
}

pub fn run_command_with_progress(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<std::collections::HashMap<String, String>>,
    progress: &mut ProgressBar,
    operation: &str,
    timeout_seconds: u64
) -> Result<(), Box<dyn std::error::Error>> {
    use std::process::{Command, Stdio};
    use std::io::{BufRead, BufReader};
    use std::sync::mpsc::channel;
    use std::sync::{Arc, Mutex};
    use std::thread;
    use std::time::Duration;

    // Check if this is a CMake command
    let is_cmake_command = cmd.len() > 0 && cmd[0].contains("cmake");

    // Build the Command
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

    // Pipe stdout and stderr so we can read them
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    // Update progress to show we're starting the command
    progress.update(0.05);

    // Spawn the command
    let child = match command.spawn() {
        Ok(child) => child,
        Err(e) => return Err(format!("Failed to execute command: {}", e).into()),
    };

    // Wrap the Child in Arc<Mutex<>> so multiple threads can access
    let child_arc = Arc::new(Mutex::new(child));

    // Take ownership of stdout/stderr handles
    let stdout = child_arc
        .lock().unwrap()
        .stdout.take()
        .ok_or("Failed to capture stdout")?;
    let stderr = child_arc
        .lock().unwrap()
        .stderr.take()
        .ok_or("Failed to capture stderr")?;

    // Shared progress state
    let progress_state = Arc::new(Mutex::new(0.05f32)); // Starting at 5%

    // Buffer to collect output for error analysis
    let output_buffer = Arc::new(Mutex::new(String::new()));
    let output_buffer_clone = Arc::clone(&output_buffer);

    // Spawn a thread to continuously read from stdout and update progress
    let child_arc_out = Arc::clone(&child_arc);
    let progress_state_clone = Arc::clone(&progress_state);
    let out_handle = thread::spawn(move || {
        let reader = BufReader::new(stdout);
        let mut lines_read = 0;

        for line in reader.lines() {
            if let Ok(line) = line {
                lines_read += 1;

                // Update progress state
                let mut state = progress_state_clone.lock().unwrap();

                // Interpret progress from output
                if line.contains("Installing") || line.contains("Building") ||
                    line.contains("Downloading") || line.contains("Extracting") {
                    // Key progress indicators
                    if line.contains("Installing") {
                        *state = 0.6;
                    } else if line.contains("Building") {
                        *state = 0.7;
                    } else if line.contains("Downloading") {
                        *state = 0.4;
                    } else if line.contains("Extracting") {
                        *state = 0.5;
                    }
                } else if lines_read % 20 == 0 {
                    // Gradually increase progress based on line count
                    *state = (*state + 0.01).min(0.9);
                }

                // Look for percentage indicators in the output
                if let Some(percent_idx) = line.find('%') {
                    if percent_idx > 0 {
                        // Try to extract a percentage value
                        let start_idx = line[..percent_idx].rfind(|c: char| !c.is_digit(10) && c != '.')
                            .map_or(0, |pos| pos + 1);

                        if let Ok(percentage) = line[start_idx..percent_idx].trim().parse::<f32>() {
                            // Scale to the 5%-90% range
                            let scaled = 0.05 + (percentage / 100.0) * 0.85;
                            *state = scaled;
                        }
                    }
                }

                // Add line to buffer
                {
                    let mut buffer = output_buffer_clone.lock().unwrap();
                    buffer.push_str(&line);
                    buffer.push('\n');
                }

                // Filter stdout output based on command type and verbosity
                let should_print = if is_cmake_command {
                    // For CMake, only show output if it contains important keywords or in verbose mode
                    is_verbose() ||
                        line.contains("error") ||
                        line.contains("Error") ||
                        line.contains("WARNING") ||
                        line.contains("Warning") ||
                        line.contains("failed") ||
                        line.contains("Failed")
                } else {
                    // Still log for debugging if in verbose mode
                    is_verbose()
                };

                if is_verbose() {
                    println!("{}", line);  // No prefix, and only shown in verbose mode
                }
            }
        }
        drop(child_arc_out);
    });

    // Spawn a thread to continuously read from stderr
    let child_arc_err = Arc::clone(&child_arc);
    let progress_state_clone_err = Arc::clone(&progress_state);
    let output_buffer_clone_err = Arc::clone(&output_buffer);
    let err_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);

        for line in reader.lines() {
            if let Ok(line) = line {
                // If error lines, update progress state to show we're still working
                let mut state = progress_state_clone_err.lock().unwrap();
                *state = (*state + 0.005).min(0.9);

                // Add line to buffer
                {
                    let mut buffer = output_buffer_clone_err.lock().unwrap();
                    buffer.push_str(&line);
                    buffer.push('\n');
                }

                // Filter stderr output similar to stdout
                let should_print = if is_cmake_command {
                    // For CMake stderr, only show output with important keywords or in verbose mode
                    is_verbose() ||
                        line.contains("error") ||
                        line.contains("Error") ||
                        line.contains("WARNING") ||
                        line.contains("Warning") ||
                        line.contains("failed") ||
                        line.contains("Failed")
                } else {
                    // For other commands, always show stderr errors
                    is_verbose() ||
                        line.contains("error") ||
                        line.contains("Error") ||
                        line.contains("warning") ||
                        line.contains("Warning")
                };

                if should_print {
                    eprintln!("STDERR> {}", line);
                }
            }
        }
        drop(child_arc_err);
    });

    // Thread to update the progress bar
    let progress_state_clone_bar = Arc::clone(&progress_state);
    let progress_clone = progress.clone();
    let update_handle = thread::spawn(move || {
        let mut last_progress = 0.05f32;

        loop {
            thread::sleep(Duration::from_millis(100));

            let current_progress = *progress_state_clone_bar.lock().unwrap();

            // Only update if progress has changed meaningfully
            if (current_progress - last_progress).abs() > 0.01 {
                progress_clone.update(current_progress);
                last_progress = current_progress;
            }
        }
    });

    // We also need a channel to receive when the process completes
    let (tx, rx) = channel();
    let child_arc_wait = Arc::clone(&child_arc);

    // Thread to wait on the child's exit
    thread::spawn(move || {
        // Wait for the child to exit
        let status = child_arc_wait.lock().unwrap().wait();
        // Send the result back so we know the command is done
        let _ = tx.send(status);
    });

    // Now we wait up to `timeout_seconds` for the child to finish
    match rx.recv_timeout(Duration::from_secs(timeout_seconds)) {
        Ok(status_result) => {
            // The child exited (we got a status). Set progress to 100%
            progress.update(1.0);

            // No need to join the updater thread since we're finishing up

            // Join reading threads
            out_handle.join().ok();
            err_handle.join().ok();

            match status_result {
                Ok(status) => {
                    if status.success() {
                        progress.success();
                        Ok(())
                    } else {
                        // Get the collected output for error analysis
                        let output = output_buffer.lock().unwrap().clone();

                        // Format error output if cmake command
                        if is_cmake_command {
                            if !is_quiet() {
                                let formatted_errors = format_cpp_errors_rust_style(&output);
                                for error_line in formatted_errors {
                                    println!("{}", error_line);
                                }
                            }
                        }

                        progress.failure(&format!("Command failed with exit code: {}",
                                                  status.code().unwrap_or(-1)));
                        Err(format!("Command failed with exit code: {}",
                                    status.code().unwrap_or(-1)).into())
                    }
                },
                Err(e) => {
                    progress.failure(&format!("Command error: {}", e));
                    Err(format!("Command error: {}", e).into())
                },
            }
        },
        Err(_) => {
            // Timeout occurred: kill the child
            progress.failure(&format!("Command timed out after {} seconds", timeout_seconds));

            // Because we have Arc<Mutex<Child>>, we can kill safely
            let mut child = child_arc.lock().unwrap();
            let _ = child.kill();

            // Optionally wait() again to reap the process
            let _ = child.wait();

            Err(format!(
                "Command timed out after {} seconds: {}",
                timeout_seconds,
                cmd.join(" ")
            ).into())
        }
    }
}

pub fn run_command(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<HashMap<String, String>>
) -> Result<(), Box<dyn std::error::Error>> {
    // Only show the command in verbose mode
    if is_verbose() {
        println!("{}", format!("Running: {}", cmd.join(" ")).blue());
    }

    // Build the command
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

    // Pipe stdout and stderr to completely suppress normal output
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(e) => return Err(format!("Failed to execute command: {}", e).into()),
    };

    // Take ownership of stdout/stderr handles
    let stdout = child.stdout.take()
        .ok_or("Failed to capture stdout")?;
    let stderr = child.stderr.take()
        .ok_or("Failed to capture stderr")?;

    // Stdout reader thread - completely silent unless in verbose mode
    let stdout_handle = thread::spawn(move || {
        let reader = BufReader::new(stdout);
        for line in reader.lines().filter_map(Result::ok) {
            // In verbose mode only, show all stdout
            if is_verbose() {
                println!("{}", line);
            }
            // In normal mode, only show critical errors
            else if line.contains("fatal error:") ||
                line.contains("FAILED") ||
                line.contains("Critical:") {
                println!("{}", line);
            }
        }
    });

    // Stderr reader thread - only show actual errors
    let stderr_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);
        for line in reader.lines().filter_map(Result::ok) {
            // In verbose mode, show all stderr
            if is_verbose() {
                eprintln!("{}", line.red());
            }
            // In normal mode, show only legitimate errors (not warnings or notes)
            else if (line.contains("error:") && !line.contains("warning:") && !line.contains("note:")) ||
                line.contains("fatal:") ||
                line.contains("FAILED") {
                eprintln!("{}", line.red());
            }
        }
    });

    // Wait for command completion
    let status = child.wait()?;
    if !status.success() {
        return Err(format!("Command failed with exit code: {}",
                           status.code().unwrap_or(-1)).into());
    }

    // Wait for IO threads to finish
    let _ = stdout_handle.join();
    let _ = stderr_handle.join();

    Ok(())
}
