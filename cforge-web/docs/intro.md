---
id: intro
title: Introduction
slug: /intro
---

# CForge

![Version](https://img.shields.io/badge/beta-1.5.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey.svg)

**A modern TOML-based build system for C/C++ with CMake & vcpkg integration.**

CForge simplifies C/C++ project management with clean TOML configuration, Cargo-style output, and integrated developer tools - while leveraging the power of CMake under the hood.

:::caution Beta Software
This project is currently in **BETA**. Features may be incomplete, contain bugs, or change without notice.
:::

## Features

### Build System
- **Simple TOML Configuration** - Replace complex CMakeLists.txt with readable TOML
- **Cargo-Style Output** - Beautiful colored output with build timing
- **Multi-Platform** - Windows, macOS, and Linux support
- **Cross-Compilation** - Android, iOS, Raspberry Pi, WebAssembly toolchains
- **Workspaces** - Manage monorepos with automatic dependency resolution

### Dependency Management
- **vcpkg Integration** - First-class support for vcpkg packages
- **Conan Support** - Integrate with Conan package manager
- **Git Dependencies** - Clone and build from Git repositories
- **System Libraries** - Link against system-installed libraries
- **Dependency Visualization** - View dependency tree with `cforge tree`

### Developer Tools
- **Code Formatting** - `cforge fmt` with clang-format integration
- **Static Analysis** - `cforge lint` with clang-tidy integration
- **File Watching** - `cforge watch` auto-rebuilds on file changes
- **Documentation** - `cforge doc` generates docs with Doxygen
- **Benchmarking** - `cforge bench` runs Google Benchmark and others
- **Code Templates** - `cforge new` generates classes, headers, tests
- **Shell Completions** - Bash, Zsh, PowerShell, and Fish support

### IDE Integration
- **VS Code** - Tasks, launch configs, and settings
- **CLion** - CMake integration with run configurations
- **Visual Studio** - Solution and project files
- **Xcode** - macOS and iOS project generation

### Enhanced Diagnostics
- **Colored Output** - Clear visual distinction for errors and warnings
- **Error Context** - Shows relevant code snippets
- **Fix Suggestions** - Common fixes for linker and compiler errors
- **Template Error Parsing** - Simplified template instantiation errors

## Quick Example

**cforge.toml:**
```toml
[project]
name = "my_app"
version = "1.0.0"
type = "executable"

[dependencies]
fmt = { version = "10.1.0", source = "vcpkg" }
spdlog = { version = "1.12.0", source = "vcpkg" }
```

**Build & Run:**
```bash
$ cforge build
   Compiling my_app v1.0.0
   Compiling src/main.cpp
    Finished Debug target(s) in 1.23s

$ cforge run
     Running my_app
Hello, World!
```

## Getting Started

1. **[Installation](./installation)** - Install CForge on your system
2. **[Quick Start](./quick-start)** - Create your first project
3. **[Command Reference](./command-reference)** - Full command documentation

## Requirements

- **CMake** 3.16 or later
- **C++ Compiler** - MSVC, GCC, or Clang
- **vcpkg** (optional) - For vcpkg dependency management

## Community

- [GitHub Repository](https://github.com/ChaseSunstrom/cforge)
- [GitHub Discussions](https://github.com/ChaseSunstrom/cforge/discussions)
- [Discord Server](https://discord.gg/2pMEZGNwaN)
- [Report Issues](https://github.com/ChaseSunstrom/cforge/issues)
