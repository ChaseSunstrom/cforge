# Hot Reload (`cforge hot` / `cforge watch --hot-reload`)

## Summary

Add hot reload capability to cforge so a running host process can swap a shared library in-place without restarting. The user separates their application into a stable host (startup, platform loop, state lifetime) and a hot-reloadable module (logic, game update, UI render). When source files change, cforge rebuilds the shared library and signals the host; the host unloads the old library and loads the new one in the same process.

Targeted at game dev, UI development, and any workflow where recompile-and-restart latency is the main friction point.

## Commands

```
cforge hot                     # Start hot reload session (reads [hot_reload] config)
cforge watch --hot-reload      # Alias: same behavior, extends existing watch command
```

Both commands:
1. Build the module target as a shared library.
2. Build and launch the host executable.
3. Enter the watch loop.
4. On file change: rebuild the shared library, write a signal file, log the reload.

## cforge.toml Configuration

```toml
[hot_reload]
enabled    = true
host       = "src/host_main.cpp"   # Compiled as a standalone executable
module     = "src/game.cpp"        # Compiled as a shared library (.so/.dll/.dylib)
entry_point = "game_update"        # Symbolic name used to verify the symbol exists post-load
watch_dirs  = ["src", "include"]   # Directories to monitor (defaults to [build].source_dirs)
```

`host` and `module` are the only required keys when `enabled = true`. `entry_point` is advisory — used during startup to warn if the symbol is missing, but not enforced at runtime (the user fetches symbols themselves via `cforge_hot_get_symbol`).

## Architecture

Three components, each with a hard seam so users can adopt incrementally.

### 1. Hot Reload Runtime Library

A small, header-only-style C library that users `#include` in their host application. Ships as two files inside cforge's own source tree; also copied into the project root by `cforge init --template hot-reload` and `cforge hot` (on first run, if not already present).

**Public API (`include/cforge/cforge_hot.h`):**

```c
#ifndef CFORGE_HOT_H
#define CFORGE_HOT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cforge_hot_ctx cforge_hot_ctx;

/* Load a shared library and return an opaque context.
   Returns NULL on failure; call cforge_hot_last_error() for details. */
cforge_hot_ctx *cforge_hot_load(const char *lib_path);

/* Unload the library and free the context. Safe to call with NULL. */
void cforge_hot_unload(cforge_hot_ctx *ctx);

/* Check the signal file for a newer version. If found, reload.
   Returns 1 if the library was reloaded, 0 if nothing changed, -1 on error.
   Non-blocking: safe to call every frame. */
int cforge_hot_reload(cforge_hot_ctx *ctx);

/* Look up a symbol by name in the currently loaded library.
   Returns NULL if the symbol is not found. Re-call after every reload. */
void *cforge_hot_get_symbol(cforge_hot_ctx *ctx, const char *name);

/* Blocking helper: polls the signal file in a tight loop and reloads.
   Calls on_reload() after each successful reload. Pass NULL for no callback.
   Returns when the signal file is deleted or an unrecoverable error occurs. */
void cforge_hot_watch(cforge_hot_ctx *ctx, void (*on_reload)(cforge_hot_ctx *));

/* Human-readable string for the last error. Thread-local. */
const char *cforge_hot_last_error(void);

#ifdef __cplusplus
}
#endif
#endif /* CFORGE_HOT_H */
```

**Implementation (`src/cforge/cforge_hot.c`):**

The implementation is a single `.c` file with no external dependencies beyond the OS. Platform selection via preprocessor:

| Platform | Library loading | File watching |
|----------|----------------|---------------|
| Windows  | `LoadLibraryA` / `FreeLibrary` / `GetProcAddress` | `ReadDirectoryChangesW` or polling |
| Linux    | `dlopen` / `dlclose` / `dlsym` (`-ldl`) | `inotify` or polling |
| macOS    | `dlopen` / `dlclose` / `dlsym` | `kqueue` or polling |

Polling is the minimum viable fallback on all platforms. A `#define CFORGE_HOT_POLLING_MS 50` controls the poll interval; defaults to 50 ms.

**Windows DLL locking.** Windows holds an exclusive lock on a loaded DLL. Solution: before loading, copy the rebuilt DLL to a versioned name using a monotonic counter embedded in the filename (e.g., `game_hot_003.dll`). Load the new copy, then free the old handle. Old versioned copies are cleaned up at startup and on clean shutdown — up to 5 stale copies are tolerated (build artifacts folder).

```
build/lib/
  game.dll          <- canonical name; rebuilt by cforge
  game_hot_001.dll  <- loaded copy generation 1 (freed)
  game_hot_002.dll  <- loaded copy generation 2 (freed)
  game_hot_003.dll  <- currently loaded
```

**Signal file.** Located at `.cforge/hot_reload_signal`. Contains a plain ASCII decimal integer (monotonic counter), followed by a newline. The host reads this file on each poll iteration; if the value has increased since the last read, it triggers a reload. cforge writes this file atomically (write to `.cforge/hot_reload_signal.tmp`, then rename) to avoid the host reading a partial write.

### 2. Watch Loop Extension (`command_watch.cpp` modifications)

The existing `cforge_cmd_watch` polls source files and calls `cforge_cmd_build`. The `--hot-reload` flag changes behavior as follows:

1. Read `[hot_reload]` from `cforge.toml`. Error if the section is absent.
2. Perform initial module build with `BUILD_SHARED_LIBS=ON` injected as a CMake variable.
3. Launch the host executable as a child process (non-blocking). Store the PID.
4. Enter the existing watch loop. On change:
   a. Rebuild the shared library target only (not the host).
   b. On success: atomically write the incremented counter to `.cforge/hot_reload_signal`.
   c. On failure: log the compiler error via `logger::print_error`; do NOT update the signal file; keep the old library running.
5. On SIGINT / Ctrl-C: terminate the host child process, delete `.cforge/hot_reload_signal`, exit.

`cforge_cmd_hot` (new file `command_hot.cpp`) is a thin wrapper that sets the `--hot-reload` flag and calls `cforge_cmd_watch`. Registering it as a command alias keeps the dispatch table clean.

### 3. CMake Integration

The module target needs `BUILD_SHARED_LIBS=ON` and the appropriate CMake `SHARED` library type. cforge already generates `CMakeLists.txt`; for hot reload builds, it also injects:

```cmake
set_target_properties(${MODULE_TARGET} PROPERTIES
    PREFIX ""                      # Avoid lib prefix on Linux for consistent naming
    OUTPUT_NAME "${module_name}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"  # Windows DLL output dir
)
```

The host target links against nothing from the module at link time. Symbols are resolved at runtime via `cforge_hot_get_symbol`. If the user's `cforge.toml` lists the module as a dependency, cforge warns and excludes the link step when `[hot_reload]` is active.

## New Files

| Path | Purpose |
|------|---------|
| `include/cforge/cforge_hot.h` | Public C API, ships with cforge |
| `src/cforge/cforge_hot.c` | Cross-platform implementation |
| `src/core/commands/command_hot.cpp` | `cforge hot` command entry point |
| Modify `src/core/commands/command_watch.cpp` | Add `--hot-reload` branch |
| Modify `include/core/commands.hpp` | Declare `cforge_cmd_hot` |

## Usage Example

Host application (`src/host_main.cpp`):

```cpp
#include <cforge/cforge_hot.h>
#include <cstdio>

typedef void (*update_fn)(float dt);

int main() {
    cforge_hot_ctx *ctx = cforge_hot_load("build/lib/game.so");
    if (!ctx) {
        fprintf(stderr, "hot_load failed: %s\n", cforge_hot_last_error());
        return 1;
    }

    bool running = true;
    while (running) {
        cforge_hot_reload(ctx); // non-blocking; re-fetch symbols after this

        update_fn update = (update_fn)cforge_hot_get_symbol(ctx, "game_update");
        if (update) update(1.0f / 60.0f);
    }

    cforge_hot_unload(ctx);
}
```

Module (`src/game.cpp`):

```cpp
// No cforge_hot.h needed in the module itself.
// Export symbols with C linkage to avoid name mangling.

extern "C" void game_update(float dt) {
    // Edit this function, save, and it reloads live.
}
```

## Template

`cforge init --template hot-reload` generates:

```
my-project/
  cforge.toml              # [hot_reload] section pre-filled
  src/
    host_main.cpp          # Minimal host with cforge_hot_load loop
    game.cpp               # Stub module with extern "C" game_update
  include/
    cforge/
      cforge_hot.h         # Copied from cforge install
```

## Output Formatting

All hot reload output uses the existing `cforge::logger` system, Cargo-style:

- `logger::print_action("Reloading", "game.so (v3)")` — green, on successful reload
- `logger::print_status("Watching")` — cyan, in the wait loop header
- `logger::print_warning(...)` — yellow, for compile errors that keep old library running
- `logger::print_error(...)` — red, for unrecoverable errors (host died, signal file unwritable)
- `logger::print_verbose(...)` — gray, DLL copy paths, signal counter values

Example session output:

```
     Watching src (hot-reload active)
   Compiling game.cpp
    Reloading game.so (v2) [42ms]
   Compiling game.cpp
     warning: unused variable 'x'  (kept old library)
   Compiling game.cpp
    Reloading game.so (v3) [38ms]
```

## Error Handling

| Scenario | Behavior |
|----------|---------|
| Module fails to compile | Log error, keep old `.so`/`.dll` loaded, do NOT write signal file |
| Host exits unexpectedly | Log error, enter rebuild-wait loop; relaunch host on next successful build |
| Signal file unwritable | Log error, abort hot reload session with exit code 1 |
| Symbol not found after reload | `cforge_hot_get_symbol` returns NULL; host decides whether to crash or skip |
| Windows DLL copy fails (locked) | Retry up to 3 times with 10 ms sleep; fail with descriptive error after |

## Out of Scope (MVP)

- State serialization / persistence across reloads. Users are responsible for stable state layout or explicit save/restore logic.
- Multiple hot-reloadable modules simultaneously.
- Reloading the host itself (that requires process restart; use `cforge watch` without `--hot-reload`).
- Integration with debuggers (LLDB / WinDbg attach across reload boundaries).
- Remote hot reload (network signal instead of file signal).
