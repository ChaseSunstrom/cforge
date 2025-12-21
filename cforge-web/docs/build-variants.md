---
id: build-variants
title: Build Variants
---

## Build Variants

Build variants allow for different build configurations beyond just Debug/Release:

```toml
[variants]
default = "standard"

[variants.variants.standard]
description = "Standard build"

[variants.variants.performance]
description = "Optimized build"
optimize = "aggressive"
lto = true
defines = ["HIGH_PERF=1"]

[variants.variants.memory_safety]
description = "Build with memory safety checks"
sanitizers = ["address", "undefined"]
hardening = "full"
defines = ["ENABLE_MEMORY_SAFETY=1"]

[variants.variants.minimal]
description = "Minimal size build"
optimize = "size"
exceptions = false
rtti = false
```

Building with variants:

```bash
cforge build --variant performance
cforge build --variant memory_safety
```

### Combining with Build Configurations

Variants work alongside standard build configurations:

```toml
[build.config.debug]
optimize = "debug"
debug_info = true
warnings = "all"

[build.config.release]
optimize = "speed"
lto = true

[variants.variants.asan]
sanitizers = ["address"]
```

```bash
# Debug with address sanitizer
cforge build -c Debug --variant asan

# Release with address sanitizer
cforge build -c Release --variant asan
```