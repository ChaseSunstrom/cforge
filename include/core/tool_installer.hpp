/**
 * @file tool_installer.hpp
 * @brief Offer to install a missing external tool via the platform's package
 *        manager when a cforge command can't find it on PATH.
 *
 * Usage from a command handler:
 *
 *   if (find_clang_tidy().empty()) {
 *     if (cforge::offer_install_tool("clang-tidy") &&
 *         !find_clang_tidy().empty()) {
 *       // retry the rest of the command
 *     } else {
 *       return 1;
 *     }
 *   }
 *
 * No prompt is shown in non-interactive sessions (CI, piped stdin); the
 * function returns false immediately so callers fall back to their normal
 * "tool not found" error path.
 */

#pragma once

#include <string>

namespace cforge {

/**
 * @brief Result of an offer-to-install attempt.
 */
enum class install_result {
  installed,        // Package manager ran successfully; tool should now be on PATH.
  declined,         // User answered "no".
  no_manager,       // No supported package manager found on this platform.
  unknown_tool,     // Tool name not in the registry.
  failed,           // Package manager ran but exited non-zero.
  non_interactive,  // stdin is not a TTY — no prompt shown.
};

/**
 * @brief Outcome of `offer_install_tool` — status plus the resolved path of
 *        the tool when we managed to install + locate it.
 *
 * `path` is populated when the install succeeded AND we found the tool at a
 * well-known install location (Windows installers don't propagate PATH to
 * the already-running parent process, so caller's existing find_X() helpers
 * that only search PATH will still come up empty — using this path bypasses
 * that). Callers should prefer this path if non-empty, then fall back to
 * their normal PATH-based lookup.
 */
struct install_outcome {
  install_result status;
  std::string path;  // Absolute path, empty if unknown.
};

/**
 * @brief Look up a tool, ask the user, and run the platform's install command.
 *
 * The function:
 *   1. Returns `unknown_tool` if `tool` isn't in the registry.
 *   2. Picks the first available package manager for the current platform
 *      (winget→choco→scoop on Windows, brew on macOS, apt/dnf on Linux).
 *      Returns `no_manager` if none found.
 *   3. In non-interactive mode returns `non_interactive` without prompting.
 *   4. Prompts the user with the chosen install command and a (Y/n) default-yes
 *      confirmation. Returns `declined` if they say no.
 *   5. Runs the install command, streaming its output. Returns `installed` on
 *      success, `failed` otherwise.
 *   6. On install success, also probes well-known per-platform install
 *      directories (e.g. C:\\Program Files\\LLVM\\bin) so `path` is set even
 *      when PATH hasn't been refreshed in the current shell.
 *
 * @param tool Tool name as it appears on PATH (e.g. "clang-tidy", "doxygen").
 */
install_outcome offer_install_tool(const std::string &tool);

/**
 * @brief Look for a tool at any of the canonical install paths the registry
 *        knows about — without offering to install or hitting PATH.
 *
 * Useful when PATH may be stale (e.g. the user just installed the tool but
 * hasn't opened a new shell). Returns the first existing absolute path, or
 * an empty string if none of the candidates exist. Cheap — just filesystem
 * stat calls.
 */
std::string locate_installed_tool(const std::string &tool);

}  // namespace cforge
