# cforge

A modern C/C++ build tool with Cargo-like simplicity.

[![Version](https://img.shields.io/badge/version-3.0.1-blue.svg)](https://github.com/ChaseSunstrom/cforge/releases)
[![License](https://img.shields.io/badge/license-PolyForm%20NC-green.svg)](LICENSE)

cforge simplifies C/C++ project management with a clean TOML configuration, automatic dependency resolution, and cross-platform builds. It generates CMake under the hood while providing a much simpler interface.

## Quick Start

```bash
# Install cforge
curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash

# Create a new project
cforge init myapp
cd myapp

# Build and run
cforge build
cforge run
```

## Features

- **Simple TOML configuration** - No CMake syntax required
- **Portable compiler flags** - Write once, works on MSVC, GCC, and Clang
- **Package registry** - Search and install packages with `cforge add <package>`
- **Multiple dependency sources** - Registry, Git, vcpkg, and system libraries
- **Workspaces** - Manage multi-project repositories
- **Cross-compilation** - Android, iOS, Raspberry Pi, WebAssembly
- **IDE integration** - VS Code, CLion, Visual Studio, Xcode
- **Testing & benchmarks** - Integrated test runner with Catch2, GTest, doctest
- **Developer tools** - Formatting, linting, watch mode, documentation
- **Lock files** - Reproducible builds with exact dependency versions
- **Shell completions** - Bash, Zsh, PowerShell, Fish

## Installation

### Linux / macOS

```bash
curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash
```

### Windows (PowerShell)

```powershell
irm https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.ps1 | iex
```

### Prerequisites

- CMake 3.15+
- C/C++ compiler (GCC, Clang, or MSVC)

### Building from Source

```bash
git clone https://github.com/ChaseSunstrom/cforge.git
cd cforge
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build
```

### Shell Completions

```bash
cforge completions bash >> ~/.bashrc
cforge completions zsh >> ~/.zshrc
cforge completions fish > ~/.config/fish/completions/cforge.fish
cforge completions powershell >> $PROFILE
```

## Configuration

Projects are configured with a `cforge.toml` file:

```toml
[project]
name = "myapp"
version = "1.0.0"
cpp_standard = "17"
binary_type = "executable"

[build]
build_type = "Release"
export_compile_commands = true

[build.config.debug]
optimize = "debug"
debug_info = true
warnings = "all"
sanitizers = ["address"]

[build.config.release]
optimize = "speed"
lto = true
warnings = "all"

[dependencies]
fmt = "11.1.4"
spdlog = "1.12.0"
```

## Commands Reference

### Project Management

| Command | Description |
|---------|-------------|
| `cforge init [name]` | Initialize a new project in current or new directory |
| `cforge new <template> <name>` | Create files from templates (class, header, interface, test) |
| `cforge build` | Build the project |
| `cforge run` | Build and run the project |
| `cforge clean` | Clean build artifacts |
| `cforge install` | Install project to system |

### Dependencies

| Command | Description |
|---------|-------------|
| `cforge add <pkg>` | Add a dependency |
| `cforge remove <pkg>` | Remove a dependency |
| `cforge search <query>` | Search package registry |
| `cforge info <pkg>` | Show package details |
| `cforge deps` | Manage Git dependencies |
| `cforge list` | List dependencies or projects |
| `cforge tree` | Visualize dependency tree |
| `cforge lock` | Manage lock file for reproducible builds |
| `cforge update` | Update cforge or package registry |

### Testing & Quality

| Command | Description |
|---------|-------------|
| `cforge test` | Run tests |
| `cforge bench` | Run benchmarks |
| `cforge fmt` | Format code with clang-format |
| `cforge lint` | Static analysis with clang-tidy |
| `cforge doc` | Generate documentation with Doxygen |

### Other

| Command | Description |
|---------|-------------|
| `cforge ide` | Generate IDE project files |
| `cforge watch` | Watch for changes and auto-rebuild |
| `cforge package` | Create distributable packages |
| `cforge vcpkg` | Manage vcpkg dependencies |
| `cforge completions` | Generate shell completions |
| `cforge version` | Show version information |
| `cforge help <cmd>` | Show help for a command |

---

## Project Initialization

### Create a New Project

```bash
cforge init myapp                      # Create in new directory
cforge init                            # Initialize in current directory
cforge init --type=shared_lib          # Create a shared library
cforge init --std=c++20                # Use C++20
cforge init --with-tests               # Include test infrastructure
cforge init --template lib             # Use library template
```

### Create a Workspace

```bash
cforge init --workspace myws                    # Create empty workspace
cforge init --workspace myws --projects app lib # Workspace with projects
```

### Generate Code from Templates

```bash
cforge new class MyClass                # Create MyClass.hpp and MyClass.cpp
cforge new class MyClass -n myproj      # With namespace
cforge new header utils                 # Create utils.hpp
cforge new interface IService           # Create abstract interface
cforge new test MyClass                 # Create test file
cforge new struct Config                # Create struct header
```

---

## Building

```bash
cforge build                           # Debug build (default)
cforge build -c Release                # Release build
cforge build -j8                       # Parallel build with 8 jobs
cforge build -v                        # Verbose output
cforge build -t mytarget               # Build specific target
cforge build --force-regenerate        # Clean rebuild with fresh CMake
cforge build --skip-deps               # Skip updating Git dependencies
cforge build --profile android-arm64   # Cross-compile with profile
```

### In Workspaces

```bash
cforge build                           # Build main project
cforge build -p mylib                  # Build specific project
cforge build --gen-workspace-cmake     # Generate workspace CMakeLists.txt
```

---

## Running

```bash
cforge run                             # Build and run
cforge run -c Release                  # Run release build
cforge run --no-build                  # Run without rebuilding
cforge run -- arg1 arg2                # Pass arguments to executable
cforge run -p myapp -- --config file   # Run specific project with args
```

---

## Dependencies

### Registry Packages

```toml
[dependencies]
fmt = "11.1.4"
spdlog = "*"           # Latest version
json = "3.11.2"
```

```bash
cforge add fmt                         # Add from registry
cforge add fmt@11.1.4                  # Specific version
cforge add spdlog --features async     # With features
cforge search json                     # Search packages
cforge info nlohmann_json              # Package details
```

### Git Dependencies

```toml
[dependencies.git.imgui]
url = "https://github.com/ocornut/imgui.git"
tag = "v1.89.9"
```

```bash
cforge add mylib --git https://github.com/user/mylib --tag v1.0
cforge deps fetch                      # Update all Git deps
cforge deps checkout                   # Checkout configured refs
cforge deps list                       # List Git dependencies
```

### vcpkg Dependencies

```toml
[dependencies.vcpkg]
curl = { version = "7.80.0", features = ["ssl"] }
```

```bash
cforge add boost --vcpkg
```

### System Dependencies

```toml
[dependencies.system.OpenGL]
method = "find_package"
components = ["GL", "GLU"]

[dependencies.system.gtk3]
method = "pkg_config"
package = "gtk+-3.0"
```

### Lock Files

```bash
cforge lock                            # Generate/update lock file
cforge lock --verify                   # Verify deps match lock
cforge lock --force                    # Force regeneration
cforge lock --clean                    # Remove lock file
```

Commit `cforge.lock` to version control for reproducible builds.

### Dependency Tree

```bash
cforge tree                            # Show dependency tree
cforge tree -a                         # Include transitive deps
cforge tree -d 2                       # Limit depth
cforge tree -i                         # Inverted (show dependents)
```

---

## Portable Compiler Flags

cforge provides portable build options that automatically translate to the correct flags for each compiler:

| Option | Values | Description |
|--------|--------|-------------|
| `optimize` | `"none"`, `"debug"`, `"size"`, `"speed"`, `"aggressive"` | Optimization level |
| `warnings` | `"none"`, `"default"`, `"all"`, `"strict"`, `"pedantic"` | Warning level |
| `debug_info` | `true` / `false` | Include debug symbols |
| `sanitizers` | `["address", "undefined", "thread", "leak"]` | Runtime sanitizers |
| `lto` | `true` / `false` | Link-time optimization |
| `exceptions` | `true` / `false` | C++ exceptions |
| `rtti` | `true` / `false` | Runtime type info |
| `hardening` | `"none"`, `"basic"`, `"full"` | Security hardening |
| `stdlib` | `"default"`, `"libc++"`, `"libstdc++"` | Standard library |
| `visibility` | `"default"`, `"hidden"` | Symbol visibility |

### Flag Translation

| Portable | MSVC | GCC/Clang |
|----------|------|-----------|
| `optimize = "speed"` | `/O2` | `-O2` |
| `optimize = "size"` | `/O1 /Os` | `-Os` |
| `optimize = "aggressive"` | `/Ox` | `-O3` |
| `warnings = "all"` | `/W4` | `-Wall -Wextra` |
| `warnings = "strict"` | `/W4 /WX` | `-Wall -Wextra -Werror` |
| `lto = true` | `/GL` + `/LTCG` | `-flto` |
| `debug_info = true` | `/Zi` | `-g` |
| `sanitizers = ["address"]` | `/fsanitize=address` | `-fsanitize=address` |

---

## Platform & Compiler Configuration

```toml
# Platform-specific settings
[platform.windows]
hardening = "full"
defines = ["WIN32"]
links = ["ws2_32"]

[platform.linux]
hardening = "basic"
links = ["pthread"]

[platform.macos]
stdlib = "libc++"
frameworks = ["Cocoa"]

# Compiler-specific settings
[compiler.msvc]
warnings = "strict"
defines = ["_CRT_SECURE_NO_WARNINGS"]

[compiler.gcc]
warnings = "pedantic"

[compiler.clang]
flags = ["-fcolor-diagnostics"]
```

---

## Workspaces

For multi-project repositories:

```toml
# workspace.toml or cforge.toml
[workspace]
name = "my-workspace"
projects = ["app", "lib", "tests"]
main_project = "app"
```

```bash
cforge build                           # Build main project
cforge build -p lib                    # Build specific project
cforge build --gen-workspace-cmake     # Generate workspace CMake
```

---

## Testing

```toml
[test]
enabled = true
framework = "catch2"  # or "gtest", "doctest", "boost"
```

```bash
cforge test                            # Run all tests
cforge test -c Release                 # Test release build
cforge test Math                       # Run tests in Math category
cforge test -c Release -- Math Add     # Specific tests
cforge test -v                         # Verbose output
```

---

## Benchmarks

```toml
[benchmark]
directory = "bench"
target = "my_benchmarks"
```

```bash
cforge bench                           # Run all benchmarks
cforge bench --filter 'BM_Sort'        # Run matching benchmarks
cforge bench --no-build                # Skip rebuild
cforge bench --json > results.json     # JSON output
cforge bench --csv                     # CSV output
```

Benchmarks run in Release mode by default.

---

## Developer Tools

### Code Formatting

```bash
cforge fmt                             # Format all source files
cforge fmt --check                     # Check without modifying
cforge fmt --diff                      # Show what would change
cforge fmt --style=google              # Use specific style
```

Uses `.clang-format` if present. Requires clang-format.

### Static Analysis

```bash
cforge lint                            # Run clang-tidy checks
cforge lint --fix                      # Apply automatic fixes
cforge lint --checks='modernize-*'     # Run specific checks
```

Uses `.clang-tidy` if present. Requires clang-tidy.

### Documentation

```bash
cforge doc                             # Generate Doxygen docs
cforge doc --init                      # Create Doxyfile only
cforge doc --open                      # Generate and open in browser
cforge doc -o api-docs                 # Custom output directory
```

Requires Doxygen.

### Watch Mode

```bash
cforge watch                           # Watch and rebuild
cforge watch --run                     # Also run after build
cforge watch -c Release                # Watch with Release config
cforge watch --interval 1000           # Custom poll interval (ms)
```

Watches `.cpp`, `.cc`, `.cxx`, `.c`, `.hpp`, `.hxx`, `.h`, `.toml` files.

---

## Cross-Compilation

```toml
[cross.profile.android-arm64]
system = "Android"
processor = "aarch64"
toolchain = "${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
variables = { ANDROID_ABI = "arm64-v8a" }

[cross.profile.raspberry-pi]
system = "Linux"
processor = "arm"
c_compiler = "arm-linux-gnueabihf-gcc"
cxx_compiler = "arm-linux-gnueabihf-g++"
```

```bash
cforge build --profile android-arm64
cforge build --profile raspberry-pi
```

---

## IDE Integration

```bash
cforge ide vscode                      # Generate VS Code config
cforge ide clion                       # Generate CLion project
cforge ide vs                          # Open in Visual Studio
cforge ide xcode                       # Generate Xcode project (macOS)
```

---

## Packaging

```toml
[package]
enabled = true
generators = ["ZIP", "TGZ", "DEB"]
vendor = "Your Name"
contact = "you@example.com"
```

```bash
cforge package                         # Create packages
cforge package -c Release              # Package release build
cforge package -t ZIP                  # Specific generator
cforge package --no-build              # Skip rebuild
```

---

## Installation

Install a project to the system:

```bash
cforge install                         # Install to default location
cforge install --to /opt/myapp         # Custom install path
cforge install --add-to-path           # Add to PATH
cforge install --from https://github.com/user/repo.git  # From URL
```

---

## CMake Options

```toml
[build]
export_compile_commands = true         # CMAKE_EXPORT_COMPILE_COMMANDS
position_independent_code = true       # CMAKE_POSITION_INDEPENDENT_CODE

[build.cmake_variables]
MY_OPTION = "ON"
CUSTOM_VAR = "value"
```

### Advanced CMake

```toml
[cmake]
includes = ["cmake/custom.cmake"]
module_paths = ["cmake/modules"]
inject_before_target = "find_package(Boost REQUIRED)"
inject_after_target = "target_precompile_headers(${PROJECT_NAME} PRIVATE pch.h)"
```

---

## Project Structure

```
myproject/
├── cforge.toml          # Project configuration
├── cforge.lock          # Dependency lock file
├── src/
│   └── main.cpp
├── include/
│   └── myproject/
├── tests/
│   └── test_main.cpp
├── bench/
│   └── benchmark.cpp
└── build/               # Build output (generated)
```

---

## Cleaning

```bash
cforge clean                           # Clean current config
cforge clean --all                     # Clean all configurations
cforge clean -c Release                # Clean specific config
cforge clean --cmake-files             # Also clean CMake files
cforge clean --regenerate              # Regenerate after clean
cforge clean --deep                    # Remove dependencies too
```

---

## Updating

```bash
cforge update --self                   # Update cforge itself
cforge update --packages               # Refresh package registry
```

---

## Troubleshooting

### Build fails with missing dependencies

```bash
cforge deps fetch                      # Fetch Git dependencies
cforge tree                            # Visualize dependency tree
cforge lock --verify                   # Check lock file
```

### Clean rebuild

```bash
cforge clean --all
cforge build
```

### Force regenerate CMake

```bash
cforge build --force-regenerate
```

### Verbose output

```bash
cforge build -v
cforge test -v
```

---

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

cforge is licensed under the [PolyForm Noncommercial License](LICENSE).
