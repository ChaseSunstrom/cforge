---
id: dependencies
title: Working with Dependencies
---

## Working with Dependencies

CForge supports multiple dependency management systems to make it easy to include external libraries in your projects.

### vcpkg Integration

[vcpkg](https://vcpkg.io/) is a C/C++ package manager from Microsoft. CForge integrates seamlessly with vcpkg:

```toml
[dependencies.vcpkg]
enabled = true
path = "~/.vcpkg"          # Optional: directory of vcpkg installation
triplet = "x64-windows"    # Optional: specify vcpkg target triplet
packages = ["fmt", "boost", "nlohmann-json"]
```

CForge will automatically:
- Install vcpkg if not found
- Install the specified packages
- Configure CMake to use vcpkg's toolchain file

### Git Dependencies

Clone dependencies directly from Git repositories:

```toml
[dependencies.git.fmt]
url = "https://github.com/fmtlib/fmt.git"
tag = "11.1.4"

[dependencies.git.nlohmann_json]
url = "https://github.com/nlohmann/json.git"
tag = "v3.11.3"

[dependencies.git.imgui]
url = "https://github.com/ocornut/imgui.git"
branch = "master"
shallow = true
```

#### Git Dependency Options

| Option | Description |
|--------|-------------|
| `url` | Repository URL (required) |
| `tag` | Git tag to checkout |
| `branch` | Git branch to checkout |
| `commit` | Specific commit hash |
| `shallow` | Use shallow clone (faster) |
| `directory` | Custom clone directory |

Git dependencies are automatically cloned into the `deps` directory and included as CMake subdirectories.

### System Dependencies

System dependencies support three methods: `find_package`, `pkg_config`, and `manual`:

#### find_package Method

Use CMake's `find_package` to locate system-installed libraries:

```toml
[dependencies.system.OpenGL]
method = "find_package"
required = true
components = ["GL", "GLU"]
target = "OpenGL::GL"
```

#### pkg-config Method

Use pkg-config to locate libraries (common on Linux):

```toml
[dependencies.system.x11]
method = "pkg_config"
package = "x11"
platforms = ["linux"]  # Only on Linux
```

#### Manual Method

Manually specify library paths and flags:

```toml
[dependencies.system.custom_lib]
method = "manual"
include_dirs = ["/usr/local/include/custom"]
library_dirs = ["/usr/local/lib"]
libraries = ["custom", "custom_util"]
defines = ["USE_CUSTOM_LIB"]
platforms = ["linux", "macos"]  # Limit to specific platforms
```

#### System Dependency Options

| Option | Description |
|--------|-------------|
| `method` | Detection method: `find_package`, `pkg_config`, or `manual` |
| `required` | Whether the dependency is required (default: true) |
| `components` | CMake components for find_package |
| `target` | CMake target name to link |
| `package` | Package name for pkg-config |
| `include_dirs` | Include directories (manual method) |
| `library_dirs` | Library directories (manual method) |
| `libraries` | Library names to link (manual method) |
| `defines` | Preprocessor definitions |
| `platforms` | Limit to specific platforms |

### Subdirectory Dependencies

Use existing CMake projects as dependencies:

```toml
[dependencies.subdirectory.spdlog]
path = "extern/spdlog"
target = "spdlog::spdlog"
options = { SPDLOG_BUILD_TESTS = "OFF" }

[dependencies.subdirectory.glfw]
path = "extern/glfw"
target = "glfw"
options = { GLFW_BUILD_EXAMPLES = "OFF", GLFW_BUILD_TESTS = "OFF" }
```

#### Subdirectory Options

| Option | Description |
|--------|-------------|
| `path` | Path to the CMake project directory |
| `target` | CMake target to link |
| `options` | CMake options to set before add_subdirectory |

### Project Dependencies (Workspaces)

In a workspace, depend on other projects:

```toml
[dependencies.project.core]
include_dirs = ["include"]
link = true
link_type = "PRIVATE"
```

See [Workspaces](workspaces) for more details.

### Dependency Lock File

Ensure reproducible builds with lock files:

```bash
# Generate/update lock file
cforge lock

# Verify dependencies match lock file
cforge lock --verify

# Force regeneration
cforge lock --force
```

The lock file (`cforge.lock`) records the exact versions of all dependencies, ensuring consistent builds across different machines and times.

### Dependency Tree

Visualize your project's dependencies:

```bash
cforge tree
```

Output:
```
myproject v1.0.0
|-- fmt @ 11.1.4 (git)
|-- nlohmann_json @ v3.11.3 (git)
|-- boost (vcpkg)
`-- OpenGL (system)

Dependencies: 2 git, 1 vcpkg, 1 system
```

### Managing Dependencies

```bash
# Add a git dependency
cforge add fmt --git https://github.com/fmtlib/fmt.git --tag 11.1.4

# Add a vcpkg dependency
cforge add boost --vcpkg

# Remove a dependency
cforge remove fmt

# Update all dependencies
cforge deps update
```
