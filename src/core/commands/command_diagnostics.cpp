/**
 * @file command_diagnostics.cpp
 * @brief 'errors' and 'warnings' commands — replay diagnostics from the last
 *        build using the same Rust/Cargo-style formatter, filtered by level.
 *
 * The last build's combined stderr+stdout is persisted to
 *   <project>/build/.cforge_diagnostics.log
 * by `execute_tool()` (process_utils.cpp). We re-parse it on each invocation
 * so any improvements to the parser/formatter automatically apply.
 */

#include "cforge/log.hpp"

#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/error_format.hpp"
#include "core/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

cforge_int_t replay_diagnostics(const cforge_context_t *ctx,
                                cforge::diagnostic_level want,
                                cforge_cstring_t empty_msg) {
  // Honor -h/--help.
  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    std::string a = ctx->args.args[i];
    if (a == "-h" || a == "--help") {
      cforge::command_registry::instance().print_command_help(
          want == cforge::diagnostic_level::ERROR ? "errors" : "warnings");
      return 0;
    }
  }

  fs::path start       = fs::absolute(ctx->working_dir);
  fs::path project_dir = start;
  for (auto p = start;; p = p.parent_path()) {
    if (fs::exists(p / "cforge.toml")) {
      project_dir = p;
      break;
    }
    if (p == p.parent_path()) {
      break;
    }
  }

  std::string raw;
  if (!cforge::load_last_build_diagnostics(project_dir, raw)) {
    cforge::logger::print_warning(
        "No build diagnostics on file — run `cforge build` (or test/bench) "
        "first.");
    return 0;
  }

  auto diags = cforge::extract_diagnostics(raw);
  diags      = cforge::deduplicate_diagnostics(std::move(diags));

  cforge_int_t shown = 0;
  for (const auto &d : diags) {
    if (d.level != want) {
      continue;
    }
    cforge::print_diagnostic(d);
    ++shown;
  }

  if (shown == 0) {
    cforge::logger::print_status(empty_msg);
  } else {
    cforge::logger::print_action(want == cforge::diagnostic_level::ERROR ? "Showed" : "Showed",
                                 std::to_string(shown) + " "
                                     + (want == cforge::diagnostic_level::ERROR ? "error"
                                                                                : "warning")
                                     + (shown == 1 ? "" : "s") + " from last build");
  }

  return 0;
}

}  // namespace

cforge_int_t cforge_cmd_errors(const cforge_context_t *ctx) {
  return replay_diagnostics(ctx, cforge::diagnostic_level::ERROR, "Last build had no errors.");
}

cforge_int_t cforge_cmd_warnings(const cforge_context_t *ctx) {
  return replay_diagnostics(ctx, cforge::diagnostic_level::WARNING, "Last build had no warnings.");
}
