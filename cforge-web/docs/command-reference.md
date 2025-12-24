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
| `deps`       | Manage dependencies                      | `cforge deps add fmt`              |
| `package`    | Package project binaries                 | `cforge package --type zip`        |

## Developer Tools

| Command       | Description                             | Example                            |
|---------------|-----------------------------------------|------------------------------------|
| `fmt`         | Format code with clang-format           | `cforge fmt --check`               |
| `lint`        | Run clang-tidy static analysis          | `cforge lint --fix`                |
| `watch`       | Watch files and rebuild on changes      | `cforge watch --run`               |
| `doc`         | Generate documentation with Doxygen     | `cforge doc --open`                |
| `bench`       | Run benchmarks                          | `cforge bench --filter BM_*`       |
| `new`         | Generate code from templates            | `cforge new class MyClass`         |
| `completions` | Generate shell completions              | `cforge completions bash`          |

## Dependency Management

All dependency operations use the unified `cforge deps` command:

| Subcommand     | Description                              | Example                                |
|----------------|------------------------------------------|----------------------------------------|
| `deps add`     | Add a dependency to the project          | `cforge deps add fmt@11.1.4`           |
| `deps remove`  | Remove a dependency                      | `cforge deps remove spdlog`            |
| `deps search`  | Search package registry                  | `cforge deps search json`              |
| `deps info`    | Show package details                     | `cforge deps info spdlog --versions`   |
| `deps list`    | List current dependencies                | `cforge deps list`                     |
| `deps tree`    | Display dependency tree                  | `cforge deps tree --depth 3`           |
| `deps lock`    | Manage lock file                         | `cforge deps lock --verify`            |
| `deps update`  | Update package registry                  | `cforge deps update`                   |
| `deps outdated`| Show outdated dependencies               | `cforge deps outdated`                 |

## Project Management

| Command      | Description                              | Example                            |
|--------------|------------------------------------------|------------------------------------|
| `update`     | Update cforge itself                     | `cforge update --self`             |
| `list`       | List project information                 | `cforge list`                      |
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

### deps tree

Display the project's dependency tree.

```bash
# Show dependency tree
cforge deps tree

# Limit depth
cforge deps tree --depth 2

# Show all details
cforge deps tree -v
```

**Output:**
```
my_app v1.0.0
├── fmt (10.1.0) [index]
├── spdlog (1.12.0) [index]
│   └── fmt (10.1.0) [index]
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

Unified dependency management command. All dependency operations use `cforge deps <subcommand>`.

```bash
# Show all deps subcommands
cforge deps --help

# List current dependencies
cforge deps list

# Check for outdated dependencies
cforge deps outdated

# Update package registry
cforge deps update
```

### deps search

Search the cforge package registry.

```bash
# Search for packages
cforge deps search json

# Search for logging libraries
cforge deps search log
```

### deps info

Get detailed information about a package.

```bash
# Show package info
cforge deps info spdlog

# Show available versions
cforge deps info fmt --versions
```

### deps add / deps remove

Manage dependencies in cforge.toml.

```bash
# Add from registry (default)
cforge deps add fmt

# Add with specific version
cforge deps add fmt@11.1.4

# Add with features
cforge deps add spdlog --features async

# Add from Git
cforge deps add mylib --git https://github.com/user/mylib --tag v1.0

# Add from vcpkg
cforge deps add boost --vcpkg

# Remove a dependency
cforge deps remove spdlog
```

**Options:**
| Option | Description |
|--------|-------------|
| `@version` | Specify version (e.g., `fmt@11.1.4`) |
| `--git <url>` | Add as Git dependency |
| `--tag <tag>` | Git tag (with --git) |
| `--branch <name>` | Git branch (with --git) |
| `--vcpkg` | Add as vcpkg package |
| `--features <list>` | Comma-separated features |
| `--header-only` | Mark as header-only library |

### deps outdated

Check for dependencies with newer versions available.

```bash
# Show outdated dependencies
cforge deps outdated
```

**Output:**
```
Package         Current    Latest     Source
--------------  ---------  ---------  ------
fmt             11.1.4     12.1.0     index
spdlog          1.12.0     1.15.0     index

Found 2 outdated package(s)
```

### deps lock

Manage dependency lock file for reproducible builds.

```bash
# Generate/update lock file
cforge deps lock

# Verify dependencies match lock
cforge deps lock --verify

# Force regeneration
cforge deps lock --force

# Remove lock file
cforge deps lock --clean
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
