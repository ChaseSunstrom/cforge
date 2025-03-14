# CBuild

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
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
cargo install cbuild-tool
```

### From Source

```bash
git clone https://github.com/ChaseSunstrom/cbuild.git
cd cbuild
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
cbuild init
cbuild build
cbuild run
```

---

## 🛠️ Command Reference

| Command      | Description                         | Example                            |
|--------------|-------------------------------------|------------------------------------|
| `init`       | Create new project/workspace        | `cbuild init --template lib`       |
| `build`      | Build the project                   | `cbuild build --config Release`    |
| `clean`      | Clean build artifacts               | `cbuild clean`                     |
| `run`        | Run built executable                | `cbuild run -- arg1 arg2`          |
| `test`       | Execute tests (CTest integration)   | `cbuild test --filter MyTest`      |
| `install`    | Install project binaries            | `cbuild install --prefix /usr/local`|
| `deps`       | Manage dependencies                 | `cbuild deps --update`             |
| `script`     | Execute custom scripts              | `cbuild script format`             |
| `startup`    | Manage workspace startup project    | `cbuild startup my_app`            |
| `ide`        | Generate IDE project files          | `cbuild ide vscode`                |
| `package`    | Package project binaries            | `cbuild package --type zip`        |
| `list`       | List variants, configs, or targets  | `cbuild list variants`             |

---

## 📂 Workspace Commands

```bash
cbuild init --workspace
cbuild build <project>
cbuild run <project>
cbuild clean <project>
cbuild startup <project>
cbuild startup --list
```

---

## ⚙️ Advanced Configuration

Example `cbuild.toml`:

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
cbuild build --target android-arm64
cbuild build --target wasm
```

---

## 🖥️ IDE Integration

Generate IDE-specific files:

```bash
cbuild ide vscode
cbuild ide clion
cbuild ide xcode
cbuild ide vs2022
```

---

## 📝 Scripts & Hooks

Define scripts and hooks in `cbuild.toml`:

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
cbuild script format
```

---

## 🧩 Examples

**Simple Project**:
```bash
cbuild init
cbuild build
cbuild run
```

**External Dependencies**:
```bash
# Add dependencies in cbuild.toml
cbuild deps
cbuild build
cbuild run
```

**Multi-project Workspace**:
```bash
cbuild init --workspace
# Initialize individual projects
cbuild build
cbuild run app1
```

---

## 🔧 Troubleshooting

- **CMake not found**: Ensure it's installed and in PATH.
- **Dependency failures**: Run `cbuild deps --update`.
- **Cross-compilation**: Check environment variables (e.g., `$ANDROID_NDK`).
- **Compiler errors**: Use `cbuild build --verbosity verbose`.

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

