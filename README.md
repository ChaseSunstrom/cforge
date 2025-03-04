# CBuild

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

A TOML-based build system for C/C++ projects with seamless CMake and vcpkg integration.

## Features

- 📋 **Simple TOML Configuration** - Straightforward project configuration without complex CMake syntax  
- 🔄 **Multi-platform Support** - Works on Windows, macOS, and Linux  
- 📦 **Integrated Dependency Management** - Native support for vcpkg, Conan, Git, and custom dependencies  
- 🏗️ **Workspace Management** - Build and manage multiple projects in a single workspace  
- 🎯 **Cross-compilation** - Built-in support for Android, iOS, Raspberry Pi, and WebAssembly  
- 🧰 **IDE Integration** - Generate project files for VS Code, CLion, Xcode, and Visual Studio  
- 🧪 **Testing Support** - Run tests with CTest integration  
- 📝 **Custom Scripts** - Define and run project-specific scripts  

## Installation

### From Source

```bash
# Clone the repository
git clone https://github.com/yourusername/cbuild.git
cd cbuild

# Build and install
cargo build --release
cargo install --path .
```

### From Cargo

```bash
cargo install cbuild
```

#### Prerequisites

- Rust (for building from source)  
- CMake (3.15 or higher)  
- A C/C++ compiler (GCC, Clang, or MSVC)  

## Quick Start

```bash
# Create a new executable project
cbuild init

# Build the project
cbuild build

# Run the executable
cbuild run
```

## Command Reference

### Project Management

| Command               | Description                               | Example                             |
|-----------------------|-------------------------------------------|-------------------------------------|
| `init`               | Initialize a new project or workspace      | `cbuild init --template lib`        |
| `build`              | Build the project                          | `cbuild build --config Release`     |
| `clean`              | Clean build artifacts                      | `cbuild clean`                      |
| `run`                | Run the built executable                   | `cbuild run -- arg1 arg2`           |
| `test`               | Run tests                                  | `cbuild test --filter MyTest`       |
| `install`            | Install the project                        | `cbuild install --prefix /usr/local`|
| `script`             | Run a custom script                        | `cbuild script format`              |
| `ide`                | Generate IDE project files                 | `cbuild ide vscode`                 |
| `package`            | Package the project                        | `cbuild package --type zip`         |
| `list`               | List available configurations and options  | `cbuild list variants`              |
| `deps`               | Install project dependencies               | `cbuild deps --update`             |

### Workspace-specific Commands

When working with a multi-project workspace:

```bash
# Initialize a workspace
cbuild init --workspace

# Build a specific project in the workspace
cbuild build project_name

# Run a specific project in the workspace
cbuild run project_name
```

## Configuration Guide

CBuild uses TOML for project configuration. Here's an example `cbuild.toml` file:

```toml
[project]
name = "my_project"
version = "0.1.0"
description = "A C/C++ project built with CBuild"
type = "executable"  # Options: executable, library, static-library, header-only
language = "c++"     # Options: c, c++
standard = "c++17"   # Language standard: c11, c++17, etc.

[build]
build_dir = "build"
default_config = "Debug"  # Default build configuration

# Configuration-specific settings
[build.configs.Debug]
defines = ["DEBUG", "_DEBUG"]
flags = ["-O0", "-g"]  # Compiler-specific flags

[build.configs.Release]
defines = ["NDEBUG"]
flags = ["-O3"]

[dependencies.vcpkg]
enabled = true
path = "~/.vcpkg"
packages = ["fmt", "boost"]

[dependencies.conan]
enabled = false
packages = []

# Target configuration
[targets.default]
sources = ["src/**/*.cpp", "src/**/*.c"]
include_dirs = ["include"]
defines = []
links = ["fmt"]

# Output directory configuration
[output]
bin_dir = "bin"
lib_dir = "lib"
obj_dir = "obj"

# Custom scripts
[scripts]
scripts = { 
    "format" = "find src include -name '*.cpp' -o -name '*.h' | xargs clang-format -i",
    "count_lines" = "find src include -name '*.cpp' -o -name '*.h' | xargs wc -l"
}

# Build variants
[variants]
default = "standard"

[variants.variants.standard]
description = "Standard build with default settings"

[variants.variants.performance]
description = "Optimized for maximum performance"
defines = ["OPTIMIZE_PERFORMANCE=1"]
flags = ["-O3", "-march=native", "-flto"]
```

### Workspace Configuration

For multi-project workspaces, create a `cbuild-workspace.toml` file:

```toml
[workspace]
name = "my_workspace"

projects = [
    "projects/app1",
    "projects/app2",
    "projects/common_lib"
]
```

## Project Templates

CBuild offers several project templates to help you get started:

- **app (default)**: Creates an executable application  
- **lib**: Creates a shared library  
- **header-only**: Creates a header-only library  

```bash
# Create a new library project
cbuild init --template lib
```

## Build Variants

Build variants allow you to maintain different build settings:

```bash
# List available variants
cbuild list variants

# Build with a specific variant
cbuild build --variant performance
```

## Cross-compilation

CBuild supports several predefined cross-compilation targets:

- `android-arm64`: Android ARM64 platform (requires NDK)  
- `android-arm`: Android ARM platform (requires NDK)  
- `ios`: iOS ARM64 platform (requires Xcode)  
- `raspberry-pi`: Raspberry Pi ARM platform (requires toolchain)  
- `wasm`: WebAssembly via Emscripten  

```bash
# Cross-compile for Android ARM64
cbuild build --target android-arm64
```

## IDE Integration

Generate project files for your favorite IDE:

```bash
# Generate VS Code project files
cbuild ide vscode

# Generate CLion project files
cbuild ide clion

# Generate Xcode project files (macOS only)
cbuild ide xcode

# Generate Visual Studio project files (Windows only)
cbuild ide vs
```

## Example: Creating and Building a Project

```bash
# Create a new project
mkdir my_project
cd my_project
cbuild init

# Add some code
echo '#include <iostream>

int main() {
    std::cout << "Hello, CBuild!" << std::endl;
    return 0;
}' > src/main.cpp

# Build and run
cbuild build
cbuild run
```

## Example: Using External Dependencies

This example shows how to use the fmt library with vcpkg:

```bash
# Initialize project
cbuild init

# Edit cbuild.toml to add the dependency
# [dependencies.vcpkg]
# enabled = true
# packages = ["fmt"]
#
# [targets.default]
# links = ["fmt::fmt"]

# Install dependencies
cbuild deps

# Create a sample program
echo '#include <fmt/core.h>

int main() {
    fmt::print("Hello, {}!\n", "CBuild");
    return 0;
}' > src/main.cpp

# Build and run
cbuild build
cbuild run
```

## Troubleshooting

### Common Issues

#### CMake Not Found

Ensure CMake is installed and available in your PATH.

```bash
cmake --version
```

#### Dependency Installation Fails

Make sure you have Git installed for vcpkg and other dependencies.

```bash
git --version
```

#### Cross-compilation Issues

For cross-compilation, ensure you have the required tools:

- **Android**: Install Android NDK  
- **iOS**: Install Xcode (macOS only)  
- **WASM**: Install Emscripten  

---

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. **Fork** the repository  
2. Create your feature branch (`git checkout -b feature/amazing-feature`)  
3. Commit your changes (`git commit -m 'Add some amazing feature'`)  
4. Push to the branch (`git push origin feature/amazing-feature`)  
5. Open a Pull Request  

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
