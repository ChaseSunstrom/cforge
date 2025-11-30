---
id: quick-start
title: Quick Start
---

# Quick Start

Get up and running with CForge in minutes.

## Creating a New Project

```bash
# Create a new project in the current directory
cforge init my_project
cd my_project

# Or create in current directory
mkdir my_project && cd my_project
cforge init
```

### Project Templates

```bash
# Executable (default)
cforge init --template exe

# Static or shared library
cforge init --template lib

# Header-only library
cforge init --template header-only

# With tests enabled
cforge init --with-tests

# Specific C++ standard
cforge init --cpp=20
```

## Project Structure

After `cforge init`, you'll have:

```
my_project/
├── cforge.toml         # Project configuration
├── src/
│   └── main.cpp        # Main source file
├── include/            # Header files
└── build/              # Build output (generated)
```

## Configuration File

**cforge.toml:**
```toml
[project]
name = "my_project"
version = "1.0.0"
type = "executable"
cpp_standard = "17"

[build]
source_dir = "src"
include_dir = "include"
```

## Build & Run

```bash
# Build in Debug mode (default)
cforge build

# Build in Release mode
cforge build --config Release
```

**Output:**
```
   Compiling my_project v1.0.0
   Compiling src/main.cpp
    Finished Debug target(s) in 0.84s
```

```bash
# Run the executable
cforge run
```

**Output:**
```
     Running my_project
Hello, cforge!
```

## Adding Dependencies

Edit `cforge.toml` to add dependencies:

```toml
[dependencies]
fmt = { version = "10.1.0", source = "vcpkg" }
spdlog = { version = "1.12.0", source = "vcpkg" }
```

Or use the CLI:

```bash
cforge add fmt
cforge add spdlog
```

Then rebuild:

```bash
cforge build
```

CForge will automatically install dependencies via vcpkg.

## Running Tests

```bash
# Run all tests
cforge test

# Run with filter
cforge test --filter "Unit*"
```

## Developer Workflow

### Watch Mode

Auto-rebuild on file changes:

```bash
# Watch and rebuild
cforge watch

# Watch, rebuild, and run
cforge watch --run
```

### Code Formatting

```bash
# Format all source files
cforge fmt

# Check formatting without modifying
cforge fmt --check
```

### Static Analysis

```bash
# Run clang-tidy
cforge lint

# Auto-fix issues
cforge lint --fix
```

## IDE Setup

Generate IDE project files:

```bash
# VS Code
cforge ide vscode

# CLion
cforge ide clion

# Visual Studio
cforge ide vs
```

## Next Steps

- [Project Configuration](./project-configuration) - Full configuration reference
- [Dependencies](./dependencies) - Dependency management guide
- [Command Reference](./command-reference) - All available commands
- [Workspaces](./workspaces) - Multi-project setups
