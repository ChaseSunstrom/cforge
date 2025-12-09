# cforge - C/C++ Build System Extension

VS Code integration for the [cforge](https://github.com/ChaseSunstrom/cforge) C/C++ build system.

## Features

### Commands

Access all cforge commands directly from VS Code:

- **Build**: Build your project (`Ctrl+Shift+B` or Command Palette)
- **Run**: Run your executable
- **Test**: Run unit tests
- **Clean**: Clean build artifacts
- **Watch**: Auto-rebuild on file changes
- **Format**: Format code with clang-format
- **Lint**: Run clang-tidy static analysis

### Dependency Management

- **Add Dependency**: Add packages from the cforge registry
- **Remove Dependency**: Remove packages
- **Search Packages**: Search the package registry
- **Update Packages**: Update the package registry

### Dependency Tree View

View your project's dependencies in the Explorer sidebar. The tree shows:
- Package names and versions
- Dependency types (registry, git, vcpkg, system)

### Language Features for cforge.toml

Intelligent editing support for `cforge.toml` configuration files:

- **Autocomplete**: Suggestions for sections, keys, package names, and versions
- **Hover Information**: Documentation for configuration options and package details
- **Diagnostics**: Warnings for unknown packages and outdated versions
- **Quick Fixes**: Update to latest version with one click

### Status Bar

Shows build status and provides quick access to build commands.

### Task Provider

Run cforge commands as VS Code tasks with problem matcher support for build errors.

## Requirements

- [cforge](https://github.com/ChaseSunstrom/cforge) must be installed and available in your PATH
- A cforge project (contains `cforge.toml` or `cforge-workspace.toml`)

## Extension Settings

This extension contributes the following settings:

- `cforge.executablePath`: Path to the cforge executable (default: `"cforge"`)
- `cforge.defaultBuildConfig`: Default build configuration (default: `"Debug"`)
- `cforge.autoShowOutput`: Automatically show output panel when running commands (default: `true`)
- `cforge.watchOnSave`: Automatically rebuild on file save (default: `false`)

## Commands

| Command | Description |
|---------|-------------|
| `cforge: Initialize Project` | Create a new cforge project |
| `cforge: Build` | Build with default configuration |
| `cforge: Build (Debug)` | Build in Debug mode |
| `cforge: Build (Release)` | Build in Release mode |
| `cforge: Clean` | Clean build artifacts |
| `cforge: Run` | Run the built executable |
| `cforge: Run Tests` | Build and run tests |
| `cforge: Add Dependency` | Add a package dependency |
| `cforge: Remove Dependency` | Remove a package dependency |
| `cforge: Search Packages` | Search the package registry |
| `cforge: Update Package Registry` | Update the package index |
| `cforge: Generate Lock File` | Generate/update cforge.lock |
| `cforge: Watch & Rebuild` | Watch for changes and rebuild |
| `cforge: Format Code` | Format with clang-format |
| `cforge: Run Linter` | Run clang-tidy |
| `cforge: Refresh Dependencies` | Refresh the dependency tree |

## Supported Configuration

The extension provides autocomplete and documentation for all `cforge.toml` sections:

```toml
[project]
name = "myproject"
version = "1.0.0"
cpp_standard = "17"
binary_type = "executable"

[build]
build_type = "Debug"
source_dirs = ["src"]
include_dirs = ["include"]

[dependencies]
fmt = "11.1.4"
spdlog = "1.*"

[test]
enabled = true
framework = "catch2"
```

## Installation

### From VSIX

1. Download the `.vsix` file
2. In VS Code, press `Ctrl+Shift+P`
3. Run "Extensions: Install from VSIX..."
4. Select the downloaded file

### From Marketplace

Search for "cforge" in the VS Code Extensions view.

## Issues & Feedback

Report issues at [GitHub Issues](https://github.com/ChaseSunstrom/cforge/issues).

## License

MIT License - see [LICENSE](LICENSE) for details.
