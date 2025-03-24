# CForge

![Version](https://img.shields.io/badge/version-1.3.1-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

**A TOML-based build system for C/C++ projects with seamless CMake and vcpkg integration.**

CForge is a modern build system designed to simplify C/C++ project management. It provides a clean TOML-based configuration approach while leveraging the power of CMake and vcpkg under the hood.

---

## 📖 Table of Contents
1. [Features](#features)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [Command Reference](#command-reference)
5. [Project Configuration](#project-configuration)
6. [Working with Dependencies](#working-with-dependencies)
7. [Workspaces](#workspaces)
8. [Build Variants](#build-variants)
9. [Cross-Compilation](#cross-compilation)
10. [IDE Integration](#ide-integration)
11. [Scripts & Hooks](#scripts--hooks)
12. [Testing](#testing)
13. [Advanced Topics](#advanced-topics)
14. [Examples](#examples)
15. [Troubleshooting](#troubleshooting)
16. [Contributing](#contributing)
17. [License](#license)

---

## 🚀 Features

- **Simple TOML Configuration**: Easy project setup without complex CMake syntax
- **Multi-platform**: Supports Windows, macOS, Linux
- **Dependency Management**: Integrated support for `vcpkg`, `Conan`, Git, and custom dependencies
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

### From Cargo

```bash
cargo install cforge
```

### From Source

```bash
git clone https://github.com/ChaseSunstrom/cforge.git
cd cforge
cargo build --release
cargo install --path .
```

### Prerequisites
- Rust
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

# Run the executable (for application projects)
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
┌──────────────────────────────────────────────────┐
│           cforge - C/C++ Build System            │
│                    v1.2.0                        │
└──────────────────────────────────────────────────┘

Building: myproject
[1/4] Checking build tools
Checking for required build tools...
CMake: ✓
Compiler 'clang': ✓
Build generator 'Ninja': ✓
vcpkg: ✓ (will be configured during build)
All required build tools are available.

[2/4] Configuring project
Project configured with generator: Ninja (Debug)

[3/4] Running pre-build hooks
Running pre-build hooks
Running hook: echo Starting build process...
Starting build process...

[4/4] Building project
Building myproject in Debug configuration
✓ Compiling 1 source files (completed in 1.2s)

✓ Build completed successfully

$ cforge run
┌──────────────────────────────────────────────────┐
│           cforge - C/C++ Build System            │
│                    v1.2.0                        │
└──────────────────────────────────────────────────┘

Running: myproject
Found executable: build/bin/myproject
Running: build/bin/myproject

Program Output
────────────
Hello, cforge!

✓ Program executed successfully
```

---

## 🛠️ Command Reference

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `init`       | Create new project/workspace        | `cforge init --template lib`       |
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
- `--verbosity`: Set verbosity level (`quiet`, `normal`, `verbose`)

Many commands support these options:
- `--config`: Build/run with specific configuration (e.g., `Debug`, `Release`)
- `--variant`: Use a specific build variant
- `--target`: Specify a cross-compilation target

---

## 📋 Project Configuration

### Basic Configuration

The `cforge.toml` file is the heart of your project configuration:

```toml
[project]
name = "my_project"
version = "0.1.0"
description = "My C/C++ project"
type = "executable"  # executable, library, static-library, header-only
language = "c++"
standard = "c++17"   # c++11, c++14, c++17, c++20, c++23

[build]
build_dir = "build"
default_config = "Debug"
generator = "Ninja"  # Ninja, "Visual Studio 17 2022", NMake Makefiles, etc.

[build.configs.Debug]
defines = ["DEBUG", "_DEBUG"]
flags = ["NO_OPT", "DEBUG_INFO"]

[build.configs.Release]
defines = ["NDEBUG"]
flags = ["OPTIMIZE", "OB2", "DNDEBUG"]

[targets.default]
sources = ["src/**/*.cpp", "src/**/*.c"]
include_dirs = ["include"]
links = []
```

### Target Configuration

A project can have multiple targets (executables or libraries):

```toml
[targets.main_app]
sources = ["src/app/**/*.cpp"]
include_dirs = ["include"]
links = ["fmt", "boost_system"]

[targets.utils_lib]
sources = ["src/utils/**/*.cpp"]
include_dirs = ["include/utils"]
links = []
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

### Conan Integration

```toml
[dependencies.conan]
enabled = true
packages = ["fmt/9.1.0", "spdlog/1.10.0"]
options = { "fmt:shared": "False", "spdlog:shared": "False" }
generators = ["cmake", "cmake_find_package"]
```

### Git Dependencies

```toml
[[dependencies.git]]
name = "imgui"
url = "https://github.com/ocornut/imgui.git"
branch = "master"
shallow = true

[[dependencies.git]]
name = "glfw"
url = "https://github.com/glfw/glfw.git"
tag = "3.3.8"
cmake_options = ["-DGLFW_BUILD_EXAMPLES=OFF", "-DGLFW_BUILD_TESTS=OFF"]
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

Workspaces allow you to manage multiple related projects together:

```bash
# Initialize a workspace
cforge init --workspace

# Initialize a project within the workspace
cd projects
cforge init --template lib

# Build all projects
cd ..
cforge build

# Build a specific project
cforge build my_lib

# Run a specific project
cforge run my_app
```

### Workspace Configuration

```toml
# cforge-workspace.toml
[workspace]
name = "my_workspace"
projects = ["projects/app", "projects/lib"]
default_startup_project = "projects/app"
```

### Project Dependencies within Workspace

```toml
# projects/app/cforge.toml
[dependencies.workspace]
name = "lib"
link_type = "static"  # static, shared, interface
```

---

## 🚩 Build Variants

Build variants allow for different build configurations beyond just Debug/Release:

```toml
[variants]
default = "standard"

[variants.variants.standard]
description = "Standard build"

[variants.variants.performance]
description = "Optimized build"
defines = ["HIGH_PERF=1"]
flags = ["OPTIMIZE_MAX", "LTO"]

[variants.variants.memory_safety]
description = "Build with memory safety checks"
defines = ["ENABLE_MEMORY_SAFETY=1"]
flags = ["MEMSAFE"]
```

Building with variants:

```bash
cforge build --variant performance
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

Example VS Code launch configuration (generated automatically):
```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/bin/${workspaceFolderBasename}",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "build"
    }
  ]
}
```

---

## 📝 Scripts & Hooks

Define custom scripts and build hooks:

```toml
[scripts]
scripts = {
  "format" = "clang-format -i src/*.cpp include/*.h",
  "count_lines" = "find src include -name '*.cpp' -o -name '*.h' | xargs wc -l",
  "clean_all" = "rm -rf build bin"
}

[hooks]
pre_build = ["echo Building...", "python scripts/version_gen.py"]
post_build = ["echo Done!", "cp build/bin/myapp /tmp/"]
pre_run = ["echo Starting application..."]
post_run = ["echo Application closed."]
```

Running scripts:
```bash
cforge script format
```

---

## 🧪 Testing

CForge integrates with CTest for testing:

```toml
[tests]
directory = "tests"
enabled = true
timeout = 30  # seconds

[[tests.executables]]
name = "math_tests"
sources = ["tests/math_test.cpp"]
includes = ["include", "tests/common"]
links = ["my_project"]
labels = ["unit", "math"]
```

Example test file (`tests/math_test.cpp`):
```cpp
#include <iostream>
#include <cassert>
#include "my_project.h"

void test_addition() {
    assert(my_project::add(2, 3) == 5);
    std::cout << "Addition test passed!" << std::endl;
}

void test_multiplication() {
    assert(my_project::multiply(2, 3) == 6);
    std::cout << "Multiplication test passed!" << std::endl;
}

int main() {
    test_addition();
    test_multiplication();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
```

Running tests:
```bash
# Run all tests
cforge test

# Run tests with a specific label
cforge test --label unit

# Run tests matching a pattern
cforge test --filter math

# Initialize test directory with sample test
cforge test --init

# Discover tests and update config
cforge test --discover

# Generate test reports
cforge test --report xml
```

---

## 🧩 Advanced Topics

### Precompiled Headers

```toml
[pch]
enabled = true
header = "include/pch.h"
source = "src/pch.cpp"  # Optional
exclude_sources = ["src/no_pch.cpp"]
```

### Package Generation

```bash
# Create a package (defaults to zip/tar.gz)
cforge package

# Specify package type
cforge package --type deb  # Linux Debian package
cforge package --type rpm  # Linux RPM package
cforge package --type zip  # Zip archive
```

### Installing Projects

```bash
# Install to default location
cforge install

# Install to specific directory
cforge install --prefix /usr/local
```

---

## 📚 Examples

### Simple Application

```toml
# cforge.toml
[project]
name = "hello_app"
version = "1.0.0"
description = "Hello World Application"
type = "executable"
language = "c++"
standard = "c++17"

[build]
default_config = "Debug"

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"]
```

```cpp
// src/main.cpp
#include <iostream>

int main() {
    std::cout << "Hello, CForge!" << std::endl;
    return 0;
}
```

### Library with vcpkg Dependencies

```toml
# cforge.toml
[project]
name = "math_lib"
version = "0.1.0"
description = "Mathematics Library"
type = "library"
language = "c++"
standard = "c++17"

[dependencies.vcpkg]
enabled = true
packages = ["fmt", "doctest"]

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"]
```

```cpp
// include/math_lib.h
#pragma once

namespace math_lib {
    int add(int a, int b);
    int subtract(int a, int b);
    int multiply(int a, int b);
    int divide(int a, int b);
}
```

```cpp
// src/math_lib.cpp
#include "math_lib.h"
#include <fmt/core.h>

namespace math_lib {
    int add(int a, int b) {
        fmt::print("Adding {} and {}\n", a, b);
        return a + b;
    }
    
    int subtract(int a, int b) {
        fmt::print("Subtracting {} from {}\n", b, a);
        return a - b;
    }
    
    int multiply(int a, int b) {
        fmt::print("Multiplying {} by {}\n", a, b);
        return a * b;
    }
    
    int divide(int a, int b) {
        fmt::print("Dividing {} by {}\n", a, b);
        return a / b;
    }
}
```

### Multi-project Workspace

```toml
# cforge-workspace.toml
[workspace]
name = "calculator"
projects = ["projects/core", "projects/gui", "projects/cli"]
default_startup_project = "projects/gui"
```

```toml
# projects/core/cforge.toml
[project]
name = "calc_core"
version = "0.1.0"
description = "Calculator Core Library"
type = "library"
language = "c++"
standard = "c++17"

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"]
```

```toml
# projects/gui/cforge.toml
[project]
name = "calc_gui"
version = "0.1.0"
description = "Calculator GUI Application"
type = "executable"
language = "c++"
standard = "c++17"

[dependencies.workspace]
name = "calc_core"
link_type = "static"

[dependencies.vcpkg]
enabled = true
packages = ["imgui", "glfw3", "opengl"]

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"]
```

---

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
