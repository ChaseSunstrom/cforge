---
id: dependencies
title: Working with Dependencies
---

## Working with Dependencies

CForge supports a unified dependency configuration with multiple sources. Dependencies are consolidated under the `[dependencies]` section with source options: `index` (default), `git`, `vcpkg`, `system`, and `project`.

### Package Registry (Index Dependencies)

CForge has a built-in package registry similar to Cargo. Search and add packages easily:

```bash
# Search for packages
cforge search json

# Get package info
cforge info spdlog --versions

# Add a package (defaults to registry)
cforge add fmt@11.1.4

# Add with specific features
cforge add spdlog@1.15.0 --features async,stdout
```

Registry dependencies in `cforge.toml`:

```toml
[dependencies]
# Simple version constraint - uses CMake FetchContent by default
fmt = "11.1.4"
tomlplusplus = "3.4.0"

# With features and options
spdlog = { version = "1.15.0", features = ["async", "stdout"] }

# Header-only library
nlohmann-json = { version = "3.11.3", header_only = true }

# Wildcard versions (like Rust)
catch2 = "3.*"          # Any 3.x version
benchmark = "1.9.*"     # Any 1.9.x version
```

By default, index dependencies use CMake's FetchContent to download packages during the CMake configure step. This is the recommended approach as it integrates seamlessly with CMake's dependency management.

To disable FetchContent and pre-clone packages instead:

```toml
[dependencies]
fetch_content = false   # Pre-clone packages to vendor/ directory
directory = "vendor"    # Where to clone packages (default: "deps")
fmt = "11.1.4"
```

### Git Dependencies

For packages not in the registry, use Git directly:

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

Or use the CLI:

```bash
cforge add fmt --git https://github.com/fmtlib/fmt.git --tag 11.1.4
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

### vcpkg Integration

[vcpkg](https://vcpkg.io/) is a C/C++ package manager from Microsoft. CForge integrates seamlessly with vcpkg:

```toml
[dependencies.vcpkg]
enabled = true
path = "~/.vcpkg"          # Optional: directory of vcpkg installation
triplet = "x64-windows"    # Optional: specify vcpkg target triplet

[dependencies]
boost = { vcpkg = true }
openssl = { vcpkg = true, features = ["ssl", "crypto"] }
```

Or use the CLI:

```bash
cforge add boost --vcpkg
cforge vcpkg install openssl
```

CForge will automatically:
- Install vcpkg if not found
- Install the specified packages
- Configure CMake to use vcpkg's toolchain file

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

### Platform-Specific Dependencies

Add dependencies only for specific platforms:

```toml
[platform.windows.dependencies]
winapi = { vcpkg = true }

[platform.linux.dependencies]
x11 = { system = true, method = "pkg_config", package = "x11" }

[platform.macos.dependencies]
cocoa = { system = true, frameworks = ["Cocoa", "IOKit"] }
```

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

### Updating Dependencies

```bash
# Update all packages from registry
cforge update --packages

# Update cforge itself
cforge update --self
```

### Dependency Tree

Visualize your project's dependencies:

```bash
cforge tree
```

Output (with colors):
```
myproject v1.0.0
|-- fmt @ 11.1.4 (index)
|-- tomlplusplus @ 3.4.0 (index)
|-- boost (vcpkg)
`-- OpenGL (system)

Dependencies: 2 index, 1 vcpkg, 1 system
```

Dependencies are color-coded by type:
- **Blue**: Index dependencies (from registry)
- **Cyan**: Git dependencies
- **Magenta**: vcpkg dependencies
- **Yellow**: System dependencies
- **Green**: Project dependencies (workspace)

### Managing Dependencies

```bash
# Add a registry dependency
cforge add fmt@11.1.4

# Add a git dependency
cforge add fmt --git https://github.com/fmtlib/fmt.git --tag 11.1.4

# Add a vcpkg dependency
cforge add boost --vcpkg

# Remove a dependency
cforge remove fmt

# Update all dependencies
cforge deps update
```
