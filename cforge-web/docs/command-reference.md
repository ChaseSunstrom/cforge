---
id: command-reference
title: Command Reference
---

# Command Reference

CForge provides a comprehensive set of commands for building, testing, and managing C/C++ projects.

## Core Commands

| Command      | Description                              | Example                            |
|--------------|------------------------------------------|------------------------------------|
| `init`       | Create new project/workspace             | `cforge init --template lib`       |
| `build`      | Build the project                        | `cforge build --config Release`    |
| `clean`      | Clean build artifacts                    | `cforge clean`                     |
| `run`        | Run built executable                     | `cforge run -- arg1 arg2`          |
| `test`       | Execute tests (CTest integration)        | `cforge test --filter MyTest`      |
| `install`    | Install project binaries                 | `cforge install --prefix /usr/local`|
| `deps`       | Manage dependencies                      | `cforge deps --update`             |
| `package`    | Package project binaries                 | `cforge package --type zip`        |

## Developer Tools

| Command       | Description                             | Example                            |
|---------------|-----------------------------------------|------------------------------------|
| `fmt`         | Format code with clang-format           | `cforge fmt --check`               |
| `lint`        | Run clang-tidy static analysis          | `cforge lint --fix`                |
| `watch`       | Watch files and rebuild on changes      | `cforge watch --run`               |
| `doc`         | Generate documentation with Doxygen     | `cforge doc --open`                |
| `bench`       | Run benchmarks                          | `cforge bench --filter BM_*`       |
| `tree`        | Display dependency tree                 | `cforge tree --depth 3`            |
| `new`         | Generate code from templates            | `cforge new class MyClass`         |
| `completions` | Generate shell completions              | `cforge completions bash`          |

## Project Management

| Command      | Description                              | Example                            |
|--------------|------------------------------------------|------------------------------------|
| `add`        | Add a dependency to the project          | `cforge add fmt`                   |
| `remove`     | Remove a dependency                      | `cforge remove spdlog`             |
| `update`     | Update dependencies                      | `cforge update`                    |
| `lock`       | Lock dependency versions                 | `cforge lock`                      |
| `list`       | List variants, configs, or targets       | `cforge list variants`             |
| `ide`        | Generate IDE project files               | `cforge ide vscode`                |
| `vcpkg`      | Manage vcpkg integration                 | `cforge vcpkg install`             |

## Utility Commands

| Command      | Description                              | Example                            |
|--------------|------------------------------------------|------------------------------------|
| `version`    | Display version information              | `cforge version`                   |
| `help`       | Show help for commands                   | `cforge help build`                |

---

## Global Options

All commands accept these global options:

| Option          | Description                                    |
|-----------------|------------------------------------------------|
| `-v, --verbose` | Enable verbose output                          |
| `-q, --quiet`   | Suppress non-essential output                  |
| `-c, --config`  | Build configuration (Debug, Release, etc.)     |

---

## Command Details

### init

Create a new cforge project or workspace.

```bash
# Create a project in current directory
cforge init

# Create a named project
cforge init my_project

# Create with options
cforge init --name=my_project --cpp=20 --with-tests --with-git

# Create a workspace with projects
cforge init --workspace my_workspace --projects app lib tests
```

**Options:**
| Option | Description |
|--------|-------------|
| `--name` | Set project name |
| `--workspace` | Create a workspace |
| `--projects` | Create multiple projects |
| `--cpp` | Set C++ standard (11, 14, 17, 20, 23) |
| `--template` | Use template (exe, lib, header-only) |
| `--with-tests` | Add test infrastructure |
| `--with-git` | Initialize Git repository |

### build

Build the project using CMake.

```bash
# Debug build (default)
cforge build

# Release build
cforge build --config Release

# Verbose output
cforge build -v
```

**Output:**
```
   Compiling my_app v1.0.0
   Compiling src/main.cpp
   Compiling src/utils.cpp
    Finished Debug target(s) in 2.34s
```

### run

Build and run the executable.

```bash
# Run the default target
cforge run

# Pass arguments
cforge run -- --help --verbose

# Run specific target
cforge run my_target
```

### test

Run tests using CTest.

```bash
# Run all tests
cforge test

# Filter tests
cforge test --filter "Unit*"

# Verbose output
cforge test -v
```

### fmt

Format source code using clang-format.

```bash
# Format all files
cforge fmt

# Check formatting (no changes)
cforge fmt --check

# Format specific path
cforge fmt src/
```

**Requirements:** clang-format must be installed and available in PATH.

### lint

Run static analysis using clang-tidy.

```bash
# Analyze all files
cforge lint

# Auto-fix issues
cforge lint --fix

# Specific checks
cforge lint --checks="modernize-*"
```

**Requirements:** clang-tidy must be installed and available in PATH.

### watch

Watch for file changes and automatically rebuild.

```bash
# Watch and rebuild
cforge watch

# Watch and run after build
cforge watch --run

# Custom poll interval
cforge watch --interval 2
```

Press `Ctrl+C` to stop watching.

### doc

Generate documentation using Doxygen.

```bash
# Generate documentation
cforge doc

# Initialize Doxyfile
cforge doc --init

# Generate and open in browser
cforge doc --open
```

**Requirements:** Doxygen must be installed and available in PATH.

### bench

Run benchmark executables.

```bash
# Run all benchmarks
cforge bench

# Filter benchmarks
cforge bench --filter "BM_Sort*"

# Output formats
cforge bench --json
cforge bench --csv

# Skip build step
cforge bench --no-build
```

### tree

Display the project's dependency tree.

```bash
# Show dependency tree
cforge tree

# Limit depth
cforge tree --depth 2

# Show all details
cforge tree -v
```

**Output:**
```
my_app v1.0.0
├── fmt (10.1.0) [vcpkg]
├── spdlog (1.12.0) [vcpkg]
│   └── fmt (10.1.0) [vcpkg]
└── my_lib (local) [project]
```

### new

Generate code from templates.

```bash
# Create a class
cforge new class MyClass

# Create a header file
cforge new header utils

# Create with namespace
cforge new class MyClass -n myapp

# Create a test file
cforge new test MyClassTest

# Available templates: class, header, struct, interface, test, main
```

### completions

Generate shell completion scripts.

```bash
# Bash
cforge completions bash > ~/.local/share/bash-completion/completions/cforge

# Zsh
cforge completions zsh > ~/.zfunc/_cforge

# PowerShell
cforge completions powershell >> $PROFILE

# Fish
cforge completions fish > ~/.config/fish/completions/cforge.fish
```

### ide

Generate IDE project files.

```bash
# VS Code
cforge ide vscode

# CLion
cforge ide clion

# Visual Studio
cforge ide vs

# Xcode (macOS only)
cforge ide xcode
```

### deps

Manage project dependencies.

```bash
# Show dependencies
cforge deps

# Update all dependencies
cforge deps --update

# Check for outdated
cforge deps --check
```

### add / remove

Manage dependencies in cforge.toml.

```bash
# Add a vcpkg dependency
cforge add fmt

# Add with version
cforge add "spdlog@1.12.0"

# Remove a dependency
cforge remove spdlog
```

### package

Create distributable packages.

```bash
# Create a ZIP package
cforge package --type zip

# Create installer (platform-specific)
cforge package --type installer
```

### list

List project information.

```bash
# List build variants
cforge list variants

# List targets
cforge list targets

# List configurations
cforge list configs
```
