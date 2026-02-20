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
| Bare-metal / Embedded | `Generic` | AVR, ARM Cortex-M, ESP32, etc. |

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

---

## Embedded / Bare-Metal Development

Cross-compilation profiles support additional options for embedded bare-metal targets. These let you control linker behavior, run post-build commands (e.g., ELF to HEX conversion), and flash firmware to devices.

### Embedded Profile Options

| Option | Type | Description |
|--------|------|-------------|
| `nostdlib` | `bool` | Link with `-nostdlib` (no standard library) |
| `nostartfiles` | `bool` | Link with `-nostartfiles` (no default startup code) |
| `nodefaultlibs` | `bool` | Link with `-nodefaultlibs` (no default libraries) |
| `post_build` | `[string]` | Commands to run after successful build |
| `flash` | `string` | Command to flash/upload firmware to device |

These options are placed directly inside `[cross.profile.<name>]` alongside the standard cross-compilation fields.

### Project-Level Options for Embedded

For embedded C projects, you may also want to configure:

```toml
[project]
c_standard = "99"
c_extensions = true         # Use gnu99 instead of c99
languages = ["C", "ASM"]   # Enable assembly language support
```

| Option | Description |
|--------|-------------|
| `languages` | Override auto-detected languages. Add `"ASM"` to enable assembly file globbing (`.S`, `.s`, `.asm`) |
| `c_extensions` | When `true`, uses GNU C extensions (e.g., `gnu99` instead of `c99`) |
| `cpp_extensions` | When `true`, uses GNU C++ extensions (e.g., `gnu++17` instead of `c++17`) |

### Post-Build Commands

Post-build commands run automatically after a successful `cforge build --profile <name>`. They support CMake generator expressions and variable substitution:

```toml
[cross.profile.avr]
post_build = [
    "avr-objcopy -R .eeprom -O ihex $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex",
    "avr-size --mcu=atmega328p -C $<TARGET_FILE:${PROJECT_NAME}>"
]
```

Common post-build tasks:
- Convert ELF to HEX/BIN format
- Print firmware size/memory usage
- Generate checksums or signing

### Flashing Firmware

The `flash` field defines the command used by `cforge flash --profile <name>`:

```toml
[cross.profile.avr]
flash = "avrdude -c arduino -p atmega328p -P /dev/ttyUSB0 -b 115200 -D -U flash:w:$<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.hex"
```

```bash
cforge flash --profile avr
```

### Example: AVR ATmega328P

1. Install avr-gcc toolchain:
   ```bash
   # Ubuntu/Debian
   sudo apt install gcc-avr avr-libc avrdude
   # macOS
   brew install avr-gcc avrdude
   ```

2. Create project:
   ```bash
   cforge init blink --template embedded
   ```

3. Configure `cforge.toml`:
   ```toml
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

4. Build and flash:
   ```bash
   cforge build --profile avr
   cforge flash --profile avr
   ```

### Example: ARM Cortex-M (STM32)

```toml
[project]
name = "stm32_app"
version = "0.1.0"
c_standard = "11"
c_extensions = true
languages = ["C", "ASM"]
binary_type = "executable"

[build]
defines = ["STM32F411xE", "USE_HAL_DRIVER"]

[compiler.gcc]
flags = ["-mcpu=cortex-m4", "-mthumb", "-mfpu=fpv4-sp-d16", "-mfloat-abi=hard",
         "-ffunction-sections", "-fdata-sections"]

[linker]
scripts = ["link/STM32F411RE.ld"]
flags = ["-mcpu=cortex-m4", "-mthumb"]
dead_code_strip = true
map_file = true

[cross.profile.stm32]
system = "Generic"
processor = "arm"
compilers = { c = "arm-none-eabi-gcc", cxx = "arm-none-eabi-g++" }
variables = { CMAKE_ASM_COMPILER = "arm-none-eabi-gcc" }
nostdlib = true
nostartfiles = true
post_build = [
    "arm-none-eabi-objcopy -O binary $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin",
    "arm-none-eabi-size $<TARGET_FILE:${PROJECT_NAME}>"
]
flash = "st-flash write $<TARGET_FILE_DIR:${PROJECT_NAME}>/${PROJECT_NAME}.bin 0x08000000"
```

### Example: ESP32 (with ESP-IDF toolchain)

```toml
[cross.profile.esp32]
system = "Generic"
processor = "xtensa"
toolchain = "${IDF_PATH}/tools/cmake/toolchain-esp32.cmake"
variables = { IDF_TARGET = "esp32" }
post_build = [
    "esptool.py --chip esp32 elf2image $<TARGET_FILE:${PROJECT_NAME}>"
]
flash = "esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x10000 $<TARGET_FILE:${PROJECT_NAME}>.bin"
```

---

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

#### Post-build commands fail
Post-build commands are executed as CMake custom targets. If they fail:
- Check that the tools referenced (e.g., `avr-objcopy`, `arm-none-eabi-size`) are in your PATH
- Verify CMake generator expressions are correct
- Run `cforge build --profile <name> -v` for verbose output

#### Flash command fails
- Check that the serial port is correct and the device is connected
- Verify the flash tool is installed and in PATH
- On Linux, you may need to add your user to the `dialout` group for serial port access:
  ```bash
  sudo usermod -a -G dialout $USER
  ```
