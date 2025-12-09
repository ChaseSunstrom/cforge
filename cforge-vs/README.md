# cforge - C/C++ Build System Extension for Visual Studio

Visual Studio 2022 integration for the [cforge](https://github.com/ChaseSunstrom/cforge) C/C++ build system.

## Features

### Automatic Project Integration

When you open a folder containing a `cforge.toml` or `cforge.workspace.toml` file:
- Automatically generates VS solution and project files (stored in temp, not in your source)
- Full IntelliSense support with proper include paths
- Error List integration for build errors and warnings
- Build output in the Output window

### Build Commands

Access cforge commands from **Tools > cforge** menu:

| Command | Description | Shortcut |
|---------|-------------|----------|
| Build (Debug) | Build project in Debug configuration | `Ctrl+Shift+B` |
| Build (Release) | Build project in Release configuration | |
| Clean | Clean build artifacts | |
| Run | Build and run the executable | `Ctrl+F5` |
| Run Tests | Build and run unit tests | |
| Watch | Auto-rebuild on file changes | |
| Format Code | Format with clang-format | |
| Run Linter | Run clang-tidy static analysis | |

### Dependency Management

- **Add Dependency**: Add packages from the cforge registry
- **Update Package Registry**: Update the local package index
- **Dependency Manager Window**: Visual tool for managing dependencies

Open the Dependency Manager from **Tools > cforge > Dependency Manager**.

### Project Templates

Create new cforge projects from **File > New > Project**:

- **cforge Project**: New C++ executable project
- **cforge Library**: New C++ static library project
- **cforge Workspace**: Multi-project workspace with app and library

### Workspace Support

Full support for `cforge.workspace.toml` workspaces:
- Generates solution with all member projects
- Cross-project include paths for IntelliSense
- Build from workspace root

### Error List Integration

Build errors and warnings appear in the VS Error List:
- Click to navigate to error location
- Supports GCC/Clang and MSVC error formats
- Parses cforge's Cargo-style error output

### Build Notifications

- Status bar shows build success/failure
- InfoBar notification on build failure with error count
- Automatic Error List display when errors occur

## Requirements

- Visual Studio 2022 (17.0 or later)
- [cforge](https://github.com/ChaseSunstrom/cforge) must be installed and available in your PATH
- A cforge project (contains `cforge.toml` or `cforge.workspace.toml`)

## Installation

### From VSIX

1. Download `CforgeVS.vsix`
2. Double-click to install
3. Restart Visual Studio

### From Visual Studio Marketplace

1. Open **Extensions > Manage Extensions**
2. Search for "cforge"
3. Click **Download** and restart Visual Studio

### Building from Source

```bash
# Clone the repository
git clone https://github.com/ChaseSunstrom/cforge.git
cd cforge/cforge-vs

# Build with MSBuild
msbuild CforgeVS.csproj -p:Configuration=Release

# Or open in VS and build (F5 launches experimental instance)
```

## Usage

### Opening a cforge Project

1. **File > Open > Folder** and select your cforge project folder
2. The extension automatically:
   - Detects `cforge.toml` or `cforge.workspace.toml`
   - Generates VS project files in a temp directory
   - Opens the generated solution
   - Configures IntelliSense with proper include paths

### Building

- Press `Ctrl+Shift+B` or use **Tools > cforge > Build**
- Select Debug or Release configuration from the toolbar
- Build output appears in the Output window (select "cforge" from dropdown)

### Running

- Press `Ctrl+F5` or use **Tools > cforge > Run**
- Executable runs in Windows Terminal (or Command Prompt as fallback)
- Window stays open after program exits

### Debugging

- Press `F5` to build and debug
- Uses standard VS debugger with your executable

## How It Works

The extension generates Visual Studio project files from your `cforge.toml` configuration:

1. **Project files stored in temp**: Generated `.sln` and `.vcxproj` files are stored in `%LOCALAPPDATA%\cforge-vs\projects\` - they never pollute your source directory
2. **Absolute paths**: Source files are referenced with absolute paths back to your actual code
3. **Build delegation**: Build/Clean/Rebuild commands are delegated to `cforge build`, `cforge clean`, etc.
4. **IntelliSense**: Include paths are configured based on your `cforge.toml` settings and detected dependencies

## Supported Configuration

The extension reads your `cforge.toml` and configures VS accordingly:

```toml
[project]
name = "myproject"
version = "1.0.0"
type = "executable"  # or "static_library", "shared_library"
cpp_standard = "20"

[build]
output_dir = "build"

[build.debug]
defines = ["DEBUG_MODE"]

[build.release]
defines = ["NDEBUG"]

[dependencies]
fmt = "11.1.4"
spdlog = "1.*"
```

### Workspace Configuration

```toml
# cforge.workspace.toml
[workspace]
name = "my-workspace"
members = ["app", "lib", "tests"]
```

## Troubleshooting

### IntelliSense not finding headers

1. Build the project once with cforge (`cforge build`) to fetch dependencies
2. Reopen the folder in VS - this regenerates project files with updated include paths
3. Check Output window (cforge) for diagnostic messages

### Solution not loading

Check the cforge Output pane (**View > Output > Show output from: cforge**) for diagnostic messages showing what's happening during project generation.

### Build errors not showing in Error List

The extension parses several error formats:
- GCC/Clang: `file:line:col: error: message`
- MSVC: `file(line,col): error CODE: message`
- cforge: `error[CODE]: message` followed by ` --> file:line:col`

If your compiler uses a different format, please open an issue.

## Issues & Feedback

Report issues at [GitHub Issues](https://github.com/ChaseSunstrom/cforge/issues).

## License

MIT License - see [LICENSE](https://github.com/ChaseSunstrom/cforge/blob/main/LICENSE) for details.
