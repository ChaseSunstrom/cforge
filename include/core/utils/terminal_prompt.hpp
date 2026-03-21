/**
 * @file terminal_prompt.hpp
 * @brief Cross-platform interactive terminal prompt utilities
 *
 * Provides arrow-key selection, text input, and Y/n confirmation prompts
 * for interactive CLI flows. All prompts follow the Cargo-style formatting
 * convention (STATUS_WIDTH = 12 right-aligned labels, bold green accents).
 *
 * All functions fall back to returning their default value immediately when
 * stdin is not an interactive terminal (piped input, CI environments, etc.).
 */

#pragma once
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Check if stdin is an interactive terminal.
 *
 * Returns false when stdin is piped, redirected, or running in a CI
 * environment. When this returns false all prompt functions immediately
 * return their default value without printing anything.
 */
bool is_interactive_terminal();

/**
 * @brief Arrow-key selection prompt. Returns the index of the chosen option.
 *
 * Renders the option list with a bold-green ">" indicator on the selected
 * entry. The user navigates with Up/Down arrow keys and confirms with Enter.
 *
 * Preconditions:
 *   - options.size() > 0
 *   - 0 <= default_index < (int)options.size()
 *
 * On EOF or error, returns default_index.
 * If stdin is not a TTY, returns default_index without printing.
 *
 * @param label        Right-aligned label (padded to 12 chars).
 * @param options      Non-empty list of option strings.
 * @param default_index Index of the initially selected option (default 0).
 * @return Index of the selected option.
 */
int prompt_select(const std::string &label,
                  const std::vector<std::string> &options,
                  int default_index = 0);

/**
 * @brief Text input prompt with inline default shown in dim gray.
 *
 * Temporarily restores the terminal to cooked mode (ICANON + ECHO) so the
 * OS line editor handles backspace, delete, and other editing keys normally.
 * Returns user input, or default_value if the user presses Enter with no
 * input or if EOF is reached.
 *
 * If stdin is not a TTY, returns default_value without printing.
 *
 * @param label         Right-aligned label (padded to 12 chars).
 * @param default_value Value returned when input is empty (default "").
 * @return User-entered string, or default_value.
 */
std::string prompt_text(const std::string &label,
                        const std::string &default_value = "");

/**
 * @brief Y/n confirmation prompt. Returns true for yes.
 *
 * Displays "(Y/n)" or "(y/N)" based on the default. Reads a single
 * keystroke: y/Y -> true, n/N -> false, Enter -> default_yes.
 * On EOF, returns default_yes.
 *
 * If stdin is not a TTY, returns default_yes without printing.
 *
 * @param label       Right-aligned label (padded to 12 chars).
 * @param default_yes Default answer when Enter is pressed (default true).
 * @return true for yes, false for no.
 */
bool prompt_confirm(const std::string &label, bool default_yes = true);

} // namespace cforge
