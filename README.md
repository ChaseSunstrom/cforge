<div align="center">

# ⚠️ WARNING: BETA VERSION ⚠️

</div>

> 🚨 **This software is currently in BETA.** Features may be incomplete, contain bugs, or change without notice, and being frequently updated. Use at your own risk.
>
> - Not recommended for production environments
> - Data loss or corruption may occur
> - APIs are subject to change without warning
> - Limited support available
>
> Please report any bugs or issues in the [Issues](https://github.com/ChaseSunstrom/cforge/issues) section.

# CForge

![Version](https://img.shields.io/badge/beta-2.0.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

**A TOML-based build system for C/C++ projects with seamless CMake and vcpkg integration.**

CForge is a modern build system designed to simplify C/C++ project management. It provides a clean TOML-based configuration approach while leveraging the power of CMake and vcpkg under the hood.

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=ChaseSunstrom/cforge&type=Date)](https://www.star-history.com/#ChaseSunstrom/cforge&Date)

---

## 📖 Table of Contents
1. [Features](#-features)
2. [Installation](#-installation)
3. [Quick Start](#-quick-start)
4. [Command Reference](#-command-reference)
5. [Project Configuration](#-project-configuration)
6. [Working with Dependencies](#-working-with-dependencies)
7. [Workspaces](#-workspaces)
8. [Cross-Compilation](#-cross-compilation)
9. [IDE Integration](#-ide-integration)
10. [Scripts & Hooks](#-scripts--hooks)
11. [Testing](#-testing)
12. [Advanced Topics](#-advanced-topics)
13. [Examples](#-examples)
14. [Troubleshooting](#-troubleshooting)
15. [Goals & Roadmap](#-goals--roadmap)
16. [Contributing](#-contributing)
17. [License](#-license)

---

## 🚀 Features

- **Simple TOML Configuration**: Easy project setup without complex CMake syntax
- **Multi-platform**: Supports Windows, macOS, Linux
- **Dependency Management**: Integrated support for `vcpkg`, Git, and custom dependencies
- **Workspaces**: Manage multiple projects together with dependency resolution
- **Cross-compilation**: Support for Android, iOS, Raspberry Pi, WebAssembly
- **IDE Integration**: VS Code, CLion, Xcode, Visual Studio
- **Testing**: Integrated with CTest
- **Custom Scripts & Hooks**: Run project-specific tasks at various stages
- **Automatic Tool Setup**: Installs missing tools automatically
- **Enhanced Diagnostics**: Clear, informative compiler errors
- **Build Variants**: Easily switch between different build configurations
- **Package Management**: Create distributable packages for your software

---

## 📥 Installation

Use the provided bootstrap scripts to build and install CForge.

### Linux/macOS (Bash)

```bash
bash scripts/bootstrap.sh
```

### Windows (PowerShell)

```powershell
.\scripts\bootstrap.ps1
```

### Windows (Batch)

```batch
scripts\bootstrap.bat
```

### Prerequisites

- CMake (≥3.15)
- C/C++ Compiler (GCC, Clang, MSVC)
- Optional: Ninja, Make, or Visual Studio Build Tools

---

## ⚡ Quick Start

### Creating a New Project

```bash
# Create a new project in the current directory
cforge init

# Create a specific project type
cforge init --template lib     # Create a library project
cforge init --template header-only  # Create a header-only library

# Build the project
cforge build

# Run the executable (for executable projects)
cforge run
```

### Example Project Structure

After initializing a project with `cforge init`, you'll have a structure like this:

```
myproject/
├── cforge.toml         # Project configuration
├── src/
│   └── main.cpp        # Main source file
├── include/            # Header files
├── scripts/            # Custom scripts
└── build/              # Build artifacts (generated)
```

### Example C++ Code

`src/main.cpp` (generated automatically):
```cpp
#include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "Hello, cforge!" << std::endl;
    return 0;
}
```

### Build and Run

```bash
$ cforge build
┌───────────────────────────────────────────────────┐
│ cforge - C/C++ Build System beta-v2.0.0           │
└───────────────────────────────────────────────────┘

→ Using build configuration: Debug
→ Building project: hi2 [Debug]
→ Configuring with CMake...
→ Running CMake Configure...
→ Command: cmake -S C:\cpp-forge\hi2 -B C:\cpp-forge\hi2\build -DCMAKE_BUILD_TYPE=Debug -DDEBUG=1 -DENABLE_LOGGING=1 -DENABLE_TESTS=ON -G "Ninja Multi-Config"
✓ CMake Configure completed successfully
→ Building with CMake...
✓ Built project: hi2 [Debug]
✓ Command completed successfully

$ cforge run
┌───────────────────────────────────────────────────┐
│ cforge - C/C++ Build System beta-v2.0.0           │
└───────────────────────────────────────────────────┘

→ Running in single project context
→ Project: hi2
→ Configuration: Debug
→ Configuring project...
→ Building project...
✓ Project built successfully
→ Running executable: C:\cpp-forge\hi2\build\bin\Debug\hi2.exe
→ Program Output

Hello from hi2!

✓ Program exited with code 0
✓ Command completed successfully
```

---

## 🛠️ Command Reference

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `init`       | Create a new project or workspace (supports `--template`, `--workspace`, `--projects`) | `cforge init --template lib`       |
| `build`      | Build the project                   | `cforge build --config Release`    |
| `clean`      | Clean build artifacts               | `cforge clean`                     |
| `run`        | Run built executable                | `cforge run -- arg1 arg2`          |
| `test`       | Execute tests (CTest integration)   | `cforge test --filter MyTest`      |
| `install`    | Install project binaries            | `cforge install --prefix /usr/local`|
| `deps`       | Manage dependencies                 | `cforge deps --update`             |
| `script`     | Execute custom scripts              | `cforge script format`             |
| `startup`    | Manage workspace startup project    | `cforge startup my_app`            |
| `ide`        | Generate IDE project files          | `cforge ide vscode`                |
| `package`    | Package project binaries            | `cforge package --type zip`        |
| `list`       | List variants, configs, or targets  | `cforge list variants`             |

### Command Options

All commands accept the following global options:
- `-v/--verbose`: Set verbosity level

Many commands support these options:
- `--config`: Build/run with specific configuration (e.g., `Debug`, `Release`)

---

## 📋 Project Configuration

### Basic Configuration

The `cforge.toml` file is the heart of your project configuration:

```toml
[project]
name = "cforge"
version = "2.0.0"
description = "A C/C++ build tool with dependency management"
cpp_standard = "17"
binary_type = "executable" # executable, shared_library, static_library, header_only
authors = ["Chase Sunstrom <casunstrom@gmail.com>"]
homepage = "https://github.com/ChaseSunstrom/cforge"
repository = "https://github.com/ChaseSunstrom/cforge.git"
license = "MIT"

[build]
build_type = "Debug"
directory = "build"
source_dirs = ["src"]
include_dirs = ["include"]

[build.config.debug]
defines = ["DEBUG=1", "FMT_HEADER_ONLY=ON"]
flags      = ["DEBUG_INFO", "NO_OPT"]

[build.config.release]
defines    = ["NDEBUG=1"]
flags      = ["OPTIMIZE"]

[test]
enabled = false
framework = "Catch2"

[package]
enabled = true
generators = []
vendor = "Chase Sunstrom"
contact = "Chase Sunstrom <casunstrom@gmail.com>"
```

### Platform-specific Configuration

```toml
[platforms.windows]
defines = ["WINDOWS", "WIN32"]
flags = ["UNICODE"]

[platforms.darwin]
defines = ["OSX"]
flags = []

[platforms.linux]
defines = ["LINUX"]
flags = []
```

---

## 📦 Working with Dependencies

CForge supports multiple dependency management systems:

### vcpkg Integration

```toml
[dependencies.vcpkg]
enabled = true
path = "~/.vcpkg"  # Optional, defaults to ~/.vcpkg
packages = ["fmt", "boost", "nlohmann-json"]
```

Example C++ code using vcpkg dependencies:

```cpp
#include <fmt/core.h>
#include <nlohmann/json.hpp>

int main() {
    // Using fmt library from vcpkg
    fmt::print("Hello, {}!\n", "world");
    
    // Using nlohmann/json library from vcpkg
    nlohmann::json j = {
        {"name", "CForge"},
        {"version", "1.2.0"}
    };
    
    fmt::print("JSON: {}\n", j.dump(2));
    return 0;
}
```

### Git Dependencies

```toml
[[dependencies.git]]
name = "nlohmann_json"
url = "https://github.com/nlohmann/json.git"
tag = "v3.11.3"
# Optional settings
shallow = true  # Faster clone with reduced history
update = false  # Whether to update the repo on builds

[[dependencies.git]]
name = "fmt"
url = "https://github.com/fmtlib/fmt.git"
tag = "9.1.0"
cmake_options = ["-DFMT_TEST=OFF", "-DFMT_DOC=OFF"]  # Pass CMake options when building

[[dependencies.git]]
name = "imgui"
url = "https://github.com/ocornut/imgui.git"
branch = "master"  # Use a specific branch instead of tag
shallow = true

[[dependencies.git]]
name = "custom_repo"
url = "https://example.com/repo.git"
commit = "abc123def456"  # Use a specific commit hash
```

Git dependencies are automatically cloned into a deps directory. The libraries can be included in your project by adding their include paths to your target configuration:

```toml
[targets.default]
include_dirs = ["include", "deps/nlohmann_json/single_include", "deps/fmt/include"]
defines = ["FMT_HEADER_ONLY"]  # Optionally add defines for your dependencies
```

You can also use the libraries in your code immediately:

```cpp
#include <nlohmann/json.hpp>
#include <fmt/core.h>

int main() {
    // Using nlohmann/json
    nlohmann::json obj = {{"name", "cforge"}, {"version", "1.4.0"}};
    
    // Using fmt
    fmt::print("Project: {}\n", obj["name"].get<std::string>());
    return 0;
}
```

### Custom Dependencies

```toml
[[dependencies.custom]]
name = "my_library"
url = "https://example.com/my_library-1.0.0.zip"
include_path = "include"
library_path = "lib"
```

### System Dependencies

```toml
[dependencies]
system = ["X11", "pthread", "dl"]
```

---

## 🗂️ Workspaces

Workspaces allow you to manage multiple related CForge projects together. You can initialize a new workspace and specify which project directories it should include.

```bash
# Initialize a workspace named "my_workspace" managing two project folders
cforge init --workspace my_workspace --projects projects/core projects/gui
```

This generates a `cforge-workspace.toml` file at the workspace root with contents like:

```toml
[workspace]
name = "my_workspace"
projects = ["projects/core", "projects/gui"]
default_startup_project = "projects/core"
```

To build all projects in the workspace:

```bash
cforge build
```

To build or run a specific project, pass its directory name:

```bash
cforge build projects/gui
cforge run projects/gui
```

---

## 🌐 Cross-Compilation

CForge supports cross-compilation for various platforms:

```toml
[cross_compile]
enabled = true
target = "android-arm64"
sysroot = "$ANDROID_NDK/platforms/android-24/arch-arm64"
cmake_toolchain_file = "$ANDROID_NDK/build/cmake/android.toolchain.cmake"
flags = ["-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-24"]
```

Cross-compilation targets:
- `android-arm64`: Android ARM64 platform
- `android-arm`: Android ARM platform
- `ios`: iOS ARM64 platform
- `raspberry-pi`: Raspberry Pi ARM platform
- `wasm`: WebAssembly via Emscripten

Example:
```bash
cforge build --target android-arm64
```

---

## 🖥️ IDE Integration

Generate IDE-specific project files:

```bash
# VS Code
cforge ide vscode

# CLion
cforge ide clion

# Xcode (macOS only)
cforge ide xcode

# Visual Studio (Windows only)
cforge ide vs2022
cforge ide vs:x64  # With architecture specification
```