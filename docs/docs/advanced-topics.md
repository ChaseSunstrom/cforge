---
id: advanced-topics
title: Advanced Topics
---

## 🧩 Advanced Topics

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