/**
 * @file git_utils.hpp
 * @brief Consolidated Git operations for dependency management
 */

#pragma once

#include "core/process_utils.hpp"
#include "cforge/log.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cforge {

/**
 * @brief Result of a Git operation
 */
struct git_result {
  bool success = false;
  std::string output;
  std::string error;
  int exit_code = -1;
};

/**
 * @brief Options for cloning a Git repository
 */
struct git_clone_options {
  std::string url;
  std::string tag;       // Tag to checkout (optional)
  std::string branch;    // Branch to checkout (optional)
  std::string commit;    // Commit to checkout (optional)
  bool shallow = false;  // Use shallow clone (--depth 1)
  bool quiet = true;     // Suppress Git output
  int timeout = 120;     // Timeout in seconds
};

/**
 * @brief Check if Git is available
 * @return true if git command is available
 */
inline bool is_git_available() {
  return is_command_available("git", 5);
}

/**
 * @brief Check if a directory is a Git repository
 * @param dir Directory to check
 * @return true if directory contains a .git folder
 */
inline bool is_git_repository(const std::filesystem::path &dir) {
  return std::filesystem::exists(dir / ".git");
}

/**
 * @brief Execute a Git command
 *
 * @param args Git command arguments (without "git" prefix)
 * @param working_dir Working directory for the command
 * @param timeout Timeout in seconds
 * @return git_result with operation outcome
 */
inline git_result git_execute(const std::vector<std::string> &args,
                              const std::string &working_dir = "",
                              int timeout = 60) {
  git_result result;

  process_result pr = execute_process("git", args, working_dir, nullptr,
                                      nullptr, timeout);

  result.success = pr.success && pr.exit_code == 0;
  result.output = pr.stdout_output;
  result.error = pr.stderr_output;
  result.exit_code = pr.exit_code;

  return result;
}

/**
 * @brief Clone a Git repository
 *
 * @param url Repository URL
 * @param dest Destination directory
 * @param options Clone options
 * @return git_result with operation outcome
 */
inline git_result git_clone(const std::string &url,
                            const std::filesystem::path &dest,
                            const git_clone_options &options = {}) {
  std::vector<std::string> args = {"clone"};

  if (options.shallow) {
    args.push_back("--depth");
    args.push_back("1");
  }

  if (options.quiet) {
    args.push_back("--quiet");
  }

  // If we have a specific branch/tag, use it during clone
  if (!options.branch.empty()) {
    args.push_back("--branch");
    args.push_back(options.branch);
  } else if (!options.tag.empty()) {
    args.push_back("--branch");
    args.push_back(options.tag);
  }

  args.push_back(url);
  args.push_back(dest.string());

  return git_execute(args, "", options.timeout);
}

/**
 * @brief Checkout a specific reference (tag, branch, or commit)
 *
 * Automatically handles the 'v' prefix for version tags.
 * If checkout fails with the given ref, tries without/with 'v' prefix.
 *
 * @param repo_dir Repository directory
 * @param ref Reference to checkout (tag, branch, or commit hash)
 * @param quiet Suppress output
 * @return git_result with operation outcome
 */
inline git_result git_checkout(const std::filesystem::path &repo_dir,
                               const std::string &ref, bool quiet = true) {
  if (ref.empty()) {
    return {true, "", "", 0}; // Nothing to checkout
  }

  std::vector<std::string> args = {"checkout"};
  if (quiet) {
    args.push_back("--quiet");
  }
  args.push_back(ref);

  git_result result = git_execute(args, repo_dir.string());

  // If failed and ref doesn't start with 'v', try with 'v' prefix
  if (!result.success && !ref.empty() && ref[0] != 'v') {
    args.back() = "v" + ref;
    result = git_execute(args, repo_dir.string());
  }

  // If failed and ref starts with 'v', try without 'v' prefix
  if (!result.success && ref.length() > 1 && ref[0] == 'v') {
    args.back() = ref.substr(1);
    result = git_execute(args, repo_dir.string());
  }

  return result;
}

/**
 * @brief Fetch updates from remote
 *
 * @param repo_dir Repository directory
 * @param fetch_tags Also fetch tags
 * @param quiet Suppress output
 * @return git_result with operation outcome
 */
inline git_result git_fetch(const std::filesystem::path &repo_dir,
                            bool fetch_tags = true, bool quiet = true) {
  std::vector<std::string> args = {"fetch"};

  if (fetch_tags) {
    args.push_back("--tags");
  }

  if (quiet) {
    args.push_back("--quiet");
  }

  return git_execute(args, repo_dir.string(), 120);
}

/**
 * @brief Pull latest changes from remote
 *
 * @param repo_dir Repository directory
 * @param quiet Suppress output
 * @return git_result with operation outcome
 */
inline git_result git_pull(const std::filesystem::path &repo_dir,
                           bool quiet = true) {
  std::vector<std::string> args = {"pull"};

  if (quiet) {
    args.push_back("--quiet");
  }

  return git_execute(args, repo_dir.string(), 120);
}

/**
 * @brief Get current HEAD commit hash
 *
 * @param repo_dir Repository directory
 * @param short_hash Return short (7 char) hash
 * @return Commit hash or empty string on failure
 */
inline std::string git_get_head_commit(const std::filesystem::path &repo_dir,
                                       bool short_hash = false) {
  std::vector<std::string> args = {"rev-parse"};

  if (short_hash) {
    args.push_back("--short");
  }

  args.push_back("HEAD");

  git_result result = git_execute(args, repo_dir.string());

  if (result.success) {
    // Trim whitespace
    std::string hash = result.output;
    hash.erase(hash.find_last_not_of(" \n\r\t") + 1);
    return hash;
  }

  return "";
}

/**
 * @brief Get current branch name
 *
 * @param repo_dir Repository directory
 * @return Branch name or empty string if detached HEAD
 */
inline std::string git_get_current_branch(const std::filesystem::path &repo_dir) {
  git_result result = git_execute(
      {"rev-parse", "--abbrev-ref", "HEAD"}, repo_dir.string());

  if (result.success) {
    std::string branch = result.output;
    branch.erase(branch.find_last_not_of(" \n\r\t") + 1);
    if (branch != "HEAD") { // HEAD means detached
      return branch;
    }
  }

  return "";
}

/**
 * @brief Get the tag pointing to current HEAD (if any)
 *
 * @param repo_dir Repository directory
 * @return Tag name or empty string if no tag points to HEAD
 */
inline std::string git_get_head_tag(const std::filesystem::path &repo_dir) {
  git_result result = git_execute(
      {"describe", "--tags", "--exact-match", "HEAD"}, repo_dir.string());

  if (result.success) {
    std::string tag = result.output;
    tag.erase(tag.find_last_not_of(" \n\r\t") + 1);
    return tag;
  }

  return "";
}

/**
 * @brief Clone or update a Git dependency
 *
 * If the repository already exists, fetches and checks out the requested ref.
 * If it doesn't exist, clones and checks out.
 *
 * @param url Repository URL
 * @param dest Destination directory
 * @param options Clone/checkout options
 * @param verbose Show detailed output
 * @return true if operation succeeded
 */
inline bool clone_or_update_dependency(const std::string &url,
                                       const std::filesystem::path &dest,
                                       const git_clone_options &options,
                                       bool verbose = false) {
  // Determine what ref to checkout
  std::string ref = options.commit;
  if (ref.empty()) ref = options.tag;
  if (ref.empty()) ref = options.branch;

  if (std::filesystem::exists(dest) && is_git_repository(dest)) {
    // Repository exists - fetch and checkout
    if (verbose) {
      logger::print_verbose("Updating existing repository: " + dest.string());
    }

    git_result fetch_result = git_fetch(dest, true, !verbose);
    if (!fetch_result.success) {
      logger::print_warning("Failed to fetch updates for " + dest.string());
      // Continue anyway - might still work with existing checkout
    }

    if (!ref.empty()) {
      git_result checkout_result = git_checkout(dest, ref, !verbose);
      if (!checkout_result.success) {
        logger::print_error("Failed to checkout " + ref + " in " + dest.string());
        return false;
      }
    }

    return true;
  }

  // Clone new repository
  if (verbose) {
    logger::print_status("Cloning " + url + " to " + dest.string());
  }

  // Create parent directory if needed
  if (!std::filesystem::exists(dest.parent_path())) {
    try {
      std::filesystem::create_directories(dest.parent_path());
    } catch (const std::exception &e) {
      logger::print_error("Failed to create directory: " + std::string(e.what()));
      return false;
    }
  }

  git_result clone_result = git_clone(url, dest, options);
  if (!clone_result.success) {
    logger::print_error("Failed to clone " + url);
    if (!clone_result.error.empty()) {
      logger::print_error(clone_result.error);
    }
    return false;
  }

  // If we need a specific ref that wasn't specified during clone, checkout now
  if (!ref.empty() && options.branch.empty() && options.tag.empty()) {
    git_result checkout_result = git_checkout(dest, ref, !verbose);
    if (!checkout_result.success) {
      logger::print_error("Failed to checkout " + ref);
      return false;
    }
  }

  return true;
}

} // namespace cforge
