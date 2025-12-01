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

![Version](https://img.shields.io/badge/beta-2.2.2-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
[![GitHub stars](https://img.shields.io/github/stars/ChaseSunstrom/cforge?style=social)](https://github.com/ChaseSunstrom/cforge)


**A TOML-based build system for C/C++ projects with seamless CMake and vcpkg integration.**

CForge is a modern build system designed to simplify C/C++ project management. It provides a clean TOML-based configuration approach while leveraging the power of CMake and vcpkg under the hood.

---

## 📖 Table of Contents
1. [Features](#-features)
2. [Installation](#-installation)
3. [Quick Start](#-quick-start)
4. [Command Reference](#-command-reference)
5. [Project Configuration](#-project-configuration)
6. [Working with Dependencies](#-working-with-dependencies)
7. [Workspaces](#-workspaces)
8. [Developer Tools](#-developer-tools)
9. [Cross-Compilation](#-cross-compilation)
10. [IDE Integration](#-ide-integration)
11. [Scripts & Hooks](#-scripts--hooks)
12. [Testing & Benchmarking](#-testing--benchmarking)
13. [Advanced Topics](#-advanced-topics)
14. [Examples](#-examples)
15. [Troubleshooting](#-troubleshooting)
16. [Goals & Roadmap](#-goals--roadmap)
17. [Contributing](#-contributing)
18. [License](#-license)

---

## 🚀 Features

- **Simple TOML Configuration**: Easy project setup without complex CMake syntax
- **Multi-platform**: Supports Windows, macOS, Linux
- **Dependency Management**: Integrated support for `vcpkg`, Git, and custom dependencies
- **Workspaces**: Manage multiple projects together with dependency resolution
- **Cross-compilation**: Support for Android, iOS, Raspberry Pi, WebAssembly
- **IDE Integration**: VS Code, CLion, Xcode, Visual Studio
- **Testing & Benchmarking**: Integrated test runner and benchmark support
- **Custom Scripts & Hooks**: Run project-specific tasks at various stages
- **Automatic Tool Setup**: Installs missing tools automatically
- **Enhanced Diagnostics**: Cargo-style colored error output with fix suggestions
- **Build Timing**: See exactly how long builds take
- **Developer Tools**: Code formatting, linting, file templates, watch mode
- **Shell Completions**: Tab completion for bash, zsh, PowerShell, fish
- **Documentation Generation**: Integrated Doxygen support
- **Package Management**: Create distributable packages for your software

---

## 📥 Installation

Use the provided install scripts to build and install CForge.

### Linux/macOS (Bash)

```bash
# One-liner installation:
curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash

# Or with options:
curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash -s -- --prefix=/usr/local

```

### Windows (PowerShell)

```powershell
# One-liner installation (run in PowerShell):
irm https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.ps1 | iex
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
cforge init --template static-lib     # Create a static library project
cforge init --template header-only    # Create a header-only library

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

CForge provides beautiful, Cargo-style colored output with build timing:

```
$ cforge build

cforge - C/C++ Build System beta-v2.2.0
    Building myproject [Debug]
  Setting up Git dependencies
    Fetching 2 Git dependencies
    Finished all Git dependencies are set up
 Configuring project with CMake
 Configuring CMake
    Finished CMake configuration
   Compiling myproject
    Finished Debug target(s) in 3.67s
    Finished Command completed successfully
```

```
$ cforge run

cforge - C/C++ Build System beta-v2.2.0
    Building myproject [Debug]
    Finished Debug target(s) in 0.42s
     Running myproject

Hello, cforge!

    Finished Program exited with code 0
```

---

## 🛠️ Command Reference

### Core Commands

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `init`       | Create a new project or workspace   | `cforge init --template lib`       |
| `build`      | Build the project                   | `cforge build --config Release`    |
| `clean`      | Clean build artifacts               | `cforge clean`                     |
| `run`        | Run built executable                | `cforge run -- arg1 arg2`          |
| `test`       | Build and run unit tests            | `cforge test -c Release Math`      |
| `bench`      | Run benchmarks                      | `cforge bench --filter BM_Sort`    |

### Dependency Management

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `deps`       | Manage Git dependencies             | `cforge deps fetch`                |
| `vcpkg`      | Manage vcpkg dependencies           | `cforge vcpkg install fmt`         |
| `add`        | Add a dependency                    | `cforge add fmt --git`             |
| `remove`     | Remove a dependency                 | `cforge remove fmt`                |
| `tree`       | Visualize dependency tree           | `cforge tree`                      |
| `lock`       | Manage dependency lock file         | `cforge lock --verify`             |

### Developer Tools

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `fmt`        | Format code with clang-format       | `cforge fmt --check`               |
| `lint`       | Run clang-tidy static analysis      | `cforge lint --fix`                |
| `watch`      | Auto-rebuild on file changes        | `cforge watch --run`               |
| `new`        | Create files from templates         | `cforge new class MyClass`         |
| `doc`        | Generate documentation              | `cforge doc --open`                |

### Project Management

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `install`    | Install project binaries            | `cforge install --prefix /usr/local`|
| `package`    | Create distributable packages       | `cforge package --type zip`        |
| `ide`        | Generate IDE project files          | `cforge ide vscode`                |
| `list`       | List projects, dependencies, etc.   | `cforge list build-order`          |
| `completions`| Generate shell completions          | `cforge completions bash`          |

### Utility Commands

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `version`    | Show version information            | `cforge version`                   |
| `help`       | Show help for a command             | `cforge help build`                |
| `update`     | Update cforge itself                | `cforge update`                    |

### Command Options

All commands accept the following global options:
- `-v/--verbose`: Enable verbose output
- `-q/--quiet`: Suppress non-essential output

Many commands support these options:
- `-c/--config`: Build/run with specific configuration (e.g., `Debug`, `Release`)

---

## 🔧 Developer Tools

CForge includes powerful developer tools to improve your workflow.

### Code Formatting (`cforge fmt`)

Format your code using clang-format:

```bash
# Format all source files
cforge fmt

# Check formatting without modifying files
cforge fmt --check

# Show diff of what would change
cforge fmt --diff

# Use a specific style
cforge fmt --style=google
```

Output:
```
cforge - C/C++ Build System beta-v2.2.0
 Formatting 15 source files with clang-format
  Formatted src/main.cpp
  Formatted src/utils.cpp
    Finished Formatted 15 files
```

### Static Analysis (`cforge lint`)

Run clang-tidy static analysis:

```bash
# Run all checks
cforge lint

# Apply automatic fixes
cforge lint --fix

# Run specific checks
cforge lint --checks='modernize-*,bugprone-*'
```

### Watch Mode (`cforge watch`)

Automatically rebuild when files change:

```bash
# Watch and rebuild
cforge watch

# Watch, rebuild, and run
cforge watch --run

# Watch with Release configuration
cforge watch -c Release
```

Output:
```
cforge - C/C++ Build System beta-v2.2.0
     Watching for changes...
     Tracking 47 files
     Press Ctrl+C to stop

    Building myproject [Debug]
    Finished Debug target(s) in 1.23s

     Changes detected:
    Modified main.cpp

    Building myproject [Debug]
    Finished Debug target(s) in 0.89s
```

### File Templates (`cforge new`)

Create files from templates:

```bash
# Create a class with header and source
cforge new class MyClass

# Create with namespace
cforge new class MyClass -n myproject

# Create a header-only file
cforge new header utils

# Create a struct
cforge new struct Config

# Create an interface (abstract class)
cforge new interface IService

# Create a test file
cforge new test MyClass

# Create main.cpp
cforge new main
```

Output:
```
cforge - C/C++ Build System beta-v2.2.0
    Created include/my_class.hpp
    Created src/my_class.cpp
```

### Dependency Tree (`cforge tree`)

Visualize your project dependencies:

```bash
cforge tree
```

Output (with colors):
```
cforge v2.2.0
|-- fmt @ 11.1.4 (git)
`-- tomlplusplus @ v3.4.0 (git)

Dependencies: 2 git
```

Dependencies are color-coded by type:
- **Cyan**: Git dependencies
- **Magenta**: vcpkg dependencies
- **Yellow**: System dependencies
- **Green**: Project dependencies (workspace)

### Documentation (`cforge doc`)

Generate documentation with Doxygen:

```bash
# Generate documentation
cforge doc

# Create Doxyfile for customization
cforge doc --init

# Generate and open in browser
cforge doc --open
```

### Shell Completions (`cforge completions`)

Enable tab completion in your shell:

```bash
# Bash
cforge completions bash >> ~/.bashrc

# Zsh
cforge completions zsh >> ~/.zshrc

# PowerShell
cforge completions powershell >> $PROFILE

# Fish
cforge completions fish > ~/.config/fish/completions/cforge.fish
```

---

## 📋 Project Configuration

### Basic Configuration

The `cforge.toml` file is the heart of your project configuration:

```toml
[project]
name = "myproject"
version = "1.0.0"
description = "My awesome C++ project"
cpp_standard = "17"
c_standard = "11"
binary_type = "executable" # executable, shared_library, static_library, header_only
authors = ["Your Name <you@example.com>"]
license = "MIT"

[build]
build_type = "Debug"
directory = "build"
source_dirs = ["src"]
include_dirs = ["include"]

[build.config.debug]
defines = ["DEBUG=1"]
flags = ["DEBUG_INFO", "NO_OPT"]

[build.config.release]
defines = ["NDEBUG=1"]
flags = ["OPTIMIZE"]

[test]
enabled = true
directory = "tests"
framework = "catch2"  # or "gtest"

[benchmark]
directory = "bench"
target = "benchmarks"

[package]
enabled = true
generators = ["ZIP", "TGZ"]
vendor = "Your Name"
```

### Using Version in Code

The version from `cforge.toml` is automatically available as compile definitions:

```cpp
#include <iostream>

int main() {
    // Generic version macros (works for any project)
    std::cout << "Version: " << PROJECT_VERSION << std::endl;
    std::cout << "Major: " << PROJECT_VERSION_MAJOR << std::endl;
    std::cout << "Minor: " << PROJECT_VERSION_MINOR << std::endl;
    std::cout << "Patch: " << PROJECT_VERSION_PATCH << std::endl;

    // Project-specific macros (e.g., for project named "myapp")
    // std::cout << myapp_VERSION << std::endl;

    return 0;
}
```

Available macros:
| Macro | Description | Example |
|-------|-------------|---------|
| `PROJECT_VERSION` | Full version string | `"1.2.3"` |
| `PROJECT_VERSION_MAJOR` | Major version number | `1` |
| `PROJECT_VERSION_MINOR` | Minor version number | `2` |
| `PROJECT_VERSION_PATCH` | Patch version number | `3` |
| `<ProjectName>_VERSION` | Project-specific version | `"1.2.3"` |

### CMake Integration

Customize CMake behavior with includes, injections, and module paths:

```toml
[cmake]
version = "3.15"                      # Minimum CMake version
generator = "Ninja"                    # CMake generator
includes = ["cmake/custom.cmake"]      # Custom CMake files to include
module_paths = ["cmake/modules"]       # Custom module search paths

# Inject custom CMake code
inject_before_target = """
# Code inserted before add_executable/add_library
include(FetchContent)
"""

inject_after_target = """
# Code inserted after add_executable/add_library
target_precompile_headers(${PROJECT_NAME} PRIVATE <pch.hpp>)
"""

[cmake.compilers]
c = "/usr/bin/gcc-12"
cxx = "/usr/bin/g++-12"

[cmake.visual_studio]
platform = "x64"
toolset = "v143"
```

### Platform-Specific Configuration

Configure settings per platform (windows, linux, macos):

```toml
[platform.windows]
defines = ["WIN32", "_WINDOWS"]
flags = ["/W4"]
links = ["kernel32", "user32"]

[platform.linux]
defines = ["LINUX"]
flags = ["-Wall", "-Wextra"]
links = ["pthread", "dl"]

[platform.macos]
defines = ["MACOS"]
flags = ["-Wall"]
frameworks = ["Cocoa", "IOKit"]  # macOS frameworks
```

### Compiler-Specific Configuration

Configure settings per compiler (msvc, gcc, clang, apple_clang, mingw):

```toml
[compiler.msvc]
flags = ["/W4", "/WX", "/permissive-"]
defines = ["_CRT_SECURE_NO_WARNINGS"]

[compiler.gcc]
flags = ["-Wall", "-Wextra", "-Wpedantic"]

[compiler.clang]
flags = ["-Wall", "-Wextra", "-Wpedantic"]

[compiler.mingw]
flags = ["-Wall", "-Wextra"]
defines = ["MINGW"]
```

### Platform + Compiler Combinations

Combine platform and compiler for fine-grained control:

```toml
[platform.windows.compiler.msvc]
flags = ["/W4"]
defines = ["_CRT_SECURE_NO_WARNINGS"]

[platform.windows.compiler.mingw]
defines = ["MINGW_BUILD"]
links = ["mingw32"]

[platform.linux.compiler.gcc]
flags = ["-Wall", "-Wextra", "-fPIC"]
```

---

## 📦 Working with Dependencies

CForge supports multiple dependency management systems:

### vcpkg Integration

```toml
[dependencies.vcpkg]
enabled = true
path = "~/.vcpkg"          # Optional: directory of vcpkg installation
triplet = "x64-windows"    # Optional: specify vcpkg target triplet
packages = ["fmt", "boost", "nlohmann-json"]
```

### Git Dependencies

```toml
[dependencies.git.fmt]
url = "https://github.com/fmtlib/fmt.git"
tag = "11.1.4"

[dependencies.git.nlohmann_json]
url = "https://github.com/nlohmann/json.git"
tag = "v3.11.3"

[dependencies.git.imgui]
url = "https://github.com/ocornut/imgui.git"
branch = "master"
shallow = true
```

### System Dependencies

System dependencies support three methods: `find_package`, `pkg_config`, and `manual`:

```toml
# Auto-detect with CMake find_package
[dependencies.system.OpenGL]
method = "find_package"
required = true
components = ["GL", "GLU"]
target = "OpenGL::GL"

# Auto-detect with pkg-config
[dependencies.system.x11]
method = "pkg_config"
package = "x11"
platforms = ["linux"]  # Only on Linux

# Manual specification
[dependencies.system.custom_lib]
method = "manual"
include_dirs = ["/usr/local/include/custom"]
library_dirs = ["/usr/local/lib"]
libraries = ["custom", "custom_util"]
defines = ["USE_CUSTOM_LIB"]
platforms = ["linux", "macos"]  # Limit to specific platforms
```

### Subdirectory Dependencies

Use existing CMake projects as dependencies:

```toml
[dependencies.subdirectory.spdlog]
path = "extern/spdlog"
target = "spdlog::spdlog"
options = { SPDLOG_BUILD_TESTS = "OFF" }

[dependencies.subdirectory.glfw]
path = "extern/glfw"
target = "glfw"
options = { GLFW_BUILD_EXAMPLES = "OFF", GLFW_BUILD_TESTS = "OFF" }
```

### Dependency Lock File

Ensure reproducible builds with lock files:

```bash
# Generate/update lock file
cforge lock

# Verify dependencies match lock file
cforge lock --verify

# Force regeneration
cforge lock --force
```

---

## 🗂️ Workspaces

Workspaces allow you to manage multiple related CForge projects together.

```bash
# Initialize a workspace
cforge init --workspace my_workspace --projects core gui
```

This generates a `cforge-workspace.toml`:

```toml
[workspace]
name = "my_workspace"
projects = ["core", "gui"]
default_startup_project = "core"
```

### Workspace Commands

```bash
# Build all projects
cforge build

# Build specific project
cforge build -p gui

# List workspace projects
cforge list projects

# Show build order
cforge list build-order

# Visualize dependencies
cforge tree
```

### Project Dependencies

```toml
[dependencies.project.core]
include_dirs = ["include"]
link = true
link_type = "PRIVATE"
```

---

## 🧪 Testing & Benchmarking

### Running Tests

```bash
# Run all tests
cforge test

# Run specific category
cforge test Math

# Run specific tests in Release
cforge test -c Release Math Add Divide

# Verbose output
cforge test -v
```

### Running Benchmarks

```bash
# Run all benchmarks (builds in Release by default)
cforge bench

# Run specific benchmark
cforge bench --filter 'BM_Sort'

# Skip build
cforge bench --no-build

# Output formats
cforge bench --json > results.json
cforge bench --csv > results.csv
```

Configure benchmarks in `cforge.toml`:
```toml
[benchmark]
directory = "bench"
target = "my_benchmarks"
```

---

## 🌐 Cross-Compilation

CForge supports cross-compilation with a unified configuration:

```toml
[cross]
enabled = true

[cross.target]
system = "Linux"           # CMAKE_SYSTEM_NAME
processor = "aarch64"      # CMAKE_SYSTEM_PROCESSOR
toolchain = "path/to/toolchain.cmake"  # Optional

[cross.compilers]
c = "/usr/bin/aarch64-linux-gnu-gcc"
cxx = "/usr/bin/aarch64-linux-gnu-g++"

[cross.paths]
sysroot = "/path/to/sysroot"
find_root = "/path/to/find/root"
```

### Cross-Compilation Profiles

Define reusable cross-compilation profiles:

```toml
[cross.profile.android-arm64]
system = "Android"
processor = "aarch64"
toolchain = "${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
variables = { ANDROID_ABI = "arm64-v8a", ANDROID_PLATFORM = "android-24" }

[cross.profile.raspberry-pi]
system = "Linux"
processor = "armv7l"
compilers = { c = "arm-linux-gnueabihf-gcc", cxx = "arm-linux-gnueabihf-g++" }
sysroot = "/path/to/rpi-sysroot"
```

```bash
cforge build --profile android-arm64
```

Supported platforms: Android, iOS, Raspberry Pi, WebAssembly, and more!.

---

## 🖥️ IDE Integration

Generate IDE-specific project files:

```bash
cforge ide vscode    # VS Code
cforge ide clion     # CLion
cforge ide xcode     # Xcode (macOS)
cforge ide vs2022    # Visual Studio 2022
```

---

## 📜 Scripts & Hooks

Run custom scripts at build stages:

```toml
[scripts]
pre_build  = ["scripts/setup_env.py", "scripts/gen_code.py"]
post_build = ["scripts/deploy.py"]
```

---

## 🔧 Troubleshooting

### Enhanced Error Diagnostics

CForge provides Cargo-style error output with helpful suggestions:

```
error: undefined reference to `math_lib::divide(int, int)'
  --> src/main.cpp:12:5
   |
12 |     math_lib::divide(10, 0);
   |     ^~~~~~~~~~~~~~~~~~~~~~~
   |
   = help: The function 'divide' is declared but not defined
   = hint: Check if the library containing this symbol is linked

error: no member named 'vetor' in namespace 'std'
  --> src/utils.cpp:8:10
   |
 8 |     std::vetor<int> nums;
   |          ^~~~~
   |
   = help: Did you mean 'vector'?

error: 'string' was not declared in this scope
  --> src/parser.cpp:15:5
   |
15 |     string name;
   |     ^~~~~~
   |
   = help: Add '#include <string>' and use 'std::string'

error[summary]: Build failed with 3 errors and 2 warnings
```

### Linker Error Improvements

CForge provides helpful suggestions for common linker errors:

```
error: LNK2019: unresolved external symbol "void __cdecl foo()"
  = help: Common causes:
    - Missing library in target_link_libraries
    - Function declared but not defined
    - Mismatched calling conventions
  = hint: For 'WinMain': Link with kernel32.lib or check /SUBSYSTEM setting
```

### Useful Commands

```bash
# Verbose build output
cforge build -v

# Check formatting issues
cforge fmt --check

# Verify lock file
cforge lock --verify

# List configurations
cforge list configs
```

---

## 🚀 Goals & Roadmap

### ✅ Completed Features

- **Simple TOML Configuration**: Easy project setup without complex CMake syntax
- **Multi-platform Support**: Windows, macOS, Linux compatibility
- **Dependency Management**: vcpkg, Git, and system dependencies
- **Workspace Support**: Multi-project management with dependency resolution
- **IDE Integration**: VS Code, CLion, Visual Studio, Xcode
- **Testing**: Integrated test runner with category/filter support
- **Benchmarking**: Google Benchmark integration
- **Build Variants**: Multiple configuration support
- **Package Generation**: ZIP, TGZ, DEB, NSIS packages
- **Code Formatting**: clang-format integration
- **Static Analysis**: clang-tidy integration
- **Watch Mode**: Auto-rebuild on file changes
- **File Templates**: Create classes, headers, tests from templates
- **Documentation**: Doxygen integration
- **Shell Completions**: bash, zsh, PowerShell, fish
- **Dependency Visualization**: Tree view of dependencies
- **Enhanced Diagnostics**: Cargo-style colored errors with suggestions
- **Build Timing**: Duration tracking for builds
- **Lock Files**: Reproducible builds with dependency locking
- **Platform-Specific Configuration**: Per-platform defines, flags, links, frameworks
- **Compiler-Specific Configuration**: Per-compiler settings (MSVC, GCC, Clang, MinGW)
- **Enhanced System Dependencies**: find_package, pkg_config, manual methods with platform filtering
- **Subdirectory Dependencies**: Use existing CMake projects as dependencies
- **CMake Integration**: Custom includes, module paths, code injection
- **Cross-Compilation Profiles**: Reusable cross-compilation configurations

### 📝 Planned Features

- **Plugin System**: Custom build steps via plugins
- **Code Coverage**: Coverage report generation
- **Sanitizer Integration**: ASan, TSan, UBSan support
- **CI/CD Templates**: GitHub Actions, GitLab CI
- **Remote Builds**: Cloud/container-based builds
- **Conan 2.0 Support**: Additional package manager

---

## 🤝 Contributing

Contributions welcome!

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/new-feature`)
3. Commit your changes (`git commit -m 'Add new feature'`)
4. Push to your branch (`git push origin feature/new-feature`)
5. Open a Pull Request

---

## 📄 License

**MIT License** — see [LICENSE](LICENSE).
