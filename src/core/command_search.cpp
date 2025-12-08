/**
 * @file command_search.cpp
 * @brief Implementation of the 'search' command
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/registry.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace cforge;

/**
 * @brief Handle the 'search' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_search(const cforge_context_t *ctx) {
  // Parse arguments
  std::string query;
  size_t limit = 20;
  bool update_index = false;

  for (int i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];
    if (arg == "--limit" || arg == "-l") {
      if (i + 1 < ctx->args.arg_count) {
        try {
          limit = std::stoul(ctx->args.args[++i]);
        } catch (...) {
          logger::print_error("Invalid limit value");
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
    logger::print_error("No search query provided");
    logger::print_action("Usage", "cforge search <query> [--limit N] [--update]");
    return 1;
  }

  // Initialize registry
  registry reg;

  // Update index if needed or requested
  if (update_index || reg.needs_update()) {
    if (!reg.update(update_index)) {
      logger::print_warning("Failed to update package index, using cached version");
    }
  }

  // Search for packages
  logger::print_action("Searching", "for '" + query + "'");
  auto results = reg.search(query, limit);

  if (results.empty()) {
    logger::print_warning("No packages found matching '" + query + "'");
    return 0;
  }

  // Display results
  std::cout << std::endl;

  // Find maximum name length for alignment
  size_t max_name_len = 0;
  for (const auto &name : results) {
    max_name_len = std::max(max_name_len, name.length());
  }
  max_name_len = std::min(max_name_len, size_t(30));

  // Print each result
  for (const auto &name : results) {
    auto pkg = reg.get_package(name);
    if (!pkg) {
      continue;
    }

    // Get latest version
    std::string version = pkg->versions.empty() ? "?" : pkg->versions[0].version;

    // Format name with padding
    std::string display_name = name;
    if (display_name.length() > max_name_len) {
      display_name = display_name.substr(0, max_name_len - 3) + "...";
    }

    // Format description (truncate if needed)
    std::string desc = pkg->description;
    size_t max_desc_len = 50;
    if (desc.length() > max_desc_len) {
      desc = desc.substr(0, max_desc_len - 3) + "...";
    }

    // Print with color formatting
    std::cout << "  ";

    // Print name (green)
    std::cout << "\033[32m" << std::left << std::setw(max_name_len + 2) << display_name << "\033[0m";

    // Print version (cyan)
    std::cout << "\033[36m" << std::setw(12) << version << "\033[0m";

    // Print description
    std::cout << desc;

    // Print verified badge if applicable
    if (pkg->verified) {
      std::cout << " \033[33m[verified]\033[0m";
    }

    // Print header-only badge if applicable
    if (pkg->integration.type == "header_only") {
      std::cout << " \033[35m[header-only]\033[0m";
    }

    std::cout << std::endl;
  }

  std::cout << std::endl;
  logger::finished(std::to_string(results.size()) + " package(s) found");

  return 0;
}
