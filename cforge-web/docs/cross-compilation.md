---
id: cross-compilation
title: Cross-Compilation
---

## Cross-Compilation

CForge supports cross-compilation with a unified configuration system that makes it easy to build for different target platforms.

### Basic Configuration

Enable cross-compilation with the `[cross]` section in your `cforge.toml`:

```toml
[cross]
enabled = true

[cross.target]
system = "Linux"           # CMAKE_SYSTEM_NAME
processor = "aarch64"      # CMAKE_SYSTEM_PROCESSOR
toolchain = "path/to/toolchain.cmake"  # Optional CMake toolchain file

[cross.compilers]
c = "/usr/bin/aarch64-linux-gnu-gcc"
cxx = "/usr/bin/aarch64-linux-gnu-g++"

[cross.paths]
sysroot = "/path/to/sysroot"
find_root = "/path/to/find/root"

[cross.variables]
MY_CUSTOM_VAR = "value"
```

### Configuration Options

| Section | Key | Description |
|---------|-----|-------------|
| `[cross]` | `enabled` | Enable cross-compilation (true/false) |
| `[cross.target]` | `system` | Target system name (CMAKE_SYSTEM_NAME) |
| `[cross.target]` | `processor` | Target processor (CMAKE_SYSTEM_PROCESSOR) |
| `[cross.target]` | `toolchain` | Path to CMake toolchain file |
| `[cross.compilers]` | `c` | Path to C compiler |
| `[cross.compilers]` | `cxx` | Path to C++ compiler |
| `[cross.paths]` | `sysroot` | System root path (CMAKE_SYSROOT) |
| `[cross.paths]` | `find_root` | Find root path (CMAKE_FIND_ROOT_PATH) |
| `[cross.variables]` | `*` | Custom CMake variables |

### Cross-Compilation Profiles

Define reusable cross-compilation profiles for different targets:

```toml
[cross.profile.android-arm64]
system = "Android"
processor = "aarch64"
toolchain = "${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
variables = { ANDROID_ABI = "arm64-v8a", ANDROID_PLATFORM = "android-24" }

[cross.profile.raspberry-pi]
system = "Linux"
processor = "armv7l"
compilers = { c = "arm-linux-gnueabihf-gcc", cxx = "arm-linux-gnueabihf-g++" }
sysroot = "/path/to/rpi-sysroot"

[cross.profile.ios]
system = "iOS"
processor = "arm64"
toolchain = "/path/to/ios.toolchain.cmake"
variables = { PLATFORM = "OS64" }

[cross.profile.wasm]
system = "Emscripten"
toolchain = "${EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
```

### Using Profiles

Build with a specific profile using the `--profile` flag:

```bash
# Build for Android ARM64
cforge build --profile android-arm64

# Build for Raspberry Pi
cforge build --profile raspberry-pi

# Build for WebAssembly
cforge build --profile wasm
```

### Environment Variables

Toolchain paths support environment variable expansion:

```toml
[cross.profile.android-arm64]
toolchain = "${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
```

The `${ANDROID_NDK}` will be replaced with the value of the `ANDROID_NDK` environment variable at build time.

### Supported Platforms

CForge has been tested with the following cross-compilation targets:

| Platform | System Name | Notes |
|----------|-------------|-------|
| Android | `Android` | Requires Android NDK |
| iOS | `iOS` | Requires Xcode and iOS SDK |
| Raspberry Pi | `Linux` | ARM cross-compiler toolchain |
| WebAssembly | `Emscripten` | Requires Emscripten SDK |
| Linux ARM64 | `Linux` | aarch64-linux-gnu toolchain |
| Windows (MinGW) | `Windows` | MinGW-w64 cross-compiler |

### Example: Android Setup

1. Install the Android NDK and set `ANDROID_NDK` environment variable
2. Add a profile to your `cforge.toml`:

```toml
[cross.profile.android-arm64]
system = "Android"
processor = "aarch64"
toolchain = "${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
variables = { ANDROID_ABI = "arm64-v8a", ANDROID_PLATFORM = "android-24" }
```

3. Build:

```bash
cforge build --profile android-arm64
```

### Example: Raspberry Pi Setup

1. Install the ARM cross-compiler:
   ```bash
   # Ubuntu/Debian
   sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
   ```

2. Add a profile:

```toml
[cross.profile.raspberry-pi]
system = "Linux"
processor = "armv7l"
compilers = { c = "arm-linux-gnueabihf-gcc", cxx = "arm-linux-gnueabihf-g++" }
```

3. Build:

```bash
cforge build --profile raspberry-pi
```

### Troubleshooting

#### Toolchain file not found
Make sure environment variables are set correctly:
```bash
echo $ANDROID_NDK  # Should print path to NDK
```

#### Compiler not found
Verify the cross-compiler is installed and in PATH:
```bash
which arm-linux-gnueabihf-gcc
```

#### Profile not found
Check that the profile name matches exactly (case-sensitive):
```bash
cforge build --profile android-arm64  # Correct
cforge build --profile Android-ARM64  # Wrong - case mismatch
```
