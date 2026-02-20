---
id: examples
title: Examples
---

## Examples

### Simple Application

```toml
# cforge.toml
[project]
name = "hello_app"
version = "1.0.0"
description = "Hello World Application"
cpp_standard = "17"
binary_type = "executable"

[build]
build_type = "Debug"
source_dirs = ["src"]
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

### Library with Dependencies

```toml
# cforge.toml
[project]
name = "math_lib"
version = "0.1.0"
description = "Mathematics Library"
cpp_standard = "17"
binary_type = "static_lib"

[build]
source_dirs = ["src"]
include_dirs = ["include"]

[dependencies]
fmt = "11.1.4"

[build.config.release]
optimize = "speed"
lto = true
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
main_project = "projects/gui"
```

```toml
# projects/core/cforge.toml
[project]
name = "calc_core"
version = "0.1.0"
description = "Calculator Core Library"
cpp_standard = "17"
binary_type = "static_lib"

[build]
source_dirs = ["src"]
include_dirs = ["include"]
```

```toml
# projects/gui/cforge.toml
[project]
name = "calc_gui"
version = "0.1.0"
description = "Calculator GUI Application"
cpp_standard = "17"
binary_type = "executable"

[build]
source_dirs = ["src"]
include_dirs = ["include"]

[dependencies.vcpkg]
imgui = "*"
glfw3 = "*"
```

### Embedded Bare-Metal (AVR)

```bash
# Create from embedded template
cforge init blink --template embedded
```

```toml
# cforge.toml
[project]
name = "blink"
version = "0.1.0"
c_standard = "99"
c_extensions = true
languages = ["C", "ASM"]
binary_type = "executable"

[build]
build_type = "Release"
source_dirs = ["src"]
include_dirs = ["include"]
defines = ["F_CPU=16000000UL"]

[build.config.release]
optimize = "size"
warnings = "all"

[compiler.gcc]
flags = ["-mmcu=atmega328p", "-funsigned-char", "-ffunction-sections", "-fdata-sections"]

[linker]
flags = ["-mmcu=atmega328p"]
dead_code_strip = true
map_file = true

[cross.profile.avr]
system = "Generic"
processor = "avr"
compilers = { c = "avr-gcc", cxx = "avr-g++" }
variables = { CMAKE_ASM_COMPILER = "avr-gcc" }
nostdlib = true
nostartfiles = true
nodefaultlibs = true
post_build = [
    "avr-objcopy -R .eeprom -O ihex $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex",
    "avr-size --mcu=atmega328p -C $<TARGET_FILE:${PROJECT_NAME}>"
]
flash = "avrdude -c arduino -p atmega328p -P /dev/ttyUSB0 -b 115200 -D -U flash:w:$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex"
```

```c
// src/main.c
#include <stdint.h>

int main(void) {
    // Hardware initialization here

    while (1) {
        // Main loop
    }

    return 0;
}
```

```bash
# Build and flash
cforge build --profile avr
cforge flash --profile avr
```
