---
id: project-configuration
title: Project Configuration
---

## 📋 Project Configuration

### Basic Configuration

The `cforge.toml` file is the heart of your project configuration:

```toml
[project]
name = "my_project"
version = "0.1.0"
description = "My C/C++ project"
type = "executable"  # executable, library, static-library, header-only
language = "c++"
standard = "c++17"   # c++11, c++14, c++17, c++20, c++23

[build]
build_dir = "build"
default_config = "Debug"
generator = "Ninja"  # Ninja, "Visual Studio 17 2022", NMake Makefiles, etc.

[build.configs.Debug]
defines = ["DEBUG", "_DEBUG"]
flags = ["NO_OPT", "DEBUG_INFO"]

[build.configs.Release]
defines = ["NDEBUG"]
flags = ["OPTIMIZE", "OB2", "DNDEBUG"]

[targets.default]
sources = ["src/**/*.cpp", "src/**/*.c"]
include_dirs = ["include"]
links = [] 
```

### Target Configuration

A project can have multiple targets (executables or libraries):

```toml
[targets.main_app]
sources = ["src/app/**/*.cpp"]
include_dirs = ["include"]
links = ["fmt", "boost_system"]

[targets.utils_lib]
sources = ["src/utils/**/*.cpp"]
include_dirs = ["include/utils"]
links = [] 
```

### Platform-specific Configuration

```toml
[platforms.windows]
defines = ["WINDOWS", "WIN32"]
flags = ["UNICODE"]

[platforms.darwin]
defines = ["OSX"]
flags = []

[platforms.linux]
defines = ["LINUX"]
flags = [] 
```