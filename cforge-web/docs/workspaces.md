---
id: workspaces
title: Workspaces
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

# Set the startup project
cforge startup my_app

# List all projects in workspace
cforge startup --list

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
default_startup_project = "projects/app" ``
```

### Project Dependencies within Workspace

```toml 
# projects/app/cforge.toml
[dependencies.workspace]
name = "lib"
link_type = "static"  # static, shared, interface 
```