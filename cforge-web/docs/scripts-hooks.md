---
id: scripts-hooks
title: Scripts & Hooks
---

## 📝 Scripts & Hooks

Define scripts and hooks in `cforge.toml`:

```toml
[scripts]
scripts = {
  "format" = "clang-format -i src/*.cpp include/*.h"
}

[hooks]
pre_build = ["echo Building..."]
post_build = ["echo Done!"]
```

Run scripts:
```
cforge script format
```