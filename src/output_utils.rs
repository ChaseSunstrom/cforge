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

        // Only create the handle if we're not in quiet mode
        let handle = if !is_quiet() {
            let msg = message.clone();
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

                    // Use carriage return to rewrite the line
                    print!("\r{} {} {} ",
                           spinner_char.cyan(),
                           msg.blue(),
                           status);
                    io::stdout().flush().unwrap();

                    thread::sleep(Duration::from_millis(80));
                }

                // Important: clear the line completely on exit
                print!("\r{}\r", " ".repeat(120));
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

    pub fn update_status(&self, status: &str) {
        if !is_quiet() && self.handle.is_some() {
            // Send the new status through the channel
            let _ = self.update_channel.0.send(status.to_string());
        }
    }

    pub fn success(self) {
        let elapsed = self.start_time.elapsed();
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        if !is_quiet() {
            // The spinner thread already cleared the line
            println!("✓ {} (completed in {})",
                     self.message.green(),
                     format_duration(elapsed).yellow());
        }
    }

    pub fn failure(self, error: &str) {
        let elapsed = self.start_time.elapsed();
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        // The spinner thread already cleared the line
        println!("✗ {}: {}",
                 self.message.red().bold(),
                 error.red());

        // Add a newline after failure messages to improve readability
        println!();
    }
}

// Manual implementation of Clone for SpinningWheel
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
        // Get terminal size
        let (width, height) = match detect_terminal_size() {
            Some((w, h)) => (w, h),
            None => (100, 30), // Default fallback size
        };

        // Check if terminal supports Unicode (simplified check)
        let supports_unicode = true; // In practice, you'd want to detect this

        // Check if terminal supports colors (colored crate already does this)
        let supports_colors = colored::control::SHOULD_COLORIZE.should_colorize();

        Self {
            width,
            height,
            supports_unicode,
            supports_colors,
            theme: Theme::Default,
        }
    }

    pub fn get_box_chars(&self) -> (&str, &str, &str, &str, &str, &str) {
        if self.supports_unicode {
            ("╭", "╮", "╰", "╯", "│", "─")
        } else {
            ("+", "+", "+", "+", "|", "-")
        }
    }

    pub fn get_spinner_chars(&self) -> Vec<&str> {
        if self.supports_unicode {
            match self.theme {
                Theme::Ocean => vec!["◐", "◓", "◑", "◒"],
                Theme::Forest => vec!["◰", "◳", "◲", "◱"],
                _ => vec!["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"],
            }
        } else {
            vec!["-", "\\", "|", "/"]
        }
    }

    pub fn get_width(&self) -> usize {
        self.width
    }

    pub fn get_progress_chars(&self) -> (String, String) {
        if self.supports_unicode {
            match self.theme {
                Theme::Ocean => ("▰".to_string(), "▱".to_string()),
                Theme::Forest => ("█".to_string(), "░".to_string()),
                _ => ("█".to_string(), "░".to_string()),
            }
        } else {
            ("#".to_string(), "-".to_string())
        }
    }

    pub fn set_theme(&mut self, theme: Theme) {
        self.theme = theme;
    }
}

// Helper function to detect terminal size
fn detect_terminal_size() -> Option<(usize, usize)> {
    // A more practical implementation would use an actual terminal size detection
    // For now, return a sensible default
    Some((100, 30))
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

pub fn set_theme(theme_name: &str) {
    let mut layout = LAYOUT.lock().unwrap();
    layout.set_theme(match theme_name.to_lowercase().as_str() {
        "dark" => Theme::Dark,
        "light" => Theme::Light,
        "ocean" => Theme::Ocean,
        "forest" => Theme::Forest,
        _ => Theme::Default,
    });
}

// Acquire a progress bar if possible, returns true if we can create a new one
pub fn can_acquire_progress_bar() -> bool {
    let mut count = ACTIVE_PROGRESS_BARS.lock().unwrap();
    if *count >= 1 {
        // Only allow one active progress bar at a time
        false
    } else {
        *count += 1;
        true
    }
}

// Release a progress bar when done
pub fn release_progress_bar() {
    let mut count = ACTIVE_PROGRESS_BARS.lock().unwrap();
    if *count > 0 {
        *count -= 1;
    }
}


//==============================================================================
// TimedProgressBar - Core for all long-running operations
//==============================================================================

pub struct TimedProgressBar {
    message: String,
    start_time: Instant,
    expected_duration: Duration,
    width: usize,
    stop_signal: Arc<Mutex<bool>>,
    handle: Option<thread::JoinHandle<()>>,
    update_channel: (std::sync::mpsc::Sender<String>, Arc<Mutex<String>>),
    last_update: Arc<Mutex<Instant>>,
}

impl TimedProgressBar {
    pub fn start(message: &str, expected_seconds: u64) -> Self {
        let message = message.to_string();
        let expected_duration = Duration::from_secs(expected_seconds);
        let start_time = Instant::now();
        let stop_signal = Arc::new(Mutex::new(false));
        let stop_clone = stop_signal.clone();

        // Channel for status updates
        let (tx, rx) = std::sync::mpsc::channel::<String>();
        let status_message = Arc::new(Mutex::new(String::new()));
        let status_clone = status_message.clone();

        // Track last update time to show activity
        let last_update = Arc::new(Mutex::new(Instant::now()));
        let last_update_clone = last_update.clone();

        // Get the necessary data from layout *before* spawning the thread
        let progress_chars = {
            let layout = LAYOUT.lock().unwrap();
            // Clone the actual characters instead of getting references
            let (filled, empty) = layout.get_progress_chars();
            (filled.to_string(), empty.to_string())
        };
        let (filled_char, empty_char) = progress_chars;

        // Calculate width now too
        let width = {
            let layout = LAYOUT.lock().unwrap();
            cmp::min(layout.get_width() * 7 / 10, 60)
        };

        // Only create the handle if we're not in quiet mode and we can acquire a progress bar
        let handle = if !is_quiet() && can_acquire_progress_bar() {
            let msg = message.clone();
            Some(thread::spawn(move || {
                let mut last_percent: f32 = 0.0;

                loop {
                    {
                        let should_stop = *stop_clone.lock().unwrap();
                        if should_stop {
                            break;
                        }
                    }

                    // Calculate progress based on elapsed time vs expected time
                    let elapsed = start_time.elapsed();
                    let percent = (elapsed.as_secs_f32() / expected_duration.as_secs_f32())
                        .min(0.99); // Cap at 99% since we don't know actual completion

                    // Get status message if available
                    let status = match rx.try_recv() {
                        Ok(new_status) => {
                            *status_clone.lock().unwrap() = new_status.clone();
                            *last_update_clone.lock().unwrap() = Instant::now();
                            new_status
                        },
                        Err(_) => {
                            // Show elapsed time if no status updates in a while
                            let last = *last_update_clone.lock().unwrap();
                            if last.elapsed() > Duration::from_secs(3) {
                                *last_update_clone.lock().unwrap() = Instant::now();
                                format!("Working... ({}s elapsed)", elapsed.as_secs())
                            } else {
                                status_clone.lock().unwrap().clone()
                            }
                        }
                    };

                    // Only redraw if percentage changed or new status
                    if (percent - last_percent).abs() > 0.01 || !status.is_empty() {
                        let filled_width = (width as f32 * percent) as usize;
                        let empty_width = width - filled_width;

                        // Create the progress bar
                        let bar = filled_char.repeat(filled_width) + &empty_char.repeat(empty_width);

                        print!("\r{} [{}] {:.1}% {} ",
                               msg.blue().bold(),
                               bar,
                               percent * 100.0,
                               status);
                        io::stdout().flush().unwrap();

                        last_percent = percent;
                    }

                    thread::sleep(Duration::from_millis(100));
                }

                // Clear the line
                let status_len = status_clone.lock().unwrap().len();
                print!("\r{}\r", " ".repeat(msg.len() + width + status_len + 20));
                io::stdout().flush().unwrap();

                // Release the progress bar
                release_progress_bar();
            }))
        } else {
            None
        };

        Self {
            message,
            start_time,
            expected_duration,
            width,
            stop_signal,
            handle,
            update_channel: (tx, status_message),
            last_update,
        }
    }

    pub fn update_status(&self, status: &str) {
        if !is_quiet() && self.handle.is_some() {
            let _ = self.update_channel.0.send(status.to_string());
        }
    }

    pub fn success(self) {
        let elapsed = self.start_time.elapsed();
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        } else {
            // If we didn't create a handle (because of quiet mode or too many bars),
            // we still need to release the progress bar if we acquired one
            if !is_quiet() {
                release_progress_bar();
            }
        }

        if !is_quiet() {
            println!("{} {} (completed in {})",
                     "✓".green().bold(),
                     self.message,
                     format_duration(elapsed).yellow());
        }
    }

    pub fn failure(self, error: &str) {
        let elapsed = self.start_time.elapsed();
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        } else {
            // If we didn't create a handle, release the progress bar if we acquired one
            if !is_quiet() {
                release_progress_bar();
            }
        }

        print_error(&format!("{}: {} (after {})",
                             self.message,
                             error,
                             format_duration(elapsed)), None, None);
    }
}

//==============================================================================
// Command execution with progress bar
//==============================================================================

// Run a command with progress updates
pub fn run_command_with_progress(
    cmd: Vec<String>,
    cwd: Option<&str>,
    env: Option<std::collections::HashMap<String, String>>,
    progress: &TimedProgressBar,
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

                // Send interesting output to progress bar
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

//==============================================================================
// Basic Output Functions
//==============================================================================

// Beautifully formatted header with optional icon
pub fn print_header(message: &str, icon: Option<&str>) {
    if is_quiet() {
        return;
    }

    println!(); // Only one line before header

    let prefix = match icon {
        Some(i) => format!("{} ", i),
        None => "".to_string(),
    };

    println!("{}{}", prefix, message.bold().blue());
    println!("{}", "─".repeat(message.len() + prefix.len()).blue());
}

// Standard status message
// Standard status message with consistent color
pub fn print_status(message: &str) {
    if is_quiet() {
        return;
    }
    println!("{}", message.blue().bold());
}

// Success message with proper coloring
pub fn print_success(message: &str, details: Option<&str>) {
    if is_quiet() {
        return;
    }

    println!("✓ {}", message.green().bold());

    if let Some(d) = details {
        println!("  {}", d.green());
    }
}

// Warning message with consistent color
pub fn print_warning(message: &str, solution: Option<&str>) {
    // Always print warnings, even in quiet mode
    println!("⚠ {}", message.yellow().bold());

    if let Some(s) = solution {
        println!("  Suggestion: {}", s.yellow());
    }
}

// Error message with optional code and solution
pub fn print_error(message: &str, code: Option<&str>, solution: Option<&str>) {
    // Always print errors, even in quiet mode
    let error_prefix = match code {
        Some(c) => format!("✗ [{}] ", c),
        None => "✗ ".to_string(),
    };

    println!("{}{}", error_prefix.red().bold(), message.red().bold());

    if let Some(s) = solution {
        println!("  Solution: {}", s.yellow());
    }
}

// Print a step with an action and a target
pub fn print_step(action: &str, target: &str) {
    if is_quiet() {
        return;
    }
    println!("\n→ {} {}", action.bold().blue(), target);
}

// Print a substep with bullet point
pub fn print_substep(message: &str) {
    if is_quiet() {
        return;
    }
    println!("  • {}", message);
}

// Print detailed information (only in verbose mode)
pub fn print_detailed(message: &str) {
    if !is_verbose() {
        return;
    }
    println!("  {}", message.dimmed());
}

// Format a project name with appropriate styling
pub fn format_project_name(name: &str) -> ColoredString {
    name.cyan().bold()
}

//==============================================================================
// Progress Tracking Components
//==============================================================================

// Standard progress bar for tracking discrete operations
pub struct ProgressBar {
    message: String,
    start_time: Instant,
    width: usize,
    stop_signal: Arc<Mutex<bool>>,
    progress: Arc<Mutex<f32>>,  // 0.0 to 1.0 for progress percentage
    handle: Option<thread::JoinHandle<()>>,
}

impl ProgressBar {
    pub fn start(message: &str) -> Self {
        let message = message.to_string();
        let start_time = Instant::now();
        let stop_signal = Arc::new(Mutex::new(false));
        let stop_clone = stop_signal.clone();
        let progress = Arc::new(Mutex::new(0.0f32));
        let progress_clone = progress.clone();

        // Get the necessary data from layout *before* spawning the thread
        let (filled_char, empty_char) = {
            let layout = LAYOUT.lock().unwrap();
            let (filled, empty) = layout.get_progress_chars();
            (filled.to_string(), empty.to_string())
        };

        // Calculate width
        let width = {
            let layout = LAYOUT.lock().unwrap();
            cmp::min(layout.get_width() * 7 / 10, 60)
        };

        // Only create the handle if we're not in quiet mode and we can acquire a progress bar
        let handle = if !is_quiet() && can_acquire_progress_bar() {
            let msg = message.clone();
            Some(thread::spawn(move || {
                let mut last_percent: f32 = 0.0;

                loop {
                    {
                        let should_stop = *stop_clone.lock().unwrap();
                        if should_stop {
                            break;
                        }
                    }

                    // Get current progress
                    let percent = *progress_clone.lock().unwrap();

                    // Only redraw if percentage changed
                    if (percent - last_percent).abs() > 0.01 {
                        let filled_width = (width as f32 * percent) as usize;
                        let empty_width = width - filled_width;

                        // Create the progress bar
                        let bar = filled_char.repeat(filled_width) + &empty_char.repeat(empty_width);

                        // Show elapsed time as additional info
                        let elapsed = start_time.elapsed();
                        let elapsed_str = format!("{}s elapsed", elapsed.as_secs());

                        print!("\r{} [{}] {:.1}% {} ",
                               msg.blue().bold(),
                               bar,
                               percent * 100.0,
                               elapsed_str);
                        io::stdout().flush().unwrap();

                        last_percent = percent;
                    }

                    thread::sleep(Duration::from_millis(100));
                }

                // Clear the line
                print!("\r{}\r", " ".repeat(msg.len() + width + 40));
                io::stdout().flush().unwrap();

                // Release the progress bar
                release_progress_bar();
            }))
        } else {
            None
        };

        Self {
            message,
            start_time,
            width,
            stop_signal,
            progress,
            handle,
        }
    }

    pub fn update(&self, percent: f32) {
        if !is_quiet() && self.handle.is_some() {
            let mut progress = self.progress.lock().unwrap();
            *progress = percent.max(0.0).min(1.0); // Clamp between 0 and 1
        }
    }

    pub fn success(&mut self) {
        let elapsed = self.start_time.elapsed();

        // Make sure it shows 100% at the end
        {
            let mut progress = self.progress.lock().unwrap();
            *progress = 1.0;
        }

        // Give a brief moment to see 100%
        thread::sleep(Duration::from_millis(100));

        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        } else {
            // If we didn't create a handle, release the progress bar if we acquired one
            if !is_quiet() {
                release_progress_bar();
            }
        }

        if !is_quiet() {
            // First clear the entire line
            let terminal_width = {
                let layout = LAYOUT.lock().unwrap();
                layout.get_width()
            };

            print!("\r{}\r", " ".repeat(terminal_width));
            io::stdout().flush().unwrap();

            // Now print the completion message on a new line
            println!("{} {} (completed in {})",
                     "✓".green().bold(),
                     self.message,
                     format_duration(elapsed).yellow());
        }
    }

    pub fn failure(&mut self, error: &str) {
        let elapsed = self.start_time.elapsed();
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle.take() {
            let _ = handle.join();
        } else {
            // If we didn't create a handle, release the progress bar if we acquired one
            if !is_quiet() {
                release_progress_bar();
            }
        }

        // Clear the line completely first
        if !is_quiet() {
            let terminal_width = {
                let layout = LAYOUT.lock().unwrap();
                layout.get_width()
            };

            print!("\r{}\r", " ".repeat(terminal_width));
            io::stdout().flush().unwrap();
        }

        print_error(&format!("{}: {} (after {})",
                             self.message,
                             error,
                             format_duration(elapsed)), None, None);
    }
}


impl Clone for ProgressBar {
    fn clone(&self) -> Self {
        // When cloning, we don't want to create a new progress bar thread
        // if there's already an active one
        let mut new_bar = Self {
            message: self.message.clone(),
            start_time: self.start_time,
            width: self.width,
            stop_signal: Arc::new(Mutex::new(false)),
            progress: Arc::new(Mutex::new(0.0f32)),
            handle: None,
        };

        // Copy the current progress
        {
            let current_progress = *self.progress.lock().unwrap();
            *new_bar.progress.lock().unwrap() = current_progress;
        }

        // Only create a handle if we can acquire a progress bar and not in quiet mode
        if !is_quiet() && can_acquire_progress_bar() {
            // Now create a new handle with the same behavior
            let msg = self.message.clone();
            let width = self.width;
            let progress_clone = new_bar.progress.clone();
            let stop_clone = new_bar.stop_signal.clone();
            let start_time = self.start_time;

            // Get progress bar characters
            let (filled_char, empty_char) = {
                let layout = LAYOUT.lock().unwrap();
                let (filled, empty) = layout.get_progress_chars();
                (filled.to_string(), empty.to_string())
            };

            new_bar.handle = Some(thread::spawn(move || {
                let mut last_percent: f32 = 0.0;

                loop {
                    {
                        let should_stop = *stop_clone.lock().unwrap();
                        if should_stop {
                            break;
                        }
                    }

                    // Get current progress
                    let percent = *progress_clone.lock().unwrap();

                    // Only redraw if percentage changed
                    if (percent - last_percent).abs() > 0.01 {
                        let filled_width = (width as f32 * percent) as usize;
                        let empty_width = width - filled_width;

                        // Create the progress bar
                        let bar = filled_char.repeat(filled_width) + &empty_char.repeat(empty_width);

                        // Show elapsed time as additional info
                        let elapsed = start_time.elapsed();
                        let elapsed_str = format!("{}s elapsed", elapsed.as_secs());

                        print!("\r{} [{}] {:.1}% {} ",
                               msg.blue().bold(),
                               bar,
                               percent * 100.0,
                               elapsed_str);
                        io::stdout().flush().unwrap();

                        last_percent = percent;
                    }

                    thread::sleep(Duration::from_millis(100));
                }

                // Clear the line
                print!("\r{}\r", " ".repeat(msg.len() + width + 40));
                io::stdout().flush().unwrap();

                // Release the progress bar
                release_progress_bar();
            }));
        }

        new_bar
    }
}

// Tracking overall build progress
pub struct BuildProgress {
    total_steps: usize,
    current_step: usize,
    project_name: String,
    start_time: Instant,
    steps: Vec<String>,
}

impl BuildProgress {
    pub fn new(project_name: &str, total_steps: usize) -> Self {
        if !is_quiet() {
            print_project_box(project_name, "", "");
        }

        Self {
            total_steps,
            current_step: 0,
            project_name: project_name.to_string(),
            start_time: Instant::now(),
            steps: Vec::with_capacity(total_steps),
        }
    }

    // For BuildProgress next_step method
    pub fn next_step(&mut self, step_name: &str) {
        self.current_step += 1;
        if !is_quiet() {
            println!("[{}/{}] {}",
                     self.current_step.to_string().cyan().bold(),
                     self.total_steps,
                     step_name);

            // Show a spinning wheel to indicate overall progress
            let wheel = SpinningWheel::start("Overall progress");
            thread::sleep(Duration::from_millis(500)); // Show wheel briefly
            wheel.success();
        }
    }

    pub fn complete(&self) {
        if !is_quiet() {
            let duration = self.start_time.elapsed();
            let formatted_time = format_duration(duration);

            println!("✓ {} {}",
                     format!("{} completed in", self.project_name).green().bold(),
                     formatted_time.yellow().bold());

            print_build_summary(&self.project_name, duration);
        }
    }
}

// Task list for visually tracking multiple tasks
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

        println!("\nTasks:");
        for (i, task) in self.tasks.iter().enumerate() {
            let prefix = if Some(i) == self.current_task {
                "→".blue().bold()
            } else if self.completed_tasks[i] {
                "✓".green().bold()
            } else {
                "○".normal().dimmed()
            };
            println!("  {} {}", prefix, task);
        }

        println!();  // Add spacing after the list
    }

    pub fn start_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.current_task = Some(index);
        if !is_quiet() {
            println!(); // Add a line break before starting a new task
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
            println!("✓ Completed: {}", self.tasks[index].green());
        }
    }

    pub fn all_completed(&self) -> bool {
        self.completed_tasks.iter().all(|&completed| completed)
    }
}

//==============================================================================
// UI Components
//==============================================================================

pub fn print_project_box(project_name: &str, version: &str, build_type: &str) {
    if is_quiet() {
        return;
    }

    // Set a fixed width for consistency - make it wider
    let width = 80;

    // Top border
    println!("╭{}╮", "─".repeat(width - 2));

    // Title line with better padding calculation
    let mut title = format!("  {}  ", project_name);
    if !version.is_empty() {
        title = format!("  {} v{}  ", project_name, version);
    }

    // Get the visible length without ANSI codes
    let visible_length = strip_ansi_codes(&title).len();
    let padding = width - 2 - visible_length;

    println!("│{}{}│", title.bold(), " ".repeat(padding));

    // Bottom border
    println!("╰{}╯", "─".repeat(width - 2));

    // Add a newline after the box for better spacing
    println!();
}

// Helper function to calculate visual width by removing ANSI codes
fn strip_ansi_codes(s: &str) -> String {
    let ansi_regex = Regex::new(r"\x1B\[[0-9;]*[a-zA-Z]").unwrap();
    ansi_regex.replace_all(s, "").to_string()
}

pub fn print_build_summary(project: &str, duration: Duration) {
    if is_quiet() {
        return;
    }

    let width = 80;
    let title = " BUILD SUMMARY ";
    let title_padding = (width - 2 - strip_ansi_codes(title).len()) / 2;

    println!("╭{}{}{}╮",
             "─".repeat(title_padding),
             title.blue().bold(),
             "─".repeat(width - 2 - strip_ansi_codes(title).len() - title_padding));

    // Project line with proper padding
    let project_text = format!(" Project: {} ", project);
    let padding = width - 2 - strip_ansi_codes(&project_text).len();
    println!("│{}{}│", project_text.cyan(), " ".repeat(padding));

    // Time line with proper padding
    let time_text = format!(" Time: {} ", format_duration(duration).yellow());
    let time_padding = width - 2 - strip_ansi_codes(&time_text).len();
    println!("│{}{}│", time_text, " ".repeat(time_padding));

    // Bottom border
    println!("╰{}╯", "─".repeat(width - 2));

    // Add a newline after the summary for better spacing
    println!();
}

//==============================================================================
// Utility Functions
//==============================================================================

// Format a duration in a human-readable way
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

// Prompt the user for input with a timeout (returns default if timeout reached)
pub fn prompt_with_timeout(message: &str, default: &str, timeout_seconds: u64) -> String {
    print!("{} [{}]: ", message, default);
    io::stdout().flush().unwrap();

    let (tx, rx) = std::sync::mpsc::channel();

    // Spawn a thread to read user input
    thread::spawn(move || {
        let mut input = String::new();
        match io::stdin().read_line(&mut input) {
            Ok(_) => { let _ = tx.send(input.trim().to_string()); },
            Err(_) => { let _ = tx.send(String::new()); }
        }
    });

    // Wait for input with timeout
    match rx.recv_timeout(Duration::from_secs(timeout_seconds)) {
        Ok(input) if !input.is_empty() => input,
        _ => {
            println!("\nTimeout reached, using default: {}", default);
            default.to_string()
        }
    }
}

// Has command - Check if a command is available
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
