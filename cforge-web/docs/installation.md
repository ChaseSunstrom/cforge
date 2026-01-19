---
id: installation
title: Installation
---

# Installation

Install CForge on your system.

## Quick Install

### Windows (PowerShell)

```powershell
irm https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.ps1 | iex
```

### macOS / Linux

```bash
curl -sSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash
```

## Manual Installation

### From Source

```bash
# Clone the repository
git clone https://github.com/ChaseSunstrom/cforge.git
cd cforge

# Build with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Install (may require sudo on Linux/macOS)
cmake --install build
```

### From Releases

1. Download the latest release from [GitHub Releases](https://github.com/ChaseSunstrom/cforge/releases)
2. Extract the archive
3. Add the `bin` directory to your PATH

## Prerequisites

### Required

- **CMake** 3.16 or later
- **C++ Compiler** with C++17 support:
  - Windows: MSVC (Visual Studio 2019+) or MinGW-w64
  - macOS: Clang (Xcode Command Line Tools)
  - Linux: GCC 8+ or Clang 7+

### Optional

- **vcpkg** - For package management (CForge can bootstrap this automatically)
- **Ninja** - Faster builds than Make
- **clang-format** - For `cforge fmt` command
- **clang-tidy** - For `cforge lint` command
- **Doxygen** - For `cforge doc` command

## Verify Installation

```bash
cforge version
```

Expected output:
```
cforge 3.1.0
```

## Shell Completions

Enable tab completion for your shell:

### Bash

```bash
cforge completions bash > ~/.local/share/bash-completion/completions/cforge
```

### Zsh

```bash
cforge completions zsh > ~/.zfunc/_cforge
# Add to .zshrc: fpath+=~/.zfunc
```

### PowerShell

```powershell
cforge completions powershell >> $PROFILE
```

### Fish

```bash
cforge completions fish > ~/.config/fish/completions/cforge.fish
```

## Upgrading CForge

### Using the Upgrade Command

The easiest way to update cforge to the latest version:

```bash
cforge upgrade
```

This will automatically download, build, and install the latest version.

### Using Install Script

Alternatively, run the install script again:

```bash
# Linux/macOS
curl -sSL https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.sh | bash

# Windows (PowerShell)
irm https://raw.githubusercontent.com/ChaseSunstrom/cforge/master/scripts/install.ps1 | iex
```

### From Source

```bash
cd cforge
git pull
cmake --build build --config Release
cmake --install build
```

## Uninstalling

### Linux/macOS

```bash
# Remove binary and data
rm -rf ~/.local/share/cforge
rm -rf ~/.config/cforge

# If installed to /usr/local
rm -f /usr/local/bin/cforge
```

### Windows

```powershell
# Remove cforge directory (includes binary and cache)
Remove-Item "$env:LOCALAPPDATA\cforge" -Recurse -Force
```

## Data Locations

CForge stores all data in a single location per platform:

**Windows:** `%LOCALAPPDATA%\cforge\`
- `installed\cforge\bin\cforge.exe` - Binary
- `cache\` - Binary cache for dependencies
- `registry\` - Package registry index

**Linux/macOS:** `~/.local/share/cforge/`
- `installed/cforge/bin/cforge` - Binary
- `cache/` - Binary cache
- `registry/` - Package registry

**Config:** `~/.config/cforge/config.toml` (Linux/macOS)

## Troubleshooting

If installation fails:

1. Ensure CMake is installed: `cmake --version`
2. Ensure a C++ compiler is available: `g++ --version` or `cl`
3. Check [Troubleshooting](./troubleshooting) for common issues
4. File an issue on [GitHub](https://github.com/ChaseSunstrom/cforge/issues)
