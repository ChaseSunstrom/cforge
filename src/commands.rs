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
use crate::output_utils::{is_quiet, is_verbose, print_detailed, print_error, print_substep, print_warning};
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
    mut spinner: SpinningWheel,
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

    // Clone spinner for threads
    let spinner_out = spinner.update_channel.0.clone();
    let spinner_err = spinner.update_channel.0.clone();

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
                    // Update spinner status instead of updating a progress percentage
                    let _ = spinner_out.send(format!("{} ({}%)", pattern, (value * 100.0) as i32));
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
                    // Update spinner status instead of updating a progress percentage
                    let _ = spinner_err.send(format!("{} ({}%)", pattern, (value * 100.0) as i32));
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
                        spinner.failure(&format!("Command failed with exit code: {}",
                                                 status.code().unwrap_or(-1)));
                        return Err(format!("Command failed with exit code: {}",
                                           status.code().unwrap_or(-1)).into());
                    }
                },
                Err(e) => {
                    spinner.failure(&format!("Command error: {}", e));
                    return Err(format!("Command error: {}", e).into());
                }
            }
        },
        Err(_) => {
            spinner.failure("Command timed out after 10 minutes");
            return Err("Command timed out after 10 minutes".into());
        }
    }

    // Wait for IO threads to finish
    let _ = stdout_handle.join();
    let _ = stderr_handle.join();

    spinner.success();
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

    // Spawn the command
    let child = match command.spawn() {
        Ok(child) => child,
        Err(e) => return Err(format!("Failed to execute command: {}", e).into()),
    };

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
                if is_verbose() {
                    println!("{}", line);
                }
            }
        }
        drop(child_arc_out);
    });

    // Spawn a thread to continuously read from stderr
    let child_arc_err = Arc::clone(&child_arc);
    let err_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);
        for line in reader.lines() {
            if let Ok(line) = line {
                if is_verbose() || line.contains("error") || line.contains("Error") || line.contains("WARNING") {
                    eprintln!("{}", line.red());
                }
            }
        }
        drop(child_arc_err);
    });

    // We also need a channel to receive when the process completes
    let (tx, rx) = channel();
    let child_arc_wait = Arc::clone(&child_arc);
    let kill_tx = tx.clone();

    // Thread to wait on the child's exit
    thread::spawn(move || {
        // Wait for the child to exit
        let status = child_arc_wait.lock().unwrap().wait();
        // Send the result back so we know the command is done
        let _ = tx.send(status);
    });

    // Add a safety watchdog timer that will forcibly kill the process if it hangs
    let child_arc_kill = Arc::clone(&child_arc);

    thread::spawn(move || {
        // Sleep for double the timeout period to ensure we kill only truly hung processes
        thread::sleep(Duration::from_secs(timeout_seconds * 2));

        // If we get here and the process is still running, it's definitely hung
        let mut child_lock = match child_arc_kill.try_lock() {
            Ok(lock) => lock,
            Err(_) => return, // Already finished or being handled
        };

        // Check if process is still running (try_wait returns None if still running)
        match child_lock.try_wait() {
            Ok(None) => {
                // Process is still running after twice the timeout - kill it
                let _ = child_lock.kill();

                // Send a timeout error
                let _ = kill_tx.send(Err(std::io::Error::new(
                    std::io::ErrorKind::TimedOut,
                    format!("Command execution timed out after {} seconds", timeout_seconds * 2)
                )));
            },
            _ => (), // Process already finished
        }
    });

    // Now we wait up to `timeout_seconds` for the child to finish
    match rx.recv_timeout(Duration::from_secs(timeout_seconds)) {
        Ok(status_result) => {
            // The child exited (we got a status). Join reading threads
            let _ = out_handle.join();
            let _ = err_handle.join();

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
            if is_verbose() {
                eprintln!(
                    "Command timed out after {} seconds: {}",
                    timeout_seconds,
                    cmd.join(" ")
                );
            }

            // Because we have Arc<Mutex<Child>>, we can kill safely
            let mut child = match child_arc.try_lock() {
                Ok(lock) => lock,
                Err(_) => {
                    // Already being handled by another thread
                    return Err(format!(
                        "Command timed out after {} seconds and could not acquire lock to kill it",
                        timeout_seconds
                    ).into());
                }
            };

            // Try to kill the process, but don't panic if it's already gone
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


pub fn run_command_with_progress(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<std::collections::HashMap<String, String>>,
    progress: &mut SpinningWheel,
    operation: &str,
    timeout_seconds: u64
) -> Result<(), Box<dyn std::error::Error>> {
    use std::process::{Command, Stdio};
    use std::io::{BufRead, BufReader};
    use std::sync::mpsc::channel;

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
    progress.update_status(&format!("Starting: {}", operation));

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

    // Spawn a thread to continuously read from stdout and update progress
    let child_arc_out = Arc::clone(&child_arc);
    let progress_clone = progress.update_channel.0.clone();
    let out_handle = thread::spawn(move || {
        let reader = BufReader::new(stdout);
        let mut lines_read = 0;

        for line in reader.lines() {
            if let Ok(line) = line {
                lines_read += 1;

                // Send interesting output to progress spinner
                if line.contains("Installing") || line.contains("Building") ||
                    line.contains("Downloading") || line.contains("Extracting") {
                    let _ = progress_clone.send(line.clone());
                } else if lines_read % 20 == 0 {
                    // Every 20 lines, update with a status
                    let _ = progress_clone.send(format!("Processing... ({} lines)", lines_read));
                }

                // Still log for debugging if in verbose mode
                if is_verbose() {
                    println!("STDOUT> {}", line);
                }
            }
        }
        drop(child_arc_out);
    });

    // Spawn a thread to continuously read from stderr
    let child_arc_err = Arc::clone(&child_arc);
    let progress_clone_err = progress.update_channel.0.clone();
    let err_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);

        for line in reader.lines() {
            if let Ok(line) = line {
                // Only send error-looking lines to progress
                if line.contains("Error") || line.contains("error") ||
                    line.contains("Failed") || line.contains("failed") {
                    let _ = progress_clone_err.send(format!("Error: {}", line));
                }

                // Still log for debugging if in verbose mode
                if is_verbose() {
                    eprintln!("STDERR> {}", line);
                }
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
                        progress.update_status(&format!("Completed: {}", operation));
                        Ok(())
                    } else {
                        return Err(format!("Command failed with exit code: {}",
                                           status.code().unwrap_or(-1)).into())
                    }
                },
                Err(e) => Err(format!("Command error: {}", e).into()),
            }
        },
        Err(_) => {
            // Timeout occurred: kill the child
            progress.update_status(&format!("Command timed out after {} seconds", timeout_seconds));

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
        println!("Running: {}", cmd.join(" "));
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

    // Pipe stdout and stderr
    command.stdout(Stdio::piped());
    command.stderr(Stdio::piped());

    let mut child = match command.spawn() {
        Ok(child) => child,
        Err(e) => return Err(format!("Failed to execute command: {}", e).into()),
    };

    // Take ownership of stdout/stderr handles with proper error handling
    let stdout = match child.stdout.take() {
        Some(out) => out,
        None => return Err("Failed to capture stdout".into()),
    };

    let stderr = match child.stderr.take() {
        Some(err) => err,
        None => return Err("Failed to capture stderr".into()),
    };

    // Thread for stdout - only show in verbose mode
    let stdout_handle = thread::spawn(move || {
        let reader = BufReader::new(stdout);
        for line in reader.lines().filter_map(Result::ok) {
            if is_verbose() {
                println!("{}", line);
            }
        }
    });

    // Thread for stderr - show errors always
    let stderr_handle = thread::spawn(move || {
        let reader = BufReader::new(stderr);
        for line in reader.lines().filter_map(Result::ok) {
            if is_verbose() || line.contains("error:") || line.contains("Error:") {
                eprintln!("{}", line.red());
            }
        }
    });

    // Wait with a reasonable timeout
    match child.wait() {
        Ok(status) => {
            // Wait for IO threads with a short timeout
            let _ = stdout_handle.join();
            let _ = stderr_handle.join();

            if !status.success() {
                return Err(format!("Command failed with exit code: {}",
                                   status.code().unwrap_or(-1)).into());
            }
        },
        Err(e) => {
            return Err(format!("Failed to wait for command: {}", e).into());
        }
    }

    Ok(())
}