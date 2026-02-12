/**
 * @file command_info.cpp
 * @brief Implementation of the 'info' command
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/registry.hpp"
#include "core/types.h"
#include <algorithm>
#include <fmt/color.h>

/**
 * @brief Handle the 'info' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_info(const cforge_context_t *ctx) {
  // Parse arguments
  std::string package_name;
  bool show_versions = false;
  bool show_features = true;
  bool update_index = false;

  for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];
    if (arg == "--versions" || arg == "-V") {
      show_versions = true;
    } else if (arg == "--no-features") {
      show_features = false;
    } else if (arg == "--update" || arg == "-u") {
      update_index = true;
    } else if (arg[0] != '-') {
      package_name = arg;
    }
  }

  if (package_name.empty()) {
    cforge::logger::print_error("No package name provided");
    cforge::logger::print_action("Usage", "cforge info <package> [--versions] [--update]");
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

  // Get package info
  auto pkg = reg.get_package(package_name);
  if (!pkg) {
    cforge::logger::print_error("Package '" + package_name + "' not found");
    cforge::logger::print_action("Hint", "Run 'cforge search " + package_name + "' to search for similar packages");
    return 1;
  }

  // Display package information
  cforge::logger::print_blank();

  // Package name and version
  std::string latest_version = pkg->versions.empty() ? "?" : pkg->versions[0].version;
  std::string header = pkg->name + " " + latest_version;
  if (pkg->verified) {
    header += " [verified]";
  }
  cforge::logger::print_emphasis(header);

  // Description
  if (!pkg->description.empty()) {
    cforge::logger::print_plain(pkg->description);
  }
  cforge::logger::print_blank();

  // Details table
  if (!pkg->repository.empty()) {
    cforge::logger::print_kv("Repository:", pkg->repository);
  }
  if (!pkg->homepage.empty()) {
    cforge::logger::print_kv("Homepage:", pkg->homepage);
  }
  if (!pkg->documentation.empty()) {
    cforge::logger::print_kv("Documentation:", pkg->documentation);
  }
  if (!pkg->license.empty()) {
    cforge::logger::print_kv("License:", pkg->license);
  }
  if (!pkg->integration.type.empty()) {
    cforge::logger::print_kv("Type:", pkg->integration.type);
  }

  if (!pkg->integration.cmake_target.empty()) {
    cforge::logger::print_kv("CMake target:", pkg->integration.cmake_target);
  }

  // Keywords
  if (!pkg->keywords.empty()) {
    std::string keywords_str;
    for (cforge_size_t i = 0; i < pkg->keywords.size(); ++i) {
      if (i > 0) keywords_str += ", ";
      keywords_str += pkg->keywords[i];
    }
    cforge::logger::print_kv("Keywords:", keywords_str);
  }

  // Categories
  if (!pkg->categories.empty()) {
    std::string categories_str;
    for (cforge_size_t i = 0; i < pkg->categories.size(); ++i) {
      if (i > 0) categories_str += ", ";
      categories_str += pkg->categories[i];
    }
    cforge::logger::print_kv("Categories:", categories_str);
  }

  cforge::logger::print_blank();

  // Features
  if (show_features && !pkg->features.empty()) {
    cforge::logger::print_section("Features:");

    // Default features
    if (!pkg->default_features.empty()) {
      std::string defaults;
      for (cforge_size_t i = 0; i < pkg->default_features.size(); ++i) {
        if (i > 0) defaults += ", ";
        defaults += pkg->default_features[i];
      }
      cforge::logger::print_kv("Default:", defaults);
      cforge::logger::print_blank();
    }

    // All features
    cforge_size_t max_name_len = 0;
    for (const auto &[name, feat] : pkg->features) {
      max_name_len = std::max(max_name_len, name.length());
    }
    max_name_len = std::min(max_name_len, cforge_size_t(20));

    for (const auto &[name, feat] : pkg->features) {
      std::string feature_line = name;
      feature_line.resize(max_name_len + 2, ' ');

      if (!feat.description.empty()) {
        feature_line += "- " + feat.description;
      }

      // Show if it's a default feature
      bool is_default = std::find(pkg->default_features.begin(),
                                   pkg->default_features.end(),
                                   name) != pkg->default_features.end();
      if (is_default) {
        feature_line += " [default]";
      }

      // Show required dependencies
      if (!feat.required_deps.empty()) {
        feature_line += " (requires: ";
        for (cforge_size_t i = 0; i < feat.required_deps.size(); ++i) {
          if (i > 0) feature_line += ", ";
          feature_line += feat.required_deps[i];
        }
        feature_line += ")";
      }

      cforge::logger::print_kv_colored(name,
        feature_line.substr(max_name_len + 2),
        fmt::color::white,
        max_name_len + 2);
    }

    cforge::logger::print_blank();
  }

  // Versions
  if (show_versions && !pkg->versions.empty()) {
    cforge::logger::print_section("Versions:");

    cforge_size_t count = std::min(cforge_size_t(10), pkg->versions.size());
    for (cforge_size_t i = 0; i < count; ++i) {
      const auto &ver = pkg->versions[i];
      std::string version_info = "tag: " + ver.tag;

      if (ver.min_cpp > 11) {
        version_info += " (C++" + std::to_string(ver.min_cpp) + "+)";
      }

      if (ver.yanked) {
        version_info += " [YANKED]";
      }

      cforge::logger::print_kv_colored(ver.version, version_info, fmt::color::white, 12);
    }

    if (pkg->versions.size() > count) {
      cforge::logger::print_dim("... and " + std::to_string(pkg->versions.size() - count) + " more", 2);
    }

    cforge::logger::print_blank();
  } else if (!show_versions && !pkg->versions.empty()) {
    // Just show version count
    cforge::logger::print_dim(std::to_string(pkg->versions.size()) + " version(s) available. Use --versions to see all.");
    cforge::logger::print_blank();
  }

  // Maintainers
  if (!pkg->maintainer_owners.empty() || !pkg->maintainer_authors.empty()) {
    cforge::logger::print_section("Maintainers:");
    if (!pkg->maintainer_owners.empty()) {
      std::string owners;
      for (cforge_size_t i = 0; i < pkg->maintainer_owners.size(); ++i) {
        if (i > 0) owners += ", ";
        owners += pkg->maintainer_owners[i];
      }
      cforge::logger::print_kv("Owners:", owners);
    }
    if (!pkg->maintainer_authors.empty()) {
      std::string authors;
      for (cforge_size_t i = 0; i < pkg->maintainer_authors.size(); ++i) {
        if (i > 0) authors += ", ";
        authors += pkg->maintainer_authors[i];
      }
      cforge::logger::print_kv("Authors:", authors);
    }
    cforge::logger::print_blank();
  }

  // Usage example
  cforge::logger::print_section("Usage:");
  cforge::logger::print_dim("# Add to your cforge.toml:", 2);
  cforge::logger::print_plain("  " + pkg->name + " = \"" + latest_version + "\"");

  if (!pkg->features.empty()) {
    cforge::logger::print_blank();
    cforge::logger::print_dim("# With features:", 2);

    // Show first feature as example
    auto it = pkg->features.begin();
    std::string feature_example = "  " + pkg->name + " = { version = \"" + latest_version + "\", features = [";
    if (it != pkg->features.end()) {
      feature_example += "\"" + it->first + "\"";
    }
    feature_example += "] }";
    cforge::logger::print_plain(feature_example);
  }

  cforge::logger::print_blank();

  return 0;
}
