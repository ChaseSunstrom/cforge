# Interactive `cforge init`

## Summary

Add an interactive prompt flow to `cforge init` that walks users through project setup when no arguments are given. Arrow-key selection for multiple choice, typed input for text fields. All existing flags continue to work and skip their corresponding prompt. A `-y` flag accepts all remaining defaults.

## Trigger Logic

1. Parse all flags first (existing behavior).
2. Determine which prompts have answers from flags.
3. If stdin is not a TTY (piped, CI), skip interactive mode — use defaults for any missing values (same as `-y`).
4. If all values are provided by flags, skip interactive mode entirely.
5. If `--workspace`, `--from-file`, or `--projects` is active, skip interactive mode (these paths are fundamentally multi-project and have no single-project prompts).
6. Otherwise, enter interactive mode for unanswered prompts.
7. If `-y` is passed, use defaults for all unanswered prompts without prompting.

## Prompt Flow

Six prompts, in order:

| # | Prompt | Type | Default | Skipped by flag |
|---|--------|------|---------|-----------------|
| 1 | Project name | Text input | Current directory name | positional arg / `--name` |
| 2 | Template | Arrow-key select | executable | `--template` |
| 3 | C++ standard | Arrow-key select | 17 | `--cpp` |
| 4 | Include tests | Y/n confirm | Yes | `--with-tests` / `-t` (only skips to true; no `--no-tests` flag exists) |
| 5 | Initialize git | Y/n confirm | Yes | `--with-git` / `-g` (only skips to true; no `--no-git` flag exists) |
| 6 | License | Arrow-key select | MIT | (new) `--license` |

After all prompts, display a summary and ask for final Y/n confirmation. On "n", abort without creating anything.

**`--license` validation:** If the user passes `--license foobar` (invalid value), print an error and exit. Valid values: `MIT`, `Apache-2.0`, `GPL-3.0`, `BSD-2-Clause`, `None` (case-insensitive — `none`, `NONE`, `None` all accepted and normalized to canonical `None`).

## Output Formatting

All output uses the existing `cforge::logger` system to match Cargo-style formatting:

- **Status words** right-aligned to 12 chars, bold green (via `print_action`/`print_status_line`)
- **Summary key-value pairs** use a custom right-aligned format matching `STATUS_WIDTH` (not `print_kv`, which left-aligns keys). Use `fmt::format("{:>12}", key)` followed by the value, matching the same pattern as `print_status_line`.
- **Default values** shown in dim gray (via `fmt::color::gray`)
- **Arrow-key selector** uses bold green `>` indicator, options indented to align with the 12-char status column
- **Section separators** via `print_rule`

Example output:

```
    Creating new project

 Project name: (my-folder) _

    Template:
       > executable
         static-lib
         shared-library
         header-only
         embedded

  C++ standard:
       > 17
         11
         14
         20
         23

 Include tests? (Y/n) _
 Initialize git? (Y/n) _

     License:
       > MIT
         Apache-2.0
         GPL-3.0
         BSD-2-Clause
         None

    ──────────────────────────────────
        Name: my-app
    Template: executable
         C++: 17
       Tests: yes
         Git: yes
     License: MIT
    ──────────────────────────────────

   Continue? (Y/n) _
```

## New Files

### `include/core/utils/terminal_prompt.hpp`

```cpp
#pragma once
#include <string>
#include <vector>

namespace cforge {

/// Check if stdin is an interactive terminal.
/// Returns false for piped stdin, redirected input, CI environments.
bool is_interactive_terminal();

/// Arrow-key selection prompt. Returns selected index.
/// Renders options with `>` indicator, navigates with Up/Down, confirms with Enter.
/// Falls back to numbered input if ANSI escape support is unavailable.
///
/// Preconditions:
///   - options.size() > 0
///   - 0 <= default_index < options.size()
///
/// On EOF or error, returns default_index.
/// In numbered fallback mode, out-of-range input re-prompts.
int prompt_select(const std::string &label,
                  const std::vector<std::string> &options,
                  int default_index = 0);

/// Text input prompt with inline default shown in dim text.
/// Restores terminal to cooked mode (ICANON + ECHO) before reading,
/// so backspace/delete work normally via the OS line editor.
/// Returns user input or default_value if empty/EOF.
std::string prompt_text(const std::string &label,
                        const std::string &default_value = "");

/// Y/n confirmation prompt. Returns true for yes.
/// On EOF, returns default_yes.
bool prompt_confirm(const std::string &label, bool default_yes = true);

} // namespace cforge
```

### `src/core/utils/terminal_prompt.cpp`

Cross-platform implementation:

**Terminal state management (RAII):**

```cpp
class terminal_raw_mode {
public:
    terminal_raw_mode();   // Save current state, enter raw mode
    ~terminal_raw_mode();  // Restore saved state
    // Non-copyable, non-movable
};
```

On POSIX: saves `termios` state, disables `ICANON` and `ECHO` via `tcsetattr`. Restores in destructor. Also installs `atexit` handler and `SIGINT` handler to restore terminal state on abnormal exit (SIGKILL is not catchable and is acceptable).

On Windows: saves console mode via `GetConsoleMode`, disables `ENABLE_LINE_INPUT` and `ENABLE_ECHO_INPUT` via `SetConsoleMode`. Restores in destructor. Note: `_getch()` bypasses the console mode, so `prompt_select` and `prompt_confirm` work regardless. However, `prompt_text` uses `std::getline` which goes through the CRT and requires `ENABLE_LINE_INPUT` and `ENABLE_ECHO_INPUT` to be active. Therefore, `prompt_text` must explicitly restore these two flags before calling `getline` on Windows, then re-disable them afterward if more prompts follow.

**Key reading:**
- Windows: `_getch()` from `<conio.h>` for single keypress
- Linux/macOS: `read(STDIN_FILENO, &ch, 1)` in raw mode for single chars

**Arrow-key detection:**
- Windows: `_getch()` returns `0` or `0xE0` as a prefix byte for extended keys. Only if the first byte is `0` or `0xE0`, read a second byte: `72` = Up, `80` = Down. If the first byte is NOT `0` or `0xE0`, treat it as normal character input (do not read a second byte).
- POSIX: escape sequence `\033[A` (Up), `\033[B` (Down). Read `\033`, then `[`, then the direction char.

**`prompt_select` rendering:**
1. Print label right-aligned to 12 chars using `fmt::format("{:>12}", label)` (matching `STATUS_WIDTH`)
2. Print all options, each on its own line, with `>` in bold green for selected, space for unselected
3. On Up/Down: move cursor up `options.size()` lines (`\033[{N}A`), then reprint all option lines (each cleared with `\r\033[K` before writing). This achieves in-place redraw.
4. On Enter, return selected index

**`prompt_text` rendering:**
1. Restore terminal to cooked mode (re-enable `ICANON` and `ECHO`) so the OS handles backspace, delete, and line editing
2. Print label right-aligned to 12 chars
3. Show default in parentheses using `fmt::color::gray`
4. Use `std::getline(std::cin, input)` for line reading
5. Re-enter raw mode if needed for subsequent prompts
6. Return input or default if empty

**`prompt_confirm` rendering:**
1. Print label right-aligned to 12 chars
2. Show `(Y/n)` or `(y/N)` based on default
3. Read single char — `y`/`Y`/Enter(if default yes) returns true, `n`/`N`/Enter(if default no) returns false

**Fallback:** If `!is_interactive_terminal()`, all functions return their default value without printing prompts.

## Changes to Existing Files

### `src/core/commands/command_init.cpp`

**New local variables and option arrays:**

Declare at the top of `cforge_cmd_init`, alongside existing locals:

```cpp
bool yes_flag = false;
std::string license_type = "MIT";
bool has_name_flag = false;
bool has_template_flag = false;
bool has_cpp_flag = false;
bool has_tests_flag = false;
bool has_git_flag = false;
bool has_license_flag = false;

// Option arrays for interactive prompts
static const std::vector<std::string> template_options =
    {"executable", "static-lib", "shared-library", "header-only", "embedded"};
static const std::vector<std::string> standard_options =
    {"17", "11", "14", "20", "23"};
static const std::vector<std::string> license_options =
    {"MIT", "Apache-2.0", "GPL-3.0", "BSD-2-Clause", "None"};
```

**New flags in the argument parsing loop:**

```cpp
// -y / --yes
else if (arg == "-y" || arg == "--yes") {
    yes_flag = true;
}
// --license
else if (arg == "--license") {
    if (i + 1 < ctx->args.arg_count) {
        std::string raw_license = ctx->args.args[++i];
        has_license_flag = true;
        // Case-insensitive validation against canonical options
        auto to_lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        std::string raw_lower = to_lower(raw_license);
        bool valid = false;
        for (const auto &opt : license_options) {
            if (to_lower(opt) == raw_lower) {
                license_type = opt; // Store canonical form, not raw input
                valid = true;
                break;
            }
        }
        if (!valid) {
            cforge::logger::print_error("Invalid license type: " + raw_license);
            return 1;
        }
    }
}
```

Set `has_name_flag`, `has_template_flag`, etc. to `true` in the existing flag handlers where `project_name`, `template_name`, `cpp_standard`, `with_tests`, `with_git` are assigned. **Important:** The positional argument check (lines 1459-1461, before the flag loop) also sets `project_name` — set `has_name_flag = true` there as well, so `cforge init myapp` skips the name prompt.

**Interactive flow insertion:**

Insert **after** line 1574 (`g_template_name = template_name;`), **before** the `create_multiple_projects` calculation at line 1578. This ensures the global `g_template_name` is set from the parsed `template_name` local before the interactive block potentially overwrites `template_name`.

```cpp
// Interactive mode: only for single-project, non-workspace flows with a TTY
bool needs_interactive = is_interactive_terminal()
    && !yes_flag
    && !is_workspace
    && !from_file
    && !has_projects_flag;

if (needs_interactive) {
    cforge::logger::print_action("Creating", "new project");
    cforge::logger::print_blank();

    if (!has_name_flag)
        project_name = prompt_text("Project name", project_name);
    if (!has_template_flag) {
        int idx = prompt_select("Template", template_options, 0);
        template_name = template_options[idx];
    }
    if (!has_cpp_flag) {
        int idx = prompt_select("C++ standard", standard_options, 0);
        cpp_standard = standard_options[idx];
    }
    if (!has_tests_flag)
        with_tests = prompt_confirm("Include tests", true);
    if (!has_git_flag)
        with_git = prompt_confirm("Initialize git", true);
    if (!has_license_flag) {
        int idx = prompt_select("License", license_options, 0);
        license_type = license_options[idx];
    }

    // Show summary and confirm
    print_init_summary(project_name, template_name, cpp_standard,
                       with_tests, with_git, license_type);
    if (!prompt_confirm("Continue", true)) {
        cforge::logger::print_action("Aborted", "project creation");
        return 0;
    }

    // Propagate interactive selections to existing variables
    g_template_name = template_name;
}
```

**Note on `g_template_name`:** The existing code uses a file-scope global `static std::string g_template_name` (line 26) that `create_cmakelists()`, `create_cforge_toml()`, `create_main_cpp()`, and `create_include_files()` all read directly. The non-interactive path already sets it at line 1574 (`g_template_name = template_name`). In the interactive path, `g_template_name` gets set twice: first at line 1574 (to the flag/default value), then again at the end of the interactive block (to the user's selection). The second assignment is the authoritative one for the interactive path — do NOT remove it, as it propagates the interactive selection to all the `create_*` functions that read the global directly.

**`print_init_summary` — new static helper:**

```cpp
static void print_init_summary(const std::string &name,
                                const std::string &tmpl,
                                const std::string &cpp,
                                bool tests, bool git,
                                const std::string &license) {
    cforge::logger::print_blank();
    cforge::logger::print_rule(34);
    // Right-aligned keys matching STATUS_WIDTH (12 chars)
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{:>12}", "Name");
    fmt::print(": {}\n", name);
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{:>12}", "Template");
    fmt::print(": {}\n", tmpl);
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{:>12}", "C++");
    fmt::print(": {}\n", cpp);
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{:>12}", "Tests");
    fmt::print(": {}\n", tests ? "yes" : "no");
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{:>12}", "Git");
    fmt::print(": {}\n", git ? "yes" : "no");
    fmt::print(fg(fmt::color::white) | fmt::emphasis::bold, "{:>12}", "License");
    fmt::print(": {}\n", license);
    cforge::logger::print_rule(34);
    cforge::logger::print_blank();
}
```

### `create_license_file()` — extend for license types

**Updated signature:**

```cpp
static bool create_license_file(const std::filesystem::path &project_path,
                                const std::string &project_name,
                                const std::string &license_type = "MIT");
```

**Implementation changes:**
- If `license_type` is `"None"` (case-insensitive), skip license file creation and return true
- MIT: existing implementation (unchanged)
- Apache-2.0: standard Apache License 2.0 text
- GPL-3.0: GPL 3.0 header referencing the full license
- BSD-2-Clause: standard BSD 2-Clause text
- The license name written to the file header adjusts to match the selected type

### `create_project()` — thread `license_type` through

**Updated signature:**

```cpp
static bool create_project(const std::filesystem::path &project_path,
                           const std::string &project_name,
                           const std::string &cpp_version, bool with_git,
                           bool with_tests,
                           const std::string &cmake_preset = "",
                           const std::string &build_type = "Debug",
                           const std::string &license_type = "MIT");
```

Inside `create_project()`, update both calls:
- `create_license_file(project_path, project_name)` becomes `create_license_file(project_path, project_name, license_type)`
- `create_cforge_toml(project_path, project_name, cpp_version, with_tests)` becomes `create_cforge_toml(project_path, project_name, cpp_version, with_tests, license_type)`

### `create_cforge_toml()` — use selected license

**Updated signature:**

```cpp
static bool create_cforge_toml(const std::filesystem::path &project_path,
                                const std::string &project_name,
                                const std::string &cpp_version,
                                bool with_tests,
                                const std::string &license_type = "MIT");
```

The hardcoded `config << "license = \"MIT\"\n";` (line 545 for standard, line 458 for embedded) changes to `config << "license = \"" << license_type << "\"\n";`. For `license_type == "None"`, write `license = ""` or omit the line entirely.

### `command_registry.cpp` — update help text

Update the `init` command's registered help to document:
- `-y` / `--yes` — Accept all defaults without prompting
- `--license <type>` — Set license (MIT, Apache-2.0, GPL-3.0, BSD-2-Clause, none)
- Note that `cforge init` without arguments enters interactive mode

## What Stays the Same

- All existing flags and their behavior
- `create_cmakelists()` — untouched
- Workspace init flow (`--workspace`, `--projects`, `--from-file`) — remains fully non-interactive
- Template file generation — untouched
- The `--overwrite` flag — untouched

## Edge Cases

- **Piped stdin / CI:** `is_interactive_terminal()` returns false, all prompts use defaults silently (equivalent to `-y`)
- **Partial flags:** Only unanswered prompts are shown. `cforge init myapp --cpp 20` asks template, tests, git, license — skips name and C++ standard
- **Ctrl+C during prompts:** Terminal state must be restored before exit. The `terminal_raw_mode` RAII class restores state in its destructor. On POSIX, additionally install a `SIGINT` handler that restores `termios` state and an `atexit` handler as a safety net. `SIGKILL` is not catchable and is acceptable — the user can run `reset` to fix their terminal in that extreme case.
- **Windows Terminal vs cmd.exe:** Both support ANSI escape codes on modern Windows 10+. For older systems or if ANSI detection fails, fall back to numbered input.
- **`-y` with some flags:** `-y` fills in defaults for any prompt not covered by an explicit flag.
- **Workspace/multi-project paths:** The `needs_interactive` guard explicitly checks `!is_workspace && !from_file && !has_projects_flag`, so interactive mode is never entered for these flows.
- **Invalid `--license` value:** Rejected with error message and exit code 1.
- **Empty options vector:** `prompt_select` has a precondition that `options.size() > 0`. All call sites use hardcoded non-empty arrays, so this is guaranteed at compile time.

## Testing

- Unit test `terminal_prompt` functions with mock stdin (inject input strings)
- Integration test: `cforge init myapp --template executable --cpp 17 -t -g --license MIT` — verify no prompts, same output as before
- Integration test: `cforge init -y` — verify uses all defaults without prompting (run in a temp directory)
- Integration test: `cforge init -y --license Apache-2.0` — verify Apache license generated
- Integration test: `cforge init -y --license none` — verify no LICENSE file created
- Integration test: `cforge init --license foobar` — verify error and exit code 1
- Integration test: pipe input to stdin — verify non-interactive fallback
- Integration test: `cforge init -w myworkspace -p proj1,proj2` — verify no interactive prompts
