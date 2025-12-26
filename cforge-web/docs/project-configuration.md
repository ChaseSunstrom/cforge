---
id: project-configuration
title: Project Configuration
---

## Project Configuration

### Basic Configuration

The `cforge.toml` file is the heart of your project configuration:

```toml
[project]
name = "myproject"
version = "1.0.0"
description = "My awesome C++ project"
cpp_standard = "17"
c_standard = "11"
binary_type = "executable" # executable, shared_library, static_library, header_only
authors = ["Your Name <you@example.com>"]
license = "MIT"

[build]
build_type = "Debug"
directory = "build"
source_dirs = ["src"]
include_dirs = ["include"]

[build.config.debug]
defines = ["DEBUG=1"]
flags = ["DEBUG_INFO", "NO_OPT"]

[build.config.release]
defines = ["NDEBUG=1"]
flags = ["OPTIMIZE"]

[test]
enabled = true
directory = "tests"
framework = "catch2"  # or "gtest"

[benchmark]
directory = "bench"
target = "benchmarks"

[package]
enabled = true
generators = ["ZIP", "TGZ"]
vendor = "Your Name"
```

### Configuration Options

| Section | Key | Description |
|---------|-----|-------------|
| `[project]` | `name` | Project name |
| `[project]` | `version` | Project version (semantic versioning) |
| `[project]` | `description` | Project description |
| `[project]` | `cpp_standard` | C++ standard (11, 14, 17, 20, 23) |
| `[project]` | `c_standard` | C standard (99, 11, 17) |
| `[project]` | `binary_type` | Output type (executable, shared_library, static_library, header_only) |
| `[project]` | `authors` | List of authors |
| `[project]` | `license` | License identifier |
| `[build]` | `build_type` | Default build type (Debug, Release, RelWithDebInfo, MinSizeRel) |
| `[build]` | `directory` | Build output directory |
| `[build]` | `source_dirs` | Source file directories |
| `[build]` | `include_dirs` | Header file directories |

### Using Version in Code

The version from `cforge.toml` is automatically available as compile definitions:

```cpp
#include <iostream>

int main() {
    // Generic version macros (works for any project)
    std::cout << "Version: " << PROJECT_VERSION << std::endl;
    std::cout << "Major: " << PROJECT_VERSION_MAJOR << std::endl;
    std::cout << "Minor: " << PROJECT_VERSION_MINOR << std::endl;
    std::cout << "Patch: " << PROJECT_VERSION_PATCH << std::endl;

    // Project-specific macros (e.g., for project named "myapp")
    // std::cout << MYAPP_VERSION << std::endl;

    return 0;
}
```

Available macros:

| Macro | Description | Example |
|-------|-------------|---------|
| `PROJECT_VERSION` | Full version string | `"1.2.3"` |
| `PROJECT_VERSION_MAJOR` | Major version number | `1` |
| `PROJECT_VERSION_MINOR` | Minor version number | `2` |
| `PROJECT_VERSION_PATCH` | Patch version number | `3` |
| `<PROJECTNAME>_VERSION` | Project-specific version | `"1.2.3"` |

### CMake Integration

Customize CMake behavior with includes, injections, and module paths:

```toml
[cmake]
version = "3.15"                      # Minimum CMake version
generator = "Ninja"                    # CMake generator
includes = ["cmake/custom.cmake"]      # Custom CMake files to include
module_paths = ["cmake/modules"]       # Custom module search paths

# Inject custom CMake code
inject_before_target = """
# Code inserted before add_executable/add_library
include(FetchContent)
"""

inject_after_target = """
# Code inserted after add_executable/add_library
target_precompile_headers(${PROJECT_NAME} PRIVATE <pch.hpp>)
"""

[cmake.compilers]
c = "/usr/bin/gcc-12"
cxx = "/usr/bin/g++-12"

[cmake.visual_studio]
platform = "x64"
toolset = "v143"
```

### Platform-Specific Configuration

Configure settings per platform (windows, linux, macos):

```toml
[platform.windows]
defines = ["WIN32", "_WINDOWS"]
flags = ["/W4"]
links = ["kernel32", "user32"]

[platform.linux]
defines = ["LINUX"]
flags = ["-Wall", "-Wextra"]
links = ["pthread", "dl"]

[platform.macos]
defines = ["MACOS"]
flags = ["-Wall"]
frameworks = ["Cocoa", "IOKit"]  # macOS frameworks
```

### Compiler-Specific Configuration

Configure settings per compiler (msvc, gcc, clang, apple_clang, mingw):

```toml
[compiler.msvc]
flags = ["/W4", "/WX", "/permissive-"]
defines = ["_CRT_SECURE_NO_WARNINGS"]

[compiler.gcc]
flags = ["-Wall", "-Wextra", "-Wpedantic"]

[compiler.clang]
flags = ["-Wall", "-Wextra", "-Wpedantic"]

[compiler.mingw]
flags = ["-Wall", "-Wextra"]
defines = ["MINGW"]
```

### Platform + Compiler Combinations

Combine platform and compiler for fine-grained control:

```toml
[platform.windows.compiler.msvc]
flags = ["/W4"]
defines = ["_CRT_SECURE_NO_WARNINGS"]

[platform.windows.compiler.mingw]
defines = ["MINGW_BUILD"]
links = ["mingw32"]

[platform.linux.compiler.gcc]
flags = ["-Wall", "-Wextra", "-fPIC"]
```

### Build Configurations

Define build configurations with portable compiler options:

```toml
[build.config.debug]
optimize = "debug"
debug_info = true
warnings = "all"
sanitizers = ["address"]
defines = ["DEBUG=1"]

[build.config.release]
optimize = "speed"
warnings = "all"
lto = true
defines = ["NDEBUG=1"]

[build.config.relwithdebinfo]
optimize = "speed"
debug_info = true
defines = ["NDEBUG=1"]
```

### Portable Compiler Flags

CForge provides portable build options that automatically translate to the correct flags for each compiler:

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
| `optimize = "none"` | `/Od` | `-O0` |
| `optimize = "debug"` | `/Od` | `-Og` |
| `optimize = "size"` | `/O1 /Os` | `-Os` |
| `optimize = "speed"` | `/O2` | `-O2` |
| `optimize = "aggressive"` | `/Ox` | `-O3` |
| `warnings = "all"` | `/W4` | `-Wall -Wextra` |
| `warnings = "strict"` | `/W4 /WX` | `-Wall -Wextra -Werror` |
| `warnings = "pedantic"` | `/W4 /WX /permissive-` | `-Wall -Wextra -Wpedantic -Werror` |
| `lto = true` | `/GL` + `/LTCG` | `-flto` |
| `debug_info = true` | `/Zi` | `-g` |
| `sanitizers = ["address"]` | `/fsanitize=address` | `-fsanitize=address` |
| `hardening = "basic"` | `/GS /sdl` | `-fstack-protector-strong -D_FORTIFY_SOURCE=2` |
| `hardening = "full"` | `/GS /sdl /GUARD:CF` | `-fstack-protector-all -D_FORTIFY_SOURCE=2 -fPIE` |

These portable options can be used in:
- `[build.config.<name>]` - Per-configuration settings
- `[platform.<name>]` - Per-platform settings
- `[compiler.<name>]` - Per-compiler settings

Raw compiler flags can still be used alongside portable options with the `flags` array.

### Linker Configuration

CForge provides comprehensive linker configuration through the `[linker]` section. Options can be specified globally or for specific platforms, compilers, and build configurations.

```toml
# Global linker options
[linker]
flags = ["-Wl,--as-needed"]     # Raw linker flags
library_dirs = ["lib", "vendor/lib"]  # Library search paths
scripts = ["memory.ld"]          # Linker scripts (GCC/Clang)
strip = false                    # Strip symbols from binary
dead_code_strip = false          # Remove unused code sections
linker = "default"               # Preferred linker: default, lld, gold, mold, bfd
rpath = ["$ORIGIN", "$ORIGIN/../lib"]  # Runtime library search paths
static_runtime = false           # Static link C++ runtime
allow_undefined = false          # Allow undefined symbols
map_file = false                 # Generate map file
pie = false                      # Position independent executable
relro = "none"                   # RELRO: none, partial, full

# Platform-specific linker options
[linker.platform.windows]
flags = ["/DEBUG"]
subsystem = "console"            # windows or console
entry_point = "main"             # Entry point override
def_file = "exports.def"         # Module definition file

[linker.platform.linux]
flags = ["-Wl,--no-undefined"]
linker = "mold"                  # Use mold for faster linking
scripts = ["layout.ld"]          # Custom memory layout
version_script = "version.map"   # Symbol version script
relro = "full"                   # Full RELRO for security

[linker.platform.macos]
install_name = "@rpath/libfoo.dylib"  # dylib install name
exported_symbols = "exports.txt"      # Exported symbols list
unexported_symbols = "hidden.txt"     # Hidden symbols list
order_file = "symbols.order"          # Symbol ordering

# Compiler-specific linker options
[linker.compiler.gcc]
flags = ["-Wl,--gc-sections"]
static_runtime = true

[linker.compiler.msvc]
dead_code_strip = true           # /OPT:REF /OPT:ICF

# Platform+Compiler combined
[linker.platform.linux.compiler.clang]
linker = "lld"

# Build configuration specific
[linker.config.release]
strip = true
dead_code_strip = true

[linker.config.debug]
map_file = true
```

### Linker Options Reference

| Option | Values | Description |
|--------|--------|-------------|
| `flags` | `[...]` | Raw linker flags passed directly |
| `library_dirs` | `[...]` | Library search directories |
| `scripts` | `[...]` | Linker scripts (GCC/Clang: `-T`) |
| `strip` | `true` / `false` | Strip symbols from binary |
| `dead_code_strip` | `true` / `false` | Remove unused code sections |
| `linker` | `"default"`, `"lld"`, `"gold"`, `"mold"`, `"bfd"` | Preferred linker |
| `rpath` | `[...]` | Runtime library search paths |
| `static_runtime` | `true` / `false` | Static link C++ runtime |
| `allow_undefined` | `true` / `false` | Allow undefined symbols |
| `map_file` | `true` / `false` | Generate linker map file |
| `def_file` | `"path"` | Module definition file (MSVC) |
| `version_script` | `"path"` | Symbol version script (Linux) |
| `exported_symbols` | `"path"` | Exported symbols file (macOS) |
| `unexported_symbols` | `"path"` | Unexported symbols file (macOS) |
| `order_file` | `"path"` | Symbol ordering file (macOS) |
| `subsystem` | `"console"`, `"windows"` | Windows subsystem |
| `entry_point` | `"name"` | Entry point override |
| `install_name` | `"path"` | macOS dylib install name |
| `whole_archive` | `true` / `false` | Force include all symbols |
| `pie` | `true` / `false` | Position independent executable |
| `relro` | `"none"`, `"partial"`, `"full"` | RELRO security hardening |

### Linker Flag Translation

| Portable | MSVC | GCC/Clang |
|----------|------|-----------|
| `strip = true` | (no debug PDB) | `-s` |
| `dead_code_strip = true` | `/OPT:REF /OPT:ICF` | `-Wl,--gc-sections` |
| `scripts = ["x.ld"]` | N/A | `-Tx.ld` |
| `linker = "lld"` | (uses lld-link) | `-fuse-ld=lld` |
| `linker = "mold"` | N/A | `-fuse-ld=mold` |
| `static_runtime = true` | (CMAKE_MSVC_RUNTIME_LIBRARY) | `-static-libgcc -static-libstdc++` |
| `pie = true` | N/A | `-pie` |
| `relro = "full"` | N/A | `-Wl,-z,relro,-z,now` |
| `map_file = true` | `/MAP` | `-Wl,-Map,output.map` |
| `exported_symbols` | N/A | `-exported_symbols_list` (macOS) |
| `order_file` | N/A | `-order_file` (macOS) |

Linker options follow the same priority order as other configuration:
1. `[linker]` - Base (lowest priority)
2. `[linker.platform.<name>]` - Platform-specific
3. `[linker.compiler.<name>]` - Compiler-specific
4. `[linker.platform.<name>.compiler.<name>]` - Combined
5. `[linker.config.<name>]` - Build configuration (highest priority)
