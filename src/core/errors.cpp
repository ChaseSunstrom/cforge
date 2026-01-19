/**
 * @file errors.cpp
 * @brief Error handling implementation
 */

#include "core/errors.hpp"
#include "cforge/log.hpp"

#include <sstream>

namespace cforge {

error_category get_error_category(error_code code) {
  int value = static_cast<int>(code);
  if (value == 0) return error_category::GENERAL;
  if (value < 100) return error_category::GENERAL;
  if (value < 200) return error_category::CONFIG;
  if (value < 300) return error_category::DEPENDENCY;
  if (value < 400) return error_category::BUILD;
  if (value < 500) return error_category::CACHE;
  if (value < 600) return error_category::NETWORK;
  return error_category::IO;
}

std::string error_code_name(error_code code) {
  switch (code) {
  case error_code::SUCCESS: return "SUCCESS";
  case error_code::UNKNOWN_ERROR: return "UNKNOWN_ERROR";
  case error_code::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
  case error_code::NOT_IMPLEMENTED: return "NOT_IMPLEMENTED";
  case error_code::OPERATION_CANCELLED: return "OPERATION_CANCELLED";

  case error_code::CONFIG_NOT_FOUND: return "CONFIG_NOT_FOUND";
  case error_code::CONFIG_PARSE_ERROR: return "CONFIG_PARSE_ERROR";
  case error_code::CONFIG_INVALID_VALUE: return "CONFIG_INVALID_VALUE";
  case error_code::CONFIG_MISSING_FIELD: return "CONFIG_MISSING_FIELD";
  case error_code::CONFIG_INVALID_TOML: return "CONFIG_INVALID_TOML";
  case error_code::WORKSPACE_NOT_FOUND: return "WORKSPACE_NOT_FOUND";
  case error_code::WORKSPACE_INVALID: return "WORKSPACE_INVALID";

  case error_code::DEP_NOT_FOUND: return "DEP_NOT_FOUND";
  case error_code::DEP_VERSION_NOT_FOUND: return "DEP_VERSION_NOT_FOUND";
  case error_code::DEP_VERSION_CONFLICT: return "DEP_VERSION_CONFLICT";
  case error_code::DEP_FETCH_FAILED: return "DEP_FETCH_FAILED";
  case error_code::DEP_BUILD_FAILED: return "DEP_BUILD_FAILED";
  case error_code::DEP_INVALID_SPEC: return "DEP_INVALID_SPEC";
  case error_code::DEP_CIRCULAR: return "DEP_CIRCULAR";
  case error_code::REGISTRY_UPDATE_FAILED: return "REGISTRY_UPDATE_FAILED";
  case error_code::REGISTRY_OFFLINE: return "REGISTRY_OFFLINE";

  case error_code::BUILD_CMAKE_NOT_FOUND: return "BUILD_CMAKE_NOT_FOUND";
  case error_code::BUILD_CMAKE_CONFIG_FAILED: return "BUILD_CMAKE_CONFIG_FAILED";
  case error_code::BUILD_CMAKE_BUILD_FAILED: return "BUILD_CMAKE_BUILD_FAILED";
  case error_code::BUILD_COMPILE_ERROR: return "BUILD_COMPILE_ERROR";
  case error_code::BUILD_LINK_ERROR: return "BUILD_LINK_ERROR";
  case error_code::BUILD_GENERATOR_NOT_FOUND: return "BUILD_GENERATOR_NOT_FOUND";
  case error_code::BUILD_TARGET_NOT_FOUND: return "BUILD_TARGET_NOT_FOUND";
  case error_code::BUILD_BINARY_NOT_FOUND: return "BUILD_BINARY_NOT_FOUND";

  case error_code::CACHE_NOT_FOUND: return "CACHE_NOT_FOUND";
  case error_code::CACHE_INVALID: return "CACHE_INVALID";
  case error_code::CACHE_WRITE_FAILED: return "CACHE_WRITE_FAILED";
  case error_code::CACHE_READ_FAILED: return "CACHE_READ_FAILED";
  case error_code::CACHE_REMOTE_UNAVAILABLE: return "CACHE_REMOTE_UNAVAILABLE";
  case error_code::CACHE_REMOTE_AUTH_FAILED: return "CACHE_REMOTE_AUTH_FAILED";
  case error_code::CACHE_REMOTE_UPLOAD_FAILED: return "CACHE_REMOTE_UPLOAD_FAILED";

  case error_code::NETWORK_UNAVAILABLE: return "NETWORK_UNAVAILABLE";
  case error_code::NETWORK_TIMEOUT: return "NETWORK_TIMEOUT";
  case error_code::NETWORK_SSL_ERROR: return "NETWORK_SSL_ERROR";
  case error_code::NETWORK_HTTP_ERROR: return "NETWORK_HTTP_ERROR";

  case error_code::IO_FILE_NOT_FOUND: return "IO_FILE_NOT_FOUND";
  case error_code::IO_PERMISSION_DENIED: return "IO_PERMISSION_DENIED";
  case error_code::IO_DISK_FULL: return "IO_DISK_FULL";
  case error_code::IO_READ_ERROR: return "IO_READ_ERROR";
  case error_code::IO_WRITE_ERROR: return "IO_WRITE_ERROR";
  case error_code::IO_PATH_TOO_LONG: return "IO_PATH_TOO_LONG";

  default: return "UNKNOWN";
  }
}

cforge_error cforge_error::make(error_code code, const std::string &message) {
  return cforge_error{code, message, "", {}};
}

cforge_error cforge_error::make(error_code code, const std::string &message,
                                const std::string &context) {
  return cforge_error{code, message, context, {}};
}

cforge_error &cforge_error::with_help(const std::string &suggestion) {
  help.push_back(suggestion);
  return *this;
}

cforge_error &cforge_error::with_help(const std::vector<std::string> &suggestions) {
  help.insert(help.end(), suggestions.begin(), suggestions.end());
  return *this;
}

void cforge_error::print() const {
  logger::print_error(message);

  if (!context.empty()) {
    logger::print_plain("");
    logger::print_plain("  " + context);
  }

  if (!help.empty()) {
    logger::print_plain("");
    for (const auto &h : help) {
      logger::print_plain("  help: " + h);
    }
  }
}

std::string cforge_error::format() const {
  std::stringstream ss;
  ss << "error: " << message;

  if (!context.empty()) {
    ss << "\n  " << context;
  }

  if (!help.empty()) {
    ss << "\n";
    for (const auto &h : help) {
      ss << "\n  help: " << h;
    }
  }

  return ss.str();
}

// ============================================================================
// Common error helpers
// ============================================================================

cforge_error package_not_found_error(const std::string &package_name,
                                     const std::vector<std::string> &suggestions) {
  auto err = cforge_error::make(
      error_code::DEP_NOT_FOUND,
      "Package '" + package_name + "' not found in registry");

  if (!suggestions.empty()) {
    err.with_help("Did you mean '" + suggestions[0] + "'?");
  }
  err.with_help("Run 'cforge deps search " + package_name + "' to find packages");
  err.with_help("Run 'cforge deps update' to refresh the package registry");

  return err;
}

cforge_error config_not_found_error(const std::string &path) {
  return cforge_error::make(
      error_code::CONFIG_NOT_FOUND,
      "Configuration file not found: " + path)
      .with_help("Run 'cforge init' to create a new project")
      .with_help("Make sure you're in a cforge project directory");
}

cforge_error build_failed_error(const std::string &target,
                                const std::string &output) {
  auto err = cforge_error::make(
      error_code::BUILD_CMAKE_BUILD_FAILED,
      "Build failed for target: " + target);

  if (!output.empty()) {
    err.context = output;
  }

  err.with_help("Check the compiler output above for details");
  err.with_help("Run 'cforge clean' and try again");

  return err;
}

cforge_error command_not_found_error(const std::string &command,
                                     const std::vector<std::string> &suggestions) {
  auto err = cforge_error::make(
      error_code::UNKNOWN_ERROR,
      "Unknown command: " + command);

  if (!suggestions.empty()) {
    std::string suggestion_list;
    for (size_t i = 0; i < suggestions.size(); i++) {
      if (i > 0) suggestion_list += ", ";
      suggestion_list += "'" + suggestions[i] + "'";
    }
    err.with_help("Did you mean: " + suggestion_list + "?");
  }
  err.with_help("Run 'cforge help' for a list of available commands");

  return err;
}

} // namespace cforge
