use std::collections::HashMap;
use std::env;
use std::path::Path;
use crate::config::{CrossCompileConfig, ProjectConfig};
use crate::expand_env_vars;

pub fn get_predefined_cross_target(target_name: &str) -> Option<CrossCompileConfig> {
    match target_name {
        "android-arm64" => Some(CrossCompileConfig {
            enabled: true,
            target: "aarch64-linux-android".to_string(),
            toolchain: None,
            sysroot: None,
            cmake_toolchain_file: Some("$ANDROID_NDK/build/cmake/android.toolchain.cmake".to_string()),
            define_prefix: Some("ANDROID".to_string()),
            flags: Some(vec![
                "-DANDROID_ABI=arm64-v8a".to_string(),
                "-DANDROID_PLATFORM=android-24".to_string(),
                "-DANDROID_STL=c++_shared".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "android-arm" => Some(CrossCompileConfig {
            enabled: true,
            target: "armv7a-linux-androideabi".to_string(),
            toolchain: None,
            sysroot: None,
            cmake_toolchain_file: Some("$ANDROID_NDK/build/cmake/android.toolchain.cmake".to_string()),
            define_prefix: Some("ANDROID".to_string()),
            flags: Some(vec![
                "-DANDROID_ABI=armeabi-v7a".to_string(),
                "-DANDROID_PLATFORM=android-19".to_string(),
                "-DANDROID_STL=c++_shared".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "ios" => Some(CrossCompileConfig {
            enabled: true,
            target: "arm64-apple-ios".to_string(),
            toolchain: None,
            sysroot: None,
            cmake_toolchain_file: None,
            define_prefix: Some("IOS".to_string()),
            flags: Some(vec![
                "-DCMAKE_SYSTEM_NAME=iOS".to_string(),
                "-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0".to_string(),
                "-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO".to_string(),
                "-DCMAKE_XCODE_ATTRIBUTE_ENABLE_BITCODE=YES".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "raspberry-pi" => Some(CrossCompileConfig {
            enabled: true,
            target: "arm-linux-gnueabihf".to_string(),
            toolchain: Some("arm-linux-gnueabihf".to_string()),
            sysroot: None,
            cmake_toolchain_file: None,
            define_prefix: Some("RASPBERRY_PI".to_string()),
            flags: Some(vec![
                "-DCMAKE_SYSTEM_NAME=Linux".to_string(),
                "-DCMAKE_SYSTEM_PROCESSOR=arm".to_string(),
                "-DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc".to_string(),
                "-DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        "wasm" => Some(CrossCompileConfig {
            enabled: true,
            target: "wasm32-unknown-emscripten".to_string(),
            toolchain: Some("emscripten".to_string()),
            sysroot: None,
            cmake_toolchain_file: Some("$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake".to_string()),
            define_prefix: Some("WASM".to_string()),
            flags: Some(vec![
                "-DCMAKE_SYSTEM_NAME=Emscripten".to_string(),
            ]),
            env_vars: Some(HashMap::new()),
        }),
        _ => None,
    }
}

pub fn setup_cross_compilation(config: &ProjectConfig, cross_config: &CrossCompileConfig) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut cmake_options = Vec::new();

    // Add toolchain file if provided
    if let Some(toolchain_file) = &cross_config.cmake_toolchain_file {
        // Expand environment variables in toolchain file path
        let expanded_path = expand_env_vars(toolchain_file);
        cmake_options.push(format!("-DCMAKE_TOOLCHAIN_FILE={}", expanded_path));
    }

    // Add target-specific flags
    if let Some(flags) = &cross_config.flags {
        cmake_options.extend(flags.clone());
    }

    // Add sysroot if provided
    if let Some(sysroot) = &cross_config.sysroot {
        // Expand environment variables in sysroot path
        let expanded_sysroot = expand_env_vars(sysroot);
        cmake_options.push(format!("-DCMAKE_SYSROOT={}", expanded_sysroot));
    }

    // Add define prefix if provided (useful for conditional compilation)
    if let Some(prefix) = &cross_config.define_prefix {
        cmake_options.push(format!("-D{}=1", prefix));
    }

    // If no toolchain file is provided but toolchain name is, set explicit compiler paths
    if cross_config.cmake_toolchain_file.is_none() && cross_config.toolchain.is_some() {
        let toolchain = cross_config.toolchain.as_ref().unwrap();

        // Set C and C++ compilers
        cmake_options.push(format!("-DCMAKE_C_COMPILER={}-gcc", toolchain));
        cmake_options.push(format!("-DCMAKE_CXX_COMPILER={}-g++", toolchain));

        // Set other tools
        cmake_options.push(format!("-DCMAKE_AR={}-ar", toolchain));
        cmake_options.push(format!("-DCMAKE_RANLIB={}-ranlib", toolchain));
        cmake_options.push(format!("-DCMAKE_STRIP={}-strip", toolchain));
    }

    // Set system name and processor if not already set by flags
    if !cmake_options.iter().any(|opt| opt.starts_with("-DCMAKE_SYSTEM_NAME=")) {
        let target_parts: Vec<&str> = cross_config.target.split('-').collect();
        if target_parts.len() >= 3 {
            let arch = target_parts[0];
            let vendor = target_parts[1];
            let sys = target_parts[2];

            let system_name = match sys {
                "windows" => "Windows",
                "darwin" | "ios" | "macos" => "Darwin",
                "android" => "Android",
                _ => "Linux",
            };

            cmake_options.push(format!("-DCMAKE_SYSTEM_NAME={}", system_name));
            cmake_options.push(format!("-DCMAKE_SYSTEM_PROCESSOR={}", arch));
        }
    }

    Ok(cmake_options)
}

pub fn get_cross_compilation_env(cross_config: &CrossCompileConfig) -> HashMap<String, String> {
    let mut env_vars = HashMap::new();

    // Add environment variables specified in config
    if let Some(vars) = &cross_config.env_vars {
        for (key, value) in vars {
            env_vars.insert(key.clone(), expand_env_vars(value));
        }
    }

    // Add common environment variables based on target
    match cross_config.target.as_str() {
        "aarch64-linux-android" | "armv7a-linux-androideabi" => {
            // Add Android SDK/NDK environment variables if not already set
            if !env_vars.contains_key("ANDROID_NDK") && env::var("ANDROID_NDK").is_err() {
                // Try to find Android NDK in common locations
                for path in &[
                    "$ANDROID_HOME/ndk-bundle",
                    "$ANDROID_HOME/ndk/latest",
                    "$ANDROID_HOME/ndk/21.0.6113669", // Common version
                ] {
                    let expanded_path = expand_env_vars(path);
                    if Path::new(&expanded_path).exists() {
                        env_vars.insert("ANDROID_NDK".to_string(), expanded_path);
                        break;
                    }
                }
            }
        },
        "wasm32-unknown-emscripten" => {
            // Add Emscripten environment variables if not already set
            if !env_vars.contains_key("EMSCRIPTEN") && env::var("EMSCRIPTEN").is_err() {
                // Try to find Emscripten in common locations
                for path in &[
                    "$HOME/emsdk/upstream/emscripten",
                    "/usr/local/emsdk/upstream/emscripten",
                    "C:/emsdk/upstream/emscripten",
                ] {
                    let expanded_path = expand_env_vars(path);
                    if Path::new(&expanded_path).exists() {
                        env_vars.insert("EMSCRIPTEN".to_string(), expanded_path);
                        break;
                    }
                }
            }
        },
        _ => {},
    }

    env_vars
}