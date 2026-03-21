# cforge

A modern C/C++ build tool with Cargo-like simplicity.

[![Version](https://img.shields.io/badge/version-3.1.0-blue.svg)](https://github.com/ChaseSunstrom/cforge/releases)
[![License](https://img.shields.io/badge/license-PolyForm%20NC-green.svg)](LICENSE)

cforge simplifies C/C++ project management with a clean TOML configuration, automatic dependency resolution, and cross-platform builds. It generates CMake under the hood while providing a much simpler interface.

## Quick Start

```bash
# Install cforge
curl -fsSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash

# Create a new project (interactive prompts guide you through setup)
cforge init myapp
cd myapp

# Build and run
cforge build
cforge run

# Or skip prompts with flags
cforge init myapp --template executable --cpp 17 --with-tests --license MIT -y
```

## Features

- **Simple TOML configuration** - No CMake syntax required
- **Portable compiler flags** - Write once, works on MSVC, GCC, and Clang
- **Package registry** - Search and install packages with `cforge deps add <package>`
- **Multiple dependency sources** - Registry, Git, vcpkg, and system libraries
- **Workspaces** - Manage multi-project repositories
- **Cross-compilation** - Android, iOS, Raspberry Pi, WebAssembly, embedded bare-metal
- **Embedded development** - Bare-metal targets with post-build commands, flash/upload
- **IDE integration** - VS Code, CLion, Visual Studio, Xcode
- **Testing & benchmarks** - Integrated test runner with Catch2, GTest, doctest
- **Developer tools** - Formatting, linting, watch mode, documentation
- **Hot reload** - Shared library live-swapping for rapid iteration
- **CMake migration** - Import existing CMakeLists.txt projects with `cforge migrate`
- **Lock files** - Reproducible builds with exact dependency versions
- **Shell completions** - Bash, Zsh, PowerShell, Fish
- **CI integration** - GitHub Action for automated builds and caching

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
# languages = ["C", "CXX", "ASM"]  # Override auto-detected languages
# c_extensions = true               # Enable C GNU extensions (gnu11 instead of c11)
# cpp_extensions = true             # Enable C++ GNU extensions (gnu++17 instead of c++17)

[build]
build_type = "Release"
source_dirs = ["src"]               # Source directories (default: ["src"])
include_dirs = ["include"]          # Include directories (default: ["include"])
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
| `cforge init [name]` | Initialize a new project (interactive if no args given) |
| `cforge migrate [path]` | Import CMakeLists.txt into cforge.toml |
| `cforge new <template> <name>` | Create files from templates (class, header, interface, test) |
| `cforge build` | Build the project |
| `cforge run` | Build and run the project |
| `cforge clean` | Clean build artifacts |
| `cforge install` | Install project to system |
| `cforge flash` | Flash firmware to embedded target |
| `cforge circular` | Check for circular dependencies |

### Dependencies

| Command | Description |
|---------|-------------|
| `cforge deps add <pkg>` | Add a dependency |
| `cforge deps remove <pkg>` | Remove a dependency |
| `cforge deps search <query>` | Search package registry |
| `cforge deps info <pkg>` | Show package details |
| `cforge deps list` | List current dependencies |
| `cforge deps tree` | Visualize dependency tree (with conflict detection) |
| `cforge deps lock` | Manage lock file for reproducible builds |
| `cforge deps update` | Update package registry |
| `cforge deps outdated` | Show dependencies with newer versions |

### Testing & Quality

| Command | Description |
|---------|-------------|
| `cforge test` | Run tests |
| `cforge bench` | Run benchmarks |
| `cforge fmt` | Format code with clang-format |
| `cforge lint` | Static analysis with clang-tidy |
| `cforge doc` | Generate documentation with Doxygen |

### Tools & IDE

| Command | Description |
|---------|-------------|
| `cforge ide` | Generate IDE project files |
| `cforge watch` | Watch for changes and auto-rebuild |
| `cforge hot` | Hot reload session (shared library live-swapping) |
| `cforge package` | Create distributable packages |
| `cforge cache` | Manage binary cache |
| `cforge completions` | Generate shell completions |

### Other

| Command | Description |
|---------|-------------|
| `cforge version` | Show version information |
| `cforge upgrade` | Upgrade cforge to the latest version |
| `cforge doctor` | Diagnose environment and check for required tools |
| `cforge help <cmd>` | Show help for a command |

---

## Project Initialization

### Create a New Project

Running `cforge init` without arguments starts an interactive setup wizard with arrow-key selection:

```bash
cforge init                            # Interactive mode (prompts for template, standard, etc.)
cforge init myapp                      # Interactive, project name pre-filled
cforge init myapp -y                   # Accept all defaults (no prompts)
cforge init myapp --template shared-library --cpp 20 --with-tests --license Apache-2.0
```

All flags skip their corresponding prompt. Available flags:

| Flag | Description |
|------|-------------|
| `--template <type>` | executable, static-lib, shared-library, header-only, embedded |
| `--cpp <std>` | C++ standard: 11, 14, 17, 20, 23 |
| `--with-tests` / `-t` | Include test infrastructure |
| `--with-git` / `-g` | Initialize git repository |
| `--license <type>` | MIT, Apache-2.0, GPL-3.0, BSD-2-Clause, None |
| `-y` / `--yes` | Accept all defaults without prompting |

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
cforge flash --profile avr            # Flash firmware to embedded target
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
cforge deps add fmt                    # Add from registry
cforge deps add fmt@11.1.4             # Specific version
cforge deps add spdlog --features async # With features
cforge deps search json                # Search packages
cforge deps info nlohmann_json         # Package details
cforge deps outdated                   # Check for updates
```

### Git Dependencies

```toml
[dependencies.git.imgui]
url = "https://github.com/ocornut/imgui.git"
tag = "v1.89.9"
```

```bash
cforge deps add mylib --git https://github.com/user/mylib --tag v1.0
```

### vcpkg Dependencies

```toml
[dependencies.vcpkg]
curl = { version = "7.80.0", features = ["ssl"] }
```

```bash
cforge deps add boost --vcpkg
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
cforge deps lock                       # Generate/update lock file
cforge deps lock --verify              # Verify deps match lock
cforge deps lock --force               # Force regeneration
cforge deps lock --clean               # Remove lock file
```

Commit `cforge.lock` to version control for reproducible builds.

### Dependency Tree

```bash
cforge deps tree                       # Show dependency tree
cforge deps tree -a                    # Include all details
cforge deps tree -d 2                  # Limit depth
cforge deps tree --check               # Detect version conflicts (exit code 1 if found)
cforge deps tree --format dot          # Graphviz DOT output
cforge deps tree --format dot | dot -Tpng -o deps.png  # Generate graph image
```

In workspaces, `cforge deps tree` also shows the inter-project dependency graph and warns about version conflicts across projects.

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
framework = "google"       # google, nanobench, catch2
auto_link_project = true   # Link project library
```

```bash
cforge bench                           # Run all benchmarks
cforge bench --filter 'BM_Sort'        # Run matching benchmarks
cforge bench --no-build                # Skip rebuild
cforge bench --json > results.json     # JSON output
cforge bench --csv                     # CSV output
cforge bench -c Release                # Build configuration
```

Supported frameworks:
- **Google Benchmark** - Industry-standard microbenchmarking
- **nanobench** - Header-only, easy to integrate
- **Catch2 BENCHMARK** - Use Catch2's built-in benchmarking

Benchmarks run in Release mode by default for accurate timing. Files with 'bench' or 'perf' in the name are auto-discovered.

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

### Hot Reload

Live-swap shared libraries without restarting your application. Ideal for game dev, UI work, and rapid iteration.

```toml
[hot_reload]
enabled = true
host = "src/host_main.cpp"        # Host application source
module = "src/game.cpp"           # Hot-reloadable module source
entry_point = "game_update"       # Function to reload (optional)
```

```bash
cforge hot                             # Start hot reload session
```

In your host application, include the runtime library:

```c
#include <cforge/cforge_hot.h>

int main() {
    cforge_hot_ctx *ctx = cforge_hot_load("build/lib/game.so");
    typedef void (*update_fn)(float dt);

    while (running) {
        cforge_hot_reload(ctx);  // Non-blocking check for new version
        update_fn update = (update_fn)cforge_hot_get_symbol(ctx, "game_update");
        if (update) update(delta_time);
    }

    cforge_hot_unload(ctx);
}
```

Build failures keep the old module running — no crashes during iteration.

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
compilers = { c = "arm-linux-gnueabihf-gcc", cxx = "arm-linux-gnueabihf-g++" }
```

```bash
cforge build --profile android-arm64
cforge build --profile raspberry-pi
```

---

## Embedded / Bare-Metal Development

Cross-compilation profiles support embedded-specific options for bare-metal targets:

```toml
[project]
name = "blink"
version = "0.1.0"
c_standard = "99"
c_extensions = true            # GNU extensions (gnu99)
languages = ["C", "ASM"]      # Enable C and assembly
binary_type = "executable"

[build]
source_dirs = ["src"]
include_dirs = ["include"]
defines = ["F_CPU=16000000UL"]

[build.config.release]
optimize = "size"

[compiler.gcc]
flags = ["-mmcu=atmega328p", "-funsigned-char", "-ffunction-sections", "-fdata-sections"]

[linker]
scripts = ["link/linker.ld"]
flags = ["-mmcu=atmega328p"]
dead_code_strip = true
map_file = true

[cross.profile.avr]
system = "Generic"
processor = "avr"
compilers = { c = "avr-gcc", cxx = "avr-g++" }
variables = { CMAKE_ASM_COMPILER = "avr-gcc" }
nostdlib = true
nostartfiles = true
nodefaultlibs = true
post_build = [
    "avr-objcopy -R .eeprom -O ihex $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex",
    "avr-size --mcu=atmega328p -C $<TARGET_FILE:${PROJECT_NAME}>"
]
flash = "avrdude -c arduino -p atmega328p -P /dev/ttyUSB0 -b 115200 -D -U flash:w:$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex"
```

```bash
# Create an embedded project from template
cforge init blink --template embedded

# Build for target
cforge build --profile avr

# Flash firmware
cforge flash --profile avr
```

### Embedded Profile Options

| Option | Type | Description |
|--------|------|-------------|
| `nostdlib` | `bool` | Link with `-nostdlib` (no standard library) |
| `nostartfiles` | `bool` | Link with `-nostartfiles` (no default startup code) |
| `nodefaultlibs` | `bool` | Link with `-nodefaultlibs` (no default libraries) |
| `post_build` | `[string]` | Commands to run after build (e.g., ELF to HEX conversion) |
| `flash` | `string` | Command to flash/upload firmware to device |

Post-build commands run automatically after `cforge build --profile <name>`. Flash commands are invoked with `cforge flash --profile <name>`.

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

## Migrating from CMake

Import an existing CMakeLists.txt project into cforge:

```bash
cforge migrate                         # Convert CMakeLists.txt in current dir
cforge migrate path/to/project         # Convert from specific path
cforge migrate --dry-run               # Preview without writing
cforge migrate --backup                # Back up existing cforge.toml first
cforge migrate --output custom.toml    # Write to specific file
```

The migration extracts project name, version, C++ standard, binary type, source/include directories, dependencies (`find_package`, `FetchContent`), compiler definitions, and link libraries. The result is a best-effort conversion — review the generated `cforge.toml` before building.

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

## CI / GitHub Actions

Use the official GitHub Action to install cforge in CI:

```yaml
- uses: ChaseSunstrom/cforge-action@v1
  with:
    version: '3.1.0'    # optional, default: latest
    cache: true          # optional, cache vendor/ dir

- run: cforge build -c Release
- run: cforge test -c Release
```

The action handles binary installation, PATH setup, and dependency caching across Linux, macOS, and Windows runners.

---

## Upgrading cforge

```bash
cforge upgrade                         # Upgrade cforge to latest version
cforge upgrade --path /opt/cforge      # Upgrade to custom location
cforge deps update                     # Refresh package registry
```

## Data Locations

cforge stores all data in a single location per platform:

**Windows:** `%LOCALAPPDATA%\cforge\`
```
%LOCALAPPDATA%\cforge\
├── installed\cforge\bin\cforge.exe    # Binary installation
├── cache\                              # Binary cache for dependencies
├── registry\                           # Package registry index
└── config.toml                         # Global configuration
```

**Linux/macOS:** `~/.local/share/cforge\` (XDG compliant)
```
~/.local/share/cforge/
├── installed/cforge/bin/cforge        # Binary installation
├── cache/                              # Binary cache
└── registry/                           # Package registry

~/.config/cforge/
└── config.toml                         # Global configuration
```

---

## Troubleshooting

### Build fails with missing dependencies

```bash
cforge deps list                       # List current dependencies
cforge deps tree                       # Visualize dependency tree
cforge deps lock --verify              # Check lock file
cforge deps outdated                   # Check for newer versions
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
