---
id: command-reference
title: Command Reference
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

### Init Command

The `init` command creates new cforge projects or workspaces. It supports several modes of operation:

#### Creating a Single Project

```bash
# Create a project in the current directory using the directory name as project name
cforge init

# Create a project with a specific name (in a new directory)
cforge init my_project

# Create a project with specific options
cforge init --name=my_project --cpp=20 --with-tests --with-git
```

#### Creating Multiple Projects

```bash
# Create multiple standalone projects (each in their own directory)
cforge init --projects project1 project2 project3

# Alternative syntax with comma-separated list
cforge init --projects=project1,project2,project3
```

#### Creating a Workspace with Projects

```bash
# Create a workspace with default project
cforge init --workspace my_workspace

# Create a workspace with multiple projects
cforge init --workspace my_workspace --projects app lib tests

# Create a workspace with C++20 standard for all projects
cforge init --workspace my_workspace --projects app lib --cpp=20 --with-tests
```

#### Init Command Options

| Option            | Description                                   | Example                      |
|-------------------|-----------------------------------------------|-----------------------------|
| `--name`          | Set project name                              | `--name=my_app`             |
| `--workspace`     | Create a workspace                            | `--workspace my_workspace`  |
| `--projects`      | Create multiple projects                      | `--projects app lib tests`  |
| `--cpp`           | Set C++ standard                              | `--cpp=20`                  |
| `--template`      | Use a specific template                       | `--template=lib`            |
| `--with-tests`    | Add test infrastructure                       | `--with-tests`              |
| `--with-git`      | Initialize Git repository                     | `--with-git`                |
| `--from-file`     | Initialize from existing workspace file       | `--from-file`               |