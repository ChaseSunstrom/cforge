---
id: workspaces
title: Workspaces
---

## Workspaces

Workspaces allow you to manage multiple related CForge projects together, sharing dependencies and enabling cross-project references.

### Creating a Workspace

```bash
# Initialize a workspace with projects
cforge init --workspace my_workspace --projects core gui

# Or create an empty workspace
cforge init --workspace my_workspace
```

This generates a `cforge-workspace.toml`:

```toml
[workspace]
name = "my_workspace"
projects = ["core", "gui"]
default_startup_project = "core"
```

### Workspace Structure

```
my_workspace/
├── cforge-workspace.toml    # Workspace configuration
├── core/
│   ├── cforge.toml          # Core project config
│   ├── src/
│   └── include/
└── gui/
    ├── cforge.toml          # GUI project config
    ├── src/
    └── include/
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

Projects in a workspace can depend on each other:

```toml
# gui/cforge.toml
[dependencies.project.core]
include_dirs = ["include"]
link = true
link_type = "PRIVATE"
```

#### Dependency Options

| Option | Description |
|--------|-------------|
| `include_dirs` | Include directories to expose |
| `link` | Whether to link the project library |
| `link_type` | Link visibility: `PUBLIC`, `PRIVATE`, or `INTERFACE` |
| `target_name` | Override the CMake target name |

### Build Order

CForge automatically determines the correct build order based on project dependencies:

```bash
$ cforge list build-order

Build order for workspace 'my_workspace':
  1. core
  2. gui (depends on: core)
```

### Workspace Configuration Options

```toml
[workspace]
name = "my_workspace"
projects = ["core", "gui", "tools"]
default_startup_project = "gui"

# Shared build settings
[workspace.build]
build_type = "Debug"
directory = "build"

# Shared dependencies for all projects
[workspace.dependencies.git.fmt]
url = "https://github.com/fmtlib/fmt.git"
tag = "11.1.4"
```

### Running Projects

```bash
# Run the default startup project
cforge run

# Run a specific project
cforge run -p gui

# Set default startup project
cforge startup gui
```

### Cleaning

```bash
# Clean all projects
cforge clean

# Clean specific project
cforge clean -p core

# Clean all build artifacts
cforge clean --all
```

### IDE Integration

Generate IDE project files for the workspace:

```bash
# Visual Studio Code
cforge ide vscode

# CLion
cforge ide clion

# Visual Studio 2022
cforge ide vs2022
```
