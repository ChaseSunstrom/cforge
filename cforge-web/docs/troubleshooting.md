---
id: troubleshooting
title: Troubleshooting
---

# Troubleshooting

Common issues and their solutions.

## Build Issues

### CMake not found

**Error:** `CMake not found in PATH`

**Solution:**
1. Install CMake from [cmake.org](https://cmake.org/download/)
2. Add CMake to your PATH
3. Verify with `cmake --version`

### Compiler not found

**Error:** `No suitable C++ compiler found`

**Solution:**
- **Windows:** Install Visual Studio with C++ workload, or MinGW
- **macOS:** Run `xcode-select --install`
- **Linux:** Install `build-essential` (Ubuntu/Debian) or `gcc` (Fedora)

### vcpkg issues

**Error:** `Failed to install vcpkg packages`

**Solution:**
```bash
# Reset vcpkg state
cforge vcpkg clean

# Update vcpkg
cforge vcpkg update

# Retry build
cforge build
```

## Enhanced Error Diagnostics

CForge provides Cargo-style error messages with context and suggestions.

### Linker Errors

```
error[E0001]: undefined reference to 'calculate(int, int)'
  --> src/main.cpp:15
   |
15 |     int result = calculate(10, 5);
   |                  ^^^^^^^^^
   |
   = note: symbol not found in any linked library
   = help: possible fixes:
     - Add the library containing this symbol to dependencies
     - Check if the function is declared but not defined
     - Verify the function signature matches the declaration
```

### Missing Headers

```
error: fatal error: 'fmt/core.h' file not found
  --> src/main.cpp:3
   |
 3 | #include <fmt/core.h>
   |          ^~~~~~~~~~~~
   |
   = help: install the 'fmt' package:
     cforge deps add fmt
```

### Template Errors

CForge simplifies complex template errors:

```
error: type mismatch in template instantiation
  --> src/container.cpp:42
   |
42 |     std::vector<MyClass> vec;
   |     ^^^^^^^^^^^^^^^^^^^^^^^^
   |
   = note: 'MyClass' does not satisfy the requirements of 'std::vector'
   = help: ensure MyClass has a default constructor and copy/move semantics
```

## Dependency Issues

### Dependency not found

**Error:** `Package 'xyz' not found in vcpkg`

**Solution:**
```bash
# Search for the package
cforge vcpkg search xyz

# The package might have a different name
cforge vcpkg search json  # might be 'nlohmann-json'
```

### Version conflicts

**Error:** `Version conflict for package 'fmt'`

**Solution:**
```bash
# Check dependency tree
cforge deps tree

# Lock to specific version in cforge.toml
[dependencies]
fmt = { version = "10.1.0", source = "vcpkg" }
```

### Update dependencies

```bash
# Update package registry
cforge deps update

# Check for outdated packages
cforge deps outdated

# Force rebuild
cforge clean && cforge build
```

## Cross-Compilation Issues

### Android NDK not found

**Error:** `ANDROID_NDK environment variable not set`

**Solution:**
```bash
# Set the NDK path
export ANDROID_NDK=/path/to/android-ndk

# Or in cforge.toml
[target.android]
ndk_path = "/path/to/android-ndk"
```

### iOS toolchain issues

**Error:** `Xcode command line tools not found`

**Solution:**
```bash
xcode-select --install
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

## Developer Tools Issues

### clang-format not found

**Error:** `clang-format not found in PATH`

**Solution:**
- **Windows:** Install LLVM from [llvm.org](https://releases.llvm.org/)
- **macOS:** `brew install clang-format`
- **Linux:** `sudo apt install clang-format` or `sudo dnf install clang-tools-extra`

### clang-tidy not found

**Error:** `clang-tidy not found in PATH`

**Solution:**
- Same as clang-format, install LLVM or clang-tools package

### Doxygen not found

**Error:** `Doxygen not found`

**Solution:**
- **Windows:** Download from [doxygen.nl](https://www.doxygen.nl/download.html)
- **macOS:** `brew install doxygen`
- **Linux:** `sudo apt install doxygen`

## Useful Debug Commands

```bash
# Verbose build output
cforge build -v

# List configurations
cforge list configs

# List build variants
cforge list variants

# List cross-compilation targets
cforge list targets

# Show dependency tree
cforge deps tree

# List dependencies
cforge deps list

# Check for outdated dependencies
cforge deps outdated

# Check project configuration
cforge list
```

## Getting Help

If you're still having issues:

1. Check [GitHub Issues](https://github.com/ChaseSunstrom/cforge/issues)
2. Search [GitHub Discussions](https://github.com/ChaseSunstrom/cforge/discussions)
3. Ask on [Discord](https://discord.gg/2pMEZGNwaN)
4. File a new issue with:
   - CForge version (`cforge version`)
   - Operating system
   - Full error message
   - Steps to reproduce
