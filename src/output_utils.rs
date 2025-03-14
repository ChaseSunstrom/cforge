// output_utils.rs - Module for consistent output formatting
use colored::*;
use std::io::{self, Write};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;
use lazy_static::lazy_static;

// Global verbosity control
lazy_static! {
    static ref VERBOSITY: Mutex<Verbosity> = Mutex::new(Verbosity::Normal);
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Verbosity {
    Quiet,
    Normal,
    Verbose,
}

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

// Standard Output Functions
pub fn print_header(message: &str) {
    if is_quiet() {
        return;
    }
    println!("\n{}", message.bold().blue());
    println!("{}", "─".repeat(message.len()).blue());
}

pub fn print_status(message: &str) {
    if is_quiet() {
        return;
    }
    println!("{}", message.blue());
}

pub fn print_success(message: &str) {
    if is_quiet() {
        return;
    }
    println!("{}", format!("✓ {}", message).green());
}

pub fn print_warning(message: &str) {
    // Always print warnings, even in quiet mode
    println!("{}", format!("⚠ {}", message).yellow());
}

pub fn print_error(message: &str) {
    // Always print errors, even in quiet mode
    println!("{}", format!("✗ {}", message).red().bold());
}

pub fn print_step(step: &str, item: &str) {
    if is_quiet() {
        return;
    }
    println!("→ {} {}", step.bold().blue(), item);
}

pub fn print_substep(message: &str) {
    if is_quiet() {
        return;
    }
    println!("  • {}", message);
}

pub fn print_detailed(message: &str) {
    if !is_verbose() {
        return;
    }
    println!("  {}", message.dimmed());
}

// Progress Tracking Structure
pub struct BuildProgress {
    total_steps: usize,
    current_step: usize,
    project_name: String,
    start_time: Instant,
}

impl BuildProgress {
    pub fn new(project_name: &str, total_steps: usize) -> Self {
        if !is_quiet() {
            println!("┌{:─^50}┐", "");
            println!("│{:^50}│", format!("Building: {}", project_name.bold()));
            println!("└{:─^50}┘", "");
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
            println!("[{}/{}] {}", self.current_step, self.total_steps, step_name);
        }
    }

    pub fn complete(&self) {
        if !is_quiet() {
            let duration = self.start_time.elapsed();
            let seconds = duration.as_secs();
            let millis = duration.subsec_millis();

            println!("✓ {} completed in {}.{:03}s",
                     self.project_name.bold().green(),
                     seconds,
                     millis);
        }
    }
}

// Progress Spinner for long-running operations
pub struct ProgressSpinner {
    message: String,
    stop_signal: Arc<Mutex<bool>>,
    handle: Option<thread::JoinHandle<()>>,
}

impl ProgressSpinner {
    pub fn start(message: &str) -> Self {
        let message = message.to_string();
        let stop_signal = Arc::new(Mutex::new(false));
        let stop_clone = stop_signal.clone();

        let handle = if !is_quiet() {
            let msg = message.clone();
            Some(thread::spawn(move || {
                let spinner = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
                let mut i = 0;

                loop {
                    {
                        let should_stop = *stop_clone.lock().unwrap();
                        if should_stop {
                            break;
                        }
                    }

                    print!("\r{} {} ", spinner[i % spinner.len()].blue(), msg);
                    io::stdout().flush().unwrap();
                    thread::sleep(Duration::from_millis(100));
                    i += 1;
                }

                // Clear the line
                print!("\r{}\r", " ".repeat(msg.len() + 3));
                io::stdout().flush().unwrap();
            }))
        } else {
            None
        };

        Self {
            message,
            stop_signal,
            handle,
        }
    }

    pub fn success(self) {
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        if !is_quiet() {
            println!("{} {}", "✓".green(), self.message);
        }
    }

    pub fn failure(self, error: &str) {
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        print_error(&format!("{}: {}", self.message, error));
    }
}

// Task List for multi-step operations
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

        println!("Tasks:");
        for (i, task) in self.tasks.iter().enumerate() {
            let prefix = if Some(i) == self.current_task {
                "→".blue().bold()
            } else if self.completed_tasks[i] {
                "✓".green()
            } else {
                " ".normal()
            };
            println!(" {} {}", prefix, task);
        }
        println!();
    }

    pub fn start_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.current_task = Some(index);
        if !is_quiet() {
            print_step("Starting", &self.tasks[index]);
            self.display();
        }
    }

    pub fn complete_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.completed_tasks[index] = true;
        if !is_quiet() {
            print_success(&format!("Completed: {}", self.tasks[index]));
        }
    }

    pub fn all_completed(&self) -> bool {
        self.completed_tasks.iter().all(|&completed| completed)
    }
}

// Formatting helpers
pub fn format_project_name(name: &str) -> ColoredString {
    name.cyan().bold()
}

pub fn format_command(cmd: &str) -> ColoredString {
    cmd.magenta()
}

pub fn format_section(section: &str) -> ColoredString {
    section.blue().bold()
}