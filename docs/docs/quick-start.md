---
id: quick-start
title: Quick Start
---

## ⚡ Quick Start

### Creating a New Project

```bash 
# Create a new project in the current directory
cforge init

# Create a specific project type
cforge init --template lib     # Create a library project
cforge init --template header-only  # Create a header-only library

# Build the project
cforge build

# Run the executable (for application projects)
cforge run 
```

### Example Project Structure

After initializing a project with `cforge init`, you'll have a structure like this:

```
myproject/
├── cforge.toml         # Project configuration
├── src/
│   └── main.cpp        # Main source file
├── include/            # Header files
├── scripts/            # Custom scripts
└── build/              # Build artifacts (generated)
```

### Example C++ Code

`src/main.cpp` (generated automatically):
```cpp
 #include <iostream>

int main(int argc, char* argv[]) {
    std::cout << "Hello, cforge!" << std::endl;
    return 0;
} 
```

### Build and Run

```bash
$ cforge build
┌──────────────────────────────────────────────────┐
│           cforge - C/C++ Build System            │
│                    v1.2.0                        │
└──────────────────────────────────────────────────┘

Building: myproject
[1/4] Checking build tools
Checking for required build tools...
CMake: ✓
Compiler 'clang': ✓
Build generator 'Ninja': ✓
vcpkg: ✓ (will be configured during build)
All required build tools are available.

[2/4] Configuring project
Project configured with generator: Ninja (Debug)

[3/4] Running pre-build hooks
Running pre-build hooks
Running hook: echo Starting build process...
Starting build process...

[4/4] Building project
Building myproject in Debug configuration
✓ Compiling 1 source files (completed in 1.2s)

✓ Build completed successfully

$ cforge run
┌──────────────────────────────────────────────────┐
│           cforge - C/C++ Build System            │
│                    v1.2.0                        │
└──────────────────────────────────────────────────┘

Running: myproject
Found executable: build/bin/myproject
Running: build/bin/myproject

Program Output
────────────
Hello, cforge!

✓ Program executed successfully 
```