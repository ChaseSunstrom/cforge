---
id: advanced-topics
title: Advanced Topics
---

## Advanced Topics

### Precompiled Headers

```toml 
[pch]
enabled = true
header = "include/pch.h"
source = "src/pch.cpp"  # Optional
exclude_sources = ["src/no_pch.cpp"] 
```

### Package Generation

```bash 
# Create a package (defaults to zip/tar.gz)
cforge package

# Specify package type
cforge package --type deb  # Linux Debian package
cforge package --type rpm  # Linux RPM package
cforge package --type zip  # Zip archive 
```

### Installing Projects

```bash
# Install to default location
cforge install

# Install to specific directory
cforge install --prefix /usr/local
```

### Hot Reload

Hot reload recompiles changed translation units and reloads them into the running process without a full restart. It is useful for tightening inner feedback loops during development of applications with long startup times (e.g., game engines, GUI tools, simulations).

#### Configuration

Add a `[hot_reload]` section to `cforge.toml`:

```toml
[hot_reload]
enabled = true
watch_dirs = ["src", "include"]   # Directories to watch for changes
exclude = ["src/generated"]       # Paths to ignore
```

#### Usage

```bash
# Start the hot reload session
cforge hot

# Alias
cforge hot-reload
```

CForge watches the configured directories, recompiles modified `.cpp` files on save, and hot-swaps the resulting shared object into the live process.

#### Runtime API Example

```cpp
#include <cforge/hot_reload.h>

// Called by cforge after each successful hot-swap
extern "C" void on_hot_reload() {
    // Re-register systems, refresh caches, etc.
    MyApp::instance().refresh();
}
```

> **Note:** Hot reload requires the project to be built as a shared library (`binary_type = "shared"`). Static builds are not supported.

### CMake Migration

`cforge migrate` (alias: `cforge import`) converts an existing `CMakeLists.txt` into a `cforge.toml` so you can adopt cforge incrementally.

#### Usage

```bash
# Dry-run to preview the generated config
cforge migrate --dry-run

# Run migration with a backup of the original file
cforge migrate --backup

# Migrate a subdirectory
cforge migrate path/to/project
```

The command writes `cforge.toml` next to the detected `CMakeLists.txt`. Your original `CMakeLists.txt` is preserved; cforge generates its own alongside it.

#### Limitations

| Limitation | Notes |
|------------|-------|
| Generator expressions | Complex `$<...>` expressions are not translated |
| Custom CMake modules | `include()` calls to third-party `.cmake` modules are noted but not inlined |
| Multi-target projects | Only the first `add_executable` / `add_library` target is migrated; additional targets require manual editing |
| Conditional logic | `if()/else()` blocks are evaluated with defaults; platform-specific branches may need review |

After migrating, run `cforge build` and compare the output with your previous CMake build to verify correctness.

### CI Integration

CForge works out of the box in GitHub Actions and other CI environments.

#### GitHub Actions Example

```yaml
name: CI

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest

    steps:
      - uses: actions/checkout@v4

      - name: Install cforge
        run: curl -fsSL https://get.cforge.dev | sh

      - name: Check dependency conflicts
        run: cforge deps tree --check

      - name: Build
        run: cforge build --config Release

      - name: Test
        run: cforge test

      - name: Package
        run: cforge package --type zip
```

#### Key CI Flags

| Flag | Purpose |
|------|---------|
| `cforge init -y` | Non-interactive project scaffolding in scripts |
| `cforge deps tree --check` | Fail the build on version conflicts |
| `cforge build --config Release` | Always build Release in CI |
| `cforge test` | Run the full CTest suite |