use std::{env, fs};
use std::io::Write;
use std::path::Path;
use std::sync::{Arc, Mutex};
use crate::config::{PCHConfig, PackageInstallState, ProjectConfig};
use crate::output_utils::TimedProgressBar;

pub fn is_executable(path: &Path) -> bool {
    // First check if the file exists
    if !path.exists() || !path.is_file() {
        return false;
    }

    // Extract extension and filename to check if this is a source file
    // We don't want to consider source files as executables
    let extension = path.extension().and_then(|e| e.to_str()).unwrap_or("");
    let file_name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");

    // Skip source files and CMake files
    if extension == "c" || extension == "cpp" || extension == "h" || extension == "hpp" ||
        file_name.contains("CMakeCCompilerId") || file_name.contains("CMakeCXXCompilerId") ||
        file_name == "cmake_install.cmake" || file_name == "CMakeCache.txt" {
        return false;
    }

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        if let Ok(metadata) = fs::metadata(path) {
            // On Unix, check execute permission
            return metadata.permissions().mode() & 0o111 != 0;
        }
        false
    }

    #[cfg(windows)]
    {
        // On Windows, consider files with .exe, .bat, .cmd extensions as executable
        // or files without an extension if they're in a standard executable location
        if extension == "exe" || extension == "bat" || extension == "cmd" {
            return true;
        }

        // Without extension, check if it's in a bin directory
        if extension.is_empty() {
            let parent = path.parent().unwrap_or(Path::new(""));
            let parent_name = parent.file_name().and_then(|n| n.to_str()).unwrap_or("");
            if parent_name == "bin" || parent_name.contains("Debug") || parent_name.contains("Release") {
                // More likely to be an executable in these directories
                return true;
            }
        }

        // For Windows, if we can't determine clearly, try the best guess:
        // check if file size is reasonable for an executable and not tiny like a shell script
        if let Ok(metadata) = fs::metadata(path) {
            let file_size = metadata.len();
            // An executable file is typically at least a few KB
            return file_size > 1000;
        }

        false
    }

    #[cfg(not(any(unix, windows)))]
    {
        // On other platforms, check extension and location as best guess
        if extension == "exe" || extension.is_empty() {
            return true;
        }
        false
    }
}

pub fn is_build_command(cmd: &[String]) -> bool {
    if cmd.is_empty() {
        return false;
    }

    // Check if it's a CMake build command
    if cmd[0] == "cmake" && cmd.len() > 1 {
        return cmd.iter().any(|arg| arg == "--build");
    }

    // Check for other build commands
    matches!(cmd[0].as_str(), "make" | "ninja" | "cl" | "clang" | "g++" | "gcc" | "MSBuild.exe")
}

pub fn prompt(message: &str) -> Result<String, Box<dyn std::error::Error>> {
    print!("{}", message);
    std::io::stdout().flush()?;

    let mut input = String::new();
    std::io::stdin().read_line(&mut input)?;

    Ok(input)
}

pub fn progress_bar(message: &str) -> TimedProgressBar {
    // Create a timed progress bar with a reasonable expected duration
    TimedProgressBar::start(message, 30) // 30 seconds default timeout
}

pub fn ensure_cmake_directory(project_path: &Path, project_name: &str) -> Result<(), Box<dyn std::error::Error>> {
    let cmake_dir = project_path.join("cmake");

    // Create cmake directory if it doesn't exist
    if !cmake_dir.exists() {
        fs::create_dir_all(&cmake_dir)?;
    }

    // Create Config.cmake.in file if it doesn't exist
    let config_in_path = cmake_dir.join(format!("{}Config.cmake.in", project_name));
    if !config_in_path.exists() {
        let config_content = format!(r#"@PACKAGE_INIT@

include("${{CMAKE_CURRENT_LIST_DIR}}/{}Targets.cmake")

check_required_components({})
"#, project_name, project_name);

        fs::write(&config_in_path, config_content)?;
    }

    Ok(())
}

pub fn expand_output_tokens(s: &str, config: &ProjectConfig) -> String {
    let config_val = config.build.default_config.as_deref().unwrap_or("Debug");
    let os_val = if cfg!(windows) { "windows" } else if cfg!(target_os = "macos") { "darwin" } else { "linux" };
    let arch_val = if cfg!(target_arch = "x86_64") { "x64" } else if cfg!(target_arch = "x86") { "x86" } else { "arm64" };
    s.replace("${CONFIG}", config_val)
        .replace("${OS}", os_val)
        .replace("${ARCH}", arch_val)
}

pub fn add_pch_support(
    cmake_content: &mut Vec<String>,
    config: &ProjectConfig,
    pch_config: &PCHConfig
) {
    if !pch_config.enabled {
        return;
    }

    cmake_content.push("# Precompiled header support".to_string());
    // Define PCH variables
    cmake_content.push(format!("set(PCH_HEADER \"{}\")", pch_config.header));

    if let Some(source) = &pch_config.source {
        cmake_content.push(format!("set(PCH_SOURCE \"{}\")", source));
    }

    // Add exclude patterns if specified
    if let Some(excludes) = &pch_config.exclude_sources {
        cmake_content.push("set(PCH_EXCLUDE_SOURCES".to_string());
        for exclude in excludes {
            cmake_content.push(format!("  \"{}\"", exclude));
        }
        cmake_content.push(")".to_string());
    }

    // Unity build option
    if let Some(disable_unity) = pch_config.disable_unity_build {
        if disable_unity {
            cmake_content.push("set(CMAKE_UNITY_BUILD OFF)".to_string());
        }
    }

    cmake_content.push("# PCH function for targets".to_string());
    cmake_content.push("function(target_enable_pch target_name)".to_string());

    // Check if target is in only_for_targets list
    if let Some(only_targets) = &pch_config.only_for_targets {
        cmake_content.push("  # Check if target is in the allowed targets list".to_string());
        cmake_content.push("  set(_apply_pch FALSE)".to_string());
        cmake_content.push("  foreach(_target IN ITEMS".to_string());
        for target in only_targets {
            cmake_content.push(format!("    \"{}\"", target));
        }
        cmake_content.push("  )".to_string());
        cmake_content.push("    if(\"${target_name}\" STREQUAL \"${_target}\")".to_string());
        cmake_content.push("      set(_apply_pch TRUE)".to_string());
        cmake_content.push("    endif()".to_string());
        cmake_content.push("  endforeach()".to_string());
        cmake_content.push("  if(NOT _apply_pch)".to_string());
        cmake_content.push("    message(STATUS \"Skipping PCH for ${target_name} (not in target list)\")".to_string());
        cmake_content.push("    return()".to_string());
        cmake_content.push("  endif()".to_string());
    }

    // Modern CMake PCH approach (CMake 3.16+)
    cmake_content.push("  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)".to_string());

    // If we have both header and source (for MSVC style)
    if pch_config.source.is_some() {
        cmake_content.push("    if(MSVC)".to_string());
        cmake_content.push("      target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());

        // Add exclude patterns if necessary
        if pch_config.exclude_sources.is_some() {
            cmake_content.push("      if(PCH_EXCLUDE_SOURCES)".to_string());
            cmake_content.push("        set_source_files_properties(${PCH_EXCLUDE_SOURCES}".to_string());
            cmake_content.push("          PROPERTIES SKIP_PRECOMPILE_HEADERS ON)".to_string());
            cmake_content.push("      endif()".to_string());
        }

        cmake_content.push("    else()".to_string());
        cmake_content.push("      target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());

        // Add exclude patterns for non-MSVC as well
        if pch_config.exclude_sources.is_some() {
            cmake_content.push("      if(PCH_EXCLUDE_SOURCES)".to_string());
            cmake_content.push("        set_source_files_properties(${PCH_EXCLUDE_SOURCES}".to_string());
            cmake_content.push("          PROPERTIES SKIP_PRECOMPILE_HEADERS ON)".to_string());
            cmake_content.push("      endif()".to_string());
        }

        cmake_content.push("    endif()".to_string());
    } else {
        // Header-only PCH
        cmake_content.push("    target_precompile_headers(${target_name} PRIVATE \"${PCH_HEADER}\")".to_string());

        // Add exclude patterns
        if pch_config.exclude_sources.is_some() {
            cmake_content.push("    if(PCH_EXCLUDE_SOURCES)".to_string());
            cmake_content.push("      set_source_files_properties(${PCH_EXCLUDE_SOURCES}".to_string());
            cmake_content.push("        PROPERTIES SKIP_PRECOMPILE_HEADERS ON)".to_string());
            cmake_content.push("    endif()".to_string());
        }
    }

    // Add any additional compiler options
    if let Some(options) = &pch_config.compiler_options {
        if !options.is_empty() {
            cmake_content.push("    # Add additional compiler options for PCH".to_string());

            let opts_string = options.iter()
                .map(|opt| format!("      \"{}\"", opt))
                .collect::<Vec<_>>()
                .join("\n");

            cmake_content.push("    target_compile_options(${target_name} PRIVATE".to_string());
            cmake_content.push(opts_string);
            cmake_content.push("    )".to_string());
        }
    }

    // Fallback for older CMake versions
    cmake_content.push("  else()".to_string());
    cmake_content.push("    # Fallback for older CMake - compiler-specific flags".to_string());
    cmake_content.push("    if(MSVC)".to_string());
    cmake_content.push("      get_filename_component(PCH_NAME ${PCH_HEADER} NAME_WE)".to_string());
    cmake_content.push("      set(PCH_OUTPUT \"${CMAKE_CURRENT_BINARY_DIR}/${PCH_NAME}.pch\")".to_string());
    cmake_content.push("      # Set compiler flags for using PCH".to_string());
    cmake_content.push("      target_compile_options(${target_name} PRIVATE /Yu\"${PCH_HEADER}\" /Fp\"${PCH_OUTPUT}\")".to_string());

    // Handle separate source file for creating the PCH
    if let Some(_) = &pch_config.source {
        cmake_content.push("      # Set PCH creation flag on the source file that will create the PCH".to_string());
        cmake_content.push("      set_source_files_properties(${PCH_SOURCE} PROPERTIES COMPILE_FLAGS \"/Yc\\\"${PCH_HEADER}\\\" /Fp\\\"${PCH_OUTPUT}\\\"\")".to_string());
    }

    // Add excluded files for MSVC
    if pch_config.exclude_sources.is_some() {
        cmake_content.push("      # Exclude specified files from using PCH".to_string());
        cmake_content.push("      if(PCH_EXCLUDE_SOURCES)".to_string());
        cmake_content.push("        foreach(_src ${PCH_EXCLUDE_SOURCES})".to_string());
        cmake_content.push("          set_source_files_properties(${_src} PROPERTIES COMPILE_FLAGS \"/Y-\")".to_string());
        cmake_content.push("        endforeach()".to_string());
        cmake_content.push("      endif()".to_string());
    }

    // GCC/Clang fallback for older CMake
    cmake_content.push("    elseif(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES \"Clang\")".to_string());
    cmake_content.push("      # GCC/Clang PCH for older CMake requires custom command setup".to_string());
    cmake_content.push("      message(STATUS \"Using manual PCH setup for GCC/Clang with CMake < 3.16\")".to_string());

    // Attempt a basic setup for GCC in older CMake
    cmake_content.push("      if(CMAKE_COMPILER_IS_GNUCXX)".to_string());
    cmake_content.push("        # Create a precompiled header from the given header file".to_string());
    cmake_content.push("        get_filename_component(PCH_NAME ${PCH_HEADER} NAME)".to_string());
    cmake_content.push("        get_filename_component(PCH_DIR ${PCH_HEADER} DIRECTORY)".to_string());
    cmake_content.push("        set(PCH_BINARY \"${CMAKE_CURRENT_BINARY_DIR}/${PCH_NAME}.gch\")".to_string());

    // Add compile command for PCH
    cmake_content.push("        add_custom_command(OUTPUT ${PCH_BINARY}".to_string());
    cmake_content.push("          COMMAND ${CMAKE_CXX_COMPILER} -x c++-header ${CMAKE_CXX_FLAGS} -o ${PCH_BINARY} ${PCH_HEADER}".to_string());
    cmake_content.push("          DEPENDS ${PCH_HEADER})".to_string());

    // Add custom target for the PCH
    cmake_content.push("        add_custom_target(${target_name}_pch DEPENDS ${PCH_BINARY})".to_string());
    cmake_content.push("        add_dependencies(${target_name} ${target_name}_pch)".to_string());

    // Add include directory if needed
    cmake_content.push("        target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})".to_string());

    // Add compiler flags for using the PCH
    cmake_content.push("        target_compile_options(${target_name} PRIVATE -include ${PCH_NAME})".to_string());
    cmake_content.push("      endif()".to_string());

    cmake_content.push("    endif()".to_string());
    cmake_content.push("  endif()".to_string());
    cmake_content.push("endfunction()".to_string());

    cmake_content.push(String::new());
}

pub fn extract_percentage(line: &str) -> Option<f32> {
    if let Some(percent_idx) = line.find('%') {
        // Look backward from % for digits
        let mut start_idx = percent_idx;
        while start_idx > 0 {
            let prev_char = line.chars().nth(start_idx - 1).unwrap_or(' ');
            if prev_char.is_digit(10) || prev_char == '.' {
                start_idx -= 1;
            } else {
                break;
            }
        }

        // Try to parse the percentage
        if start_idx < percent_idx {
            if let Ok(percent) = line[start_idx..percent_idx].parse::<f32>() {
                return Some(percent);
            }
        }
    }

    None
}

pub fn extract_package_name(line: &str) -> Option<String> {
    if let Some(start_idx) = line.find("Starting package ") {
        let start = start_idx + "Starting package ".len();
        if let Some(end_idx) = line[start..].find(':') {
            return Some(line[start..start+end_idx].trim().to_string());
        }
    }

    // Try another format
    if line.contains("Building package ") {
        let parts: Vec<&str> = line.split_whitespace().collect();
        if parts.len() >= 3 {
            return Some(parts[2].trim_matches(|c| c == ':' || c == '[' || c == ']').to_string());
        }
    }

    None
}

pub fn extract_percentage_from_output(line: &str) -> Option<f32> {
    // Regular expressions to match common progress indicators

    // Pattern: "XX% complete" or "XX % done" etc.
    if let Some(percent_pos) = line.find('%') {
        if percent_pos > 0 {
            // Look backwards from % for digits
            let mut start_pos = percent_pos;
            while start_pos > 0 {
                let char_before = line.chars().nth(start_pos - 1).unwrap_or(' ');
                if char_before.is_digit(10) || char_before == '.' {
                    start_pos -= 1;
                } else {
                    break;
                }
            }

            if start_pos < percent_pos {
                // Extract the number
                let number_str = &line[start_pos..percent_pos];
                if let Ok(percent) = number_str.parse::<f32>() {
                    return Some(percent);
                }
            }
        }
    }

    // Pattern: "Processing X/Y"
    if line.contains('/') {
        let parts: Vec<&str> = line.split('/').collect();
        if parts.len() >= 2 {
            // Try to extract numbers before and after the slash
            let before_slash = parts[0].split_whitespace().last().unwrap_or("");
            let after_slash = parts[1].split_whitespace().next().unwrap_or("");

            if let (Ok(numerator), Ok(denominator)) = (before_slash.parse::<f32>(), after_slash.parse::<f32>()) {
                if denominator > 0.0 {
                    return Some((numerator / denominator) * 100.0);
                }
            }
        }
    }

    // Progress indicators like "Stage X of Y"
    if line.contains(" of ") {
        let parts: Vec<&str> = line.split(" of ").collect();
        if parts.len() >= 2 {
            // Look for numbers in the parts
            let current_part = parts[0].split_whitespace().last().unwrap_or("");
            let total_part = parts[1].split_whitespace().next().unwrap_or("");

            if let (Ok(current), Ok(total)) = (current_part.parse::<f32>(), total_part.parse::<f32>()) {
                if total > 0.0 {
                    return Some((current / total) * 100.0);
                }
            }
        }
    }

    None
}

pub fn expand_env_vars(input: &str) -> String {
    let mut result = input.to_string();

    // Find environment variables in the format $VAR or ${VAR}
    let env_var_regex = regex::Regex::new(r"\$\{?([a-zA-Z_][a-zA-Z0-9_]*)\}?").unwrap();

    for cap in env_var_regex.captures_iter(input) {
        let var_name = &cap[1];
        let var_pattern = &cap[0];

        if let Ok(var_value) = env::var(var_name) {
            result = result.replace(var_pattern, &var_value);
        }
    }

    result
}

pub fn parse_vcpkg_output(line: &str, state: &Arc<Mutex<PackageInstallState>>) {
    let mut state = state.lock().unwrap();

    // Check for package start
    if line.contains("Starting package ") {
        if let Some(pkg_name) = extract_package_name(line) {
            state.current_package = pkg_name;
            state.current_percentage = 0.0;
        }
    }
    // Check for package completion
    else if line.contains("Building package ") && line.contains("succeeded") {
        state.packages_completed += 1;
        state.current_package = String::new();
        state.current_percentage = 0.0;
    }
    // Look for percentage indicators
    else if line.contains("%") {
        if let Some(percentage) = extract_percentage(line) {
            if percentage > state.current_percentage {
                state.current_percentage = percentage;
            }
        }
    }
    // Look for specific stages
    else if line.contains("Downloading ") || line.contains("Extracting ") {
        // These are early stages - around 10-20%
        if state.current_percentage < 20.0 {
            state.current_percentage = 20.0;
        }
    }
    else if line.contains("Configuring ") {
        // Configuration stage - around 30-40%
        if state.current_percentage < 40.0 {
            state.current_percentage = 40.0;
        }
    }
    else if line.contains("Building ") {
        // Building stage - around 50-80%
        if state.current_percentage < 50.0 {
            state.current_percentage = 50.0;
        }
    }
    else if line.contains("Installing ") {
        // Installing stage - around 90%
        if state.current_percentage < 90.0 {
            state.current_percentage = 90.0;
        }
    }
}
