use std::collections::HashSet;
use std::{fmt, fs};
use std::path::Path;
use std::process::Command;
use colored::{Color, Colorize};
use lazy_static::lazy_static;
use regex::Regex;
use crate::config::ErrorDiagnostic;
use crate::output_utils::{is_verbose, print_error, print_status, print_substep, print_warning};

#[derive(Debug)]
pub struct CforgeError {
    pub message: String,
    pub file_path: Option<String>,
    pub line_number: Option<usize>,
    pub context: Option<String>,
}

impl CforgeError {
    pub fn new(message: &str) -> Self {
        CforgeError {
            message: message.to_string(),
            file_path: None,
            line_number: None,
            context: None,
        }
    }

    pub fn with_file(mut self, file_path: &str) -> Self {
        self.file_path = Some(file_path.to_string());
        self
    }

    pub fn with_line(mut self, line_number: usize) -> Self {
        self.line_number = Some(line_number);
        self
    }

    pub fn with_context(mut self, context: &str) -> Self {
        self.context = Some(context.to_string());
        self
    }
}

impl fmt::Display for CforgeError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "cforge Error: {}", self.message)?;

        if let Some(file) = &self.file_path {
            write!(f, " in file '{}'", file)?;
        }

        if let Some(line) = self.line_number {
            write!(f, " at line {}", line)?;
        }

        if let Some(context) = &self.context {
            write!(f, "\nContext: {}", context)?;
        }

        Ok(())
    }
}

impl std::error::Error for CforgeError {}

pub fn parse_compiler_errors(stdout: &str, stderr: &str) -> Vec<ErrorDiagnostic> {
    // Compile regex patterns
    let patterns = [
        // Clang/GCC pattern: file:line:col: error: message
        Regex::new(r"(?m)(.*?):(\d+):(\d+):\s+(error|warning|note):\s+(.*)").unwrap(),

        // MSVC pattern: file(line,col): error: message
        Regex::new(r"(?m)(.*?)\((\d+),(\d+)\):\s+(error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap(),

        // MSVC pattern without column: file(line): error: message
        Regex::new(r"(?m)(.*?)\((\d+)\):\s+(error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap(),
    ];

    // Track unique errors
    let mut seen_errors = HashSet::new();
    let mut diagnostics = Vec::new();

    // Check stderr first, as it usually contains the most critical messages
    for pattern in &patterns {
        for cap in pattern.captures_iter(stderr) {
            let file = if cap.len() >= 6 {
                cap[1].to_string()
            } else if cap.len() >= 5 {
                cap[1].to_string()
            } else {
                continue; // Skip if we can't get enough information
            };

            let line = if cap.len() >= 6 {
                cap[2].parse::<usize>().unwrap_or(0)
            } else if cap.len() >= 5 {
                cap[2].parse::<usize>().unwrap_or(0)
            } else {
                0
            };

            let column = if cap.len() >= 6 {
                cap[3].parse::<usize>().unwrap_or(0)
            } else {
                1 // Default column
            };

            let level = if cap.len() >= 6 {
                cap[4].to_string()
            } else if cap.len() >= 5 {
                cap[3].to_string()
            } else {
                "error".to_string()
            };

            let message = if cap.len() >= 6 {
                cap[5].to_string()
            } else if cap.len() >= 5 {
                cap[4].to_string()
            } else {
                continue; // Skip if we can't extract the message
            };

            // Generate a unique key for this error
            let error_key = format!("{}:{}:{}:{}", file, line, column, message);

            // Skip if we've already seen this exact error
            if seen_errors.contains(&error_key) {
                continue;
            }

            seen_errors.insert(error_key);

            // Generate a Rust-style error code
            let error_code = match level.as_str() {
                "error" => format!("E{:04}", hash_error_for_code(&message) % 10000),
                "warning" => format!("W{:04}", hash_error_for_code(&message) % 10000),
                _ => format!("N{:04}", hash_error_for_code(&message) % 10000),
            };

            // Extract source line if possible
            let source_line = extract_source_line(stdout, stderr, &file, line);

            // Get helpful message for the error
            let suggestion = get_suggestion_for_error(&message);

            // Create diagnostic
            let diagnostic = ErrorDiagnostic {
                file,
                line,
                column,
                level,
                message,
                error_code,
                source_line,
                suggestion: Some(suggestion),
                context: Vec::new(), // We'll fill this later if needed
            };

            diagnostics.push(diagnostic);
        }
    }

    // Only search stdout if we didn't find enough in stderr
    if diagnostics.len() < 10 {
        for pattern in &patterns {
            for cap in pattern.captures_iter(stdout) {
                // Same logic as above, just for stdout
                let file = if cap.len() >= 6 {
                    cap[1].to_string()
                } else if cap.len() >= 5 {
                    cap[1].to_string()
                } else {
                    continue;
                };

                let line = if cap.len() >= 6 {
                    cap[2].parse::<usize>().unwrap_or(0)
                } else if cap.len() >= 5 {
                    cap[2].parse::<usize>().unwrap_or(0)
                } else {
                    0
                };

                let column = if cap.len() >= 6 {
                    cap[3].parse::<usize>().unwrap_or(0)
                } else {
                    1 // Default column
                };

                let level = if cap.len() >= 6 {
                    cap[4].to_string()
                } else if cap.len() >= 5 {
                    cap[3].to_string()
                } else {
                    "error".to_string()
                };

                let message = if cap.len() >= 6 {
                    cap[5].to_string()
                } else if cap.len() >= 5 {
                    cap[4].to_string()
                } else {
                    continue;
                };

                // Generate a unique key for this error
                let error_key = format!("{}:{}:{}:{}", file, line, column, message);

                // Skip if we've already seen this exact error
                if seen_errors.contains(&error_key) {
                    continue;
                }

                seen_errors.insert(error_key);

                // Generate a Rust-style error code
                let error_code = match level.as_str() {
                    "error" => format!("E{:04}", hash_error_for_code(&message) % 10000),
                    "warning" => format!("W{:04}", hash_error_for_code(&message) % 10000),
                    _ => format!("N{:04}", hash_error_for_code(&message) % 10000),
                };

                // Extract source line if possible
                let source_line = extract_source_line(stdout, stderr, &file, line);

                // Get helpful message for the error
                let suggestion = get_suggestion_for_error(&message);

                // Create diagnostic
                let diagnostic = ErrorDiagnostic {
                    file,
                    line,
                    column,
                    level,
                    message,
                    error_code,
                    source_line,
                    suggestion: Some(suggestion),
                    context: Vec::new(),
                };

                diagnostics.push(diagnostic);
            }
        }
    }

    // Sort by importance and location
    diagnostics.sort_by(|a, b| {
        // First by severity (errors first)
        let level_order = |level: &str| match level {
            "error" => 0,
            "warning" => 1,
            _ => 2,
        };

        let a_order = level_order(&a.level);
        let b_order = level_order(&b.level);

        if a_order != b_order {
            return a_order.cmp(&b_order);
        }

        // Then by file
        let file_cmp = a.file.cmp(&b.file);
        if file_cmp != std::cmp::Ordering::Equal {
            return file_cmp;
        }

        // Then by line
        let line_cmp = a.line.cmp(&b.line);
        if line_cmp != std::cmp::Ordering::Equal {
            return line_cmp;
        }

        // Finally by column
        a.column.cmp(&b.column)
    });

    // Limit to a reasonable number
    if diagnostics.len() > 20 {
        diagnostics.truncate(20);
    }

    diagnostics
}

// Display errors in a clean, Rust-like format
pub fn display_errors_rust_style(diagnostics: &[ErrorDiagnostic]) -> Vec<String> {
    let mut output = Vec::new();
    let mut categories = HashSet::new();

    if diagnostics.is_empty() {
        output.push("No specific errors found.".to_string());
        return output;
    }

    output.push("\nBuild error details:".red().bold().to_string());

    for diagnostic in diagnostics {
        // Collect categories for later suggestions
        let error_categories = categorize_error(&diagnostic.message);
        for category in error_categories {
            categories.insert(category);
        }

        // Format the filename for display - just the filename and parent directory
        let path = Path::new(&diagnostic.file);
        let file_display = if let Some(file_name) = path.file_name() {
            if let Some(parent) = path.parent() {
                if let Some(parent_name) = parent.file_name() {
                    format!("{}/{}", parent_name.to_string_lossy(), file_name.to_string_lossy())
                } else {
                    file_name.to_string_lossy().to_string()
                }
            } else {
                file_name.to_string_lossy().to_string()
            }
        } else {
            diagnostic.file.clone()
        };

        // Choose color based on level
        let color = match diagnostic.level.as_str() {
            "error" => Color::Red,
            "warning" => Color::Yellow,
            _ => Color::Blue,
        };

        // Print error header
        output.push(format!("{}[{}]: {}",
                            diagnostic.level.to_uppercase().color(color).bold(),
                            diagnostic.error_code.color(color),
                            diagnostic.message
        ));

        // Print location
        output.push(format!(" {} {}:{}:{}",
                            "-->".blue().bold(),
                            file_display,
                            diagnostic.line,
                            diagnostic.column
        ));

        // Print source code if available
        if let Some(source_line) = &diagnostic.source_line {
            output.push(format!("  {}| {}",
                                diagnostic.line.to_string().blue().bold(),
                                source_line.trim()
            ));

            // Create caret line pointing to the error
            let mut caret_line = String::new();
            for _ in 0..diagnostic.column.saturating_sub(1) {
                caret_line.push(' ');
            }
            caret_line.push('^');

            // Add wavy underlines for longer errors
            let error_len = diagnostic.message.len().min(15).saturating_sub(1);
            for _ in 0..error_len {
                caret_line.push('~');
            }

            // Print caret line in the appropriate color
            output.push(format!("  {}| {}",
                                " ".blue().bold(),
                                caret_line.color(color).bold()
            ));
        }

        // Print suggestion if available
        if let Some(suggestion) = &diagnostic.suggestion {
            if !suggestion.is_empty() {
                output.push(format!("  {} {}",
                                    "help:".green().bold(),
                                    suggestion
                ));
            }
        }

        output.push(String::new()); // Empty line between errors
    }

    // Add general help suggestions based on error categories
    print_general_suggestions(&mut output, &categories);

    output
}

// Generate a consistent hash code for an error message
pub fn hash_error_for_code(error_text: &str) -> u32 {
    use std::hash::{Hash, Hasher};
    use std::collections::hash_map::DefaultHasher;

    let mut hasher = DefaultHasher::new();
    error_text.hash(&mut hasher);
    (hasher.finish() & 0xFFFFFFFF) as u32
}

// Try to extract the source line from compiler output or file
pub fn extract_source_line(stdout: &str, stderr: &str, file: &str, line_num: usize) -> Option<String> {
    // First try to find patterns like "line_num |   code..." in the output
    let pattern = format!(r"(?m)^.*\s*{}\s*\|\s*(.*)$", line_num);
    let line_pattern = Regex::new(&pattern).ok()?;

    // Check stderr first
    if let Some(cap) = line_pattern.captures(stderr) {
        if cap.len() >= 2 {
            return Some(cap[1].to_string());
        }
    }

    // Then check stdout
    if let Some(cap) = line_pattern.captures(stdout) {
        if cap.len() >= 2 {
            return Some(cap[1].to_string());
        }
    }

    // If we couldn't find the line in the build output, try to read from the file directly
    if Path::new(file).exists() {
        if let Ok(content) = fs::read_to_string(file) {
            let lines: Vec<&str> = content.lines().collect();
            if line_num > 0 && line_num <= lines.len() {
                return Some(lines[line_num - 1].to_string());
            }
        }
    }

    None
}

// Provide a helpful suggestion for common error types
pub fn get_suggestion_for_error(error_msg: &str) -> String {
    let error_text = error_msg.to_lowercase();

    // Template parameter pack errors
    if error_text.contains("template parameter pack must be the last template parameter") {
        return "variadic template parameters (template<typename... Args>) must always be the last parameter in the template parameter list".to_string();
    }

    // Constexpr errors
    if error_text.contains("constexpr function's return type") && error_text.contains("not a literal type") {
        return "classes used with constexpr must have at least one constexpr constructor and a trivial destructor".to_string();
    }

    // Undeclared identifiers
    if error_text.contains("use of undeclared identifier") {
        let var_name = extract_identifier(&error_text, "identifier");
        if !var_name.is_empty() {
            return format!("'{}' is used before being declared - check for typos or missing includes", var_name);
        }
        return "identifier used before being declared - check for typos or missing includes".to_string();
    }

    // Member errors
    if error_text.contains("member initializer") && error_text.contains("does not name a non-static data member") {
        let member_name = extract_identifier(&error_text, "initializer");
        if !member_name.is_empty() {
            return format!("'{}' is not a declared member of this class - add it to the class definition first", member_name);
        }
        return "trying to initialize a member that doesn't exist in the class - declare it first".to_string();
    }

    if error_text.contains("use of undeclared identifier") && error_text.contains("allin") {
        return "define the 'AllIn' concept before using it, e.g.: template<typename T, typename... Types> concept AllIn = (std::is_same_v<T, Types> || ...)".to_string();
    }

    // If no specific suggestion, return empty string
    String::new()
}

// Helper to extract a variable or identifier name from an error message
fn extract_identifier(text: &str, context: &str) -> String {
    // Try to find quoted content
    if let Some(start) = text.find('\'') {
        if let Some(end) = text[start+1..].find('\'') {
            return text[start+1..start+1+end].to_string();
        }
    }

    if let Some(start) = text.find('"') {
        if let Some(end) = text[start+1..].find('"') {
            return text[start+1..start+1+end].to_string();
        }
    }

    // Try to find the identifier after a specific context word
    if let Some(pos) = text.find(context) {
        let after_context = &text[pos + context.len()..];
        let words: Vec<&str> = after_context.split_whitespace().collect();
        if !words.is_empty() {
            // Clean up any punctuation
            let word = words[0].trim_matches(|c: char| !c.is_alphanumeric() && c != '_');
            if !word.is_empty() {
                return word.to_string();
            }
        }
    }

    String::new()
}

// Categorize errors for generating general suggestions
pub fn categorize_error(error_msg: &str) -> Vec<String> {
    let error_text = error_msg.to_lowercase();
    let mut categories = Vec::new();

    // Template errors
    if error_text.contains("template") {
        categories.push("template_general".to_string());

        if error_text.contains("parameter pack") {
            categories.push("template_parameter_pack".to_string());
        }
    }

    // Constexpr errors
    if error_text.contains("constexpr") {
        categories.push("constexpr_general".to_string());

        if error_text.contains("not a literal type") {
            categories.push("constexpr_not_literal".to_string());
        }
    }

    // Undeclared/undefined errors
    if error_text.contains("undeclared") || error_text.contains("undefined") {
        categories.push("undeclared_general".to_string());

        if error_text.contains("identifier") {
            categories.push("undeclared_identifier".to_string());
        }
    }

    // Class/member errors
    if error_text.contains("member") || error_text.contains("class") || error_text.contains("struct") {
        categories.push("class_general".to_string());

        if error_text.contains("does not name") {
            categories.push("no_member".to_string());
        }
    }

    // Ensure we have at least one category
    if categories.is_empty() {
        categories.push("general".to_string());
    }

    categories
}

// Print general suggestions based on error categories
pub fn print_general_suggestions(output: &mut Vec<String>, categories: &HashSet<String>) {
    output.push("Help for common errors:".yellow().bold().to_string());

    let mut added_suggestions = false;

    // Template errors
    if categories.iter().any(|c| c.starts_with("template_")) {
        output.push("● For template errors:".bold().to_string());

        if categories.contains("template_parameter_pack") {
            output.push("  - Variadic templates (template<typename... Args>) must be the last parameter".to_string());
            output.push("  - Change `template<typename... T, typename U>` to `template<typename U, typename... T>`".to_string());
        }

        output.push("  - Remember that template code must be in header files or explicitly instantiated".to_string());
        output.push(String::new());
        added_suggestions = true;
    }

    // Constexpr errors
    if categories.iter().any(|c| c.starts_with("constexpr_")) {
        output.push("● For constexpr errors:".bold().to_string());

        if categories.contains("constexpr_not_literal") {
            output.push("  - A class used with constexpr must be a literal type, which requires:".to_string());
            output.push("    * At least one constexpr constructor".to_string());
            output.push("    * A trivial or constexpr destructor".to_string());
            output.push("    * All non-static data members must be literal types".to_string());
            output.push("  - Example fix: Add `constexpr YourClass() = default;` to your class".to_string());
        }

        output.push(String::new());
        added_suggestions = true;
    }

    // Undeclared identifier errors
    if categories.iter().any(|c| c.starts_with("undeclared_")) {
        output.push("● For undeclared identifier errors:".bold().to_string());
        output.push("  - Ensure the variable or function is declared before use".to_string());
        output.push("  - Check for typos in the identifier name".to_string());
        output.push("  - Verify that required headers are included".to_string());
        output.push(String::new());
        added_suggestions = true;
    }

    // Class member errors
    if categories.contains("class_general") || categories.contains("no_member") {
        output.push("● For class member errors:".bold().to_string());
        output.push("  - The member variable or function doesn't exist in this class".to_string());
        output.push("  - Ensure the member is declared in the class definition".to_string());
        output.push("  - Member variables must be declared in the class body, not in constructors".to_string());
        output.push(String::new());
        added_suggestions = true;
    }

    // General help if we didn't provide specific suggestions
    if !added_suggestions {
        output.push("● General troubleshooting:".bold().to_string());
        output.push("  - Check for missing semicolons or unbalanced brackets".to_string());
        output.push("  - Ensure all variables are declared before use".to_string());
        output.push("  - Verify that required headers are included".to_string());
        output.push("  - Look for mismatched types in function calls".to_string());
        output.push("  - Try `cforge clean` followed by `cforge build`".to_string());
        output.push(String::new());
    }

    // Documentation links
    output.push("For more detailed C++ language help, see: https://en.cppreference.com/".to_string());
    output.push("For compiler-specific error assistance:".to_string());
    output.push("  - GCC/Clang: https://gcc.gnu.org/onlinedocs/".to_string());
    output.push("  - MSVC: https://docs.microsoft.com/en-us/cpp/error-messages/".to_string());
    output.push("For cforge documentation, run: `cforge --help`".to_string());
}

// Main function to format and display compiler errors
pub fn format_compiler_errors(stdout: &str, stderr: &str) -> Vec<String> {
    let diagnostics = parse_compiler_errors(stdout, stderr);
    display_errors_rust_style(&diagnostics)
}

// Function to replace the existing display_syntax_errors function
pub fn display_syntax_errors(stdout: &str, stderr: &str) -> usize {
    let output = format_compiler_errors(stdout, stderr);

    for line in &output {
        println!("{}", line);
    }

    // Return number of errors processed
    if output.len() > 3 {  // Account for header and possibly some help text
        output.len() - 3
    } else {
        if output.len() > 0 { 1 } else { 0 }
    }
}

pub fn expand_tilde(path: &str) -> String {
    if path.starts_with("~/") {
        if let Some(home) = dirs::home_dir() {
            return home.join(path.strip_prefix("~/").unwrap()).to_string_lossy().to_string();
        }
    }
    path.to_string()
}

pub fn glob_to_regex(pattern: &str) -> String {
    let mut regex_pattern = "^".to_string();

    // Split the pattern by path separators
    let parts: Vec<&str> = pattern.split('/').collect();

    for (i, part) in parts.iter().enumerate() {
        if i > 0 {
            regex_pattern.push_str("/");
        }

        if part == &"**" {
            regex_pattern.push_str(".*");
        } else {
            // Escape regex special characters and convert glob patterns
            let mut part_pattern = part.replace(".", "\\.")
                .replace("*", ".*")
                .replace("?", ".");

            // Handle character classes [abc]
            // This is simplified - a real implementation would handle ranges and negation
            if part_pattern.contains('[') && part_pattern.contains(']') {
                part_pattern = part_pattern; // Keep as is, regex handles character classes
            }

            regex_pattern.push_str(&part_pattern);
        }
    }

    regex_pattern.push_str("$");
    regex_pattern
}

// Temporarily define format_cpp_errors_rust_style to forward to our new function
pub fn format_cpp_errors_rust_style(output: &str) -> Vec<String> {
    format_compiler_errors(output, "")
}

pub fn parse_toml_error(err: toml::de::Error, file_path: &str, file_content: &str) -> CforgeError {
    let message = err.to_string();

    // Try to extract line number from the error message
    let line_number = if let Some(span) = err.span() {
        // If we have a span, we can calculate the line number


        let content_up_to_error = &file_content[..span.start];
        Some(content_up_to_error.lines().count())
    } else {
        // Try to parse line number from error message (varies by TOML parser)
        message.lines()
            .find_map(|line| {
                if line.contains("line") {
                    line.split_whitespace()
                        .find_map(|word| {
                            if word.chars().all(|c| c.is_digit(10)) {
                                word.parse::<usize>().ok()
                            } else {
                                None
                            }
                        })
                } else {
                    None
                }
            })
    };

    // Extract context - the line with the error and a few lines before/after
    let context = if let Some(line_num) = line_number {
        let lines: Vec<&str> = file_content.lines().collect();
        let start = line_num.saturating_sub(2);
        let end = std::cmp::min(line_num + 2, lines.len());

        let mut result = String::new();
        for i in start..end {
            let line_prefix = if i == line_num - 1 { " -> " } else { "    " };
            if i < lines.len() {
                result.push_str(&format!("{}{}: {}\n", line_prefix, i + 1, lines[i]));
            }
        }
        Some(result)
    } else {
        None
    };

    // Find specific value that caused the error
    let problem_value = if let Some(span) = err.span() {
        if span.start < file_content.len() && span.end <= file_content.len() {
            Some(file_content[span.start..span.end].to_string())
        } else {
            None
        }
    } else {
        None
    };

    // Create detailed error message
    let detailed_message = if let Some(value) = problem_value {
        format!("{}\nProblem with value: '{}'", message, value)
    } else {
        message
    };

    let mut error = CforgeError::new(&detailed_message).with_file(file_path);

    if let Some(line) = line_number {
        error = error.with_line(line);
    }

    if let Some(ctx) = context {
        error = error.with_context(&ctx);
    }

    error
}

pub fn display_raw_errors(stdout: &str, stderr: &str) {
    // Use our enhanced error formatter instead of the old regex approach
    let formatted_errors = format_compiler_errors(stdout, stderr);

    // Print the formatted errors
    for error_line in &formatted_errors {
        println!("{}", error_line);
    }

    // If we found no errors at all, show a generic message
    if formatted_errors.is_empty() || (formatted_errors.len() == 1 && formatted_errors[0].contains("No specific errors found")) {
        eprintln!("{}", "Build command failed but no specific errors were found".red().bold());

        // Try to show the last few lines of stderr or stdout as context
        if !stderr.is_empty() {
            let last_lines: Vec<&str> = stderr.lines().rev().take(5).collect();
            println!("{}", "Last few lines of stderr:".blue());
            for line in last_lines.iter().rev() {
                println!("  • {}", line);
            }
        } else if !stdout.is_empty() {
            let last_lines: Vec<&str> = stdout.lines().rev().take(5).collect();
            println!("{}", "Last few lines of stdout:".blue());
            for line in last_lines.iter().rev() {
                println!("  • {}", line);
            }
        }
    }
}