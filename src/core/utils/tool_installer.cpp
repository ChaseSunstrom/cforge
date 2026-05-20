/**
 * @file tool_installer.cpp
 * @brief Offer-to-install support for missing external tools.
 */

#include "core/tool_installer.hpp"

#include "cforge/log.hpp"

#include "core/platform.hpp"
#include "core/process_utils.hpp"
#include "core/utils/terminal_prompt.hpp"

#include <fmt/color.h>
#include <fmt/core.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include "core/types.h"

namespace cforge {

namespace {

// ---------------------------------------------------------------------------
// Package-manager registry
// ---------------------------------------------------------------------------
//
// Each entry maps a tool we might fail to find on PATH to the install spec
// for a given package manager. Spec is the exact command we'd invoke; if
// empty, that manager doesn't carry the tool (or we don't have a known recipe
// for it) and we'll try the next available manager.
//
// Keeping this as a flat table rather than a multi-level map keeps it cheap to
// extend — add a row and the prompt picks it up.

// Each row is self-contained: the install recipe AND the list of paths to
// probe afterwards (a ';'-separated string, may use ${VAR} for env expansion).
// Adding a tool means adding rows here — no other source file needs to know.
struct package_spec {
  cforge_cstring_t tool;          // Tool name as on PATH (e.g. "clang-tidy").
  cforge_cstring_t manager;       // Manager binary (e.g. "winget", "brew", "apt-get").
  cforge_cstring_t install_args;  // Args following the manager (one shell string).
  bool needs_sudo;           // Prepend "sudo " on POSIX systems.
  // Candidate absolute paths to probe after a successful install. ';'-separated.
  // ${VAR} sequences are expanded against the current environment at lookup
  // time. Empty / nullptr means "rely on PATH after install" (good enough on
  // POSIX; on Windows the parent process's PATH won't pick up new entries
  // until a fresh shell, so list them explicitly).
  cforge_cstring_t post_install_paths;
};

// clang-format off
constexpr std::array<package_spec, 34> kRegistry = {{
  // ---- Windows ---------------------------------------------------------
  {"git",          "winget", "install --silent --accept-package-agreements --accept-source-agreements Git.Git", false,
   "C:\\Program Files\\Git\\bin\\git.exe;C:\\Program Files (x86)\\Git\\bin\\git.exe"},
  {"clang-tidy",   "winget", "install --silent --accept-package-agreements --accept-source-agreements LLVM.LLVM", false,
   "C:\\Program Files\\LLVM\\bin\\clang-tidy.exe;C:\\Program Files (x86)\\LLVM\\bin\\clang-tidy.exe;${LOCALAPPDATA}\\Programs\\LLVM\\bin\\clang-tidy.exe"},
  {"clang-format", "winget", "install --silent --accept-package-agreements --accept-source-agreements LLVM.LLVM", false,
   "C:\\Program Files\\LLVM\\bin\\clang-format.exe;C:\\Program Files (x86)\\LLVM\\bin\\clang-format.exe;${LOCALAPPDATA}\\Programs\\LLVM\\bin\\clang-format.exe"},
  {"doxygen",      "winget", "install --silent --accept-package-agreements --accept-source-agreements DimitriVanHeesch.Doxygen", false,
   "C:\\Program Files\\doxygen\\bin\\doxygen.exe;C:\\Program Files (x86)\\doxygen\\bin\\doxygen.exe"},
  {"cmake",        "winget", "install --silent --accept-package-agreements --accept-source-agreements Kitware.CMake", false,
   "C:\\Program Files\\CMake\\bin\\cmake.exe;C:\\Program Files (x86)\\CMake\\bin\\cmake.exe"},
  {"ninja",        "winget", "install --silent --accept-package-agreements --accept-source-agreements Ninja-build.Ninja", false, nullptr},
  {"ccache",       "winget", "install --silent --accept-package-agreements --accept-source-agreements ccache.ccache", false, nullptr},

  {"git",          "choco",  "install -y git", false,
   "C:\\Program Files\\Git\\bin\\git.exe"},
  {"clang-tidy",   "choco",  "install -y llvm", false,
   "C:\\Program Files\\LLVM\\bin\\clang-tidy.exe"},
  {"clang-format", "choco",  "install -y llvm", false,
   "C:\\Program Files\\LLVM\\bin\\clang-format.exe"},
  {"doxygen",      "choco",  "install -y doxygen.install", false,
   "C:\\Program Files\\doxygen\\bin\\doxygen.exe"},
  {"cmake",        "choco",  "install -y cmake", false,
   "C:\\Program Files\\CMake\\bin\\cmake.exe"},
  {"ninja",        "choco",  "install -y ninja", false, nullptr},

  {"clang-tidy",   "scoop",  "install llvm", false,
   "${USERPROFILE}\\scoop\\shims\\clang-tidy.exe"},
  {"clang-format", "scoop",  "install llvm", false,
   "${USERPROFILE}\\scoop\\shims\\clang-format.exe"},
  {"doxygen",      "scoop",  "install doxygen", false,
   "${USERPROFILE}\\scoop\\shims\\doxygen.exe"},
  {"cmake",        "scoop",  "install cmake", false,
   "${USERPROFILE}\\scoop\\shims\\cmake.exe"},
  {"ninja",        "scoop",  "install ninja", false,
   "${USERPROFILE}\\scoop\\shims\\ninja.exe"},

  // ---- macOS -----------------------------------------------------------
  {"git",          "brew",   "install git", false,
   "/opt/homebrew/bin/git;/usr/local/bin/git"},
  {"clang-tidy",   "brew",   "install llvm", false,
   "/opt/homebrew/opt/llvm/bin/clang-tidy;/usr/local/opt/llvm/bin/clang-tidy"},
  {"clang-format", "brew",   "install clang-format", false,
   "/opt/homebrew/bin/clang-format;/usr/local/bin/clang-format"},
  {"doxygen",      "brew",   "install doxygen", false,
   "/opt/homebrew/bin/doxygen;/usr/local/bin/doxygen"},
  {"cmake",        "brew",   "install cmake", false,
   "/opt/homebrew/bin/cmake;/usr/local/bin/cmake"},
  {"ninja",        "brew",   "install ninja", false,
   "/opt/homebrew/bin/ninja;/usr/local/bin/ninja"},
  {"ccache",       "brew",   "install ccache", false,
   "/opt/homebrew/bin/ccache;/usr/local/bin/ccache"},

  // ---- Linux (Debian/Ubuntu) -------------------------------------------
  {"git",          "apt-get", "install -y git", true, nullptr},
  {"clang-tidy",   "apt-get", "install -y clang-tidy", true, nullptr},
  {"clang-format", "apt-get", "install -y clang-format", true, nullptr},
  {"doxygen",      "apt-get", "install -y doxygen", true, nullptr},
  {"cmake",        "apt-get", "install -y cmake", true, nullptr},
  {"ninja",        "apt-get", "install -y ninja-build", true, nullptr},
  {"ccache",       "apt-get", "install -y ccache", true, nullptr},

  // ---- Linux (Fedora/RHEL) ---------------------------------------------
  {"clang-tidy",   "dnf",    "install -y clang-tools-extra", true, nullptr},
  {"clang-format", "dnf",    "install -y clang-tools-extra", true, nullptr},
}};
// clang-format on

// Per-platform preference order. The first manager that's actually on PATH
// AND has an entry for `tool` wins.
std::vector<std::string> preferred_managers() {
  if constexpr (cforge::platform::is_windows) {
    return {"winget", "choco", "scoop"};
  } else if constexpr (cforge::platform::is_macos) {
    return {"brew"};
  } else {
    // Linux. dnf/pacman/zypper would go here too if you add recipes.
    return {"apt-get", "dnf"};
  }
}

const package_spec *find_spec(const std::string &tool, const std::string &manager) {
  for (const auto &row : kRegistry) {
    if (tool == row.tool && manager == row.manager) {
      return &row;
    }
  }
  return nullptr;
}

// Split a single shell-style arg string on whitespace. Good enough for the
// flat winget/brew/apt invocations in the table — we don't need full shell
// quoting because none of our recipes contain spaces inside arguments.
std::vector<std::string> split_args(const std::string &s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) {
        out.push_back(std::move(cur));
        cur.clear();
      }
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    out.push_back(std::move(cur));
  }
  return out;
}

// Expand ${VAR} sequences against the current environment. Unknown vars
// expand to the empty string (so the candidate is effectively skipped — its
// path will start with a separator we never produce).
std::string expand_env(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (cforge_size_t i = 0; i < in.size();) {
    if (i + 1 < in.size() && in[i] == '$' && in[i + 1] == '{') {
      cforge_size_t end = in.find('}', i + 2);
      if (end != std::string::npos) {
        std::string var = in.substr(i + 2, end - (i + 2));
        cforge_cstring_t val = std::getenv(var.c_str());
        if (val) {
          out += val;
        }
        i = end + 1;
        continue;
      }
    }
    out.push_back(in[i++]);
  }
  return out;
}

// Probe the post-install candidates declared by the registry row that we
// just ran. Returns the first existing path, or empty if none match.
std::string probe_post_install_path(const package_spec &spec) {
  if (!spec.post_install_paths || !*spec.post_install_paths) {
    return {};
  }
  std::string raw     = spec.post_install_paths;
  cforge_size_t start = 0;
  while (start <= raw.size()) {
    cforge_size_t sep = raw.find(';', start);
    if (sep == std::string::npos) {
      sep = raw.size();
    }
    std::string candidate = expand_env(raw.substr(start, sep - start));
    if (!candidate.empty()) {
      std::error_code ec;
      if (std::filesystem::exists(candidate, ec)) {
        return candidate;
      }
    }
    if (sep == raw.size()) {
      break;
    }
    start = sep + 1;
  }
  return {};
}

}  // namespace

install_outcome offer_install_tool(const std::string &tool) {
  // 1. Find the first package manager that's both available on this system
  //    and has a recipe for the requested tool.
  const package_spec *spec = nullptr;
  std::string manager;
  for (const auto &m : preferred_managers()) {
    if (!is_command_available(m, 3)) {
      continue;
    }
    auto *candidate = find_spec(tool, m);
    if (candidate) {
      spec    = candidate;
      manager = m;
      break;
    }
  }

  if (!spec) {
    // Either no recipe at all (unknown tool for any manager), or all the
    // managers that do have a recipe aren't installed on this system.
    bool any_recipe = false;
    for (const auto &row : kRegistry) {
      if (tool == row.tool) {
        any_recipe = true;
        break;
      }
    }
    return {any_recipe ? install_result::no_manager : install_result::unknown_tool, ""};
  }

  if (!is_interactive_terminal()) {
    return {install_result::non_interactive, ""};
  }

  // 2. Build the command we'll run so we can show it verbatim in the prompt.
  std::vector<std::string> args = split_args(spec->install_args);
  std::string display = spec->needs_sudo ? std::string("sudo ") + manager : std::string(manager);
  for (const auto &a : args) {
    display += " ";
    display += a;
  }

  logger::print_action("Found", std::string(manager) + " — can install " + tool + " automatically");
  fmt::print(stderr, "             {}\n", display);
  if (!prompt_confirm("Install " + tool + " now?", true)) {
    return {install_result::declined, ""};
  }

  // 3. Run it. On POSIX we shell out to sudo so it can prompt for a password
  //    on its own controlling terminal; on Windows we exec the manager
  //    directly.
  logger::print_action("Installing", tool + " via " + manager);

  std::string exec                    = manager;
  std::vector<std::string> final_args = args;
  if (spec->needs_sudo) {
    exec = "sudo";
    final_args.insert(final_args.begin(), manager);
  }

  auto stream_stdout = [](const std::string &chunk) {
    if (!chunk.empty()) {
      std::fwrite(chunk.data(), 1, chunk.size(), stderr);
    }
  };

  auto result = execute_process(exec, final_args, "", stream_stdout, stream_stdout, 600);

  if (result.exit_code != 0) {
    logger::print_error("Package manager exited with code " + std::to_string(result.exit_code));
    return {install_result::failed, ""};
  }

  // Try to locate the freshly-installed binary. On Windows in particular the
  // PATH change made by the installer doesn't flow into this already-running
  // process, so probing the row's declared install paths gives the caller
  // something usable in the *same* invocation rather than forcing the user
  // to open a new terminal.
  std::string discovered = probe_post_install_path(*spec);
  if (discovered.empty() && is_command_available(tool, 3)) {
    discovered = tool;  // It's on PATH after all (e.g. fresh shell on Linux).
  }
  if (!discovered.empty()) {
    logger::print_action("Installed", tool + " (" + discovered + ")");
  } else {
    logger::print_action("Installed", tool);
    logger::print_hint("Installer succeeded but " + tool
                       + " isn't on PATH yet — open a new terminal and re-run cforge.");
  }
  return {install_result::installed, discovered};
}

std::string locate_installed_tool(const std::string &tool) {
  // Walk every row that mentions this tool and probe its post-install paths.
  // Catches the "installed but PATH not refreshed in this shell" case so
  // commands don't re-offer to install something the user already has.
  for (const auto &row : kRegistry) {
    if (tool != row.tool) {
      continue;
    }
    if (!row.post_install_paths || !*row.post_install_paths) {
      continue;
    }
    auto path = probe_post_install_path(row);
    if (!path.empty()) {
      return path;
    }
  }
  return {};
}

}  // namespace cforge
