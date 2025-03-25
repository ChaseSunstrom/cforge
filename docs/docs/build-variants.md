---
id: build-variants
title: Build Variants
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