/**
 * @file command_info.cpp
 * @brief Implementation of the 'info' command
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/registry.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>

using namespace cforge;

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

  for (int i = 0; i < ctx->args.arg_count; ++i) {
    std::string arg = ctx->args.args[i];
    if (arg == "--versions" || arg == "-v") {
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
    logger::print_error("No package name provided");
    logger::print_action("Usage", "cforge info <package> [--versions] [--update]");
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

  // Get package info
  auto pkg = reg.get_package(package_name);
  if (!pkg) {
    logger::print_error("Package '" + package_name + "' not found");
    logger::print_action("Hint", "Run 'cforge search " + package_name + "' to search for similar packages");
    return 1;
  }

  // Display package information
  std::cout << std::endl;

  // Package name and version
  std::string latest_version = pkg->versions.empty() ? "?" : pkg->versions[0].version;
  std::cout << "\033[1;32m" << pkg->name << "\033[0m";
  std::cout << " \033[36m" << latest_version << "\033[0m";
  if (pkg->verified) {
    std::cout << " \033[33m[verified]\033[0m";
  }
  std::cout << std::endl;

  // Description
  if (!pkg->description.empty()) {
    std::cout << pkg->description << std::endl;
  }
  std::cout << std::endl;

  // Details table
  auto print_field = [](const std::string &label, const std::string &value) {
    if (!value.empty()) {
      std::cout << "  \033[90m" << std::left << std::setw(14) << label << "\033[0m" << value << std::endl;
    }
  };

  print_field("Repository:", pkg->repository);
  print_field("Homepage:", pkg->homepage);
  print_field("Documentation:", pkg->documentation);
  print_field("License:", pkg->license);
  print_field("Type:", pkg->integration.type);

  if (!pkg->integration.cmake_target.empty()) {
    print_field("CMake target:", pkg->integration.cmake_target);
  }

  // Keywords
  if (!pkg->keywords.empty()) {
    std::string keywords_str;
    for (size_t i = 0; i < pkg->keywords.size(); ++i) {
      if (i > 0) keywords_str += ", ";
      keywords_str += pkg->keywords[i];
    }
    print_field("Keywords:", keywords_str);
  }

  // Categories
  if (!pkg->categories.empty()) {
    std::string categories_str;
    for (size_t i = 0; i < pkg->categories.size(); ++i) {
      if (i > 0) categories_str += ", ";
      categories_str += pkg->categories[i];
    }
    print_field("Categories:", categories_str);
  }

  std::cout << std::endl;

  // Features
  if (show_features && !pkg->features.empty()) {
    std::cout << "\033[1mFeatures:\033[0m" << std::endl;

    // Default features
    if (!pkg->default_features.empty()) {
      std::cout << "  \033[90mDefault:\033[0m ";
      for (size_t i = 0; i < pkg->default_features.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << "\033[32m" << pkg->default_features[i] << "\033[0m";
      }
      std::cout << std::endl << std::endl;
    }

    // All features
    size_t max_name_len = 0;
    for (const auto &[name, feat] : pkg->features) {
      max_name_len = std::max(max_name_len, name.length());
    }
    max_name_len = std::min(max_name_len, size_t(20));

    for (const auto &[name, feat] : pkg->features) {
      std::cout << "  \033[36m" << std::left << std::setw(max_name_len + 2) << name << "\033[0m";

      if (!feat.description.empty()) {
        std::cout << "- " << feat.description;
      }

      // Show if it's a default feature
      bool is_default = std::find(pkg->default_features.begin(),
                                   pkg->default_features.end(),
                                   name) != pkg->default_features.end();
      if (is_default) {
        std::cout << " \033[33m[default]\033[0m";
      }

      // Show required dependencies
      if (!feat.required_deps.empty()) {
        std::cout << " \033[90m(requires: ";
        for (size_t i = 0; i < feat.required_deps.size(); ++i) {
          if (i > 0) std::cout << ", ";
          std::cout << feat.required_deps[i];
        }
        std::cout << ")\033[0m";
      }

      std::cout << std::endl;
    }

    std::cout << std::endl;
  }

  // Versions
  if (show_versions && !pkg->versions.empty()) {
    std::cout << "\033[1mVersions:\033[0m" << std::endl;

    size_t count = std::min(size_t(10), pkg->versions.size());
    for (size_t i = 0; i < count; ++i) {
      const auto &ver = pkg->versions[i];
      std::cout << "  \033[36m" << std::setw(12) << std::left << ver.version << "\033[0m";
      std::cout << " tag: " << ver.tag;

      if (ver.min_cpp > 11) {
        std::cout << " \033[90m(C++" << ver.min_cpp << "+)\033[0m";
      }

      if (ver.yanked) {
        std::cout << " \033[31m[YANKED]\033[0m";
      }

      std::cout << std::endl;
    }

    if (pkg->versions.size() > count) {
      std::cout << "  \033[90m... and " << (pkg->versions.size() - count) << " more\033[0m" << std::endl;
    }

    std::cout << std::endl;
  } else if (!show_versions && !pkg->versions.empty()) {
    // Just show version count
    std::cout << "\033[90m" << pkg->versions.size() << " version(s) available. Use --versions to see all.\033[0m" << std::endl;
    std::cout << std::endl;
  }

  // Maintainers
  if (!pkg->maintainer_owners.empty() || !pkg->maintainer_authors.empty()) {
    std::cout << "\033[1mMaintainers:\033[0m" << std::endl;
    if (!pkg->maintainer_owners.empty()) {
      std::cout << "  Owners: ";
      for (size_t i = 0; i < pkg->maintainer_owners.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << pkg->maintainer_owners[i];
      }
      std::cout << std::endl;
    }
    if (!pkg->maintainer_authors.empty()) {
      std::cout << "  Authors: ";
      for (size_t i = 0; i < pkg->maintainer_authors.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << pkg->maintainer_authors[i];
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;
  }

  // Usage example
  std::cout << "\033[1mUsage:\033[0m" << std::endl;
  std::cout << "  # Add to your cforge.toml:" << std::endl;
  std::cout << "  \033[32m" << pkg->name << "\033[0m = \"\033[36m" << latest_version << "\033[0m\"" << std::endl;

  if (!pkg->features.empty()) {
    std::cout << std::endl;
    std::cout << "  # With features:" << std::endl;
    std::cout << "  \033[32m" << pkg->name << "\033[0m = { version = \"\033[36m" << latest_version << "\033[0m\", features = [";

    // Show first feature as example
    auto it = pkg->features.begin();
    if (it != pkg->features.end()) {
      std::cout << "\"" << it->first << "\"";
    }
    std::cout << "] }" << std::endl;
  }

  std::cout << std::endl;

  return 0;
}
