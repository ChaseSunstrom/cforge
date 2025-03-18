---
id: advanced-configuration
title: Advanced Configuration
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