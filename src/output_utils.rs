﻿// Updated output_utils.rs with cleaner, more consistent styling

use colored::*;
use std::io::{self, Write};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;
use std::cmp;
use lazy_static::lazy_static;
use regex::Regex;

// Global verbosity control
lazy_static! {
    static ref VERBOSITY: Mutex<Verbosity> = Mutex::new(Verbosity::Normal);
    static ref LAYOUT: Mutex<LayoutManager> = Mutex::new(LayoutManager::new());
    static ref ACTIVE_PROGRESS_BARS: Mutex<usize> = Mutex::new(0);
    static ref OUTPUT_MUTEX: Mutex<()> = Mutex::new(());
    static ref SPINNER_ACTIVE: Mutex<bool> = Mutex::new(false);
    static ref LAST_LINE_WAS_NEWLINE: Mutex<bool> = Mutex::new(true);
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Verbosity {
    Quiet,
    Normal,
    Verbose,
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Theme {
    Default,
    Dark,
    Light,
    Ocean,
    Forest,
}

// Unified message type for consistent display
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum MessageType {
    Header,
    Status,
    Step,
    SubStep,
    Success,
    Warning,
    Error,
    Detail,
}

// Spinning wheel for all long-running operations
pub struct SpinningWheel {
    pub message: String,
    pub start_time: Instant,
    pub stop_signal: Arc<Mutex<bool>>,
    pub handle: Option<thread::JoinHandle<()>>,
    pub update_channel: (std::sync::mpsc::Sender<String>, Arc<Mutex<String>>),
}

impl SpinningWheel {
    pub fn start(message: &str) -> Self {
        let message = message.to_string();
        let start_time = Instant::now();
        let stop_signal = Arc::new(Mutex::new(false));
        let stop_clone = stop_signal.clone();

        // Channel for status updates
        let (tx, rx) = std::sync::mpsc::channel::<String>();
        let status_message = Arc::new(Mutex::new(String::new()));
        let status_clone = status_message.clone();

        // Mark that we're showing a spinner
        {
            let mut spinner_active = SPINNER_ACTIVE.lock().unwrap();
            *spinner_active = true;
        }

        // Only create the handle if we're not in quiet mode
        let handle = if !is_quiet() {
            let msg = message.clone();
            Some(thread::spawn(move || {
                let spinner_chars = vec!["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
                let mut i = 0;

                // Acquire output lock to ensure we're not interrupted
                let _guard = OUTPUT_MUTEX.lock().unwrap();

                // Ensure we start on a new line
                ensure_single_newline();

                loop {
                    {
                        let should_stop = *stop_clone.lock().unwrap();
                        if should_stop {
                            break;
                        }
                    }

                    // Get status message if available
                    let status = match rx.try_recv() {
                        Ok(new_status) => {
                            *status_clone.lock().unwrap() = new_status.clone();
                            new_status
                        },
                        Err(_) => {
                            // Just show elapsed time in seconds
                            let elapsed = start_time.elapsed();
                            format!("{:.1}s", elapsed.as_secs_f32())
                        }
                    };

                    // Update spinner animation
                    let spinner_char = spinner_chars[i % spinner_chars.len()];
                    i = (i + 1) % spinner_chars.len();

                    // Clean, consistent output format
                    print!("\r{} {} {} ",
                           spinner_char.cyan(),
                           msg.blue().bold(),
                           status.dimmed());
                    io::stdout().flush().unwrap();

                    thread::sleep(Duration::from_millis(80));
                }

                // Clear the line completely on exit
                print!("\r{}\r", " ".repeat(120));
                io::stdout().flush().unwrap();

                // Mark spinner as done
                let mut spinner_active = SPINNER_ACTIVE.lock().unwrap();
                *spinner_active = false;

                // Mark that the line is empty now
                let mut was_newline = LAST_LINE_WAS_NEWLINE.lock().unwrap();
                *was_newline = true;
            }))
        } else {
            None
        };

        Self {
            message,
            start_time,
            stop_signal,
            handle,
            update_channel: (tx, status_message),
        }
    }

    pub fn update_status(&self, status: &str) {
        if !is_quiet() && self.handle.is_some() {
            // Send the new status through the channel
            let _ = self.update_channel.0.send(status.to_string());
        }
    }

    pub fn success(self) {
        let elapsed = self.start_time.elapsed();

        // Signal the spinner to stop
        *self.stop_signal.lock().unwrap() = true;

        // Wait for spinner thread to finish
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        if !is_quiet() {
            // Acquire output lock
            let _guard = OUTPUT_MUTEX.lock().unwrap();

            // Print success message in a clean format
            println!("{}  {}  {}", "✓".green().bold(),
                     self.message.bold(),
                     format!("({:.2}s)", elapsed.as_secs_f32()).dimmed());

            // Mark that we printed something
            mark_line_printed();
        }
    }

    pub fn failure(self, error: &str) {
        let elapsed = self.start_time.elapsed();

        // Signal the spinner to stop
        *self.stop_signal.lock().unwrap() = true;

        // Wait for spinner thread to finish
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        // Acquire output lock
        let _guard = OUTPUT_MUTEX.lock().unwrap();

        // Print failure message with consistency
        println!("{}  {}  {}", "✗".red().bold(),
                 self.message.bold(),
                 error.red());

        // Mark that we printed something
        mark_line_printed();
    }
}

impl Clone for SpinningWheel {
    fn clone(&self) -> Self {
        // Create a new spinning wheel with the same message and start time
        let message = self.message.clone();
        let start_time = self.start_time;
        let stop_signal = Arc::new(Mutex::new(false));

        // Create a new channel
        let (tx, rx) = std::sync::mpsc::channel::<String>();
        let status_message = Arc::new(Mutex::new(String::new()));

        // Only create a new handle if we're not in quiet mode
        let handle = if !is_quiet() {
            let msg = message.clone();
            let stop_clone = stop_signal.clone();
            let status_clone = status_message.clone();

            Some(thread::spawn(move || {
                let spinner_chars = vec!["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
                let mut i = 0;

                loop {
                    {
                        let should_stop = *stop_clone.lock().unwrap();
                        if should_stop {
                            break;
                        }
                    }

                    // Get status message if available
                    let status = match rx.try_recv() {
                        Ok(new_status) => {
                            *status_clone.lock().unwrap() = new_status.clone();
                            new_status
                        },
                        Err(_) => {
                            // Show elapsed time if no status updates
                            let elapsed = start_time.elapsed();
                            format!("{}s elapsed", elapsed.as_secs())
                        }
                    };

                    // Update spinner animation
                    let spinner_char = spinner_chars[i % spinner_chars.len()];
                    i = (i + 1) % spinner_chars.len();

                    print!("\r{} {} {} ",
                           spinner_char.cyan().bold(),
                           msg.blue().bold(),
                           status);
                    io::stdout().flush().unwrap();

                    thread::sleep(Duration::from_millis(80));
                }

                // Clear the line
                let status_len = status_clone.lock().unwrap().len();
                print!("\r{}\r", " ".repeat(msg.len() + status_len + 20));
                io::stdout().flush().unwrap();
            }))
        } else {
            None
        };

        Self {
            message,
            start_time,
            stop_signal,
            handle,
            update_channel: (tx, status_message),
        }
    }
}

// Helper to ensure we don't print too many newlines
fn ensure_single_newline() {
    let mut was_newline = LAST_LINE_WAS_NEWLINE.lock().unwrap();
    if !*was_newline {
        println!();
        *was_newline = true;
    }
}

// Helper to mark that we printed something
fn mark_line_printed() {
    let mut was_newline = LAST_LINE_WAS_NEWLINE.lock().unwrap();
    *was_newline = false;
}

// Terminal capabilities and layout management
pub struct LayoutManager {
    width: usize,
    height: usize,
    supports_unicode: bool,
    supports_colors: bool,
    theme: Theme,
}

impl LayoutManager {
    pub fn new() -> Self {
        // Get terminal size (simplified for example)
        let (width, height) = (100, 30);

        // In practice, you'd detect these properly
        let supports_unicode = true;
        let supports_colors = colored::control::SHOULD_COLORIZE.should_colorize();

        Self {
            width,
            height,
            supports_unicode,
            supports_colors,
            theme: Theme::Default,
        }
    }

    pub fn get_spinner_chars(&self) -> Vec<&str> {
        if self.supports_unicode {
            vec!["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
        } else {
            vec!["-", "\\", "|", "/"]
        }
    }

    pub fn get_width(&self) -> usize {
        self.width
    }

    pub fn get_progress_chars(&self) -> (String, String) {
        if self.supports_unicode {
            ("█".to_string(), "░".to_string())
        } else {
            ("#".to_string(), "-".to_string())
        }
    }
}

// Verbosity control functions
pub fn set_verbosity(level: &str) {
    let mut v = VERBOSITY.lock().unwrap();
    *v = match level.to_lowercase().as_str() {
        "quiet" => Verbosity::Quiet,
        "verbose" => Verbosity::Verbose,
        _ => Verbosity::Normal,
    };
}

pub fn get_verbosity() -> Verbosity {
    *VERBOSITY.lock().unwrap()
}

pub fn is_verbose() -> bool {
    get_verbosity() == Verbosity::Verbose
}

pub fn is_quiet() -> bool {
    get_verbosity() == Verbosity::Quiet
}

// Simplified, consistent printing functions
pub fn print_message(msg_type: MessageType, message: &str, details: Option<&str>) {
    if is_quiet() && msg_type != MessageType::Warning && msg_type != MessageType::Error {
        return;
    }

    // Acquire output lock for thread safety
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    // Handle spinner active case
    if *SPINNER_ACTIVE.lock().unwrap() {
        println!();
        *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
    }

    // Ensure proper spacing
    ensure_single_newline();

    // Format based on message type
    match msg_type {
        MessageType::Header => {
            println!("{}", "╭─────────────────────────────────────────────────╮".blue());
            println!("│  {}  │", message.blue().bold());
            println!("{}", "╰─────────────────────────────────────────────────╯".blue());
        },
        MessageType::Status => {
            println!("{}  {}", "→".blue().bold(), message);
        },
        MessageType::Step => {
            println!("{}  {}", "●".blue(), message);
        },
        MessageType::SubStep => {
            println!("  {}  {}", "•".dimmed(), message);
        },
        MessageType::Success => {
            println!("{}  {}", "✓".green().bold(), message);
            if let Some(d) = details {
                println!("  {}  {}", " ".dimmed(), d.dimmed());
            }
        },
        MessageType::Warning => {
            println!("{}  {}", "!".yellow().bold(), message.yellow());
            if let Some(d) = details {
                println!("  {}  {}", " ".dimmed(), d.yellow().dimmed());
            }
        },
        MessageType::Error => {
            println!("{}  {}", "✗".red().bold(), message.red());
            if let Some(d) = details {
                println!("  {}  {}", " ".dimmed(), d.yellow());
            }
        },
        MessageType::Detail => {
            println!("  {}  {}", " ".dimmed(), message.dimmed());
        },
    }

    mark_line_printed();
}

// Updated interface functions that use the unified print_message
pub fn print_header(message: &str, _icon: Option<&str>) {
    print_message(MessageType::Header, message, None);
}

pub fn print_status(message: &str) {
    print_message(MessageType::Status, message, None);
}

pub fn print_step(action: &str, target: &str) {
    if is_quiet() {
        return;
    }

    // Acquire output lock for thread safety
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    // Handle spinner active case
    if *SPINNER_ACTIVE.lock().unwrap() {
        println!();
        *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
    }

    // Ensure proper spacing
    ensure_single_newline();

    // Print step with consistent formatting
    println!("{}  {} {}", "→".blue().bold(), action.bold(), target);

    mark_line_printed();
}

pub fn print_substep(message: &str) {
    print_message(MessageType::SubStep, message, None);
}

pub fn print_success(message: &str, details: Option<&str>) {
    print_message(MessageType::Success, message, details);
}

pub fn print_warning(message: &str, solution: Option<&str>) {
    print_message(MessageType::Warning, message, solution);
}

pub fn print_error(message: &str, _code: Option<&str>, solution: Option<&str>) {
    print_message(MessageType::Error, message, solution);
}

pub fn print_detailed(message: &str) {
    if is_verbose() {
        print_message(MessageType::Detail, message, None);
    }
}

pub fn format_project_name(name: &str) -> ColoredString {
    name.cyan().bold()
}

// Progress tracking for overall build
pub struct BuildProgress {
    total_steps: usize,
    current_step: usize,
    project_name: String,
    start_time: Instant,
}

impl BuildProgress {
    pub fn new(project_name: &str, total_steps: usize) -> Self {
        if !is_quiet() {
            print_header(&format!("Building {}", project_name), None);
        }

        Self {
            total_steps,
            current_step: 0,
            project_name: project_name.to_string(),
            start_time: Instant::now(),
        }
    }

    pub fn next_step(&mut self, step_name: &str) {
        self.current_step += 1;
        if !is_quiet() {
            // Clean, consistent format
            print_status(&format!("[{}/{}] {}",
                                  self.current_step,
                                  self.total_steps,
                                  step_name));
        }
    }

    pub fn complete(&self) {
        if !is_quiet() {
            let duration = self.start_time.elapsed();
            print_success(&format!("{} completed", self.project_name),
                          Some(&format!("in {:.2}s", duration.as_secs_f32())));
        }
    }
}

// Task list for tracking multiple projects
pub struct TaskList {
    tasks: Vec<String>,
    current_task: Option<usize>,
    completed_tasks: Vec<bool>,
}

impl TaskList {
    pub fn new(tasks: Vec<String>) -> Self {
        let len = tasks.len();
        Self {
            tasks,
            current_task: None,
            completed_tasks: vec![false; len],
        }
    }

    pub fn display(&self) {
        if is_quiet() {
            return;
        }

        println!();
        for (i, task) in self.tasks.iter().enumerate() {
            let prefix = if Some(i) == self.current_task {
                "→".blue().bold()
            } else if self.completed_tasks[i] {
                "✓".green().bold()
            } else {
                "○".normal().dimmed()
            };
            println!("  {}  {}", prefix, task);
        }
        println!();
    }

    pub fn start_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.current_task = Some(index);
        if !is_quiet() {
            println!();
            print_status(&format!("Starting {}", self.tasks[index]));
            self.display();
        }
    }

    pub fn complete_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.completed_tasks[index] = true;
        if !is_quiet() {
            print_success(&format!("Completed {}", self.tasks[index]), None);
        }
    }

    pub fn all_completed(&self) -> bool {
        self.completed_tasks.iter().all(|&completed| completed)
    }
}

// Helper for time formatting
pub fn format_duration(duration: Duration) -> String {
    let total_secs = duration.as_secs();
    let millis = duration.subsec_millis();

    if total_secs < 60 {
        format!("{}.{:03}s", total_secs, millis)
    } else if total_secs < 3600 {
        let mins = total_secs / 60;
        let secs = total_secs % 60;
        format!("{}m {}s", mins, secs)
    } else {
        let hours = total_secs / 3600;
        let mins = (total_secs % 3600) / 60;
        format!("{}h {}m", hours, mins)
    }
}

pub fn has_command(cmd: &str) -> bool {
    use std::process::Command;

    let cmd_str = if cfg!(windows) && !cmd.ends_with(".exe") && !cmd.ends_with(".bat") && !cmd.ends_with(".cmd") {
        format!("{}.exe", cmd)
    } else {
        cmd.to_string()
    };

    // Try with --version flag first (most common)
    let version_result = Command::new(&cmd_str)
        .arg("--version")
        .output()
        .is_ok();

    if version_result {
        return true;
    }

    // Some tools don't support --version, so try with no arguments
    let no_args_result = Command::new(&cmd_str)
        .output()
        .is_ok();

    if no_args_result {
        return true;
    }

    // For MSVC cl.exe specially
    if cmd == "cl" {
        let cl_result = Command::new("cl")
            .arg("/?")
            .output()
            .is_ok();

        return cl_result;
    }

    false
}