use std::collections::HashSet;
use std::{fmt, fs};
use std::path::Path;
use std::process::Command;
use colored::{Color, Colorize};
use lazy_static::lazy_static;
use regex::Regex;
use crate::config::CompilerDiagnostic;
use crate::output_utils::{print_error, print_status, print_substep, print_warning};

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

pub fn display_syntax_errors(stdout: &str, stderr: &str) -> usize {
    // Use regex to extract errors from stdout and stderr
    let error_patterns = [
        // Clang/GCC style errors with file:line:col: error: message
        (regex::Regex::new(r"(?m)(.*?):(\d+):(\d+):\s+(error|warning|note):\s+(.*)").unwrap(), true),

        // MSVC style errors
        (regex::Regex::new(r"(?m)(.*?)\((\d+),(\d+)\):\s+(error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap(), true),

        // MSVC style errors without column
        (regex::Regex::new(r"(?m)(.*?)\((\d+)\):\s+(error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap(), true),

        // Other common patterns
        (regex::Regex::new(r"(?m)^.*error(?:\[E\d+\])?:\s+(.*)$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*Error:\s+(.*)$").unwrap(), true),
        (regex::Regex::new(r"(?m)^ninja:\s+error:\s+(.*)$").unwrap(), true),
        (regex::Regex::new(r"(?m)^CMake\s+Error:\s+(.*)$").unwrap(), true),
    ];

    // Used to track unique errors to avoid duplicates
    let mut seen_errors = HashSet::new();
    let mut displayed_error_count = 0;
    let max_errors_to_display = 10;

    // Track error categories for later generating generic suggestions
    let mut error_categories = HashSet::new();

    // Process stderr first
    let mut errors = Vec::new();

    for (pattern, _) in &error_patterns {
        for cap in pattern.captures_iter(stderr) {
            if cap.len() >= 6 {
                // clang/GCC style error with file, line, column
                let file = &cap[1];
                let line = cap[2].parse::<usize>().unwrap_or(0);
                let column = cap[3].parse::<usize>().unwrap_or(0);
                let error_type = &cap[4];
                let message = &cap[5];

                let error_key = format!("{}:{}:{}:{}", file, line, column, message);
                if !seen_errors.contains(&error_key) {
                    seen_errors.insert(error_key);
                    errors.push((file.to_string(), line, column, error_type.to_string(), message.to_string()));

                    // Categorize the error
                    let categories = categorize_error(message);
                    for category in categories {
                        error_categories.insert(category);
                    }
                }
            } else if cap.len() >= 5 {
                // MSVC style error without column
                let file = &cap[1];
                let line = cap[2].parse::<usize>().unwrap_or(0);
                let error_type = &cap[3];
                let message = &cap[4];

                let error_key = format!("{}:{}:{}", file, line, message);
                if !seen_errors.contains(&error_key) {
                    seen_errors.insert(error_key);
                    errors.push((file.to_string(), line, 1, error_type.to_string(), message.to_string()));

                    // Categorize the error
                    let categories = categorize_error(message);
                    for category in categories {
                        error_categories.insert(category);
                    }
                }
            } else if cap.len() >= 2 {
                // Simple error without file info
                let message = &cap[0];

                if !seen_errors.contains(message) {
                    seen_errors.insert(message.to_string());
                    errors.push(("unknown".to_string(), 0, 0, "error".to_string(), message.to_string()));

                    // Categorize the error
                    let categories = categorize_error(message);
                    for category in categories {
                        error_categories.insert(category);
                    }
                }
            }
        }
    }

    // Process stdout only if we didn't find enough errors in stderr
    if errors.len() < max_errors_to_display {
        for (pattern, _) in &error_patterns {
            for cap in pattern.captures_iter(stdout) {
                if errors.len() >= max_errors_to_display {
                    break;
                }

                if cap.len() >= 6 {
                    // clang/GCC style error
                    let file = &cap[1];
                    let line = cap[2].parse::<usize>().unwrap_or(0);
                    let column = cap[3].parse::<usize>().unwrap_or(0);
                    let error_type = &cap[4];
                    let message = &cap[5];

                    let error_key = format!("{}:{}:{}:{}", file, line, column, message);
                    if !seen_errors.contains(&error_key) {
                        seen_errors.insert(error_key);
                        errors.push((file.to_string(), line, column, error_type.to_string(), message.to_string()));

                        // Categorize the error
                        let categories = categorize_error(message);
                        for category in categories {
                            error_categories.insert(category);
                        }
                    }
                } else if cap.len() >= 5 {
                    // MSVC style without column
                    let file = &cap[1];
                    let line = cap[2].parse::<usize>().unwrap_or(0);
                    let error_type = &cap[3];
                    let message = &cap[4];

                    let error_key = format!("{}:{}:{}", file, line, message);
                    if !seen_errors.contains(&error_key) {
                        seen_errors.insert(error_key);
                        errors.push((file.to_string(), line, 1, error_type.to_string(), message.to_string()));

                        // Categorize the error
                        let categories = categorize_error(message);
                        for category in categories {
                            error_categories.insert(category);
                        }
                    }
                }
            }
        }
    }

    // Sort errors by file and line number
    errors.sort_by(|a, b| {
        let file_cmp = a.0.cmp(&b.0);
        if file_cmp != std::cmp::Ordering::Equal {
            return file_cmp;
        }

        let line_cmp = a.1.cmp(&b.1);
        if line_cmp != std::cmp::Ordering::Equal {
            return line_cmp;
        }

        a.2.cmp(&b.2)
    });

    // Display errors in Rust style
    for (file, line, column, error_type, message) in &errors {
        // Create error code (Rust-like)
        let error_code = match error_type.as_str() {
            "error" => format!("E{:04}", hash_error_for_code(message) % 10000),
            "warning" => format!("W{:04}", hash_error_for_code(message) % 10000),
            _ => format!("N{:04}", hash_error_for_code(message) % 10000),
        };

        // Format file path for display - just show filename and parent directory
        let path = std::path::Path::new(file);
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
            file.clone()
        };

        // Print header line
        if error_type == "error" {
            println!("{}[{}]: {}", "error".red().bold(), error_code.red(), message);
        } else if error_type == "warning" {
            println!("{}[{}]: {}", "warning".yellow().bold(), error_code.yellow(), message);
        } else {
            println!("{}[{}]: {}", "note".blue().bold(), error_code.blue(), message);
        }

        // Print location
        println!(" {} {}:{}:{}", "-->".blue().bold(), file, line, column);

        // Get source line if available
        if let Some(source_line) = extract_source_line(stdout, stderr, file, *line) {
            println!("  {}| {}", line.to_string().blue().bold(), source_line.trim());

            // Create caret line
            let mut caret_line = String::new();
            for _ in 0..*column {
                caret_line.push(' ');
            }
            caret_line.push('^');

            // Add wavy underlines for longer errors
            let error_len = message.len().min(15);
            for _ in 0..error_len {
                caret_line.push('~');
            }

            // Print with the correct color
            if error_type == "error" {
                println!("  {}| {}", " ".blue().bold(), caret_line.red().bold());
            } else if error_type == "warning" {
                println!("  {}| {}", " ".blue().bold(), caret_line.yellow().bold());
            } else {
                println!("  {}| {}", " ".blue().bold(), caret_line.blue().bold());
            }
        }

        // Get specific help message for this error
        let help = get_help_for_error(message);
        if !help.is_empty() {
            println!("  {} {}", "help:".green().bold(), help);
        }

        println!();  // Add empty line between errors
        displayed_error_count += 1;
    }

    // Add general suggestions for each error category
    if displayed_error_count > 0 {
        print_general_suggestions(&error_categories);
    }

    displayed_error_count
}

pub fn hash_error_for_code(error_text: &str) -> u32 {
    use std::hash::{Hash, Hasher};
    use std::collections::hash_map::DefaultHasher;

    let mut hasher = DefaultHasher::new();
    error_text.hash(&mut hasher);
    (hasher.finish() & 0xFFFFFFFF) as u32
}

pub fn categorize_error(error_msg: &str) -> Vec<String> {
    let error_text = error_msg.to_lowercase();
    let mut categories = Vec::new();

    // Template errors - many subcategories
    if error_text.contains("template") {
        categories.push("template_general".to_string());

        if error_text.contains("parameter pack") {
            categories.push("template_parameter_pack".to_string());
        }
        if error_text.contains("specialization") {
            categories.push("template_specialization".to_string());
        }
        if error_text.contains("deduction") || error_text.contains("deduce") {
            categories.push("template_deduction".to_string());
        }
        if error_text.contains("requires") || error_text.contains("concept") {
            categories.push("template_constraints".to_string());
        }
        if error_text.contains("partial") {
            categories.push("template_partial_spec".to_string());
        }
        if error_text.contains("instantiation") {
            categories.push("template_instantiation".to_string());
        }
        if error_text.contains("template argument") {
            categories.push("template_arguments".to_string());
        }
        if error_text.contains("non-type") {
            categories.push("template_non_type_param".to_string());
        }
    }

    // Constexpr errors
    if error_text.contains("constexpr") || error_text.contains("constant expression") {
        categories.push("constexpr_general".to_string());

        if error_text.contains("not a literal type") {
            categories.push("constexpr_not_literal".to_string());
        }
        if error_text.contains("cannot") || error_text.contains("invalid") {
            categories.push("constexpr_invalid".to_string());
        }
        if error_text.contains("non-constexpr") {
            categories.push("constexpr_non_constexpr".to_string());
        }
        if error_text.contains("constexpr if") {
            categories.push("constexpr_if".to_string());
        }
    }

    // Undeclared/undefined errors
    if error_text.contains("undeclared") || error_text.contains("undefined") {
        categories.push("undeclared_general".to_string());

        if error_text.contains("identifier") {
            categories.push("undeclared_identifier".to_string());
        }
        if error_text.contains("function") {
            categories.push("undefined_function".to_string());
        }
        if error_text.contains("reference") {
            categories.push("undefined_reference".to_string());
        }
        if error_text.contains("variable") {
            categories.push("undeclared_variable".to_string());
        }
        if error_text.contains("type") {
            categories.push("undefined_type".to_string());
        }
    }

    // Class/member errors
    if error_text.contains("class") || error_text.contains("struct") || error_text.contains("member") {
        categories.push("class_general".to_string());

        if error_text.contains("does not name") || error_text.contains("no member named") {
            categories.push("no_member".to_string());
        }
        if error_text.contains("private") || error_text.contains("protected") {
            categories.push("access_control".to_string());
        }
        if error_text.contains("virtual") {
            categories.push("virtual_function".to_string());
        }
        if error_text.contains("static") {
            categories.push("static_member".to_string());
        }
        if error_text.contains("constructor") {
            categories.push("constructor".to_string());
        }
        if error_text.contains("destructor") {
            categories.push("destructor".to_string());
        }
        if error_text.contains("deleted function") {
            categories.push("deleted_function".to_string());
        }
        if error_text.contains("override") {
            categories.push("override".to_string());
        }
        if error_text.contains("abstract") {
            categories.push("abstract_class".to_string());
        }
        if error_text.contains("pure virtual") {
            categories.push("pure_virtual".to_string());
        }
    }

    // Type errors - many subcategories
    if error_text.contains("type") {
        categories.push("type_general".to_string());

        if error_text.contains("cannot convert") || error_text.contains("incompatible") {
            categories.push("type_conversion".to_string());
        }
        if error_text.contains("no matching") {
            categories.push("no_matching_function".to_string());
        }
        if error_text.contains("overloaded") {
            categories.push("overload_resolution".to_string());
        }
        if error_text.contains("ambiguous") {
            categories.push("ambiguous_call".to_string());
        }
        if error_text.contains("could not deduce") {
            categories.push("type_deduction".to_string());
        }
        if error_text.contains("incomplete type") {
            categories.push("incomplete_type".to_string());
        }
        if error_text.contains("static_cast") || error_text.contains("dynamic_cast") ||
            error_text.contains("reinterpret_cast") || error_text.contains("const_cast") {
            categories.push("cast_error".to_string());
        }
    }

    // Concept errors (C++20)
    if error_text.contains("concept") || error_text.contains("requires") {
        categories.push("concept_general".to_string());

        if error_text.contains("constraint") {
            categories.push("concept_constraint".to_string());
        }
        if error_text.contains("satisfaction") {
            categories.push("concept_satisfaction".to_string());
        }
        if error_text.contains("requirement") {
            categories.push("concept_requirement".to_string());
        }
    }

    // Initialization errors
    if error_text.contains("initialize") || error_text.contains("initializer") {
        categories.push("initialization".to_string());

        if error_text.contains("constructor") {
            categories.push("constructor_init".to_string());
        }
        if error_text.contains("list") {
            categories.push("initializer_list".to_string());
        }
        if error_text.contains("member") {
            categories.push("member_init".to_string());
        }
        if error_text.contains("default") {
            categories.push("default_init".to_string());
        }
    }

    // Lambda errors
    if error_text.contains("lambda") {
        categories.push("lambda_general".to_string());

        if error_text.contains("capture") {
            categories.push("lambda_capture".to_string());
        }
        if error_text.contains("this") {
            categories.push("lambda_this".to_string());
        }
    }

    // Smart pointer errors
    if error_text.contains("unique_ptr") || error_text.contains("shared_ptr") ||
        error_text.contains("weak_ptr") || error_text.contains("auto_ptr") {
        categories.push("smart_pointer".to_string());
    }

    // STL errors
    if error_text.contains("vector") || error_text.contains("map") ||
        error_text.contains("set") || error_text.contains("list") ||
        error_text.contains("queue") || error_text.contains("stack") ||
        error_text.contains("string") || error_text.contains("iterator") ||
        error_text.contains("algorithm") {
        categories.push("stl".to_string());

        if error_text.contains("iterator") {
            categories.push("stl_iterator".to_string());
        }
        if error_text.contains("out_of_range") || error_text.contains("out of range") {
            categories.push("stl_out_of_range".to_string());
        }
        if error_text.contains("allocator") {
            categories.push("stl_allocator".to_string());
        }
    }

    // Memory errors
    if error_text.contains("memory") || error_text.contains("allocation") ||
        error_text.contains("free") || error_text.contains("delete") ||
        error_text.contains("new") {
        categories.push("memory".to_string());

        if error_text.contains("leak") {
            categories.push("memory_leak".to_string());
        }
        if error_text.contains("double free") || error_text.contains("delete") {
            categories.push("double_free".to_string());
        }
        if error_text.contains("null") || error_text.contains("nullptr") {
            categories.push("null_pointer".to_string());
        }
        if error_text.contains("uninitialized") {
            categories.push("uninitialized_memory".to_string());
        }
    }

    // Missing files/includes
    if error_text.contains("no such file") || error_text.contains("cannot open") ||
        error_text.contains("file not found") || error_text.contains("#include") {
        categories.push("missing_file".to_string());
    }

    // Linker errors
    if error_text.contains("link") || error_text.contains("ld") ||
        error_text.contains("undefined reference") || error_text.contains("unresolved external") {
        categories.push("linker".to_string());

        if error_text.contains("duplicate") || error_text.contains("multiple definition") {
            categories.push("linker_duplicate".to_string());
        }
        if error_text.contains("LNK") {
            categories.push("msvc_linker".to_string());
        }
        if error_text.contains("undefined symbol") || error_text.contains("undefined reference") {
            categories.push("undefined_symbol".to_string());
        }
    }

    // Preprocessor errors
    if error_text.contains("#include") || error_text.contains("#define") ||
        error_text.contains("#if") || error_text.contains("#ifdef") ||
        error_text.contains("macro") {
        categories.push("preprocessor".to_string());

        if error_text.contains("redefined") {
            categories.push("macro_redefined".to_string());
        }
        if error_text.contains("#if") || error_text.contains("#ifdef") || error_text.contains("#endif") {
            categories.push("conditional_compilation".to_string());
        }
    }

    // Build system errors
    if error_text.contains("cmake") || error_text.contains("ninja") ||
        error_text.contains("make") || error_text.contains("msbuild") {
        categories.push("build_system".to_string());
    }

    // Syntax errors
    if error_text.contains("syntax") || error_text.contains("expected") {
        categories.push("syntax".to_string());

        if error_text.contains("expected") && error_text.contains(";") {
            categories.push("missing_semicolon".to_string());
        }
        if error_text.contains("expected") &&
            (error_text.contains("{") || error_text.contains("}") ||
                error_text.contains("(") || error_text.contains(")")) {
            categories.push("mismatched_brackets".to_string());
        }
    }

    // C++11/14/17/20 specific features
    if error_text.contains("auto") {
        categories.push("auto_type".to_string());
    }
    if error_text.contains("decltype") {
        categories.push("decltype".to_string());
    }
    if error_text.contains("nullptr") {
        categories.push("nullptr".to_string());
    }
    if error_text.contains("move") || error_text.contains("rvalue") || error_text.contains("&&") {
        categories.push("move_semantics".to_string());
    }
    if error_text.contains("variadic") {
        categories.push("variadic_templates".to_string());
    }
    if error_text.contains("fold") && error_text.contains("expression") {
        categories.push("fold_expressions".to_string());
    }
    if error_text.contains("structured binding") {
        categories.push("structured_binding".to_string());
    }
    if error_text.contains("if constexpr") {
        categories.push("if_constexpr".to_string());
    }
    if error_text.contains("consteval") {
        categories.push("consteval".to_string());
    }
    if error_text.contains("concept") {
        categories.push("concepts".to_string());
    }
    if error_text.contains("module") && (error_text.contains("import") || error_text.contains("export")) {
        categories.push("modules".to_string());
    }

    // Add at least one category if none was found
    if categories.is_empty() {
        categories.push("general".to_string());
    }

    categories
}

pub fn display_raw_errors(stdout: &str, stderr: &str) {
    let mut error_lines = Vec::new();

    // First check stderr
    for line in stderr.lines() {
        if line.contains("error") || line.contains("Error") || line.contains("failed") ||
            line.contains("Failed") || line.contains("missing") {
            error_lines.push(line);
        }
    }

    // If no errors found in stderr, check stdout
    if error_lines.is_empty() {
        for line in stdout.lines() {
            if line.contains("error") || line.contains("Error") || line.contains("failed") ||
                line.contains("Failed") || line.contains("missing") {
                error_lines.push(line);
            }
        }
    }

    // Display the errors (up to 10)
    let max_errors = 10;
    for (i, line) in error_lines.iter().take(max_errors).enumerate() {
        // Clean up the line by removing ANSI escape codes and trimming
        let clean_line = line.trim();
        print_error(&format!("[{}] {}", i+1, clean_line), None, None);
    }

    // Show how many more errors there are
    if error_lines.len() > max_errors {
        print_warning(&format!("... and {} more errors", error_lines.len() - max_errors), None);
    }

    // If we found no errors at all, show a generic message
    if error_lines.is_empty() {
        print_error("Build command failed but no specific errors were found", None, None);

        // Try to show the last few lines of stderr or stdout as context
        if !stderr.is_empty() {
            let last_lines: Vec<&str> = stderr.lines().rev().take(5).collect();
            print_status("Last few lines of stderr:");
            for line in last_lines.iter().rev() {
                print_substep(line);
            }
        } else if !stdout.is_empty() {
            let last_lines: Vec<&str> = stdout.lines().rev().take(5).collect();
            print_status("Last few lines of stdout:");
            for line in last_lines.iter().rev() {
                print_substep(line);
            }
        }
    }
}

pub fn extract_build_errors(stdout: &str, stderr: &str) -> Vec<String> {
    let mut errors = Vec::new();

    // Common error patterns to look for
    let error_patterns = [
        (regex::Regex::new(r"(?m)^(?:.*?)error(?::|\[)[^\n]*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*Error:.*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*fatal error:.*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*undefined reference to.*$").unwrap(), true),
        (regex::Regex::new(r"(?m)^.*cannot find.*$").unwrap(), false),
        (regex::Regex::new(r"(?m)^.*No such file or directory.*$").unwrap(), false),
    ];

    // Search for errors in stderr first (more likely to contain actual errors)
    for (pattern, is_important) in &error_patterns {
        for cap in pattern.captures_iter(stderr) {
            let error = cap[0].to_string();
            if *is_important || errors.len() < 10 {  // Only include less important errors if we don't have many
                errors.push(error);
            }
        }
    }

    // If we didn't find any errors in stderr, check stdout too
    if errors.is_empty() {
        for (pattern, is_important) in &error_patterns {
            for cap in pattern.captures_iter(stdout) {
                let error = cap[0].to_string();
                if *is_important || errors.len() < 10 {
                    errors.push(error);
                }
            }
        }
    }

    // Sort errors by importance and uniqueness
    errors.sort_by_key(|e| (!e.contains("error:"), !e.contains("Error:"), e.clone()));

    // Remove duplicates
    errors.dedup();

    // To make the error list more manageable, group similar errors
    let mut grouped_errors = Vec::new();
    let mut seen_patterns = HashSet::new();

    for error in errors {
        // Try to extract just the key error message without file/line details
        if let Some(idx) = error.find(':') {
            let error_type = &error[idx+1..];
            // Get a simplified version for deduplication
            let simple = error_type.trim()
                .chars()
                .filter(|c| !c.is_whitespace())
                .collect::<String>();

            if simple.len() > 10 && !seen_patterns.contains(&simple) {
                seen_patterns.insert(simple);
                grouped_errors.push(error);
            }
        } else {
            grouped_errors.push(error);
        }
    }

    // If we have too many errors, just take the most important ones
    if grouped_errors.len() > 20 {
        grouped_errors.truncate(20);
    }

    grouped_errors
}

pub fn extract_source_line(stdout: &str, stderr: &str, file: &str, line_num: usize) -> Option<String> {
    // Try to find patterns like "line_num |   code..." in the output
    let pattern = format!(r"(?m)^.*\s*{}\s*\|\s*(.*)$", line_num);
    let line_pattern = regex::Regex::new(&pattern).ok()?;

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

pub fn create_caret_line(column: usize, message: &str) -> String {
    let mut caret_line = String::new();

    // Add spaces until we reach the column
    for _ in 0..column.saturating_sub(1) {
        caret_line.push(' ');
    }

    // Add the caret
    caret_line.push('^');

    // Add a few squiggly lines
    let squiggle_length = message.len().min(30);
    for _ in 0..squiggle_length.saturating_sub(1) {
        caret_line.push('~');
    }

    caret_line
}

pub fn format_cpp_errors_rust_style(output: &str) -> Vec<String> {
    let mut results = Vec::new();
    let mut diagnostics = Vec::new();

    // We use these regexes for capturing:
    let clang_style = Regex::new(r"(?m)(.*?):(\d+):(\d+): (error|warning|note): (.*)").unwrap();
    let msvc_style  = Regex::new(r"(?m)(.*?)\((\d+),(\d+)\): (error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap();
    // MSVC sometimes omits the column:
    let msvc_style_alt = Regex::new(r"(?m)(.*?)\((\d+)\): (error|warning|note)(?:\s+[A-Z]\d+)?: (.*)").unwrap();

    // This will help gather lines that appear immediately after an error line for context
    let lines: Vec<&str> = output.lines().collect();

    // We'll store all matched diagnostics in a Vec, then we’ll add context.
    // clang/gcc pattern first:
    for cap in clang_style.captures_iter(output) {
        let file    = cap[1].to_string();
        let line    = cap[2].parse::<usize>().unwrap_or(0);
        let column  = cap[3].parse::<usize>().unwrap_or(0);
        let level   = cap[4].to_string();
        let message = cap[5].to_string();

        diagnostics.push(CompilerDiagnostic {
            file, line, column,
            level,
            message,
            context: vec![],
        });
    }

    // Next, MSVC pattern (with column):
    for cap in msvc_style.captures_iter(output) {
        let file    = cap[1].to_string();
        let line    = cap[2].parse::<usize>().unwrap_or(0);
        let column  = cap[3].parse::<usize>().unwrap_or(0);
        let level   = cap[4].to_string();
        let message = cap[5].to_string();

        diagnostics.push(CompilerDiagnostic {
            file, line, column,
            level,
            message,
            context: vec![],
        });
    }

    // Next, MSVC pattern (no column):
    for cap in msvc_style_alt.captures_iter(output) {
        let file    = cap[1].to_string();
        let line    = cap[2].parse::<usize>().unwrap_or(0);
        let level   = cap[3].to_string();
        let message = cap[4].to_string();
        // We guess a column of 1
        diagnostics.push(CompilerDiagnostic {
            file,
            line,
            column: 1,
            level,
            message,
            context: vec![],
        });
    }

    // Now we add some snippet context for each error line. We'll look up to ~2 lines around.
    // We do this by scanning through `lines` to find occurrences of e.g. "file:line"
    // But let's do a simpler approach: we have the file + line in diagnostics;
    // we won't open that file from disk — we'll just try to glean lines from the *compiler output itself.*
    // For truly Rust-like snippet context, you'd read the actual file from disk. But below uses the raw compiler lines.
    //
    // For big/bulky logs, you might want a more advanced approach. For now, we do a best-effort gather.

    // We'll just do the "In file included from..." lines or the next 1–3 lines if they appear to be code context.
    // For example, clang often shows something like:
    //   <source>:20:10: error: ...
    //   20 |     int x = ...
    //      |          ^
    //   ...
    // We'll collect those lines.

    // We'll do a simpler approach: if the line has "line:col: error", the next 2 lines might be code context
    for diag in &mut diagnostics {
        // We'll try to find the exact line text in the compiler output that has the caret afterward.
        // That usually is the next line after the line that matched. Let’s find that index:
        let re_for_search = format!("{}:{}:{}:", diag.file, diag.line, diag.column);
        let pos = lines.iter().position(|ln| ln.contains(&re_for_search));
        if let Some(idx) = pos {
            // Next lines might contain code context or caret:
            // We'll gather up to 3 lines after that
            for offset in 1..=3 {
                if idx + offset < lines.len() {
                    let possible_code_line = lines[idx + offset];
                    // If it looks like an error or warning line, we stop:
                    if possible_code_line.contains("error:") || possible_code_line.contains("warning:") {
                        break;
                    }
                    diag.context.push(possible_code_line.to_string());
                }
            }
        }
    }

    // Deduplicate
    let mut seen = HashSet::new();
    let mut unique_diagnostics = Vec::new();
    for d in &diagnostics {
        let key = format!("{}:{}:{}:{}:{}",
                          d.file, d.line, d.column, d.level, d.message
        );
        if !seen.contains(&key) {
            seen.insert(key);
            unique_diagnostics.push(d.clone());
        }
    }

    // Sort by level (error first, warning, then note) and by file + line
    unique_diagnostics.sort_by(|a, b| {
        let order_level = cmp_level(&a.level).cmp(&cmp_level(&b.level));
        if order_level != std::cmp::Ordering::Equal {
            return order_level;
        }
        let order_file = a.file.cmp(&b.file);
        if order_file != std::cmp::Ordering::Equal {
            return order_file;
        }
        a.line.cmp(&b.line)
    });

    // Now we create fancy multiline strings
    for diag in &mut unique_diagnostics {
        // Make a Rust-like header with error code
        let error_code = if diag.level == "error" { "E0001" }
        else if diag.level == "warning" { "W0001" }
        else { "N0001" };

        let color = match diag.level.as_str() {
            "error"   => Color::Red,
            "warning" => Color::Yellow,
            "note"    => Color::BrightBlue,
            _         => Color::White,
        };

        // Create a nicer header with error code and file location
        let header = format!(
            "{}[{}]: {}",
            format!("{}{}", diag.level.to_uppercase(), if diag.level == "error" { format!("[{}]", error_code) } else { String::new() }),
            short_path(&diag.file),
            diag.message
        )
            .color(color)
            .bold()
            .to_string();

        // Add line/column indicator like Rust
        let location = format!(
            " --> {}:{}:{}",
            diag.file,
            diag.line,
            diag.column
        )
            .color(color)
            .to_string();

        // Create a nicer code snippet with line numbers
        let mut snippet_lines = Vec::new();

        // Add line number for context
        snippet_lines.push(format!("{} |", diag.line.to_string().blue().bold()));

        // Add the code line if available
        if !diag.context.is_empty() {
            snippet_lines.push(format!("{} | {}", " ".blue().bold(), diag.context[0]));

            // Add caret line with appropriate spacing
            let mut indicator = String::new();
            for _ in 0..(diag.column.saturating_sub(1)) {
                indicator.push(' ');
            }
            indicator.push('^');
            for _ in 0..diag.message.len().min(3) {
                indicator.push('~');
            }

            snippet_lines.push(format!("{} | {}", " ".blue().bold(), indicator.color(color).bold()));
        }

        // Add a help message for common errors
        let help_message = get_help_for_error(&diag.message);
        if !help_message.is_empty() {
            snippet_lines.push(format!("{}: {}", "help".green().bold(), help_message));
        }

        results.push(header);
        results.push(location);
        results.push(String::new());  // Empty line for spacing
        results.extend(snippet_lines);
        results.push(String::new());  // Empty line for spacing
    }

    results
}

pub fn get_help_for_error(error_msg: &str) -> String {
    let error_text = error_msg.to_lowercase();

    // Template parameter pack errors
    if error_text.contains("template parameter pack must be the last template parameter") {
        return "variadic template parameters (template<typename... Args>) must always be the last parameter in the template parameter list".to_string();
    }

    // Constexpr errors
    if error_text.contains("constexpr function's return type") && error_text.contains("not a literal type") {
        return "classes used with constexpr must have at least one constexpr constructor and a trivial destructor".to_string();
    }

    if error_text.contains("constexpr") && error_text.contains("non-constexpr") {
        return "a constexpr function can only call other constexpr functions and use constexpr variables".to_string();
    }

    // Undeclared identifiers
    if error_text.contains("use of undeclared identifier") {
        let var_name = extract_quoted_or_word_after(&error_text, "identifier");
        if !var_name.is_empty() {
            return format!("'{}' is used before being declared - check for typos or missing includes", var_name);
        }
        return "identifier used before being declared - check for typos or missing includes".to_string();
    }

    // Member errors
    if error_text.contains("member initializer") && error_text.contains("does not name a non-static data member") {
        let member_name = extract_quoted_or_word_after(&error_text, "initializer");
        if !member_name.is_empty() {
            return format!("'{}' is not a declared member of this class - add it to the class definition first", member_name);
        }
        return "trying to initialize a member that doesn't exist in the class - declare it first".to_string();
    }

    if error_text.contains("no member named") {
        let member_name = extract_quoted_or_word_after(&error_text, "named");
        if !member_name.is_empty() {
            return format!("no member named '{}' in this class - check for typos or add the member", member_name);
        }
        return "member doesn't exist in this class - check for typos or missing declaration".to_string();
    }

    // Concept errors
    if error_text.contains("requires") && error_text.contains("concept") {
        return "ensure your types satisfy all constraints of the concept".to_string();
    }

    if error_text.contains("undeclared identifier") && error_text.contains("allin") {
        return "define the 'AllIn' concept before using it, e.g.: template<typename T, typename... Types> concept AllIn = (std::is_same_v<T, Types> || ...)".to_string();
    }

    // No matching function
    if error_text.contains("no matching function for call") {
        return "the arguments don't match any available function overload - check parameter types".to_string();
    }

    if error_text.contains("no matching member function for call") {
        return "this class doesn't have a method that matches these arguments - check signature".to_string();
    }

    // Type conversion errors
    if error_text.contains("cannot convert") || error_text.contains("invalid conversion") {
        let from_type = extract_between(&error_text, "from ", " to");
        let to_type = extract_after(&error_text, "to ");

        if !from_type.is_empty() && !to_type.is_empty() {
            return format!("cannot convert from '{}' to '{}' - consider using an explicit cast", from_type, to_type);
        }
        return "types are incompatible - an explicit conversion may be required (static_cast<Type>)".to_string();
    }

    // Private/protected member access
    if error_text.contains("is a private member") {
        let member_name = extract_quoted_or_word_after(&error_text, "member");
        if !member_name.is_empty() {
            return format!("'{}' is private and can only be accessed within the class or by friends", member_name);
        }
        return "trying to access a private member - use public accessor methods instead".to_string();
    }

    if error_text.contains("is a protected member") {
        return "protected members can only be accessed by the class itself and derived classes".to_string();
    }

    // Reference errors
    if error_text.contains("undefined reference to") {
        let symbol = extract_quoted_or_word_after(&error_text, "to");
        if !symbol.is_empty() {
            return format!("'{}' is declared but not defined - ensure implementation is provided and linked", symbol);
        }
        return "symbol is declared but not defined - check implementation file is included in build".to_string();
    }

    if error_text.contains("unresolved external symbol") {
        return "function or variable is declared but not defined - check implementation is linked".to_string();
    }

    // Include errors
    if error_text.contains("file not found") {
        let file_name = extract_quoted(&error_text);
        if !file_name.is_empty() {
            return format!("cannot find '{}' - check file path and include directories", file_name);
        }
        return "header file not found - check include paths and file names".to_string();
    }

    // Virtual function errors
    if error_text.contains("override") && error_text.contains("virtual") {
        return "function signature doesn't match the base class method it's trying to override".to_string();
    }

    // Missing semicolon
    if error_text.contains("expected ';'") {
        return "missing semicolon at the end of a statement or declaration".to_string();
    }

    // Default constructor
    if error_text.contains("no default constructor") {
        return "this class has no default constructor - provide arguments or define a default constructor".to_string();
    }

    // Deleted function
    if error_text.contains("deleted function") {
        return "attempting to call a function marked as deleted (maybe copy constructor/assignment)".to_string();
    }

    // Ambiguous call
    if error_text.contains("ambiguous") && error_text.contains("call") {
        return "multiple overloads match this call - provide more specific types or explicit casts".to_string();
    }

    // Auto type deduction
    if error_text.contains("unable to deduce") && error_text.contains("auto") {
        return "auto type deduction failed - ensure the expression has a well-defined type".to_string();
    }

    // Template argument deduction
    if error_text.contains("failed template argument deduction") {
        return "cannot deduce template arguments - consider specifying them explicitly".to_string();
    }

    // Lambda captures
    if error_text.contains("lambda") && error_text.contains("capture") {
        return "issue with lambda capture - ensure captured variables exist in the enclosing scope".to_string();
    }

    // Structured bindings
    if error_text.contains("structured binding") {
        return "check that the number and types of variables match the structure being bound".to_string();
    }

    // Missing return
    if error_text.contains("no return statement") || (error_text.contains("return") && error_text.contains("void")) {
        return "function needs a return statement with a value of the correct return type".to_string();
    }

    // Incomplete type
    if error_text.contains("incomplete type") {
        return "trying to use a type that's only forward-declared - include the full definition".to_string();
    }

    // Parameter type mismatch
    if error_text.contains("argument") && (error_text.contains("mismatch") || error_text.contains("invalid")) {
        return "function arguments don't match parameter types - check function signature".to_string();
    }

    // Vector/container errors
    if error_text.contains("vector") && error_text.contains("range") {
        return "accessing vector with invalid index - use .at() for bounds checking or check index".to_string();
    }

    // STL errors
    if error_text.contains("iterator") && (error_text.contains("end") || error_text.contains("dereference")) {
        return "dereferencing invalid iterator (such as end() or after erase) - be careful with iterator validity".to_string();
    }

    // Return no specific help if we didn't identify the error
    String::new()
}

pub fn extract_between(text: &str, start_pattern: &str, end_pattern: &str) -> String {
    if let Some(start_idx) = text.find(start_pattern) {
        let after_start = &text[start_idx + start_pattern.len()..];
        if let Some(end_idx) = after_start.find(end_pattern) {
            return after_start[..end_idx].trim().to_string();
        }
    }
    String::new()
}

pub fn extract_after(text: &str, pattern: &str) -> String {
    if let Some(idx) = text.find(pattern) {
        return text[idx + pattern.len()..].trim().to_string();
    }
    String::new()
}

pub fn extract_quoted(text: &str) -> String {
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
    String::new()
}

pub fn extract_quoted_or_word_after(text: &str, pattern: &str) -> String {
    // Try quoted first
    let quoted = extract_quoted(text);
    if !quoted.is_empty() {
        return quoted;
    }

    // Try word after pattern
    if let Some(idx) = text.find(pattern) {
        let after = &text[idx + pattern.len()..];
        let words: Vec<&str> = after.split_whitespace().collect();
        if !words.is_empty() {
            // Clean up any punctuation
            let word = words[0].trim_matches(|c: char| !c.is_alphanumeric() && c != '_');
            return word.to_string();
        }
    }

    String::new()
}

pub fn cmp_level(lv: &str) -> u8 {
    match lv {
        "error"   => 0,
        "warning" => 1,
        "note"    => 2,
        _         => 3,
    }
}

pub fn short_path(path: &str) -> String {
    // e.g. "C:/Arcnum/Spark/include\spark_event.hpp" => "include/spark_event.hpp"
    let p = std::path::Path::new(path);
    if let Some(fname) = p.file_name() {
        let fname_s = fname.to_string_lossy();
        // Also try to grab one directory level up:
        if let Some(parent) = p.parent() {
            if let Some(pdir) = parent.file_name() {
                return format!("{}/{}", pdir.to_string_lossy(), fname_s);
            }
        }
        fname_s.into_owned()
    } else {
        path.to_string()
    }
}

pub fn process_error_context(
    formatted_errors: &mut Vec<String>,
    seen: &mut HashSet<String>,
    file: &str,
    context: &[String],
    line_num: usize,
    column: usize,
    message: &str,
    error_type: &str
) {
    if file.is_empty() || line_num == 0 || context.is_empty() {
        return;
    }

    // Create a unique key for the error
    let key = format!("{}:{}:{}:{}", file, line_num, column, message);
    if seen.contains(&key) {
        return; // Skip duplicate error
    }
    seen.insert(key);

    let error_color = if error_type == "error" {
        "red"
    } else if error_type == "warning" {
        "yellow"
    } else {
        "blue"
    };

    let formatted_file = format_file_path(file);
    let error_header = format!("{}[{}]: {}", error_type.to_uppercase(), formatted_file, message);
    formatted_errors.push(format!("{}", error_header.color(error_color).bold()));

    // Pick a representative line from the context (e.g. second line if available)
    let error_line = if context.len() >= 3 {
        context.get(1).unwrap_or(&context[0]).clone()
    } else {
        context[0].clone()
    };

    formatted_errors.push(format!("   {} |", line_num.to_string().blue().bold()));
    formatted_errors.push(format!("   {} | {}", " ".blue().bold(), error_line));

    // Create an indicator line with a caret at the error column
    let mut indicator = String::new();
    for _ in 0..(column.saturating_sub(1)) {
        indicator.push(' ');
    }
    indicator.push('^');
    for _ in 0..message.len().min(3) {
        indicator.push('~');
    }
    formatted_errors.push(format!("   {} | {}", " ".blue().bold(), indicator.color(error_color).bold()));
    formatted_errors.push(String::new());
}

pub fn format_file_path(path: &str) -> String {
    // Extract just the filename and a bit of path context
    let path_obj = std::path::Path::new(path);
    if let Some(filename) = path_obj.file_name() {


        let filename_str = filename.to_string_lossy();

        // Try to include the parent directory for context
        if let Some(parent) = path_obj.parent() {
            if let Some(dirname) = parent.file_name() {
                return format!("{}/{}", dirname.to_string_lossy(), filename_str);
            }
        }

        return filename_str.to_string();
    }

    path.to_string()
}

pub fn analyze_cpp_errors(error_output: &str) -> Vec<String> {
    let mut suggestions = Vec::new();

    // Common C++ error patterns and their solutions
    if error_output.contains("constexpr function's return type") && error_output.contains("is not a literal type") {
        suggestions.push("Add a constexpr constructor to the class or remove constexpr from the function".to_string());
        suggestions.push("Example: 'constexpr VertexLayout() = default;' in your class definition".to_string());
    }

    if error_output.contains("template parameter pack must be the last template parameter") {
        suggestions.push("Move the variadic template parameter to the end of the parameter list".to_string());
        suggestions.push("Example: Change 'template <typename... As, typename... Bs>' to 'template <typename A, typename... Bs>'".to_string());
    }

    if error_output.contains("use of undeclared identifier 'AllIn'") {
        suggestions.push("Define the 'AllIn' template concept before using it".to_string());
        suggestions.push("Example: 'template<typename T, typename... Types> concept AllIn = (std::is_same_v<T, Types> || ...);'".to_string());
    }

    if error_output.contains("member initializer") && error_output.contains("does not name a non-static data member") {
        suggestions.push("Declare the member variable in your class before initializing it in the constructor".to_string());
        suggestions.push("Example: Add 'VariantT m_variant;' to your class definition".to_string());
    }

    if error_output.contains("No such file or directory") || error_output.contains("cannot open include file") {
        suggestions.push("Check that the include path is correct and the file exists".to_string());
        suggestions.push("Make sure all dependencies are installed with 'cforge deps'".to_string());
    }

    if error_output.contains("undefined reference to") || error_output.contains("unresolved external symbol") {
        suggestions.push("Ensure the required library is linked in your cforge.toml".to_string());
        suggestions.push("Check that the function/symbol is defined in the linked libraries".to_string());
    }

    if error_output.contains("error[E0282]") || error_output.contains("type annotations needed") {
        suggestions.push("Add explicit type annotations to ambiguous variable declarations".to_string());
        suggestions.push("Example: Change 'let x = func();' to 'let x: ReturnType = func();'".to_string());
    }

    // Return only unique suggestions
    suggestions.sort();
    suggestions.dedup();
    suggestions
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

pub fn parse_error_line(line: &str, line_buffer: &mut Vec<String>, in_error_context: &mut bool) -> Option<CompilerDiagnostic> {
    // Check for common error message patterns

    // GCC/Clang style errors (file:line:col: error/warning: message)


    // Check for a new error message
    if let Some(cap) = crate::CLANG_STYLE.captures(line) {
        // We have a new error message, clear the buffer
        line_buffer.clear();
        *in_error_context = true;

        // Add the current line to the buffer
        line_buffer.push(line.to_string());

        // Create a diagnostic
        let file = cap[1].to_string();
        let line = cap[2].parse().unwrap_or(0);
        let column = cap[3].parse().unwrap_or(0);
        let level = cap[4].to_string();
        let message = cap[5].to_string();

        return Some(CompilerDiagnostic {
            file,
            line,
            column,
            level,
            message,
            context: line_buffer.clone(),
        });
    }
    else if let Some(cap) = crate::MSVC_STYLE.captures(line) {
        // We have a new error message, clear the buffer
        line_buffer.clear();
        *in_error_context = true;

        // Add the current line to the buffer
        line_buffer.push(line.to_string());

        // Create a diagnostic
        let file = cap[1].to_string();
        let line = cap[2].parse().unwrap_or(0);
        let column = cap[3].parse().unwrap_or(0);
        let level = cap[4].to_string();
        let message = cap[5].to_string();

        return Some(CompilerDiagnostic {
            file,
            line,
            column,
            level,
            message,
            context: line_buffer.clone(),
        });
    }
    else if let Some(cap) = crate::MSVC_SIMPLE.captures(line) {
        // We have a new error message, clear the buffer
        line_buffer.clear();
        *in_error_context = true;

        // Add the current line to the buffer
        line_buffer.push(line.to_string());

        // Create a diagnostic
        let file = cap[1].to_string();
        let line = cap[2].parse().unwrap_or(0);
        let level = cap[3].to_string();
        let message = cap[4].to_string();

        return Some(CompilerDiagnostic {
            file,
            line,
            column: 0, // No column information
            level,
            message,
            context: line_buffer.clone(),
        });
    }

    // If we're in an error context, collect additional lines
    if *in_error_context {
        // Check if this line could be context for the previous error
        if !line.contains("error:") && !line.contains("warning:") && !line.trim().is_empty() {
            line_buffer.push(line.to_string());

            // Check for end of context - usually a blank line or a line with code indicators
            if line.trim().is_empty() || line.contains("^") {
                *in_error_context = false;
            }
        }
    }

    None
}

pub fn analyze_build_error(error_output: &str) -> Vec<String> {
    let mut suggestions = Vec::new();

    // Common C++ build errors and their solutions
    if error_output.contains("No such file or directory") || error_output.contains("cannot open include file") {
        suggestions.push("Missing header file. Check that all dependencies are installed.".to_string());
        suggestions.push("Verify include paths are correct in your cforge.toml.".to_string());
    }

    if error_output.contains("undefined reference to") || error_output.contains("unresolved external symbol") {
        suggestions.push("Missing library or object file. Check that all dependencies are installed.".to_string());
        suggestions.push("Verify library paths and link options in your cforge.toml.".to_string());
        suggestions.push("Make sure the library was built with the same compiler/settings.".to_string());
    }

    if error_output.contains("incompatible types") || error_output.contains("cannot convert") {
        suggestions.push("Type mismatch error. This might be caused by using different compiler flags or standard versions.".to_string());
        suggestions.push("Make sure all libraries and your code use compatible C++ standards.".to_string());
    }

    if error_output.contains("permission denied") {
        suggestions.push("Permission error. Try running the command with administrative privileges.".to_string());
        suggestions.push("Check if the files or directories are read-only or locked by another process.".to_string());
    }

    if error_output.contains("vcpkg") && error_output.contains("not found") {
        suggestions.push("vcpkg issue. Make sure vcpkg is properly installed.".to_string());
        suggestions.push("Check if the vcpkg path in cforge.toml is correct.".to_string());
        suggestions.push("Try running 'cforge deps' to install dependencies first.".to_string());
    }

    if error_output.contains("cl.exe") && error_output.contains("not recognized") {
        suggestions.push("Visual Studio tools not found. Make sure Visual Studio or Build Tools are installed.".to_string());
        suggestions.push("Try opening a Developer Command Prompt or run from Visual Studio Command Prompt.".to_string());
        suggestions.push("Make sure the environment variables are set correctly.".to_string());
    }

    if error_output.contains("CMake Error") {
        if error_output.contains("generator") {
            suggestions.push("CMake generator issue. Make sure the requested generator is installed.".to_string());
            suggestions.push("Try using a different generator in cforge.toml or let cforge auto-detect.".to_string());
        }

        if error_output.contains("Could not find") {
            suggestions.push("CMake dependency issue. Make sure all required packages are installed.".to_string());
            suggestions.push("Run 'cforge deps' to install dependencies.".to_string());
        }
    }

    // If no specific issues found, provide general suggestions
    if suggestions.is_empty() {
        suggestions.push("Check if all build tools are installed and available in PATH.".to_string());
        suggestions.push("Make sure all dependencies are installed with 'cforge deps'.".to_string());
        suggestions.push("Try using a different compiler or generator.".to_string());
        suggestions.push("Run 'cforge clean' and then try building again.".to_string());
    }

    suggestions
}

pub fn expand_tilde(path: &str) -> String {
    if path.starts_with("~/") {
        if let Some(home) = dirs::home_dir() {
            return home.join(path.strip_prefix("~/").unwrap()).to_string_lossy().to_string();
        }
    }
    path.to_string()
}

pub fn print_general_suggestions(error_categories: &HashSet<String>) {
    let categories: Vec<String> = error_categories.iter().cloned().collect();

    println!("{}", "Help for common errors:".yellow().bold());

    let mut printed_help = false;

    // --- Template Errors ---
    if categories.iter().any(|c| c.starts_with("template_")) {
        println!("{}", "● For template errors:".bold());

        if categories.contains(&"template_parameter_pack".to_string()) {
            println!("  - Variadic templates (template<typename... Args>) must be the last parameter");
            println!("  - Change `template<typename... T, typename U>` to `template<typename U, typename... T>`");
            println!("  - Each parameter pack expansion must have matching pack sizes");
        }

        if categories.contains(&"template_deduction".to_string()) {
            println!("  - When template argument deduction fails, specify arguments explicitly");
            println!("  - Example: `func<int, float>(a, b)` instead of just `func(a, b)`");
            println!("  - Check if function parameters match the template parameter types");
        }

        if categories.contains(&"template_specialization".to_string()) {
            println!("  - Template specializations must come after the primary template");
            println!("  - Partial specializations only work for class templates, not function templates");
            println!("  - Ensure specialization syntax is correct: `template<> class MyClass<int> {{...}}`");
        }

        if categories.contains(&"template_instantiation".to_string()) {
            println!("  - Check for errors in the template body that only appear when instantiated");
            println!("  - Template code is only checked when actually instantiated with specific types");
            println!("  - Templates used with incompatible types will fail at instantiation time");
        }

        println!("  - Remember that template code must be in header files or explicitly instantiated");
        println!("  - Use concepts (C++20) or SFINAE to restrict template usage to valid types");
        println!();
        printed_help = true;
    }

    // --- Constexpr Errors ---
    if categories.iter().any(|c| c.starts_with("constexpr_")) {
        println!("{}", "● For constexpr errors:".bold());

        if categories.contains(&"constexpr_not_literal".to_string()) {
            println!("  - A class used with constexpr must be a literal type, which requires:");
            println!("    * At least one constexpr constructor");
            println!("    * A trivial or constexpr destructor");
            println!("    * All non-static data members must be literal types");
            println!("  - Example fix: Add `constexpr YourClass() = default;` to your class");
        }

        if categories.contains(&"constexpr_invalid".to_string()) || categories.contains(&"constexpr_non_constexpr".to_string()) {
            println!("  - Constexpr functions can only contain:");
            println!("    * Literal values and constexpr variables");
            println!("    * Calls to other constexpr functions");
            println!("    * Simple control flow (if/else, for loops with known bounds)");
            println!("  - Cannot use: dynamic memory allocation, virtual functions, try/catch");
        }

        if categories.contains(&"constexpr_if".to_string()) {
            println!("  - `if constexpr` requires a constant expression condition");
            println!("  - Use to conditionally compile code based on template parameters");
        }

        println!("  - Consider if constexpr is necessary for your use case");
        println!("  - C++20 relaxes many constexpr restrictions (dynamic allocation, try/catch)");
        println!();
        printed_help = true;
    }

    // --- Undeclared Identifier Errors ---
    if categories.iter().any(|c| c.starts_with("undeclared_")) {
        println!("{}", "● For undeclared identifier errors:".bold());

        if categories.contains(&"undeclared_identifier".to_string()) {
            println!("  - Ensure the variable or function is declared before use");
            println!("  - Check for typos in the identifier name");
            println!("  - Variables declared in inner scopes aren't visible in outer scopes");
            println!("  - Variables declared in if/for/while conditions are only visible inside");
        }

        if categories.contains(&"undefined_function".to_string()) || categories.contains(&"undefined_reference".to_string()) {
            println!("  - Function is declared but not defined (implemented)");
            println!("  - Ensure the implementation file (.cpp) is included in the build");
            println!("  - For templates, implementation must be visible at point of instantiation");
            println!("  - Check that function signature exactly matches the declaration");
        }

        if categories.contains(&"undefined_type".to_string()) {
            println!("  - Class/struct/enum type not defined before use");
            println!("  - Include the header that defines the type");
            println!("  - Check for missing 'struct'/'class' keywords in C-style code");
        }

        println!("  - Verify that required headers are included");
        println!("  - Check if the identifier is in a namespace (use `namespace::identifier`)");
        println!("  - Consider using forward declarations where appropriate");
        println!();
        printed_help = true;
    }

    // --- Member Errors ---
    if categories.iter().any(|c| c == "no_member" || c == "class_general" || c == "access_control") {
        println!("{}", "● For class member errors:".bold());

        if categories.contains(&"no_member".to_string()) {
            println!("  - The member variable or function doesn't exist in this class");
            println!("  - Ensure the member is declared in the class definition");
            println!("  - Check for typos in the member name");
            println!("  - Member variables must be declared in the class body, not in constructors");
            println!("  - Example: Add `Type memberName;` to your class definition");
        }

        if categories.contains(&"access_control".to_string()) {
            println!("  - Private members can only be accessed within the class itself");
            println!("  - Protected members can only be accessed by the class and its descendants");
            println!("  - Consider making the member public or providing accessor methods");
            println!("  - Friend functions/classes can access private members");
        }

        if categories.contains(&"constructor".to_string()) {
            println!("  - Check constructor parameter types match the arguments");
            println!("  - Default constructor is not generated if any constructor is defined");
            println!("  - Use = default to explicitly request default constructors");
            println!("  - Member initializer lists should use : not = and separate with commas");
        }

        println!("  - Remember that each class instance has its own copy of non-static members");
        println!("  - Static members must be defined outside the class in a .cpp file");
        println!("  - Inherited members might be hidden by same-named members in derived classes");
        println!();
        printed_help = true;
    }

    // --- Type Errors ---
    if categories.iter().any(|c| c.starts_with("type_")) {
        println!("{}", "● For type conversion and function matching errors:".bold());

        if categories.contains(&"type_conversion".to_string()) {
            println!("  - Types are incompatible for implicit conversion");
            println!("  - Use explicit casts: `static_cast<TargetType>(value)`");
            println!("  - For custom types, consider adding conversion operators or constructors");
            println!("  - Be careful with numeric conversions that might lose precision");
        }

        if categories.contains(&"no_matching_function".to_string()) || categories.contains(&"overload_resolution".to_string()) {
            println!("  - No function exactly matches the argument types you provided");
            println!("  - Check function parameter types and ensure they match your arguments");
            println!("  - Look at compiler suggestions for valid function signatures");
            println!("  - Try explicitly casting arguments to match the expected types");
        }

        if categories.contains(&"ambiguous_call".to_string()) {
            println!("  - Multiple overloaded functions could match these arguments");
            println!("  - Use explicit casts to select a specific overload");
            println!("  - Make function calls more specific to avoid ambiguity");
        }

        if categories.contains(&"type_deduction".to_string()) {
            println!("  - Auto type deduction or template argument deduction failed");
            println!("  - Specify types explicitly instead of relying on deduction");
            println!("  - Check that expressions have well-defined types");
        }

        if categories.contains(&"incomplete_type".to_string()) {
            println!("  - Using a type that's only forward-declared, not fully defined");
            println!("  - Include the complete definition before using the type");
            println!("  - Forward declarations only work for pointers/references, not full objects");
        }

        println!("  - Consider using `auto` for complex types or template results");
        println!("  - Use `decltype` to refer to the exact type of another variable");
        println!("  - Check for const/volatile qualifiers that might affect matching");
        println!();
        printed_help = true;
    }

    // --- Concept Errors ---
    if categories.iter().any(|c| c.starts_with("concept_")) {
        println!("{}", "● For C++20 concept and constraints errors:".bold());

        println!("  - Define concepts before using them in requires clauses");
        println!("  - Example: `template<typename T> concept Addable = requires(T a, T b) {{ a + b; }};`");
        println!("  - Make sure type constraints are satisfied by the provided types");
        println!("  - Check if you need to include additional headers for concept definitions");
        println!("  - Concept requirements are strictly checked - no implicit conversions");
        println!("  - Use `static_assert` with concepts to provide better error messages");
        println!();
        printed_help = true;
    }

    // --- Initialization Errors ---
    if categories.contains(&"initialization".to_string()) || categories.contains(&"constructor_init".to_string()) {
        println!("{}", "● For initialization errors:".bold());

        println!("  - Check constructor initializer list syntax: `Constructor() : member(value) `");
        println!("  - Members should be initialized in the same order as declared in the class");
        println!("  - Initialize all members either in the initializer list or in the constructor body");
        println!("  - Use uniform initialization syntax with braces for clearer initialization");
        println!("  - Remember that class members without initializers get default-initialized");
        println!();
        printed_help = true;
    }

    // --- Lambda Errors ---
    if categories.iter().any(|c| c.starts_with("lambda_")) {
        println!("{}", "● For lambda expression errors:".bold());

        println!("  - Capture variables from enclosing scope that you use inside the lambda");
        println!("  - Use [=] to capture by value or [&] to capture by reference");
        println!("  - For specific variables: [x, &y] captures x by value, y by reference");
        println!("  - Capture this pointer explicitly with [this] if needed");
        println!("  - Lambda parameters and return type can be explicitly specified");
        println!("  - Mutable lambdas allow modifying captured-by-value variables");
        println!();
        printed_help = true;
    }

    // --- STL Errors ---
    if categories.contains(&"stl".to_string()) || categories.iter().any(|c| c.starts_with("stl_")) {
        println!("{}", "● For Standard Library (STL) errors:".bold());

        if categories.contains(&"stl_iterator".to_string()) {
            println!("  - Don't dereference end() iterators or invalidated iterators");
            println!("  - Operations like erase() invalidate iterators to the erased element");
            println!("  - Container modifications may invalidate iterators (especially for vector)");
            println!("  - Use iterator ranges carefully: [begin, end) where end is not included");
        }

        if categories.contains(&"stl_out_of_range".to_string()) {
            println!("  - Array or container access is out of valid range");
            println!("  - Use .at() instead of [] for bounds checking");
            println!("  - Always check container size before accessing elements");
        }

        println!("  - Use container member functions like find() instead of algorithms when available");
        println!("  - STL algorithms expect specific iterator categories (input, forward, etc.)");
        println!("  - Use std::make_shared and std::make_unique for smart pointers");
        println!();
        printed_help = true;
    }

    // --- Memory Errors ---
    if categories.iter().any(|c| c.starts_with("memory")) {
        println!("{}", "● For memory management errors:".bold());

        println!("  - Match each new with exactly one delete (or use smart pointers)");
        println!("  - Use new[] with delete[] for arrays, regular new with delete for single objects");
        println!("  - Check for null pointers before dereferencing");
        println!("  - Consider using smart pointers instead of raw pointers");
        println!("  - std::unique_ptr for exclusive ownership");
        println!("  - std::shared_ptr for shared ownership");
        println!("  - std::weak_ptr for temporary references to shared objects");
        println!();
        printed_help = true;
    }

    // --- Linker Errors ---
    if categories.contains(&"linker".to_string()) || categories.iter().any(|c| c.starts_with("linker_")) {
        println!("{}", "● For linker errors:".bold());

        if categories.contains(&"undefined_symbol".to_string()) {
            println!("  - Symbol is declared but not defined or not included in the build");
            println!("  - Make sure implementation files (.cpp) are included in your build");
            println!("  - Check that function signatures match exactly between declaration and definition");
            println!("  - Template definitions must be visible at point of instantiation");
        }

        if categories.contains(&"linker_duplicate".to_string()) {
            println!("  - Multiple definitions of the same symbol");
            println!("  - Non-inline functions should be defined only once across all translation units");
            println!("  - Don't define functions or variables in header files without inline/static");
            println!("  - Use include guards or #pragma once in headers");
        }

        println!("  - Ensure all necessary libraries are linked (add them in cforge.toml)");
        println!("  - Check library order - sometimes order matters for dependencies");
        println!("  - Run `cforge deps` to install all dependencies");
        println!();
        printed_help = true;
    }

    // --- Include and File Errors ---
    if categories.contains(&"missing_file".to_string()) {
        println!("{}", "● For missing file errors:".bold());

        println!("  - Check file paths and names for typos");
        println!("  - Use angle brackets for system headers: #include <vector>");
        println!("  - Use quotes for your own headers: #include \"myheader.h\"");
        println!("  - Make sure include directories are correctly set in cforge.toml");
        println!("  - Verify that dependencies are installed: `cforge deps`");
        println!("  - Check relative paths if using non-standard include structures");
        println!();
        printed_help = true;
    }

    // --- Preprocessor Errors ---
    if categories.contains(&"preprocessor".to_string()) {
        println!("{}", "● For preprocessor errors:".bold());

        println!("  - Use include guards or #pragma once in header files");
        println!("  - Each #if must have a matching #endif");
        println!("  - Check for recursive includes that might cause problems");
        println!("  - Macros are textual replacements - watch for unexpected expansions");
        println!("  - Use () around macro parameters to avoid operator precedence issues");
        println!("  - Consider using constexpr and inline functions instead of macros");
        println!();
        printed_help = true;
    }

    // --- Syntax Errors ---
    if categories.contains(&"syntax".to_string()) || categories.contains(&"missing_semicolon".to_string()) {
        println!("{}", "● For syntax errors:".bold());

        println!("  - Check for missing semicolons at the end of statements");
        println!("  - Ensure braces {{}} are properly balanced");
        println!("  - Parentheses () must match for function calls and conditions");
        println!("  - Watch for typos in keywords and operators");
        println!("  - Class definitions end with a semicolon after the closing brace");
        println!("  - C++ is case-sensitive (myVar != MyVar)");
        println!();
        printed_help = true;
    }

    // --- Modern C++ Feature Errors ---
    if categories.contains(&"auto_type".to_string()) ||
        categories.contains(&"move_semantics".to_string()) ||
        categories.contains(&"concepts".to_string()) ||
        categories.contains(&"if_constexpr".to_string()) {
        println!("{}", "● For modern C++ feature errors:".bold());

        if categories.contains(&"auto_type".to_string()) {
            println!("  - auto requires an initializer with a deducible type");
            println!("  - auto with initializer lists requires explicit type: auto x = {{1, 2, 3}}; // error");
        }

        if categories.contains(&"move_semantics".to_string()) {
            println!("  - Use std::move() to convert to rvalue references");
            println!("  - Don't use moved-from objects except to reassign or destroy them");
            println!("  - Rule of five: if you need one, you usually need all five special members");
        }

        if categories.contains(&"concepts".to_string()) {
            println!("  - Concepts require C++20 support - check your standard version");
            println!("  - Define concepts before using them in requires clauses");
        }

        if categories.contains(&"if_constexpr".to_string()) {
            println!("  - if constexpr requires a constant expression condition");
            println!("  - Use to conditionally compile code based on template parameters");
        }

        println!("  - Check your compiler supports the C++ standard you're using");
        println!("  - Some features require additional headers (e.g., <concepts>)");
        println!();
        printed_help = true;
    }

    // --- Build System Errors ---
    if categories.contains(&"build_system".to_string()) {
        println!("{}", "● For build system errors:".bold());

        println!("  - Check your cforge.toml for syntax errors");
        println!("  - Ensure all tools (CMake, compilers) are correctly installed");
        println!("  - Try running `cforge clean` and then build again");
        println!("  - Check build generator compatibility with your system");
        println!("  - Verify that dependencies are installed: `cforge deps`");
        println!("  - Make sure source file patterns match your project structure");
        println!();
        printed_help = true;
    }

    // General help if we couldn't categorize the errors or as a fallback
    if !printed_help || categories.contains(&"general".to_string()) {
        println!("{}", "● General troubleshooting:".bold());
        println!("  - Check for missing semicolons or unbalanced brackets");
        println!("  - Ensure all variables are declared before use");
        println!("  - Verify that required headers are included");
        println!("  - Look for mismatched types in function calls and assignments");
        println!("  - Check that you're including the correct libraries in cforge.toml");
        println!("  - Try `cforge clean` followed by `cforge build`");
        println!("  - Read error messages from top to bottom - earlier errors often cause later ones");
        println!();
    }

    // Add documentation pointer
    println!("For more detailed C++ language help, see: {}", "https://en.cppreference.com/".underline());
    println!("For compiler-specific error assistance:");
    println!("  - GCC/Clang: {}", "https://gcc.gnu.org/onlinedocs/".underline());
    println!("  - MSVC: {}", "https://docs.microsoft.com/en-us/cpp/error-messages/".underline());
    println!("For cforge documentation, run: `cforge --help`");
}

pub fn add_error_suggestions(stdout: &str, stderr: &str) {
    let combined = format!("{}\n{}", stdout, stderr);

    // Look for common error patterns and provide suggestions
    let mut suggestions = Vec::new();

    if combined.contains("undefined reference") || combined.contains("unresolved external symbol") {
        suggestions.push("• Check library linking settings in your cforge.toml");
        suggestions.push("• Make sure all dependencies are installed with 'cforge deps'");
        suggestions.push("• Verify that all required libraries are in your PATH or system paths");
    }

    if combined.contains("No such file or directory") || combined.contains("cannot find") ||
        combined.contains("not found") {
        suggestions.push("• Verify include paths in your cforge.toml");
        suggestions.push("• Make sure all dependencies are installed with 'cforge deps'");
        suggestions.push("• Check file paths for typos");
    }

    if combined.contains("constexpr") && combined.contains("not a literal type") {
        suggestions.push("• Add a constexpr constructor to your class");
        suggestions.push("• Example: 'constexpr ClassName() = default;'");
    }

    if combined.contains("template parameter pack") {
        suggestions.push("• Move the variadic template parameter to the end of the parameter list");
        suggestions.push("• Example: Change 'template<typename... Ts, typename U>' to 'template<typename U, typename... Ts>'");
    }

    if combined.contains("undeclared identifier") {
        suggestions.push("• Make sure the variable is declared before use");
        suggestions.push("• Check for typos in variable names");
        suggestions.push("• Verify that required headers are included");
    }

    if combined.contains("does not name a non-static data member") {
        suggestions.push("• Declare the member variable in your class definition");
        suggestions.push("• Example: 'YourType m_member;' in the class body");
    }

    if !suggestions.is_empty() {
        println!();
        print_status("Suggestions to fix the errors:");
        for suggestion in suggestions {
            print_substep(suggestion);
        }
        println!();
    }
}

pub fn try_fix_cmake_errors(build_path: &Path, is_msvc_style: bool) -> bool {
    let mut fixed_something = false;

    // Check for CMakeCache.txt
    let cache_path = build_path.join("CMakeCache.txt");
    if cache_path.exists() {
        // Remove CMakeCache.txt to force CMake to reconfigure
        if let Err(e) = fs::remove_file(&cache_path) {
            println!("{}", format!("Warning: Could not remove CMakeCache.txt: {}", e).yellow());
        } else {
            println!("{}", "Removed CMakeCache.txt to force reconfiguration.".green());
            fixed_something = true;
        }
    }

    // Check for CMakeFiles directory
    let cmake_files_path = build_path.join("CMakeFiles");
    if cmake_files_path.exists() {
        // Remove CMakeFiles directory to force CMake to reconfigure
        if let Err(e) = fs::remove_dir_all(&cmake_files_path) {
            println!("{}", format!("Warning: Could not remove CMakeFiles directory: {}", e).yellow());
        } else {
            println!("{}", "Removed CMakeFiles directory to force reconfiguration.".green());
            fixed_something = true;
        }
    }

    // Create a minimal CMakeLists.txt in build directory to test CMake
    let test_cmake_path = build_path.join("test_cmake.txt");
    let test_content = "cmake_minimum_required(VERSION 3.10)\nproject(test_cmake)\n";
    if let Err(e) = fs::write(&test_cmake_path, test_content) {
        println!("{}", format!("Warning: Could not create test CMake file: {}", e).yellow());
    } else {
        println!("{}", "Created test CMake file to check configuration.".green());

        // Try to run a minimal CMake command to verify it works
        let mut test_cmd = vec!["cmake".to_string(), "-P".to_string(), "test_cmake.txt".to_string()];
        match Command::new(&test_cmd[0])
            .args(&test_cmd[1..])
            .current_dir(build_path)
            .output() {
            Ok(_) => {
                println!("{}", "CMake appears to be working.".green());
                fixed_something = true;
            },
            Err(e) => {
                println!("{}", format!("Warning: CMake test failed: {}", e).yellow());
            }
        }

        // Remove test file
        if let Err(e) = fs::remove_file(&test_cmake_path) {
            println!("{}", format!("Warning: Could not remove test CMake file: {}", e).yellow());
        }
    }

    // For MSVC, try to configure environment variables
    if is_msvc_style {
        println!("{}", "Trying to set up MSVC environment variables...".blue());

        // Try to find vcvarsall.bat
        let possible_paths = [
            r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvarsall.bat",
            r"C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat",
        ];

        for path in &possible_paths {
            if Path::new(path).exists() {
                println!("{}", format!("Found vcvarsall.bat at: {}", path).green());

                // Create a batch file to set up the environment
                let batch_path = build_path.join("setup_env.bat");
                let batch_content = format!(
                    "@echo off\n\"{}\" x64\necho Environment set up for MSVC\n",
                    path
                );

                if let Err(e) = fs::write(&batch_path, batch_content) {
                    println!("{}", format!("Warning: Could not create environment setup batch file: {}", e).yellow());
                } else {
                    println!("{}", "Created environment setup batch file. Please run it before building.".green());
                    fixed_something = true;
                }

                break;
            }
        }
    }

    fixed_something
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