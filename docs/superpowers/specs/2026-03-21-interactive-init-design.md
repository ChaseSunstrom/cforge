# Interactive `cforge init`

## Summary

Add an interactive prompt flow to `cforge init` that walks users through project setup when no arguments are given. Arrow-key selection for multiple choice, typed input for text fields. All existing flags continue to work and skip their corresponding prompt. A `-y` flag accepts all remaining defaults.

## Trigger Logic

1. Parse all flags first (existing behavior).
2. Determine which prompts have answers from flags.
3. If stdin is not a TTY (piped, CI), skip interactive mode — use defaults for any missing values (same as `-y`).
4. If all values are provided by flags, skip interactive mode entirely.
5. Otherwise, enter interactive mode for unanswered prompts.
6. If `-y` is passed, use defaults for all unanswered prompts without prompting.

## Prompt Flow

Six prompts, in order:

| # | Prompt | Type | Default | Skipped by flag |
|---|--------|------|---------|-----------------|
| 1 | Project name | Text input | Current directory name | positional arg / `--name` |
| 2 | Template | Arrow-key select | executable | `--template` |
| 3 | C++ standard | Arrow-key select | 17 | `--cpp` |
| 4 | Include tests | Y/n confirm | Yes | `--with-tests` / `-t` |
| 5 | Initialize git | Y/n confirm | Yes | `--with-git` / `-g` |
| 6 | License | Arrow-key select | MIT | (new) `--license` |

After all prompts, display a summary using `print_kv` and ask for final Y/n confirmation. On "n", abort without creating anything.

## Output Formatting

All output uses the existing `cforge::logger` system to match Cargo-style formatting:

- **Status words** right-aligned to 12 chars, bold green (via `print_action`/`print_status_line`)
- **Summary key-value pairs** via `print_kv` (right-aligned labels, 12-char key column)
- **Default values** shown in dim gray (via `print_dim` style / `fmt::color::gray`)
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
        Name:      my-app
    Template:      executable
         C++:      17
       Tests:      yes
         Git:      yes
     License:      MIT
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

/// Check if stdin is an interactive terminal
bool is_interactive_terminal();

/// Arrow-key selection prompt. Returns selected index.
/// Renders options with `>` indicator, navigates with Up/Down, confirms with Enter.
/// Falls back to numbered input if terminal detection fails.
int prompt_select(const std::string &label,
                  const std::vector<std::string> &options,
                  int default_index = 0);

/// Text input prompt with inline default shown in dim text.
/// Returns user input or default_value if empty.
std::string prompt_text(const std::string &label,
                        const std::string &default_value = "");

/// Y/n confirmation prompt. Returns true for yes.
bool prompt_confirm(const std::string &label, bool default_yes = true);

} // namespace cforge
```

### `src/core/utils/terminal_prompt.cpp`

Cross-platform implementation:

**Key reading:**
- Windows: `_getch()` from `<conio.h>` for single keypress, `GetConsoleMode`/`SetConsoleMode` for raw mode
- Linux/macOS: `termios` struct to disable `ICANON` and `ECHO`, `read()` for single chars

**Arrow-key detection:**
- Windows: `_getch()` returns 0 or 0xE0 for extended keys, followed by key code (72=Up, 80=Down)
- POSIX: escape sequence `\033[A` (Up), `\033[B` (Down)

**`prompt_select` rendering:**
1. Print label right-aligned to 12 chars using `fmt::format` (matching `STATUS_WIDTH`)
2. Print each option indented, with `>` in bold green for selected
3. On Up/Down, clear lines with `\033[K` and reprint
4. On Enter, return selected index

**`prompt_text` rendering:**
1. Print label right-aligned to 12 chars
2. Show default in parentheses using `fmt::color::gray`
3. Read line from stdin
4. Return input or default if empty

**`prompt_confirm` rendering:**
1. Print label right-aligned to 12 chars
2. Show `(Y/n)` or `(y/N)` based on default
3. Read single char — `y`/`Y`/Enter(if default yes) returns true

**Fallback:** If `!is_interactive_terminal()`, all functions return their default value without printing prompts.

## Changes to Existing Files

### `src/core/commands/command_init.cpp`

**New flag: `-y`**

Add parsing for `-y` / `--yes` flag alongside existing flags.

**New flag: `--license`**

Add parsing for `--license <type>` with values: `MIT`, `Apache-2.0`, `GPL-3.0`, `BSD-2-Clause`, `none`.

**Interactive flow insertion:**

After flag parsing (around line 1571), before the workspace/project creation branches, add:

```cpp
// Determine which prompts need interactive input
bool needs_interactive = is_interactive_terminal() && !yes_flag;

if (needs_interactive) {
    if (!has_name_flag)
        project_name = prompt_text("Project name", project_name);
    if (!has_template_flag) {
        int idx = prompt_select("Template",
            {"executable", "static-lib", "shared-library", "header-only", "embedded"},
            0);
        template_name = templates[idx];
    }
    if (!has_cpp_flag) {
        int idx = prompt_select("C++ standard", {"17", "11", "14", "20", "23"}, 0);
        cpp_standard = standards[idx];
    }
    if (!has_tests_flag)
        with_tests = prompt_confirm("Include tests", true);
    if (!has_git_flag)
        with_git = prompt_confirm("Initialize git", true);
    if (!has_license_flag) {
        int idx = prompt_select("License",
            {"MIT", "Apache-2.0", "GPL-3.0", "BSD-2-Clause", "None"}, 0);
        license_type = licenses[idx];
    }

    // Show summary and confirm
    print_summary(project_name, template_name, cpp_standard,
                  with_tests, with_git, license_type);
    if (!prompt_confirm("Continue", true)) {
        logger::print_action("Aborted", "project creation");
        return 0;
    }
}
```

The rest of `cforge_cmd_init` proceeds unchanged. The `g_template_name`, `cpp_standard`, `with_tests`, `with_git` variables are already wired into the existing `create_project()` call.

### `create_license_file()` — extend for license types

Add a `license_type` parameter. Currently hardcodes MIT. Extend to support:

- **MIT** — existing implementation
- **Apache-2.0** — standard Apache 2.0 text
- **GPL-3.0** — GPL 3.0 header with full text reference
- **BSD-2-Clause** — BSD 2-clause text
- **None** — skip license file creation entirely

### `command_registry.cpp` — update help text

Update the `init` command's registered help to document `-y`, `--license`, and the interactive behavior.

## What Stays the Same

- All existing flags and their behavior
- `create_project()`, `create_cforge_toml()`, `create_cmakelists()` — untouched
- Workspace init flow (`--workspace`, `--projects`, `--from-file`) — remains fully non-interactive
- Template file generation — untouched
- The `--overwrite` flag — untouched

## Edge Cases

- **Piped stdin / CI:** `is_interactive_terminal()` returns false, all prompts use defaults silently (equivalent to `-y`)
- **Partial flags:** Only unanswered prompts are shown. `cforge init myapp --cpp 20` asks template, tests, git, license — skips name and C++ standard
- **Ctrl+C during prompts:** Terminal mode must be restored before exit. Use RAII wrapper or signal handler to restore terminal settings.
- **Windows Terminal vs cmd.exe:** Both support ANSI escape codes on modern Windows 10+. For older systems, the numbered fallback handles it.
- **`-y` with some flags:** `-y` fills in defaults for any prompt not covered by an explicit flag.

## Testing

- Unit test `terminal_prompt` functions with mock stdin (inject input strings)
- Integration test: `cforge init myapp --template executable --cpp 17 -t -g --license MIT` — verify no prompts, same output as before
- Integration test: `cforge init -y` — verify uses all defaults without prompting
- Integration test: pipe input to stdin — verify non-interactive fallback
