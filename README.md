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
cforge init --template static-lib     # Create a static library project
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
→ Command: cmake -S C:\hi2 -B C:\hi2\build -DCMAKE_BUILD_TYPE=Debug -DDEBUG=1 -DENABLE_LOGGING=1 -DENABLE_TESTS=ON -G "Ninja Multi-Config"
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
→ Running executable: C:\hi2\build\bin\Debug\hi2.exe
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
| `build`      | Build the project (supports cross-compilation with `--target`) | `cforge build --config Release --target android-arm64` |
| `clean`      | Clean build artifacts               | `cforge clean`                     |
| `run`        | Run built executable                | `cforge run -- arg1 arg2`          |
| `test`       | Build and run unit tests (supports config, category & test filters) | `cforge test -c Release Math Add` |
| `install`    | Install project binaries            | `cforge install --prefix /usr/local`|
| `deps`       | Manage dependencies                 | `cforge deps --update`             |
| `script`     | Execute custom scripts              | `cforge script format`             |
| `startup`    | Manage workspace startup project    | `cforge startup my_app`            |
| `ide`        | Generate IDE project files          | `cforge ide vscode`                |
| `package`    | Package project binaries            | `cforge package --type zip`        |
| `list`       | List configurations, projects, or build-order | `cforge list build-order`             |

### Command Options

All commands accept the following global options:
- `-v/--verbose`: Set verbosity level

Many commands support these options:
- `--config`: Build/run with specific configuration (e.g., `Debug`, `Release`)

## 🧪 Testing

CForge includes a built-in test runner (no external CTest calls needed). By default your tests live in the directory specified by `test.directory` in `cforge.toml` (defaults to `tests`). To build and run tests:

```bash
# Run all tests in Debug (default)
cforge test

# Run only the 'Math' category in Debug
cforge test Math

# Run specific tests in Release
cforge test -c Release Math Add Divide

# Explicitly separate CForge flags from filters
cforge test -c Release -- Math Add Divide
```

Options:
- `-c, --config <config>`: Build configuration (Debug, Release, etc.)
- `-v, --verbose`: Show verbose build & test output

Positional arguments (after flags, or after `--`):
- `<category>`: Optional test category to run
- `<test_name> ...`: Optional list of test names under the category

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
c_standard = "11"
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

[package]
enabled = true
generators = []
vendor = "Chase Sunstrom"
contact = "Chase Sunstrom <casunstrom@gmail.com>"
```

### CMake Overrides

Specify generator, platform, toolset, and explicit compilers:
```toml
[cmake]
generator    = "Ninja"                  # CMake generator
platform     = "x64"                    # CMake platform for Visual Studio
toolset      = "ClangCl"                # CMake toolset (Visual Studio or Ninja)
c_compiler   = "/usr/bin/gcc-10"        # C compiler
cxx_compiler = "/usr/bin/g++-10"        # C++ compiler
c_standard   = "11"                      # C standard for C projects (e.g., 11, 17, 23)
cxx_standard = "17"                      # C++ standard override (e.g., 11, 14, 17, 20)
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
path = "~/.vcpkg"          # Optional: directory of vcpkg installation (defaults to VCPKG_ROOT or ./vcpkg)
triplet = "x64-windows"    # Optional: specify vcpkg target triplet
packages = ["fmt", "boost", "nlohmann-json"]
```

CMake is automatically configured to use the vcpkg toolchain file from the specified `path` or environment variable `VCPKG_ROOT`, and will pass the `VCPKG_TARGET_TRIPLET` when provided.

```bash
# Build with vcpkg integration
cforge build --config Release
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

### System Dependencies

```toml
[dependencies]
system = ["X11", "pthread", "dl"]
```

### 🗂️ Project-to-Project Dependencies (Workspaces)

In a workspace, you can declare dependencies on other projects in the same workspace:

```toml
[dependencies.project.MyLib]
include_dirs = ["include"]   # Relative to MyLib directory
link = true                    # Link the MyLib target
link_type = "PRIVATE"        # PUBLIC, PRIVATE, or INTERFACE
target_name = "MyLib"        # Optional CMake target name (defaults to project name)
```

cforge will automatically add the appropriate `include_directories` and
`target_link_libraries` entries when generating each project's CMakeLists.txt.

Run `cforge list graph` to visualize project dependencies in your workspace.

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

+```bash
+# List workspace projects
+cforge list projects
+
+# Show workspace build order
+cforge list build-order
+```

---

## 🌐 Cross-Compilation

CForge supports cross-compilation by specifying toolchain and system settings in `cforge.toml` under `[build.cross]`:

```toml
[build.cross]
toolchain_file = "/path/to/toolchain.cmake"      # CMake toolchain file
system_name = "Android"                          # CMAKE_SYSTEM_NAME
system_processor = "arm64"                       # CMAKE_SYSTEM_PROCESSOR
c_compiler = "aarch64-linux-android21-clang"     # CMAKE_C_COMPILER
cxx_compiler = "aarch64-linux-android21-clang++" # CMAKE_CXX_COMPILER
```

You can invoke cross compilation via the `--target` option to the `build` command:

```bash
cforge build --target android-arm64
```

Currently supported platforms include Android, iOS, Raspberry Pi, and WebAssembly.

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


## 🔧 Troubleshooting

### Common Issues

- **CMake not found**: Ensure it's installed and in PATH.
- **Dependency failures**: Run `cforge deps --update`.
- **Cross-compilation**: Check environment variables (e.g., `$ANDROID_NDK`).
- **Compiler errors**: Use `cforge build --verbosity verbose`.

CForge provides enhanced error diagnostics:

```
Build error details:
ERROR[E0001]: undefined reference to 'math_lib::divide(int, int)'
 --> src/main.cpp:12:5
  12| math_lib::divide(10, 0);
     ^~~~~~~~~~~~~~~~

help: The function 'divide' is used but not defined. Check if the library is properly linked.
```

### Useful Commands

```bash
# List available configurations
cforge list configs

# List available build variants
cforge list variants

# List cross-compilation targets
cforge list targets

# List custom scripts
cforge list scripts
```

---

## 🚀 Goals & Roadmap

CForge is continuously evolving to simplify C/C++ project management while providing powerful features. Here's what we've accomplished and where we're headed next:

### ✅ Completed Features

- **Simple TOML Configuration**: Easy project setup without complex CMake syntax
- **Multi-platform Support**: Windows, macOS, Linux compatibility
- **Core Dependency Management**: Integrated vcpkg and git dependencies
- **Workspace Support**: Basic multi-project management
- **IDE Integration**: VS Code, CLion support
- **Testing**: Test integration
- **Build Variants**: Multiple configuration support
- **Package Generation**: Create distributable packages

### 🚧 In Progress

- **Enhanced Workspace Dependencies**: Improving library detection and linking
- **Precompiled Header Optimization**: Better build performance
- **Diagnostic Improvements**: Clearer error messages and suggestions
- **Documentation Expansion**: More examples and quick-start guides

### 📝 Planned Features

- **Enhanced IDE Support**
  - Better Visual Studio project generation
  - QtCreator integration
  - Eclipse CDT support
  - Xcode project improvements

- **Plugin System**
  - Custom build steps via plugins
  - Language server integration
  - Code generation tools

- **Documentation Features**
  - Automatic API documentation generation
  - Doxygen integration

- **Advanced Testing**
  - Code coverage reports
  - Benchmark framework integration
  - Sanitizer integrations

- **CI/CD Pipeline Integration**
  - GitHub Actions templates
  - GitLab CI templates
  - Azure DevOps integration

- **Mobile Development**
  - Improved Android/iOS workflows
  - Mobile-specific templates

- **Cloud Development**
  - Remote build capabilities
  - Container-based builds
  - Package registry integration

- **Package Manager Enhancements**
  - Complete Conan 2.0 support
  - Lock file management
  - Recipe generation

We welcome contributions to help achieve these goals! If you're interested in working on a specific feature or have suggestions, please open an issue or submit a pull request.

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
