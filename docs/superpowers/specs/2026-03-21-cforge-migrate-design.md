# `cforge migrate` — Import CMakeLists.txt into cforge.toml

## Summary

Add a `cforge migrate` command that reads an existing `CMakeLists.txt` and generates a `cforge.toml`. Extraction is best-effort and regex-based — not a full CMake interpreter. Users are expected to review and tweak the output. The command prints a clear summary of what was extracted and what was skipped.

## Usage

```
cforge migrate [path]              # Migrate CMakeLists.txt in [path] (default: .)
cforge migrate --dry-run           # Print what would be written, no file created
cforge migrate --backup            # Back up existing cforge.toml before overwriting
cforge migrate --output file.toml  # Write to a specific file instead of cforge.toml
```

`[path]` may point to a directory containing `CMakeLists.txt` or directly to a `CMakeLists.txt` file.

## Output Format

```
   Migrating CMakeLists.txt -> cforge.toml

     Extracted project name: myapp
     Extracted version: 1.0.0
     Extracted C++ standard: 17
     Extracted binary type: executable
     Extracted 3 source directories
     Extracted 2 include directories
     Extracted 3 dependencies
     Extracted 4 compiler definitions

     warning: Could not parse custom CMake function 'setup_compiler_flags'
     warning: Conditional blocks (if/else) were skipped
     warning: FetchContent dependency 'mylib' has no GIT_TAG — using "latest"

     Created cforge.toml
```

All output uses `cforge::logger`. Status words use `print_action("Migrating", ...)` (green). Per-field extractions use `print_status(...)` (cyan). Warnings use `print_warning(...)`. Final result uses `logger::created(...)` or `print_error(...)` on failure.

## New Files

### `include/core/cmake_parser.hpp`

```cpp
#pragma once
#include <string>
#include <vector>

namespace cforge {

/// A single dependency extracted from CMakeLists.txt.
struct cmake_dependency {
    std::string name;           // Package/library name
    std::string version;        // Version string, empty if not found
    std::string git_url;        // GIT_REPOSITORY if from FetchContent
    std::string git_tag;        // GIT_TAG if from FetchContent, empty if not found
    bool is_fetch_content;      // True if from FetchContent_Declare
    bool is_find_package;       // True if from find_package
    bool is_subdirectory;       // True if from add_subdirectory
};

/// Aggregated result of parsing one CMakeLists.txt file.
struct cmake_parse_result {
    // [project]
    std::string project_name;       // from project()
    std::string version;            // from project(... VERSION x.y.z ...)
    std::string cpp_standard;       // from set(CMAKE_CXX_STANDARD xx)
    std::string c_standard;         // from set(CMAKE_C_STANDARD xx)
    std::string binary_type;        // "executable", "static", "shared", "interface"
    std::string target_name;        // first target name from add_executable/add_library

    // [build]
    std::vector<std::string> source_dirs;   // deduced from file(GLOB...) or target_sources
    std::vector<std::string> include_dirs;  // from target_include_directories

    // [dependencies]
    std::vector<cmake_dependency> dependencies;

    // Compiler settings
    std::vector<std::string> compile_definitions;  // from target_compile_definitions
    std::vector<std::string> compile_options;       // from target_compile_options
    std::vector<std::string> link_libraries;        // from target_link_libraries (non-dep names)

    // Parse diagnostics — warnings to display to the user
    std::vector<std::string> warnings;
};

/// Parse a CMakeLists.txt file using regex-based line-by-line extraction.
/// Each extractor is independent; a failure in one does not affect others.
/// Returns a cmake_parse_result populated with everything that could be extracted.
cmake_parse_result parse_cmake_file(const std::string &cmake_path);

} // namespace cforge
```

### `src/core/cmake_parser.cpp`

Implementation notes for each extractor. Every extractor reads the same pre-loaded line vector and is called independently.

**Pre-processing:** Load the file into a `std::vector<std::string> lines`. Strip inline comments (`# ...`) from each line after joining continued lines (lines ending with `\`). Do not attempt to expand variables — treat `${VAR}` references as opaque tokens.

**Continued-line joining:** Before dispatching to extractors, make a single pass to join physical lines ending with `\` into logical lines. This handles multi-line CMake calls cleanly.

---

**`extract_project(lines, result)`**

Pattern: `project\s*\(\s*(\w+)` — captures name.

Version sub-pattern within the same call: `VERSION\s+([\d.]+)` — captures `x.y.z`.

Languages sub-pattern: `LANGUAGES\s+([\w\s]+)` — captured for reference but not emitted to toml.

Warn if `project()` not found.

---

**`extract_standards(lines, result)`**

Pattern for C++: `set\s*\(\s*CMAKE_CXX_STANDARD\s+(\d+)\s*\)` — captures standard number.

Pattern for C: `set\s*\(\s*CMAKE_C_STANDARD\s+(\d+)\s*\)`.

Also handle `target_compile_features`: `cxx_std_(\d+)` — use as fallback if `CMAKE_CXX_STANDARD` not found.

---

**`extract_binary_type(lines, result)`**

Pattern for executable: `add_executable\s*\(\s*(\w+)` — sets `binary_type = "executable"`, captures target name.

Pattern for library: `add_library\s*\(\s*(\w+)\s*(STATIC|SHARED|INTERFACE|MODULE)?` — maps to:
- `STATIC` or empty -> `"static"`
- `SHARED` -> `"shared"`
- `INTERFACE` -> `"interface"`
- `MODULE` -> `"shared"` (closest match, emit warning)

Take only the first `add_executable` or `add_library` found. If both exist, take the executable and emit a warning that libraries were also found.

---

**`extract_source_dirs(lines, result)`**

Three sub-strategies, applied in order:

1. `file\s*\(\s*GLOB(?:_RECURSE)?\s+\w+\s+([^)]+)\)` — extract glob patterns like `src/*.cpp`, deduce the directory portion (`src`). Deduplicate.

2. `target_sources\s*\(\s*\w+[^)]+\)` — collect source file arguments, deduce directories from prefixes.

3. If neither found, check for a `src/` directory relative to the CMakeLists.txt location and add it as a default with a warning: `"No explicit source globs found; defaulting to src/"`.

Emit each unique directory once. Do not include bare filenames — only directory names.

---

**`extract_include_dirs(lines, result)`**

Pattern: `target_include_directories\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)\s+([^)]+)\)`

For each match, split the captured token list on whitespace, strip quotes, skip `${...}` tokens. Collect unique directory strings.

Also capture `include_directories\s*\(\s*([^)]+)\)` as a fallback, with a warning: `"include_directories() is non-target-scoped; review include paths."`.

---

**`extract_dependencies(lines, result)`**

Three sub-strategies:

**find_package:** `find_package\s*\(\s*(\w+)(?:\s+([\d.]+))?(?:\s+REQUIRED)?`
- name = group 1, version = group 2 (may be empty)
- `is_find_package = true`

**FetchContent_Declare:** Match the block:
```
FetchContent_Declare\s*\(\s*(\w+)
```
Then within the same logical call (joined lines), find:
- `GIT_REPOSITORY\s+(https?://\S+)` -> `git_url`
- `GIT_TAG\s+(\S+)` -> `git_tag`

If `GIT_TAG` is absent, emit warning: `"FetchContent dependency '<name>' has no GIT_TAG — using \"latest\""` and leave `git_tag` empty.

`is_fetch_content = true`

**add_subdirectory:** `add_subdirectory\s*\(\s*([^)\s]+)` — captures directory name. Emit as dependency with `is_subdirectory = true`, name = directory basename. No version or git info available.

---

**`extract_compile_definitions(lines, result)`**

Pattern: `target_compile_definitions\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)\s+([^)]+)\)`

Split on whitespace, strip quotes, skip `${...}`. Collect `DEFINE=VALUE` and bare `DEFINE` strings.

Also capture `add_definitions\s*\(\s*([^)]+)\)` as fallback, stripping leading `-D`.

---

**`extract_compile_options(lines, result)`**

Pattern: `target_compile_options\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)\s+([^)]+)\)`

Collect flag strings. Skip `${...}` tokens.

---

**`extract_link_libraries(lines, result)`**

Pattern: `target_link_libraries\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)?\s*([^)]+)\)`

Collect library names. Filter out names already captured as `find_package` or `FetchContent` dependencies (they will appear in the `[dependencies]` section instead). Also filter out system libraries (`pthread`, `m`, `dl`, `rt`) — emit them as `link_libraries` entries since cforge handles these via `[build]`.

---

**Skipped constructs (emit warnings):**

- `if(` / `else()` / `endif()` blocks — skipped entirely, warn once: `"Conditional blocks (if/else) were skipped"`
- `function(` / `macro(` definitions — skipped, warn with function name
- `include(` of external CMake modules — skipped, warn with module name
- `cmake_minimum_required` — silently skipped (no toml field for it)
- `set(` of non-standard variables — silently skipped

### `src/core/commands/command_migrate.cpp`

```cpp
cforge_int_t cforge_cmd_migrate(const cforge_context_t *ctx);
```

**Argument parsing:**

| Flag | Variable | Default |
|------|----------|---------|
| positional arg 0 | `target_path` | `"."` |
| `--dry-run` | `dry_run` | `false` |
| `--backup` | `backup` | `false` |
| `--output <file>` | `output_file` | `"cforge.toml"` |

**Execution flow:**

1. Resolve `cmake_path`: if `target_path` is a directory, append `CMakeLists.txt`. Verify it exists; error if not.
2. Resolve `output_path`: `target_path` directory / `output_file` (or absolute if `output_file` is absolute).
3. `logger::print_action("Migrating", cmake_path + " -> " + output_path)` then blank line.
4. Call `cforge::parse_cmake_file(cmake_path)` to get `cmake_parse_result`.
5. Print extraction summary (see Output Format above).
6. Print all `result.warnings` via `logger::print_warning(...)`.
7. If `dry_run`, print the generated TOML to stdout and return 0.
8. If `output_path` already exists and `backup` is true, copy it to `output_path + ".bak"` and log `logger::print_action("Backup", output_path + ".bak")`.
9. Call `write_cforge_toml(output_path, result)` to write the file.
10. `logger::created(output_path)`.

**`write_cforge_toml(path, result)` — local static helper:**

Writes the toml sections in canonical order using `std::ofstream`. Sections are only written if they have content.

```toml
[project]
name = "<project_name>"                  # always written; "unknown" if empty
version = "<version>"                    # omitted if empty
description = ""                         # always written blank (user fills in)
cpp_standard = "<cpp_standard>"          # omitted if empty
c_standard = "<c_standard>"             # omitted if empty
binary_type = "<binary_type>"            # omitted if empty

[build]
source_dirs = [<source_dirs>]            # omitted if empty
include_dirs = [<include_dirs>]          # omitted if empty

[build.config.debug]
defines = [<compile_definitions>]        # omitted if empty

[dependencies]
# find_package deps: name = "<version>"  (version or "*" if unknown)
# FetchContent deps:
# [dependencies.<name>]
# git = "<git_url>"
# tag = "<git_tag>"            # omitted if empty
```

For `find_package` dependencies with no version, write `name = "*"`.

For FetchContent dependencies, write as inline table under `[dependencies]`:
```toml
[dependencies.fmt]
git = "https://github.com/fmtlib/fmt"
tag = "11.0.0"
```

`link_libraries` that are not already captured as dependencies are written as a comment block at the bottom of the file for user review:
```toml
# Additional link libraries detected (review and add manually if needed):
# link_libraries = [<link_libraries>]
```

`compile_options` are similarly written as a comment block since cforge does not have a direct `compile_options` field:
```toml
# Compiler options detected (add to build.config sections manually if needed):
# compile_options = [<compile_options>]
```

## Changes to Existing Files

### `include/core/commands.hpp`

Add declaration:

```cpp
/**
 * @brief Handle the 'migrate' command to import CMakeLists.txt into cforge.toml
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_migrate(const cforge_context_t *ctx);
```

### `src/core/command_registry.cpp`

Register in `register_builtin_commands()` in the "Project" category (add `"migrate"` to the Project category list in `print_general_help`):

```cpp
reg.register_command({
    "migrate",
    {"import"},
    "Import CMakeLists.txt into cforge.toml",
    "Parse an existing CMakeLists.txt and generate a cforge.toml.\n"
    "Extraction is best-effort; review the output before building.",
    "migrate [path] [options]",
    {
        {"", "--dry-run",  "Show generated toml without writing", "", "", false},
        {"", "--backup",   "Back up existing cforge.toml before overwriting", "", "", false},
        {"", "--output",   "Output file path", "FILE", "cforge.toml", false},
    },
    {
        "cforge migrate",
        "cforge migrate ./my-project",
        "cforge migrate --dry-run",
        "cforge migrate --backup --output my.toml",
    },
    {"init", "build"},
    false,
    cforge_cmd_migrate,
    nullptr,
});
```

Also add `"migrate"` to the `"Project"` entry in the `categories` vector inside `print_general_help()`.

## cforge.toml Field Mapping

| CMake construct | cforge.toml field |
|---|---|
| `project(name)` | `[project] name` |
| `project(... VERSION x.y.z)` | `[project] version` |
| `set(CMAKE_CXX_STANDARD xx)` | `[project] cpp_standard` |
| `set(CMAKE_C_STANDARD xx)` | `[project] c_standard` |
| `add_executable(...)` | `[project] binary_type = "executable"` |
| `add_library(... STATIC ...)` | `[project] binary_type = "static"` |
| `add_library(... SHARED ...)` | `[project] binary_type = "shared"` |
| `add_library(... INTERFACE ...)` | `[project] binary_type = "interface"` |
| `file(GLOB ...)` / `target_sources(...)` | `[build] source_dirs` |
| `target_include_directories(...)` | `[build] include_dirs` |
| `target_compile_definitions(...)` | `[build.config.debug] defines` |
| `find_package(name VERSION)` | `[dependencies] name = "version"` |
| `FetchContent_Declare(name GIT_REPOSITORY url GIT_TAG tag)` | `[dependencies.name] git = url; tag = tag` |
| `target_link_libraries(...)` | comment block (manual review) |
| `target_compile_options(...)` | comment block (manual review) |

## Known Limitations (document in --dry-run output footer)

- Variables (`${VAR}`) are not expanded. Paths containing variable references are included verbatim.
- Generator expressions (`$<...>`) are not parsed and will be dropped.
- Conditional `if()/else()` blocks are entirely skipped.
- Multi-target projects emit only the first executable or library found.
- `ExternalProject_Add` is not handled (only `FetchContent_Declare`).
- `add_subdirectory` dependencies have no version information.

## Testing

- Unit test each `extract_*` function with inline string fixtures covering the common patterns and edge cases (missing VERSION, no LANGUAGES, MODULE library, etc.).
- Integration test: run `cforge migrate --dry-run` on the cforge project itself and verify the output contains `name = "cforge"`, `version = "3.1.0"`, `cpp_standard = "17"`.
- Integration test: `cforge migrate --backup` when `cforge.toml` already exists — verify `.bak` file is created and output file is overwritten.
- Integration test: `cforge migrate --output out.toml` — verify file is written to `out.toml`, not `cforge.toml`.
- Integration test: missing `CMakeLists.txt` — verify non-zero exit code and error message.
- Integration test: CMakeLists.txt with only `project()` and nothing else — verify graceful output with warnings for all missing fields.
