---
id: examples
title: Examples
---

## 📚 Examples

### Simple Application

```toml 
# cforge.toml
[project]
name = "hello_app"
version = "1.0.0"
description = "Hello World Application"
type = "executable"
language = "c++"
standard = "c++17"

[build]
default_config = "Debug"

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"] 
```

```cpp 
// src/main.cpp
#include <iostream>

int main() {
    std::cout << "Hello, CForge!" << std::endl;
    return 0;
} 
```

### Library with vcpkg Dependencies

```toml 
# cforge.toml
[project]
name = "math_lib"
version = "0.1.0"
description = "Mathematics Library"
type = "library"
language = "c++"
standard = "c++17"

[dependencies.vcpkg]
enabled = true
packages = ["fmt", "doctest"]

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"] 
```

```cpp 
// include/math_lib.h
#pragma once

namespace math_lib {
    int add(int a, int b);
    int subtract(int a, int b);
    int multiply(int a, int b);
    int divide(int a, int b);
} 
```

```cpp 
// src/math_lib.cpp
#include "math_lib.h"
#include <fmt/core.h>

namespace math_lib {
    int add(int a, int b) {
        fmt::print("Adding {} and {}\n", a, b);
        return a + b;
    }
    
    int subtract(int a, int b) {
        fmt::print("Subtracting {} from {}\n", b, a);
        return a - b;
    }
    
    int multiply(int a, int b) {
        fmt::print("Multiplying {} by {}\n", a, b);
        return a * b;
    }
    
    int divide(int a, int b) {
        fmt::print("Dividing {} by {}\n", a, b);
        return a / b;
    }
} 
```

### Multi-project Workspace

```toml
 # cforge-workspace.toml
[workspace]
name = "calculator"
projects = ["projects/core", "projects/gui", "projects/cli"]
default_startup_project = "projects/gui" 
```

```toml
# projects/core/cforge.toml
[project]
name = "calc_core"
version = "0.1.0"
description = "Calculator Core Library"
type = "library"
language = "c++"
standard = "c++17"

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"] 
```

```toml 
# projects/gui/cforge.toml
[project]
name = "calc_gui"
version = "0.1.0"
description = "Calculator GUI Application"
type = "executable"
language = "c++"
standard = "c++17"

[dependencies.workspace]
name = "calc_core"
link_type = "static"

[dependencies.vcpkg]
enabled = true
packages = ["imgui", "glfw3", "opengl"]

[targets.default]
sources = ["src/**/*.cpp"]
include_dirs = ["include"] 
```