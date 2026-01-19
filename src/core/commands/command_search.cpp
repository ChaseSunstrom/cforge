/**
 * @file command_search.cpp
 * @brief Implementation of the 'search' command
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/registry.hpp"
#include "core/types.h"
#include <algorithm>

/**
 * @brief Handle the 'search' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_search(const cforge_context_t *ctx) {
  // Parse arguments
  std::string query;
  cforge_size_t limit = 20;
  bool update_index = false;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];
    if (arg == "--limit" || arg == "-l") {
      if (i + 1 < ctx->args.arg_count) {
        try {
          limit = std::stoul(ctx->args.args[++i]);
        } catch (...) {
          cforge::logger::print_error("Invalid limit value");
          return 1;
        }
      }
    } else if (arg == "--update" || arg == "-u") {
      update_index = true;
    } else if (arg[0] != '-') {
      if (query.empty()) {
        query = arg;
      } else {
        query += " " + arg;
      }
    }
  }

  if (query.empty()) {
    cforge::logger::print_error("No search query provided");
    cforge::logger::print_action("Usage", "cforge search <query> [--limit N] [--update]");
    return 1;
  }

  // Initialize registry
  cforge::registry reg;

  // Update index if needed or requested
  if (update_index || reg.needs_update()) {
    if (!reg.update(update_index)) {
      cforge::logger::print_warning("Failed to update package index, using cached version");
    }
  }

  // Search for packages
  cforge::logger::print_action("Searching", "for '" + query + "'");
  auto results = reg.search(query, limit);

  if (results.empty()) {
    cforge::logger::print_warning("No packages found matching '" + query + "'");
    return 0;
  }

  // Display results
  cforge::logger::print_blank();

  // Find maximum name and version length for alignment
  cforge_size_t max_name_len = 0;
  cforge_size_t max_ver_len = 0;
  for (const auto &name : results) {
    max_name_len = std::max(max_name_len, name.length());
    auto pkg = reg.get_package(name);
    if (pkg && !pkg->versions.empty()) {
      max_ver_len = std::max(max_ver_len, pkg->versions[0].version.length());
    }
  }
  max_name_len = std::min(max_name_len, cforge_size_t(25));
  max_ver_len = std::min(max_ver_len, cforge_size_t(12));

  // Print table header
  std::vector<int> widths = {static_cast<int>(max_name_len),
                             static_cast<int>(max_ver_len),
                             45};
  cforge::logger::print_table_header({"Package", "Version", "Description"}, widths, 2);

  // Print each result
  for (const auto &name : results) {
    auto pkg = reg.get_package(name);
    if (!pkg) {
      continue;
    }

    // Get latest version
    std::string version = pkg->versions.empty() ? "?" : pkg->versions[0].version;

    // Format description (truncate if needed)
    std::string desc = pkg->description;
    cforge_size_t max_desc_len = 42;
    if (desc.length() > max_desc_len) {
      desc = desc.substr(0, max_desc_len - 3) + "...";
    }

    // Add badges to description
    if (pkg->verified) {
      desc += " [verified]";
    }
    if (pkg->integration.type == "header_only") {
      desc += " [header-only]";
    }

    // Print as table row
    cforge::logger::print_table_row({name, version, desc}, widths, 2);
  }

  cforge::logger::print_blank();
  cforge::logger::finished(std::to_string(results.size()) + " package(s) found");

  return 0;
}
