/**
 * @file terminal_prompt.cpp
 * @brief Cross-platform interactive terminal prompt implementation
 *
 * Platform support:
 *   - Windows (MSVC): uses <conio.h> _getch() for raw key input and
 *     GetConsoleMode/SetConsoleMode for terminal state management.
 *   - POSIX (Linux/macOS): uses termios for raw mode and read() for input.
 *
 * ANSI escape sequences are used for cursor movement and color on all
 * platforms (Windows 10+ enables ANSI by default; older hosts fall back
 * gracefully because fmt skips color codes when the stream is not a TTY).
 */

#include "core/utils/terminal_prompt.hpp"

#include <fmt/color.h>
#include <fmt/core.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// Platform includes
// ============================================================================

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <conio.h>
#  include <io.h>     // _isatty, _fileno
#else
#  include <termios.h>
#  include <unistd.h>  // STDIN_FILENO, read
#  include <csignal>   // signal, SIGINT
#  include <cstdlib>   // atexit
#endif

namespace cforge {

// ============================================================================
// Constants
// ============================================================================

static constexpr int STATUS_WIDTH = 12;

// ============================================================================
// Key codes returned by read_key()
// ============================================================================

enum class Key {
    Up,
    Down,
    Enter,
    Char,   // any other printable character; value stored separately
    Eof,
};

struct KeyEvent {
    Key  key;
    char ch;  // valid when key == Key::Char
};

// ============================================================================
// is_interactive_terminal
// ============================================================================

bool is_interactive_terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

// ============================================================================
// RAII terminal_raw_mode
// ============================================================================

/**
 * Enters raw (non-canonical, no-echo) mode on construction and restores
 * the previous terminal state on destruction.
 *
 * On POSIX an atexit() handler and a SIGINT handler are installed the first
 * time a terminal_raw_mode object is created, so the terminal is restored
 * even if the process exits abnormally (SIGKILL is not catchable; the user
 * can run `reset` in that case).
 */
class terminal_raw_mode {
public:
    terminal_raw_mode();
    ~terminal_raw_mode();

    // Non-copyable, non-movable
    terminal_raw_mode(const terminal_raw_mode &) = delete;
    terminal_raw_mode &operator=(const terminal_raw_mode &) = delete;
    terminal_raw_mode(terminal_raw_mode &&) = delete;
    terminal_raw_mode &operator=(terminal_raw_mode &&) = delete;

    // Temporarily restore cooked mode (for prompt_text's getline call).
    void restore_cooked();
    // Re-enter raw mode after restore_cooked().
    void reenter_raw();

private:
#ifdef _WIN32
    HANDLE  m_stdin_handle;
    DWORD   m_saved_mode;
    bool    m_active;   // true while in raw mode
#else
    struct termios m_saved;
    bool           m_active;

    static struct termios  s_saved_global;
    static bool            s_atexit_installed;
    static void restore_atexit();
    static void restore_sigint(int);
#endif
};

// ============================================================================
// POSIX static members
// ============================================================================

#ifndef _WIN32
struct termios terminal_raw_mode::s_saved_global;
bool           terminal_raw_mode::s_atexit_installed = false;

void terminal_raw_mode::restore_atexit() {
    tcsetattr(STDIN_FILENO, TCSANOW, &s_saved_global);
}

void terminal_raw_mode::restore_sigint(int /*sig*/) {
    tcsetattr(STDIN_FILENO, TCSANOW, &s_saved_global);
    // Re-raise with default handler so the shell knows it was Ctrl+C.
    signal(SIGINT, SIG_DFL);
    raise(SIGINT);
}
#endif

// ============================================================================
// terminal_raw_mode constructor / destructor
// ============================================================================

terminal_raw_mode::terminal_raw_mode()
#ifdef _WIN32
    : m_stdin_handle(INVALID_HANDLE_VALUE), m_saved_mode(0), m_active(false)
#else
    : m_saved{}, m_active(false)
#endif
{
#ifdef _WIN32
    m_stdin_handle = GetStdHandle(STD_INPUT_HANDLE);
    if (m_stdin_handle == INVALID_HANDLE_VALUE) return;
    if (!GetConsoleMode(m_stdin_handle, &m_saved_mode)) return;
    DWORD raw_mode = m_saved_mode
                   & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
    SetConsoleMode(m_stdin_handle, raw_mode);
    m_active = true;
#else
    if (tcgetattr(STDIN_FILENO, &m_saved) != 0) return;

    // Save a global copy for atexit/signal handlers.
    s_saved_global = m_saved;
    if (!s_atexit_installed) {
        atexit(restore_atexit);
        signal(SIGINT, restore_sigint);
        s_atexit_installed = true;
    }

    struct termios raw = m_saved;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    m_active = true;
#endif
}

terminal_raw_mode::~terminal_raw_mode() {
#ifdef _WIN32
    if (m_active && m_stdin_handle != INVALID_HANDLE_VALUE) {
        SetConsoleMode(m_stdin_handle, m_saved_mode);
    }
#else
    if (m_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &m_saved);
    }
#endif
}

void terminal_raw_mode::restore_cooked() {
    if (!m_active) return;
#ifdef _WIN32
    if (m_stdin_handle != INVALID_HANDLE_VALUE) {
        SetConsoleMode(m_stdin_handle, m_saved_mode);
    }
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &m_saved);
#endif
    m_active = false;
}

void terminal_raw_mode::reenter_raw() {
    if (m_active) return;
#ifdef _WIN32
    if (m_stdin_handle != INVALID_HANDLE_VALUE) {
        DWORD raw_mode = m_saved_mode
                       & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
        SetConsoleMode(m_stdin_handle, raw_mode);
        m_active = true;
    }
#else
    struct termios raw = m_saved;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    m_active = true;
#endif
}

// ============================================================================
// read_key() — single keypress reader
// ============================================================================

static KeyEvent read_key() {
#ifdef _WIN32
    // _getch() does not echo and does not wait for Enter.
    int first = _getch();
    if (first == EOF || first == 26 /* Ctrl+Z */) {
        return {Key::Eof, 0};
    }
    // Extended key: prefix byte 0 or 0xE0 followed by a scan code.
    if (first == 0 || first == 0xE0) {
        int second = _getch();
        if (second == 72) return {Key::Up,   0};  // Up arrow
        if (second == 80) return {Key::Down, 0};  // Down arrow
        // Unknown extended key — ignore.
        return read_key();
    }
    if (first == '\r' || first == '\n') return {Key::Enter, 0};
    return {Key::Char, static_cast<char>(first)};
#else
    unsigned char ch = 0;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0) return {Key::Eof, 0};

    if (ch == '\n' || ch == '\r') return {Key::Enter, 0};

    // Escape sequence — try to read "[A" or "[B".
    if (ch == '\033') {
        unsigned char seq1 = 0, seq2 = 0;
        ssize_t r1 = read(STDIN_FILENO, &seq1, 1);
        if (r1 <= 0) return {Key::Char, '\033'};
        ssize_t r2 = read(STDIN_FILENO, &seq2, 1);
        if (r2 <= 0) return {Key::Char, '\033'};
        if (seq1 == '[') {
            if (seq2 == 'A') return {Key::Up,   0};
            if (seq2 == 'B') return {Key::Down, 0};
        }
        // Unknown escape — discard.
        return read_key();
    }

    return {Key::Char, static_cast<char>(ch)};
#endif
}

// ============================================================================
// print_option_line — render a single option row
// ============================================================================

static void print_option_line(const std::string &option, bool selected,
                              bool clear_first) {
    if (clear_first) {
        // Move to start of line and erase it before rewriting.
        fmt::print("\r\033[K");
    }
    if (selected) {
        // Bold green ">" indicator.
        fmt::print(fg(fmt::color::green) | fmt::emphasis::bold, "  {:>{}} ", ">",
                   STATUS_WIDTH - 2);
        fmt::print(fmt::emphasis::bold, "{}\n", option);
    } else {
        // Unselected: plain indentation matching STATUS_WIDTH columns.
        fmt::print("  {:>{}} {}\n", "", STATUS_WIDTH - 2, option);
    }
}

// ============================================================================
// prompt_select
// ============================================================================

int prompt_select(const std::string &label,
                  const std::vector<std::string> &options,
                  int default_index) {
    if (!is_interactive_terminal()) return default_index;
    if (options.empty()) return default_index;

    int selected = (default_index >= 0 && default_index < static_cast<int>(options.size()))
                   ? default_index : 0;

    // Print label right-aligned to STATUS_WIDTH chars, bold green.
    fmt::print(fg(fmt::color::green) | fmt::emphasis::bold,
               "{:>{}}", label, STATUS_WIDTH);
    fmt::print(":\n");

    // Initial render of all options.
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        print_option_line(options[i], i == selected, /*clear_first=*/false);
    }

    // Enter raw mode for arrow-key navigation.
    terminal_raw_mode raw;

    for (;;) {
        KeyEvent ev = read_key();

        if (ev.key == Key::Eof) {
            // Restore terminal and return default.
            return default_index;
        }

        if (ev.key == Key::Enter) {
            break;
        }

        int prev = selected;
        if (ev.key == Key::Up) {
            selected = (selected > 0) ? selected - 1
                                      : static_cast<int>(options.size()) - 1;
        } else if (ev.key == Key::Down) {
            selected = (selected + 1 < static_cast<int>(options.size()))
                       ? selected + 1 : 0;
        } else {
            // Ignore other keys.
            continue;
        }

        if (selected == prev) continue;

        // Move cursor up N lines to the first option row.
        int n = static_cast<int>(options.size());
        fmt::print("\033[{}A", n);

        // Redraw all option lines in-place.
        for (int i = 0; i < n; ++i) {
            print_option_line(options[i], i == selected, /*clear_first=*/true);
        }
    }

    return selected;
}

// ============================================================================
// prompt_text
// ============================================================================

std::string prompt_text(const std::string &label,
                        const std::string &default_value) {
    if (!is_interactive_terminal()) return default_value;

    // Print label right-aligned, bold green, followed by ": ".
    fmt::print(fg(fmt::color::green) | fmt::emphasis::bold,
               "{:>{}}", label, STATUS_WIDTH);
    fmt::print(": ");

    // Show default in dim gray.
    if (!default_value.empty()) {
        fmt::print(fg(fmt::color::gray), "({})", default_value);
        fmt::print(" ");
    }

    std::fflush(stdout);

    // Use a scoped raw_mode that we immediately flip to cooked for getline.
    // (If there is already a surrounding raw_mode active from prompt_select,
    // this creates a second one, which is safe — POSIX tcsetattr is not
    // reference-counted, it just sets the attributes. We restore to cooked
    // here and restore to whatever the outer scope had in the outer
    // destructor. Since prompt_text is always called without an outer raw
    // scope, this is the only raw_mode in flight.)
    terminal_raw_mode raw;
    raw.restore_cooked();  // Back to line-buffered so getline works.

    std::string input;
    if (!std::getline(std::cin, input)) {
        // EOF
        return default_value;
    }

    // raw destructor will restore original terminal state.
    return input.empty() ? default_value : input;
}

// ============================================================================
// prompt_confirm
// ============================================================================

bool prompt_confirm(const std::string &label, bool default_yes) {
    if (!is_interactive_terminal()) return default_yes;

    // Print label right-aligned, bold green.
    fmt::print(fg(fmt::color::green) | fmt::emphasis::bold,
               "{:>{}}", label, STATUS_WIDTH);

    // Show (Y/n) or (y/N) hint.
    if (default_yes) {
        fmt::print(" (Y/n) ");
    } else {
        fmt::print(" (y/N) ");
    }
    std::fflush(stdout);

    terminal_raw_mode raw;

    for (;;) {
        KeyEvent ev = read_key();

        if (ev.key == Key::Eof) {
            fmt::print("\n");
            return default_yes;
        }

        if (ev.key == Key::Enter) {
            fmt::print("\n");
            return default_yes;
        }

        if (ev.key == Key::Char) {
            char c = ev.ch;
            if (c == 'y' || c == 'Y') {
                fmt::print("y\n");
                return true;
            }
            if (c == 'n' || c == 'N') {
                fmt::print("n\n");
                return false;
            }
            // Any other character — ignore and keep waiting.
        }
    }
}

} // namespace cforge
