---
id: build-variants
title: Build Variants
---

## 🚩 Build Variants

Build variants allow for different build configurations beyond just Debug/Release:

```toml
[variants]
default = "standard"

[variants.variants.standard]
description = "Standard build"

[variants.variants.performance]
description = "Optimized build"
defines = ["HIGH_PERF=1"]
flags = ["OPTIMIZE_MAX", "LTO"]

[variants.variants.memory_safety]
description = "Build with memory safety checks"
defines = ["ENABLE_MEMORY_SAFETY=1"]
flags = ["MEMSAFE"] 
```

Building with variants:

`` cforge build --variant performance ``