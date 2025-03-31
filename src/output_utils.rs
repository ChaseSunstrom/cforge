use colored::*;
use std::io::{self, Write};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;
use std::thread::JoinHandle;
use lazy_static::lazy_static;
use regex::Regex;

lazy_static! {
    pub static ref VERBOSITY: Mutex<Verbosity> = Mutex::new(Verbosity::Normal);
    pub static ref LAYOUT: Mutex<LayoutManager> = Mutex::new(LayoutManager::new());
    // Our global list of active spinners now holds SpinnerHandle with the thread.
     pub static ref SPINNER_MAP: Mutex<std::collections::HashMap<usize, SpinnerHandleData>> = Mutex::new(std::collections::HashMap::new());
    pub static ref OUTPUT_MUTEX: Mutex<()> = Mutex::new(());
    pub static ref SPINNER_ACTIVE: Mutex<bool> = Mutex::new(false);
    pub static ref LAST_LINE_WAS_NEWLINE: Mutex<bool> = Mutex::new(true);
}

static ANSI_ERASE_LINE: &str = "\x1b[2K"; // ANSI escape code to erase the entire line

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

pub struct SpinnerHandleData {
    pub stop_signal: Arc<Mutex<bool>>,
    pub handle: Option<JoinHandle<()>>,
}


// The spinning wheel itself
pub struct SpinningWheel {
    // Unique ID for cross-referencing the SPINNER_MAP
    pub id: usize,

    pub message: String,
    pub start_time: Instant,

    // So we can set stop = true from .success() or .failure()
    pub stop_signal: Arc<Mutex<bool>>,

    // We store the actual thread handle here as well
    pub handle: Option<JoinHandle<()>>,

    // If you need a channel to update the status text
    pub update_tx: std::sync::mpsc::Sender<String>,
    pub update_msg: Arc<Mutex<String>>,
}

impl SpinningWheel {
    pub fn start(message: &str) -> Self {
        // Make a unique ID
        let spinner_id = rand::random::<u32>() as usize;

        // Clear old spinners first
        ensure_all_spinners_cleared();

        let stop_signal = Arc::new(Mutex::new(false));
        let (tx, rx) = std::sync::mpsc::channel::<String>();
        let status_msg = Arc::new(Mutex::new(String::new()));

        let message_str = message.to_string();
        let start_time = Instant::now();

        // Only create a thread if not quiet
        let handle = if !is_quiet() {
            let stop_clone = stop_signal.clone();
            let msg_clone = message_str.clone();
            let status_clone = status_msg.clone();

            Some(thread::spawn(move || {
                let spinner_chars = vec!["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"];
                let mut i = 0;

                {
                    let _lock = OUTPUT_MUTEX.lock().unwrap();
                    ensure_single_newline();
                }

                loop {
                    if *stop_clone.lock().unwrap() {
                        break;
                    }

                    // See if there's a new status from the channel
                    let status = match rx.try_recv() {
                        Ok(new_status) => {
                            *status_clone.lock().unwrap() = new_status.clone();
                            new_status
                        },
                        Err(_) => {
                            let elapsed = start_time.elapsed();
                            format!("{:.1}s", elapsed.as_secs_f32())
                        }
                    };

                    let spinner_char = spinner_chars[i % spinner_chars.len()];
                    i += 1;

                    {
                        let _guard = OUTPUT_MUTEX.lock().unwrap();
                        print!("\r");
                        print!("{}", ANSI_ERASE_LINE);
                        print!("{} {} {} ",
                               spinner_char.cyan(),
                               msg_clone.blue().bold(),
                               status.dimmed()
                        );
                        io::stdout().flush().unwrap();
                    }

                    std::thread::sleep(Duration::from_millis(80));
                }

                // Final clearing
                {
                    let _guard = OUTPUT_MUTEX.lock().unwrap();
                    print!("\r");
                    print!("{}", ANSI_ERASE_LINE);
                    print!("\r");
                    print!("                                        \r");
                    print!("\x1B[1K\r");
                    io::stdout().flush().unwrap();
                }

                *SPINNER_ACTIVE.lock().unwrap() = false;
                *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
            }))
        } else {
            None
        };

        // Register in global
        {
            let mut map = SPINNER_MAP.lock().unwrap();
            map.insert(spinner_id, SpinnerHandleData {
                stop_signal: stop_signal.clone(),
                handle,
            });
        }

        {
            *SPINNER_ACTIVE.lock().unwrap() = true;
        }

        Self {
            id: spinner_id,
            message: message_str,
            start_time,
            stop_signal,
            handle: None,  // We'll retrieve it from SPINNER_MAP in .success()/.failure()
            update_tx: tx,
            update_msg: status_msg,
        }
    }

    pub fn update_status(&self, new_status: &str) {
        if !is_quiet() {
            let _ = self.update_tx.send(new_status.to_string());
        }
    }

    pub fn success(mut self) {
        let elapsed = self.start_time.elapsed();
        self.stop_and_join();

        {
            let _guard = OUTPUT_MUTEX.lock().unwrap();
            print!("\r");
            print!("{}", ANSI_ERASE_LINE);
            print!("\r");
            print!("                                        \r");
            print!("\x1B[1K\r");
            io::stdout().flush().unwrap();
        }

        println!("✓ {} ({})", self.message.green(), format_duration(elapsed).yellow());
        mark_line_printed();
    }

    pub fn failure(mut self, error: &str) {
        self.stop_and_join();

        {
            let _guard = OUTPUT_MUTEX.lock().unwrap();
            print!("\r");
            print!("{}", ANSI_ERASE_LINE);
            io::stdout().flush().unwrap();
        }

        eprintln!("✗ {}: {}", self.message.red(), error.red());
        mark_line_printed();
    }

    /// Actually signals the thread to stop, takes the handle from the map, and joins it.
    fn stop_and_join(&mut self) {
        // 1) Set stop_signal
        *self.stop_signal.lock().unwrap() = true;

        // 2) Remove from SPINNER_MAP, join the thread
        let mut map = SPINNER_MAP.lock().unwrap();
        if let Some(mut data) = map.remove(&self.id) {
            if let Some(h) = data.handle.take() {
                let _ = h.join();
            }
        }
    }
}

// If you really want to clone and keep references to a spinner, you can do that.
impl Clone for SpinningWheel {
    fn clone(&self) -> Self {
        Self {
            id: self.id,
            message: self.message.clone(),
            start_time: self.start_time,
            stop_signal: Arc::clone(&self.stop_signal),
            // We cannot clone a JoinHandle, so we set handle = None in the clone
            handle: None,
            update_tx: self.update_tx.clone(),
            update_msg: Arc::clone(&self.update_msg),
        }
    }
}


// CHANGED: ensure_all_spinners_cleared now sets stop_signal *and joins* each thread
pub fn ensure_all_spinners_cleared() {
    let mut map = SPINNER_MAP.lock().unwrap();

    // For each spinner, set stop = true
    for (_id, data) in map.iter() {
        *data.stop_signal.lock().unwrap() = true;
    }

    // Join them
    for (_id, data) in map.iter_mut() {
        if let Some(h) = data.handle.take() {
            let _ = h.join();
        }
    }

    map.clear();

    // Finally clear the line
    let _guard = OUTPUT_MUTEX.lock().unwrap();
    print!("\r");
    print!("{}", ANSI_ERASE_LINE);
    print!("\r");
    print!("                                  \r");
    print!("\x1B[1K\r");
    io::stdout().flush().unwrap();
}

pub fn ensure_spinner_cleared() {
    let _guard = OUTPUT_MUTEX.lock().unwrap();
    print!("\r");
    print!("{}", ANSI_ERASE_LINE);
    print!("\r");
    print!("                                        \r");
    io::stdout().flush().unwrap();
}

pub fn ensure_single_newline() {
    let mut was_newline = LAST_LINE_WAS_NEWLINE.lock().unwrap();
    if !*was_newline {
        println!();
        *was_newline = true;
    }
}

pub fn mark_line_printed() {
    let mut was_newline = LAST_LINE_WAS_NEWLINE.lock().unwrap();
    *was_newline = false;
}

pub struct LayoutManager {
    width: usize,
    height: usize,
    supports_unicode: bool,
    supports_colors: bool,
    theme: Theme,
}

impl LayoutManager {
    pub fn new() -> Self {
        let (width, height) = (100, 30);
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
    pub fn get_width(&self) -> usize {
        self.width
    }
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

pub fn print_message(msg_type: MessageType, message: &str, details: Option<&str>) {
    if is_quiet() && !matches!(msg_type, MessageType::Warning | MessageType::Error) {
        return;
    }
    ensure_spinner_cleared();

    let _guard = OUTPUT_MUTEX.lock().unwrap();
    if *SPINNER_ACTIVE.lock().unwrap() {
        println!();
        *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
    }

    ensure_single_newline();

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

pub fn print_success(message: &str, details: Option<&str>) {
    if is_quiet() {
        return;
    }

    ensure_spinner_cleared();
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    println!("✓ {}", message.green());
    if let Some(d) = details {
        println!("  {}", d.green());
    }
    mark_line_printed();
}

pub fn print_warning(message: &str, suggestion: Option<&str>) {
    ensure_spinner_cleared();
    let _guard = OUTPUT_MUTEX.lock().unwrap();
    println!("⚠ {}", message.yellow());
    if let Some(s) = suggestion {
        println!("  Suggestion: {}", s.yellow());
    }
    mark_line_printed();
}

pub fn print_status(message: &str) {
    if is_quiet() {
        return;
    }

    // Acquire output lock

    ensure_spinner_cleared();
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    // Handle spinner active case without redundant logging
    if *SPINNER_ACTIVE.lock().unwrap() {
        println!();
        *LAST_LINE_WAS_NEWLINE.lock().unwrap() = true;
    }

    // Simple, consistent prefix
    println!("→ {}", message.blue());
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

pub fn format_project_name(name: &str) -> ColoredString {
    name.cyan().bold()
}

pub fn print_detailed(message: &str) {
    if is_verbose() {
        ensure_spinner_cleared();
        print_message(MessageType::Detail, message, None);
    }
}

pub fn print_step(action: &str, target: &str) {
    if is_quiet() {
        return;
    }

    // Acquire output lock for thread safety

    ensure_spinner_cleared();
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
    if is_quiet() {
        return;
    }

    ensure_spinner_cleared();
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    // More compact bullet style
    println!("  • {}", message);

    // Mark that we printed something
    mark_line_printed();
}

pub fn print_error(message: &str, code: Option<&str>, solution: Option<&str>) {
    ensure_spinner_cleared();
    let _guard = OUTPUT_MUTEX.lock().unwrap();

    let error_prefix = code.map_or("✗".to_string(), |c| format!("✗ [{}]", c));
    println!("{} {}", error_prefix.red().bold(), message.red());

    if let Some(s) = solution {
        println!("  Solution: {}", s.yellow());
    }
    mark_line_printed();
}

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