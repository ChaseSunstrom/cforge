---
id: cross-compilation
title: Cross-Compilation
---

## 🌐 Cross-Compilation

CForge supports cross-compilation for various platforms:

```toml 
[cross_compile]
enabled = true
target = "android-arm64"
sysroot = "$ANDROID_NDK/platforms/android-24/arch-arm64"
cmake_toolchain_file = "$ANDROID_NDK/build/cmake/android.toolchain.cmake"
flags = ["-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-24"] 
```

Cross-compilation targets:
- `android-arm64`: Android ARM64 platform
- `android-arm`: Android ARM platform
- `ios`: iOS ARM64 platform
- `raspberry-pi`: Raspberry Pi ARM platform
- `wasm`: WebAssembly via Emscripten

Example:
`` cforge build --target android-arm64 ``