// Updated output_utils.rs with cleaner, more consistent styling

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
    pub static ref VERBOSITY: Mutex<Verbosity> = Mutex::new(Verbosity::Normal);
    pub static ref LAYOUT: Mutex<LayoutManager> = Mutex::new(LayoutManager::new());
    pub static ref ACTIVE_PROGRESS_BARS: Mutex<usize> = Mutex::new(0);
    pub static ref OUTPUT_MUTEX: Mutex<()> = Mutex::new(());
    pub static ref SPINNER_ACTIVE: Mutex<bool> = Mutex::new(false);
    pub static ref LAST_LINE_WAS_NEWLINE: Mutex<bool> = Mutex::new(true);
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
            let stop_clone = stop_clone.clone();  // Clone again for the thread
            let status_clone = status_clone.clone();  // Clone again for the thread

            Some(thread::spawn(move || {
                let spinner_chars = vec!["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
                let mut i = 0;

                // Ensure we start on a new line - with a temporary guard
                {
                    let _guard = OUTPUT_MUTEX.lock().unwrap();
                    ensure_single_newline();
                }

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

                    // Acquire lock just for printing, then release it
                    {
                        let _guard = OUTPUT_MUTEX.lock().unwrap();
                        print!("\r{} {} {} ",
                               spinner_char.cyan(),
                               msg.blue().bold(),
                               status.dimmed());
                        io::stdout().flush().unwrap();
                    }

                    thread::sleep(Duration::from_millis(80));
                }

                // Final cleanup with lock
                {
                    let _guard = OUTPUT_MUTEX.lock().unwrap();
                    // Clear the line completely on exit
                    print!("\r{}\r", " ".repeat(120));
                    io::stdout().flush().unwrap();
                }

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

        // Signal the spinner to stop - use try_lock with a fallback
        match self.stop_signal.try_lock() {
            Ok(mut guard) => {
                *guard = true;
            },
            Err(_) => {
                // If we can't get the lock, print directly without waiting for spinner
                println!("✓ {} ({})", self.message.green(), format_duration(elapsed).yellow());
                return;
            }
        }

        // Use a timeout for joining the thread
        if let Some(handle) = self.handle {
            let thread_join_timeout = Duration::from_secs(1);
            let (tx, rx) = std::sync::mpsc::channel();

            // Spawn a thread to do the join
            let join_thread = thread::spawn(move || {
                let _ = handle.join();
                let _ = tx.send(());
            });

            // Wait with timeout
            match rx.recv_timeout(thread_join_timeout) {
                Ok(_) => {}, // Thread joined successfully
                Err(_) => {  // Join timed out, continue anyway
                    // We'll print the success message ourselves
                }
            }
        }

        // Only try to acquire the output lock if it's available
        if let Ok(_guard) = OUTPUT_MUTEX.try_lock() {
            // Print success message
            println!("✓ {} ({})",
                     self.message.green(),
                     format_duration(elapsed).yellow());

            // Mark that we printed something
            mark_line_printed();
        } else {
            // Can't get lock, print directly
            println!("✓ {} ({})", self.message.green(), format_duration(elapsed).yellow());
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

        // Print failure message - more compact
        println!("✗ {}: {}",
                 self.message.red(),
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
        let stop_clone = stop_signal.clone();

        // Create a new channel
        let (tx, rx) = std::sync::mpsc::channel::<String>();
        let status_message = Arc::new(Mutex::new(String::new()));
        let status_clone = status_message.clone();


        // Only create a new handle if we're not in quiet mode
        {
            let mut spinner_active = SPINNER_ACTIVE.lock().unwrap();
            *spinner_active = true;
        }

        // Only create the handle if we're not in quiet mode
        let handle = if !is_quiet() {
            let msg = message.clone();
            let stop_clone = stop_clone.clone();  // Clone again for the thread
            let status_clone = status_clone.clone();  // Clone again for the thread

            Some(thread::spawn(move || {
                let spinner_chars = vec!["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
                let mut i = 0;

                // Ensure we start on a new line - with a temporary guard
                {
                    let _guard = OUTPUT_MUTEX.lock().unwrap();
                    ensure_single_newline();
                }

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

                    // Acquire lock just for printing, then release it
                    {
                        let _guard = OUTPUT_MUTEX.lock().unwrap();
                        print!("\r{} {} {} ",
                               spinner_char.cyan(),
                               msg.blue().bold(),
                               status.dimmed());
                        io::stdout().flush().unwrap();
                    }

                    thread::sleep(Duration::from_millis(80));
                }

                // Final cleanup with lock
                {
                    let _guard = OUTPUT_MUTEX.lock().unwrap();
                    // Clear the line completely on exit
                    print!("\r{}\r", " ".repeat(120));
                    io::stdout().flush().unwrap();
                }

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
}

// Helper to ensure we don't print too many newlines
pub fn ensure_single_newline() {
    let mut was_newline = LAST_LINE_WAS_NEWLINE.lock().unwrap();
    if !*was_newline {
        println!();
        *was_newline = true;
    }
}

// Helper to mark that we printed something
pub fn mark_line_printed() {
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

pub fn print_header(message: &str, icon: Option<&str>) {
    if is_quiet() {
        return;
    }

    // Acquire output lock
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    // If a spinner is active, make sure we're on a new line
    if *SPINNER_ACTIVE.lock().unwrap() {
        println!();
        *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
    }

    // Don't add extra blank line before header
    if !*LAST_LINE_WAS_NEWLINE.lock().unwrap() {
        println!();
    }

    let prefix = match icon {
        Some(i) => format!("{} ", i),
        None => "".to_string(),
    };

    println!("{}{}", prefix, message.blue().bold());
    println!("{}", "─".repeat(message.len() + prefix.len()).blue());

    // Mark that we printed something
    mark_line_printed();
}

pub fn print_status(message: &str) {
    if is_quiet() {
        return;
    }

    // Acquire output lock
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    // If a spinner is active, make sure we're on a new line
    if *SPINNER_ACTIVE.lock().unwrap() {
        println!();
        *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
    }

    // More compact prefix - just a simple arrow
    println!("→ {}", message.blue());

    // Mark that we printed something
    mark_line_printed();
}

pub fn print_substep(message: &str) {
    if is_quiet() {
        return;
    }

    // Acquire output lock
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    // If a spinner is active, make sure we're on a new line
    if *SPINNER_ACTIVE.lock().unwrap() {
        println!();
        *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
    }

    // More compact bullet style
    println!("  • {}", message);

    // Mark that we printed something
    mark_line_printed();
}

pub fn print_success(message: &str, details: Option<&str>) {
    if is_quiet() {
        return;
    }

    // Don't add extra newline before success message
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    println!("✓ {}", message.green());

    if let Some(d) = details {
        println!("  {}", d.green());
    }

    // Mark that we printed something
    mark_line_printed();
}

pub fn print_warning(message: &str, solution: Option<&str>) {
    // Always print warnings, even in quiet mode
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    println!("⚠ {}", message.yellow());

    if let Some(s) = solution {
        println!("  Suggestion: {}", s.yellow());
    }

    // Mark that we printed something
    mark_line_printed();
}

pub fn print_error(message: &str, code: Option<&str>, solution: Option<&str>) {
    // Always print errors, even in quiet mode
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    let error_prefix = match code {
        Some(c) => format!("✗ [{}]", c),
        None => "✗".to_string(),
    };

    println!("{} {}", error_prefix.red().bold(), message.red());

    if let Some(s) = solution {
        println!("  Solution: {}", s.yellow());
    }

    // Mark that we printed something
    mark_line_printed();
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
    steps: Vec<usize>
}

impl BuildProgress {
    pub fn new(project_name: &str, total_steps: usize) -> Self {
        if !is_quiet() {
            // Print a simpler project header
            println!(":: Building {} ::", project_name.cyan().bold());
        }

        Self {
            total_steps,
            current_step: 0,
            project_name: project_name.to_string(),
            start_time: Instant::now(),
            steps: Vec::with_capacity(total_steps),
        }
    }

    pub fn next_step(&mut self, step_name: &str) {
        self.current_step += 1;
        if !is_quiet() {
            // Ensure we're not interrupting a spinner
            ensure_single_newline();

            // More compact step display
            println!("[{}/{}] {}",
                     self.current_step.to_string().cyan(),
                     self.total_steps,
                     step_name);
        }
    }

    pub fn complete(&self) {
        if !is_quiet() {
            let duration = self.start_time.elapsed();

            // More compact completion message
            println!("✓ {} completed in {}",
                     self.project_name.green(),
                     format_duration(duration).yellow());
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
        let tasks_len = tasks.len();
        Self {
            tasks,
            current_task: None,
            completed_tasks: vec![false; tasks_len], // Initialize with false values based on tasks length
        }
    }

    pub fn display(&self) {
        if is_quiet() {
            return;
        }

        println!("Tasks:");
        for (i, task) in self.tasks.iter().enumerate() {
            let prefix = if Some(i) == self.current_task {
                "→".blue()
            } else if i < self.completed_tasks.len() && self.completed_tasks[i] {
                "✓".green()
            } else {
                "○".normal().dimmed()
            };
            println!("{} {}", prefix, task);
        }
    }

    pub fn start_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.current_task = Some(index);
        if !is_quiet() {
            // No extra line break
            print_status(&format!("Starting {}", self.tasks[index]));
            self.display();
        }
    }

    pub fn complete_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        // Ensure the completed_tasks vector has enough elements
        while self.completed_tasks.len() <= index {
            self.completed_tasks.push(false);
        }

        self.completed_tasks[index] = true;
        if !is_quiet() {
            println!("✓ {}", self.tasks[index].green());
        }
    }

    pub fn all_completed(&self) -> bool {
        if self.completed_tasks.len() != self.tasks.len() {
            return false;
        }
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