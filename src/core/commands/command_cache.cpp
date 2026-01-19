/**
 * @file command_cache.cpp
 * @brief Binary cache management command
 *
 * Provides subcommands for managing the local and remote binary cache:
 *   cache list      - Show cached packages
 *   cache clean     - Remove cached packages
 *   cache prune     - Remove old entries to free space
 *   cache stats     - Show cache statistics
 *   cache path      - Print cache directory path
 */

#include "cforge/log.hpp"
#include "core/cache.hpp"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/remote_cache.hpp"
#include "core/types.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {

/**
 * @brief Format bytes as human-readable size
 */
std::string format_size(cforge_size_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit = 0;
  double size = static_cast<double>(bytes);

  while (size >= 1024 && unit < 4) {
    size /= 1024;
    unit++;
  }

  std::stringstream ss;
  ss << std::fixed << std::setprecision(unit > 0 ? 1 : 0) << size << " " << units[unit];
  return ss.str();
}

/**
 * @brief List cached packages
 */
cforge_int_t cache_list(const cforge_context_t *ctx) {
  cforge::package_cache cache;

  // Check for package filter (args[0] = "list", args[1] = package name if present)
  std::string filter;
  for (cforge_int_t i = 1; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg[0] != '-') {
      filter = arg;
      break;
    }
  }

  std::vector<cforge::cache_entry> entries;
  if (filter.empty()) {
    entries = cache.list();
  } else {
    entries = cache.list_package(filter);
  }

  if (entries.empty()) {
    if (filter.empty()) {
      cforge::logger::print_status("Cache is empty");
    } else {
      cforge::logger::print_status("No cached entries for '" + filter + "'");
    }
    return 0;
  }

  // Sort by package name, then version
  std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
    if (a.key.package != b.key.package) return a.key.package < b.key.package;
    return a.key.version < b.key.version;
  });

  cforge::logger::print_section("Cached Packages");
  cforge::logger::print_blank();

  // Table header
  std::vector<int> widths = {20, 12, 12, 10, 8, 10};
  cforge::logger::print_table_header(
      {"Package", "Version", "Platform", "Compiler", "Config", "Size"}, widths);

  cforge_size_t total_size = 0;
  for (const auto &entry : entries) {
    cforge::logger::print_table_row({
        entry.key.package,
        entry.key.version,
        entry.key.platform,
        entry.key.compiler + entry.key.compiler_ver,
        entry.key.config,
        format_size(entry.size_bytes)
    }, widths);
    total_size += entry.size_bytes;
  }

  cforge::logger::print_rule(82);
  cforge::logger::print_plain(
      std::to_string(entries.size()) + " package(s), " + format_size(total_size) + " total");

  return 0;
}

/**
 * @brief Clean cached packages
 */
cforge_int_t cache_clean(const cforge_context_t *ctx) {
  cforge::package_cache cache;

  // Check for package filter (args[0] = "clean", args[1] = package name if present)
  std::string filter;
  for (cforge_int_t i = 1; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg[0] != '-') {
      filter = arg;
      break;
    }
  }

  if (filter.empty()) {
    // Clean entire cache
    auto stats = cache.stats();
    cforge::logger::print_action("Cleaning", "entire cache (" + format_size(stats.total_size_bytes) + ")");

    cforge_size_t removed = cache.clear();
    cforge::logger::print_success("Removed " + std::to_string(removed) + " cached package(s)");
  } else {
    // Clean specific package
    auto entries = cache.list_package(filter);
    if (entries.empty()) {
      cforge::logger::print_warning("No cached entries for '" + filter + "'");
      return 0;
    }

    cforge_size_t total_size = 0;
    for (const auto &e : entries) total_size += e.size_bytes;

    cforge::logger::print_action("Cleaning", filter + " (" + format_size(total_size) + ")");
    cforge_size_t removed = cache.remove_package(filter);
    cforge::logger::print_success("Removed " + std::to_string(removed) + " cached version(s)");
  }

  return 0;
}

/**
 * @brief Prune old cache entries
 */
cforge_int_t cache_prune(const cforge_context_t *ctx) {
  cforge::package_cache cache;

  // Parse size limit (args[0] = "prune", args[1..n] = options)
  cforge_size_t max_size_mb = 5000;  // Default 5GB

  for (cforge_int_t i = 1; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if ((arg == "--size" || arg == "-s") && i + 1 < ctx->args.arg_count) {
      try {
        max_size_mb = std::stoull(ctx->args.args[++i]);
      } catch (...) {
        cforge::logger::print_error("Invalid size value");
        return 1;
      }
    }
  }

  auto stats_before = cache.stats();
  cforge::logger::print_action("Pruning", "cache to " + format_size(max_size_mb * 1024 * 1024));
  cforge::logger::print_verbose("Current size: " + format_size(stats_before.total_size_bytes));

  cforge_size_t removed = cache.prune(max_size_mb);

  if (removed > 0) {
    auto stats_after = cache.stats();
    cforge::logger::print_success("Removed " + std::to_string(removed) + " old package(s)");
    cforge::logger::print_status("Freed " +
        format_size(stats_before.total_size_bytes - stats_after.total_size_bytes));
  } else {
    cforge::logger::print_status("Cache is within size limit, nothing to prune");
  }

  return 0;
}

/**
 * @brief Show cache statistics
 */
cforge_int_t cache_stats(const cforge_context_t * /*ctx*/) {
  cforge::package_cache cache;
  auto stats = cache.stats();

  cforge::logger::print_section("Cache Statistics");
  cforge::logger::print_blank();

  // Local cache section
  cforge::logger::print_section("Local Cache");
  cforge::logger::print_kv("Location", cache.cache_dir().string());
  cforge::logger::print_kv("Packages", std::to_string(stats.total_entries));
  cforge::logger::print_kv("Total size", format_size(stats.total_size_bytes));
  cforge::logger::print_kv("Cache hits", std::to_string(stats.cache_hits));
  cforge::logger::print_kv("Cache misses", std::to_string(stats.cache_misses));

  auto total_requests = stats.cache_hits + stats.cache_misses;
  if (total_requests > 0) {
    double hit_rate = stats.hit_rate() * 100;
    std::string hit_rate_str = fmt::format("{:.1f}%", hit_rate);
    auto color = hit_rate >= 50 ? fmt::color::green : fmt::color::yellow;
    cforge::logger::print_kv_colored("Hit rate", hit_rate_str, color);
  }

  cforge::logger::print_blank();

  // Remote cache section
  auto remote_config = cforge::remote_cache_config::load_from_global_config();
  cforge::remote_cache remote(remote_config);

  cforge::logger::print_section("Remote Cache");
  if (remote.is_available()) {
    cforge::logger::print_kv("URL", remote.url());
    cforge::logger::print_kv("Push enabled", remote.can_push() ? "yes" : "no");
    cforge::logger::print_kv("Remote hits", std::to_string(stats.remote_hits));
    cforge::logger::print_kv("Remote misses", std::to_string(stats.remote_misses));

    // Test connection
    if (remote.test_connection()) {
      cforge::logger::print_kv_colored("Status", "connected", fmt::color::green);

      auto remote_stats = remote.stats();
      if (remote_stats) {
        cforge::logger::print_kv("Remote pkgs", std::to_string(remote_stats->total_packages));
        cforge::logger::print_kv("Remote size", format_size(remote_stats->total_size_bytes));
      }
    } else {
      cforge::logger::print_kv_colored("Status", "unreachable", fmt::color::red);
    }
  } else {
    cforge::logger::print_kv_colored("Status", "disabled", fmt::color::gray);
    cforge::logger::print_blank();
    cforge::logger::print_dim("To enable remote cache, add to ~/.cforge/config.toml:");
    cforge::logger::print_blank();
    cforge::logger::print_dim("  [cache.remote]", 2);
    cforge::logger::print_dim("  enabled = true", 2);
    cforge::logger::print_dim("  url = \"https://cache.example.com/cforge\"", 2);
  }

  return 0;
}

/**
 * @brief Print cache path
 */
cforge_int_t cache_path(const cforge_context_t * /*ctx*/) {
  cforge::package_cache cache;
  cforge::logger::print_plain(cache.cache_dir().string());
  return 0;
}

}  // namespace

/**
 * @brief Handle the 'cache' command
 */
cforge_int_t cforge_cmd_cache(const cforge_context_t *ctx) {
  // Check for help flag
  for (cforge_int_t i = 0; i < ctx->args.arg_count; i++) {
    std::string arg = ctx->args.args[i];
    if (arg == "-h" || arg == "--help") {
      cforge::command_registry::instance().print_command_help("cache");
      return 0;
    }
  }

  // Get subcommand (args[0] is the first argument after "cache")
  std::string subcommand;
  if (ctx->args.arg_count > 0) {
    subcommand = ctx->args.args[0];
  }

  if (subcommand.empty()) {
    cforge::command_registry::instance().print_command_help("cache");
    return 0;
  }

  // Dispatch to subcommand
  if (subcommand == "list" || subcommand == "ls") {
    return cache_list(ctx);
  } else if (subcommand == "clean" || subcommand == "clear") {
    return cache_clean(ctx);
  } else if (subcommand == "prune") {
    return cache_prune(ctx);
  } else if (subcommand == "stats" || subcommand == "info") {
    return cache_stats(ctx);
  } else if (subcommand == "path") {
    return cache_path(ctx);
  } else if (subcommand == "-h" || subcommand == "--help") {
    cforge::command_registry::instance().print_command_help("cache");
    return 0;
  } else {
    cforge::logger::print_error("Unknown subcommand: " + subcommand);
    cforge::logger::print_blank();
    cforge::logger::print_hint("Run 'cforge cache --help' for usage information");
    return 1;
  }
}
