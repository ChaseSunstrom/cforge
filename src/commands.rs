use std::collections::HashMap;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::sync::{Arc, Mutex};
use std::sync::mpsc::channel;
use std::thread;
use std::time::Duration;
use colored::Colorize;
use crate::{display_raw_errors, display_syntax_errors, format_cpp_errors_rust_style, list_project_targets, progress_bar, EXECUTED_COMMANDS};
use crate::build::run_script;
use crate::cli::Commands;
use crate::config::{auto_adjust_config, load_project_config, load_workspace_config};
use crate::dependencies::install_dependencies;
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
    mut progress: ProgressBar, // Take ownership instead of reference
    patterns: Vec<(String, f32)> // Use owned Strings instead of &str references
) -> Result<(), Box<dyn std::error::Error>> {
    use std::process::{Command, Stdio};
    use std::io::{BufRead, BufReader};
    use std::sync::{Arc, Mutex};
    use std::thread;

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

    // Pipe stdout and stderr
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    // Update progress to show we're starting (5%)
    progress.update(0.05);

    // Spawn the command
    let mut child = command.spawn()?;

    // Take ownership of stdout/stderr
    let stdout = child.stdout.take().ok_or("Failed to capture stdout")?;
    let stderr = child.stderr.take().ok_or("Failed to capture stderr")?;

    // Create shared state to track the highest progress seen
    let current_progress = Arc::new(Mutex::new(0.05f32));

    // Create threads to process stdout and stderr
    let patterns_arc = Arc::new(patterns);

    let stdout_patterns = Arc::clone(&patterns_arc);
    let stdout_progress = Arc::clone(&current_progress);
    let stdout_handle = thread::spawn(move || {
        let reader = BufReader::new(stdout);

        for line in reader.lines().filter_map(Result::ok) {
            // Check each pattern and update progress if matched
            for (pattern, progress_value) in stdout_patterns.iter() {
                if line.contains(pattern) {
                    let mut current = stdout_progress.lock().unwrap();
                    if *progress_value > *current {
                        *current = *progress_value;
                    }
                }
            }

            // Print in verbose mode
            if is_verbose() {
                println!("{}", line);
            }
        }
    });

    let stderr_patterns = Arc::clone(&patterns_arc);
    let stderr_progress = Arc::clone(&current_progress);
    let stderr_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);

        for line in reader.lines().filter_map(Result::ok) {
            // Check each pattern and update progress if matched
            for (pattern, progress_value) in stderr_patterns.iter() {
                if line.contains(pattern) {
                    let mut current = stderr_progress.lock().unwrap();
                    if *progress_value > *current {
                        *current = *progress_value;
                    }
                }
            }

            // Print in verbose mode or if it looks like an error
            if is_verbose() || line.contains("error") || line.contains("Error") {
                eprintln!("{}", line);
            }
        }
    });

    // Fix Error 4: Create a clone of child.id() for status checks
    // We don't move child into the thread, just create a separate wait_result variable
    let progress_clone = progress.clone();
    let update_progress = Arc::clone(&current_progress);

    // Thread to update progress bar
    let update_handle = thread::spawn(move || {
        loop {
            // Check progress value and update progress bar
            let current = {
                let guard = update_progress.lock().unwrap();
                *guard
            };

            progress_clone.update(current);

            // Sleep a bit
            thread::sleep(Duration::from_millis(100));
        }
    });

    // Wait for the command to finish
    let status = child.wait()?;

    // Wait for the threads to finish
    stdout_handle.join().ok();
    stderr_handle.join().ok();
    // Don't wait for update_handle as it runs in an infinite loop

    // Check if the command succeeded
    if status.success() {
        // Final progress update to show completion
        progress.update(1.0);
        progress.success();
        Ok(())
    } else {
        progress.failure(&format!("Command failed with exit code: {}", status));
        Err(format!("Command failed with exit code: {}", status).into())
    }
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
            if let Ok(line) = line {
                println!("STDOUT> {}", line);
            }
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
                    is_verbose() ||
                        line.contains("Installing") ||
                        line.contains("Building") ||
                        line.contains("Downloading") ||
                        line.contains("Extracting")
                };

                if should_print {
                    println!("STDOUT> {}", line);
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
    if cmd.is_empty() {
        return Err("Cannot run empty command".into());
    }

    // Determine if this is a CMake command
    let command_name = &cmd[0];
    let is_cmake = command_name.contains("cmake");
    let is_compiler_command = command_name.contains("cl") ||
        command_name.contains("clang") ||
        command_name.contains("gcc");
    let is_verbose_command = is_cmake || is_compiler_command;
    let is_build_command = is_cmake && cmd.len() > 1 && cmd.contains(&"--build".to_string());

    // Track command using a cache key to avoid duplicates
    let cache_key = cmd.join(" ");
    let already_executed = {
        let cache = EXECUTED_COMMANDS.lock().unwrap();
        cache.contains(&cache_key)
    };

    if already_executed && !is_build_command {
        print_detailed(&format!("Skipping already executed: {}", command_name));
        return Ok(());
    }

    // Show appropriate message based on command type
    if is_build_command {
        print_step("Building", "...");
    } else if is_verbose_command && !is_verbose() {
        // For verbose commands, just show a simple message in normal mode
        print_step(&format!("Running {}", command_name), "");
    } else {
        // For non-verbose commands or in verbose mode, show the full command
        if is_verbose() {
            print_step("Executing", &cmd.join(" "));
        } else {
            // Show a more compact representation
            let compact_cmd = if cmd.len() > 3 {
                format!("{} {} ...", cmd[0], cmd[1])
            } else {
                cmd.join(" ")
            };
            print_step("Executing", &compact_cmd);
        }
    }

    // For long-running operations, show a spinner
    let spinner = if is_build_command || is_cmake {
        let msg = if is_build_command {
            "Building project".to_string()
        } else {
            format!("Running {}", command_name)
        };
        Some(progress_bar(&msg))
    } else {
        None
    };

    // Create and execute the command
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

    // Always capture output for processing
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    // Execute the command
    match command.output() {
        Ok(output) => {
            let stdout = String::from_utf8_lossy(&output.stdout).to_string();
            let stderr = String::from_utf8_lossy(&output.stderr).to_string();
            let status = output.status;

            // Stop spinner if any
            if let Some(s) = spinner {
                if status.success() {
                    s.success();
                } else {
                    s.failure("Command failed");
                }
            }

            // If successful
            if status.success() {
                // For CMake commands, don't flood output with details
                if is_cmake && !is_verbose() {
                    // Just print a summary or important messages
                    if stdout.contains("Configuring done") || stdout.contains("Generating done") {
                        print_substep("CMake configuration completed");
                    }

                    // Check for warnings but don't print the full output
                    if stderr.contains("Warning") || stderr.contains("WARNING") {
                        print_warning("CMake reported warnings during configuration",
                                      Some("Use verbose mode to see details"));
                    }
                }
                // For non-CMake or verbose mode, print output as before
                else if is_verbose() && !stdout.is_empty() {
                    println!("{}", stdout);
                }

                Ok(())
            } else {
                // If build command failed, format errors instead of logging
                if is_cmake || is_compiler_command {
                    print_error("Build failed with the following errors:", None, None);

                    // Extract and format errors
                    let error_count = display_syntax_errors(&stdout, &stderr);

                    // If no structured errors found, show raw errors
                    if error_count == 0 {
                        display_raw_errors(&stdout, &stderr);
                    }
                } else {
                    // For other commands, print error messages
                    if !stderr.is_empty() {
                        print_error(&stderr, None, None);
                    } else {
                        print_error(&format!("Command failed with status: {}", status), None, None);
                    }
                }

                Err("Command failed".into())
            }
        },
        Err(e) => {
            // Command failed to execute
            if let Some(s) = spinner {
                s.failure(&e.to_string());
            }
            print_error(&format!("Failed to execute command: {}", e), None, None);
            Err(e.into())
        }
    }
}
