# CForge

![Version](https://img.shields.io/badge/version-1.2.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

**A TOML-based build system for C/C++ projects with seamless CMake and vcpkg integration.**

---

## 📖 Table of Contents
1. [Features](#features)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [Command Reference](#command-reference)
5. [Workspace Commands](#workspace-commands)
6. [Advanced Configuration](#advanced-configuration)
7. [Build Variants](#build-variants)
8. [Cross-Compilation](#cross-compilation)
9. [IDE Integration](#ide-integration)
10. [Scripts & Hooks](#scripts--hooks)
11. [Examples](#examples)
12. [Troubleshooting](#troubleshooting)
13. [Contributing](#contributing)
14. [License](#license)

---

## 🚀 Features

- 📋 **Simple TOML Configuration**: Easy project setup without complex CMake syntax.
- 🔄 **Multi-platform**: Supports Windows, macOS, Linux.
- 📦 **Dependency Management**: Integrated support for `vcpkg`, `Conan`, Git, and custom dependencies.
- 🏗️ **Workspaces**: Manage multiple projects together.
- 🎯 **Cross-compilation**: Android, iOS, Raspberry Pi, WebAssembly.
- 🧰 **IDE Integration**: VS Code, CLion, Xcode, Visual Studio.
- 🧪 **Testing**: Integrated with CTest.
- 📝 **Custom Scripts**: Run project-specific tasks.
- ⚙️ **Automatic Tool Setup**: Installs missing tools automatically.
- 🔍 **Enhanced Diagnostics**: Clear, informative compiler errors.

---

## 📥 Installation

### From Cargo

```bash
cargo install cforge
```

### From Source

```bash
git clone https://github.com/ChaseSunstrom/cforge.git
cd cforge
cargo build --release
cargo install --path .
```

### Prerequisites
- Rust
- CMake (≥3.15)
- C/C++ Compiler (GCC, Clang, MSVC)
- Optional: Ninja, Make, or Visual Studio Build Tools

---

## ⚡ Quick Start

```bash
cforge init
cforge build
cforge run
```

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

---

## 📂 Workspace Commands

```bash
cforge init --workspace
cforge build <project>
cforge run <project>
cforge clean <project>
cforge startup <project>
cforge startup --list
```

---

## ⚙️ Advanced Configuration

Example `cforge.toml`:

```toml
[project]
name = "my_project"
version = "0.1.0"
description = "My C/C++ project"
type = "executable"
language = "c++"
standard = "c++17"

[build]
build_dir = "build"
default_config = "Debug"
generator = "Ninja"

[build.configs.Debug]
defines = ["DEBUG", "_DEBUG"]
flags = ["NO_OPT", "DEBUG_INFO"]

[dependencies.vcpkg]
enabled = true
packages = ["fmt", "boost"]

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"]
links = ["fmt"]
```

---

## 🚩 Build Variants

Define variants to optimize builds:

```toml
[variants]
default = "standard"

[variants.variants.standard]
description = "Standard build"

[variants.variants.performance]
description = "Optimized build"
defines = ["HIGH_PERF=1"]
flags = ["OPTIMIZE_MAX", "LTO"]
```

---

## 🌐 Cross-Compilation

Examples:
```bash
cforge build --target android-arm64
cforge build --target wasm
```

---

## 🖥️ IDE Integration

Generate IDE-specific files:

```bash
cforge ide vscode
cforge ide clion
cforge ide xcode
cforge ide vs2022
```

---

## 📝 Scripts & Hooks

Define scripts and hooks in `cforge.toml`:

```toml
[scripts]
scripts = {
  "format" = "clang-format -i src/*.cpp include/*.h"
}

[hooks]
pre_build = ["echo Building..."]
post_build = ["echo Done!"]
```

Run scripts:

```bash
cforge script format
```

---

## 🧩 Examples

**Simple Project**:
```bash
cforge init
cforge build
cforge run
```

**External Dependencies**:
```bash
# Add dependencies in cforge.toml
cforge deps
cforge build
cforge run
```

**Multi-project Workspace**:
```bash
cforge init --workspace
# Initialize individual projects
cforge build
cforge run app1
```

---

## 🔧 Troubleshooting

- **CMake not found**: Ensure it's installed and in PATH.
- **Dependency failures**: Run `cforge deps --update`.
- **Cross-compilation**: Check environment variables (e.g., `$ANDROID_NDK`).
- **Compiler errors**: Use `cforge build --verbosity verbose`.

---

## 🤝 Contributing

Contributions welcome!

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/new-feature`)
3. Commit your changes (`git commit -m 'Add new feature'`)
4. Push to your branch (`git push origin feature/new-feature`)
5. Open a Pull Request

---

## 📄 License

**MIT License** — see [LICENSE](LICENSE).

