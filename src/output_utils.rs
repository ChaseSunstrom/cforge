use colored::*;
use std::io::{self, Write};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;
use std::cmp;
use lazy_static::lazy_static;

// Optional dependencies - would need to be added to Cargo.toml
// use terminal_size::{Width, Height, terminal_size};
// use notify_rust::Notification;

// Global verbosity control
lazy_static! {
    static ref VERBOSITY: Mutex<Verbosity> = Mutex::new(Verbosity::Normal);
    static ref LAYOUT: Mutex<LayoutManager> = Mutex::new(LayoutManager::new());
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
        // Get terminal size if the terminal_size crate is available
        let (width, height) = match get_terminal_size() {
            Some((w, h)) => (w, h),
            None => (80, 24), // Default fallback size
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

    pub fn get_progress_chars(&self) -> (&str, &str) {
        if self.supports_unicode {
            match self.theme {
                Theme::Ocean => ("▰", "▱"),
                Theme::Forest => ("█", "░"),
                _ => ("█", "░"),
            }
        } else {
            ("#", "-")
        }
    }

    pub fn set_theme(&mut self, theme: Theme) {
        self.theme = theme;
    }
}

// Helper function to get terminal size (would normally use terminal_size crate)
fn get_terminal_size() -> Option<(usize, usize)> {
    // Simplified implementation - normally would use terminal_size crate
    // if let Some((Width(w), Height(h))) = terminal_size() {
    //     Some((w as usize, h as usize))
    // } else {
    //     None
    // }

    // For this example, we'll just return a reasonable default
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

// ==================== ENHANCED OUTPUT FUNCTIONS ====================

// Beautifully formatted header with optional icon
pub fn print_header(message: &str, icon: Option<&str>) {
    if is_quiet() {
        return;
    }

    let layout = LAYOUT.lock().unwrap();
    let width = layout.get_width();
    let adjusted_width = cmp::min(width, 100);

    // Get box drawing characters based on terminal capabilities
    let (tl, tr, bl, br, v, h) = layout.get_box_chars();

    let prefix = match icon {
        Some(i) => format!("{} ", i),
        None => "".to_string(),
    };

    println!("\n{}", format!("{}{}", prefix, message).bold().blue());
    println!("{}", h.repeat(message.len() + prefix.len()).blue());
}

// Enhanced section header with background color
pub fn print_section_header(message: &str) {
    if is_quiet() {
        return;
    }

    println!("\n{} {}", "●".cyan().bold(), message.white().bold().on_blue());
    println!("  {}", "┄".repeat(message.len() + 4).cyan());
}

// Standard status message
pub fn print_status(message: &str) {
    if is_quiet() {
        return;
    }
    println!("{}", message.blue());
}

// Success message with optional details
pub fn print_success(message: &str, details: Option<&str>) {
    if is_quiet() {
        return;
    }

    println!("{}", format!("✓ {}", message).green().bold());

    if let Some(d) = details {
        println!("  {}", d.green());
    }
}

// Warning message with optional solution
pub fn print_warning(message: &str, solution: Option<&str>) {
    // Always print warnings, even in quiet mode
    println!("{}", format!("⚠ {}", message).yellow().bold());

    if let Some(s) = solution {
        println!("  {}", format!("Suggestion: {}", s).yellow());
    }
}

// Error message with optional code and solution
pub fn print_error(message: &str, code: Option<&str>, solution: Option<&str>) {
    // Always print errors, even in quiet mode
    let error_prefix = match code {
        Some(c) => format!("✗ [{}] ", c),
        None => "✗ ".to_string(),
    };

    println!("{}", format!("{}{}", error_prefix, message).red().bold());

    if let Some(s) = solution {
        println!("  {}", format!("Solution: {}", s).yellow());
    }
}

// Print a step with an action and a target
pub fn print_step(action: &str, target: &str) {
    if is_quiet() {
        return;
    }
    println!("→ {} {}", action.bold().blue(), target);
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

// Format a command with appropriate styling
pub fn format_command(cmd: &str) -> ColoredString {
    cmd.magenta()
}

// Format a section name with appropriate styling
pub fn format_section(section: &str) -> ColoredString {
    section.blue().bold()
}

// ==================== PROGRESS VISUALIZATION ====================

// Enhanced Progress Bar with smooth animation
pub struct ProgressBar {
    total: usize,
    current: usize,
    width: usize,
    message: String,
    last_render_time: Instant,
    render_throttle: Duration, // Minimum time between renders
}

impl ProgressBar {
    pub fn new(total: usize, message: &str) -> Self {
        let layout = LAYOUT.lock().unwrap();
        // Use 60% of terminal width for the progress bar, but not more than 60 chars
        let width = cmp::min(layout.get_width() * 6 / 10, 60);

        Self {
            total,
            current: 0,
            width,
            message: message.to_string(),
            last_render_time: Instant::now(),
            render_throttle: Duration::from_millis(100), // Limit updates to 10 per second
        }
    }

    pub fn update(&mut self, progress: usize) {
        self.current = cmp::min(progress, self.total);
        // Only render if enough time has passed since last render
        if self.last_render_time.elapsed() >= self.render_throttle {
            self.render();
        }
    }

    pub fn increment(&mut self, amount: usize) {
        self.current = cmp::min(self.current + amount, self.total);
        // Only render if enough time has passed since last render
        if self.last_render_time.elapsed() >= self.render_throttle {
            self.render();
        }
    }

    pub fn tick(&mut self) {
        self.increment(1);
    }

    fn render(&mut self) {
        if is_quiet() {
            return;
        }

        self.last_render_time = Instant::now();

        let percent = self.current as f32 / self.total as f32;
        let filled_width = (self.width as f32 * percent) as usize;
        let empty_width = self.width - filled_width;

        // Get progress bar characters
        let layout = LAYOUT.lock().unwrap();
        let (filled_char, empty_char) = layout.get_progress_chars();

        // Create the progress bar
        let bar = filled_char.repeat(filled_width) + &empty_char.repeat(empty_width);

        print!("\r{} [{}] {:.1}% ",
               self.message.blue().bold(),
               bar,
               percent * 100.0);
        io::stdout().flush().unwrap();
    }

    pub fn finish(&self, success: bool) {
        if is_quiet() {
            return;
        }

        // Clear the line
        print!("\r{}\r", " ".repeat(self.message.len() + self.width + 15));

        if success {
            println!("{} {}", "✓".green().bold(), self.message);
        } else {
            println!("{} {}", "✗".red().bold(), self.message);
        }
    }
}

// Progress Spinner for long-running operations with improved visuals
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
                // Get spinner characters from layout manager
                let layout = LAYOUT.lock().unwrap();
                let spinner_chars = layout.get_spinner_chars();
                let mut i = 0;

                loop {
                    {
                        let should_stop = *stop_clone.lock().unwrap();
                        if should_stop {
                            break;
                        }
                    }

                    print!("\r{} {} ",
                           spinner_chars[i % spinner_chars.len()].blue().bold(),
                           msg);
                    io::stdout().flush().unwrap();
                    thread::sleep(Duration::from_millis(80)); // Slightly faster for more visual appeal
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
            println!("{} {}", "✓".green().bold(), self.message);
        }
    }

    pub fn failure(self, error: &str) {
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        print_error(&format!("{}: {}", self.message, error), None, None);
    }

    pub fn update(self, new_message: &str) -> Self {
        *self.stop_signal.lock().unwrap() = true;
        if let Some(handle) = self.handle {
            let _ = handle.join();
        }

        ProgressSpinner::start(new_message)
    }
}

// Progress Tracking Structure with enhanced visuals
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

    pub fn with_steps(project_name: &str, steps: Vec<String>) -> Self {
        if !is_quiet() {
            print_project_box(project_name, "", "");
        }

        Self {
            total_steps: steps.len(),
            current_step: 0,
            project_name: project_name.to_string(),
            start_time: Instant::now(),
            steps,
        }
    }

    pub fn next_step(&mut self, step_name: &str) {
        self.current_step += 1;
        if !is_quiet() {
            println!("[{}/{}] {}",
                     self.current_step.to_string().cyan().bold(),
                     self.total_steps,
                     step_name);

            // Show progress bar to visualize overall progress
            let mut progress = ProgressBar::new(self.total_steps, "Overall progress");
            progress.update(self.current_step);
            progress.finish(true);
        }
    }

    pub fn complete(&self) {
        if !is_quiet() {
            let duration = self.start_time.elapsed();
            let formatted_time = format_duration(duration);

            println!("\n{} {} {}",
                     "✓".green().bold(),
                     format!("{} completed in", self.project_name).green().bold(),
                     formatted_time.yellow().bold());

            print_build_summary(&self.project_name, duration);
        }
    }

    pub fn fail(&self, error: &str) {
        if !is_quiet() {
            let duration = self.start_time.elapsed();
            let formatted_time = format_duration(duration);

            println!("\n{} {} after {}",
                     "✗".red().bold(),
                     format!("{} failed", self.project_name).red().bold(),
                     formatted_time.yellow().bold());

            print_error(error, None, None);
        }
    }
}

// Task Group for visually grouped tasks
pub struct TaskGroup {
    name: String,
    tasks: Vec<String>,
    current: Option<usize>,
    completed: Vec<bool>,
}

impl TaskGroup {
    pub fn new(name: &str, tasks: Vec<String>) -> Self {
        let task_count = tasks.len();
        Self {
            name: name.to_string(),
            tasks,
            current: None,
            completed: vec![false; task_count],
        }
    }

    pub fn display(&self) {
        if is_quiet() {
            return;
        }

        let layout = LAYOUT.lock().unwrap();
        let (_, _, _, _, v, _) = layout.get_box_chars();

        println!("{} {}", "┌".cyan(), self.name.white().bold());

        for (i, task) in self.tasks.iter().enumerate() {
            let prefix = if self.completed[i] {
                format!("{} ", "✓".green().bold())
            } else if Some(i) == self.current {
                format!("{} ", "▶".blue().bold())
            } else {
                format!("{} ", "○".dimmed())
            };

            println!("{} {}{}",
                     if i == self.tasks.len() - 1 { "└──" } else { "├──" }.cyan(),
                     prefix,
                     task);
        }

        println!();
    }

    pub fn start_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.current = Some(index);
        if !is_quiet() {
            print_step("Starting", &self.tasks[index]);
            self.display();
        }
    }

    pub fn complete_task(&mut self, index: usize) {
        if index >= self.tasks.len() {
            return;
        }

        self.completed[index] = true;
        if !is_quiet() {
            print_success(&format!("Completed: {}", self.tasks[index]), None);
            self.display();
        }
    }

    pub fn fail_task(&mut self, index: usize, error: &str) {
        if index >= self.tasks.len() {
            return;
        }

        if !is_quiet() {
            print_error(&format!("Failed: {}", self.tasks[index]), None, Some(error));
            self.display();
        }
    }

    pub fn all_completed(&self) -> bool {
        self.completed.iter().all(|&completed| completed)
    }
}

// Task List with better visuals and tracking
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
                "✓".green().bold()
            } else {
                "○".normal().dimmed()
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
            print_success(&format!("Completed: {}", self.tasks[index]), None);
        }
    }

    pub fn all_completed(&self) -> bool {
        self.completed_tasks.iter().all(|&completed| completed)
    }

    pub fn progress_percentage(&self) -> f32 {
        let completed_count = self.completed_tasks.iter().filter(|&&c| c).count();
        (completed_count as f32) / (self.tasks.len() as f32) * 100.0
    }
}

// ==================== SPECIALIZED UI COMPONENTS ====================

// Project summary box with beautiful styling
pub fn print_project_box(project_name: &str, version: &str, build_type: &str) {
    if is_quiet() {
        return;
    }

    let layout = LAYOUT.lock().unwrap();
    let (tl, tr, bl, br, v, h) = layout.get_box_chars();
    let width = cmp::min(layout.get_width(), 100);

    let title = if !version.is_empty() {
        format!(" {} v{} ", project_name.bold(), version)
    } else {
        format!(" {} ", project_name.bold())
    };

    let subtitle = if !build_type.is_empty() {
        format!(" {} build ", build_type)
    } else {
        String::new()
    };

    // Calculate spacing
    let inner_width = width - 2;
    let available_width = inner_width - title.len() - subtitle.len();

    // Top border
    println!("{}{}{}",
             tl.cyan(),
             h.repeat(inner_width).cyan(),
             tr.cyan()
    );

    // Title line
    if subtitle.is_empty() {
        println!("{} {}{} {}",
                 v.cyan(),
                 title.bright_white().on_blue(),
                 " ".repeat(inner_width - title.len() - 2),
                 v.cyan()
        );
    } else {
        println!("{} {}{}{} {}",
                 v.cyan(),
                 title.bright_white().on_blue(),
                 " ".repeat(available_width),
                 subtitle.black().on_white(),
                 v.cyan()
        );
    }

    // Bottom border
    println!("{}{}{}",
             bl.cyan(),
             h.repeat(inner_width).cyan(),
             br.cyan()
    );
}

// Error box for critical errors
pub fn print_error_box(title: &str, message: &str) {
    if is_quiet() {
        return;
    }

    let layout = LAYOUT.lock().unwrap();
    let (tl, tr, bl, br, v, h) = layout.get_box_chars();
    let width = cmp::min(layout.get_width(), 100);

    // Calculate inner width
    let inner_width = width - 2;

    // Print top border with title
    println!("{}{}{}",
             tl.red(),
             h.repeat(inner_width).red(),
             tr.red()
    );

    // Print title
    println!("{}{}{}",
             v.red(),
             format!(" {} ", title).white().on_red().to_string() +
                 &" ".repeat(inner_width - title.len() - 3),
             v.red()
    );

    // Print message (with word wrapping)
    let words = message.split_whitespace();
    let mut current_line = String::new();
    for word in words {
        if current_line.len() + word.len() + 1 > inner_width - 6 {
            println!("{}  {}{}  {}",
                     v.red(),
                     current_line,
                     " ".repeat(inner_width - current_line.len() - 6),
                     v.red()
            );
            current_line = word.to_string();
        } else {
            if !current_line.is_empty() {
                current_line.push(' ');
            }
            current_line.push_str(word);
        }
    }

    // Print the last line
    if !current_line.is_empty() {
        println!("{}  {}{}  {}",
                 v.red(),
                 current_line,
                 " ".repeat(inner_width - current_line.len() - 6),
                 v.red()
        );
    }

    // Print bottom border
    println!("{}{}{}",
             bl.red(),
             h.repeat(inner_width).red(),
             br.red()
    );
}

// Log level indicators for different message types
pub fn print_log(level: &str, message: &str) {
    if is_quiet() && level != "ERROR" && level != "WARNING" {
        return;
    }

    let (prefix, color) = match level {
        "ERROR" => ("[ERROR]", "red"),
        "WARNING" => ("[WARN ]", "yellow"),
        "INFO" => ("[INFO ]", "blue"),
        "DEBUG" => ("[DEBUG]", "magenta"),
        "TRACE" => ("[TRACE]", "cyan"),
        _ => ("[LOG  ]", "white"),
    };

    let colored_prefix = match color {
        "red" => prefix.red().bold(),
        "yellow" => prefix.yellow().bold(),
        "blue" => prefix.blue().bold(),
        "magenta" => prefix.magenta().bold(),
        "cyan" => prefix.cyan().bold(),
        _ => prefix.white().bold(),
    };

    println!("{} {}", colored_prefix, message);
}

// Build summary dashboard
pub fn print_build_summary(project: &str, duration: Duration) {
    if is_quiet() {
        return;
    }

    let layout = LAYOUT.lock().unwrap();
    let (tl, tr, bl, br, v, h) = layout.get_box_chars();
    let width = cmp::min(layout.get_width(), 100);

    let inner_width = width - 2;
    let title = " BUILD SUMMARY ";
    let title_padding = (inner_width - title.len()) / 2;

    println!("{}{}{}{}{}",
             tl.cyan(),
             h.repeat(title_padding).cyan(),
             title.white().on_blue().bold(),
             h.repeat(inner_width - title.len() - title_padding).cyan(),
             tr.cyan()
    );

    println!("{} {:<42} {}",
             v.cyan(),
             format!("Project: {}", project.cyan().bold()),
             v.cyan()
    );

    println!("{} {:<42} {}",
             v.cyan(),
             format!("Time: {}", format_duration(duration).yellow()),
             v.cyan()
    );

    println!("{}{}{}",
             bl.cyan(),
             h.repeat(inner_width).cyan(),
             br.cyan()
    );
}

// Selection list for user input
pub fn show_selection_list(title: &str, options: &[String]) -> usize {
    println!("{}", title.bold());

    for (i, option) in options.iter().enumerate() {
        println!("  {}. {}", i + 1, option);
    }

    print!("Enter selection (1-{}): ", options.len());
    io::stdout().flush().unwrap();

    let mut input = String::new();
    io::stdin().read_line(&mut input).unwrap();

    match input.trim().parse::<usize>() {
        Ok(n) if n > 0 && n <= options.len() => n - 1,
        _ => {
            println!("Invalid selection, using default (1)");
            0
        }
    }
}

// Collapsible error group
pub fn print_error_group(title: &str, errors: &[String], limit: usize) {
    if errors.is_empty() {
        return;
    }

    println!("{} {} ({} errors)", "▼".red().bold(), title.red().bold(), errors.len());

    let display_count = cmp::min(errors.len(), limit);
    for (i, error) in errors.iter().take(display_count).enumerate() {
        println!("  {}. {}", i + 1, error);
    }

    if errors.len() > limit {
        println!("  ... and {} more errors", errors.len() - limit);
        println!("  Use --verbose to see all errors");
    }
}

// ==================== UTILITY FUNCTIONS ====================

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

// Show a notification (requires notify-rust crate)
pub fn show_notification(title: &str, message: &str, is_error: bool) {
    // This would normally use the notify-rust crate
    /*
    let result = Notification::new()
        .summary(title)
        .body(message)
        .icon(if is_error { "dialog-error" } else { "dialog-information" })
        .timeout(5000) // ms
        .show();

    if let Err(e) = result {
        print_detailed(&format!("Failed to show notification: {}", e));
    }
    */

    // For now, just print to console
    if is_error {
        print_error(message, None, None);
    } else {
        print_success(title, Some(message));
    }
}

// Get estimated memory usage (simplified)
pub fn get_memory_usage() -> u64 {
    // This would normally use a proper measurement API
    // For now, return a simulated value
    128 // MB
}

// Print a loading animation while a task runs
pub fn animate_loading(message: &str, task: impl FnOnce() -> Result<(), Box<dyn std::error::Error>>) -> Result<(), Box<dyn std::error::Error>> {
    let spinner = ProgressSpinner::start(message);

    match task() {
        Ok(_) => {
            spinner.success();
            Ok(())
        },
        Err(e) => {
            spinner.failure(&e.to_string());
            Err(e)
        }
    }
}

// Show build metrics in a compact dashboard
pub fn print_build_dashboard(project: &str, config: &str, start_time: Instant) {
    if is_quiet() {
        return;
    }

    let layout = LAYOUT.lock().unwrap();
    let (tl, tr, bl, br, v, h) = layout.get_box_chars();
    let elapsed = start_time.elapsed();
    let mem_usage = get_memory_usage();

    let width = cmp::min(layout.get_width(), 100);
    let inner_width = width - 2;

    println!("{}{}{}", tl.cyan(), h.repeat(inner_width).cyan(), tr.cyan());
    println!("{} {:<42} {}", v.cyan(), format!("Project: {}", project.cyan().bold()), v.cyan());
    println!("{} {:<42} {}", v.cyan(), format!("Config:  {}", config.green()), v.cyan());
    println!("{} {:<42} {}", v.cyan(), format!("Time:    {}", format_duration(elapsed).yellow()), v.cyan());
    println!("{} {:<42} {}", v.cyan(), format!("Memory:  {} MB", mem_usage).yellow(), v.cyan());
    println!("{}{}{}", bl.cyan(), h.repeat(inner_width).cyan(), br.cyan());
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