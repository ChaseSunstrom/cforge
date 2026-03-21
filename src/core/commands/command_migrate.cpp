/**
 * @file command_migrate.cpp
 * @brief Implementation of the 'migrate' command to import CMakeLists.txt into cforge.toml
 */

#include "cforge/log.hpp"
#include "core/cmake_parser.hpp"
#include "core/command_registry.hpp"
#include "core/commands.hpp"
#include "core/types.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// TOML generation helpers
// ============================================================================

/// Quote a string for TOML output.
static std::string toml_quote(const std::string &s) {
    return "\"" + s + "\"";
}

/// Format a TOML array of strings on one line.
static std::string toml_array(const std::vector<std::string> &items) {
    std::string result = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) result += ", ";
        result += toml_quote(items[i]);
    }
    result += "]";
    return result;
}

/// Generate the cforge.toml content from parse results and return as string.
static std::string generate_cforge_toml(const cforge::cmake_parse_result &r) {
    std::ostringstream out;

    // -------------------------------------------------------------------------
    // [project]
    // -------------------------------------------------------------------------
    out << "[project]\n";
    out << "name = " << toml_quote(r.project_name.empty() ? "unknown" : r.project_name) << "\n";

    if (!r.version.empty()) {
        out << "version = " << toml_quote(r.version) << "\n";
    }

    out << "description = \"\"\n";

    if (!r.cpp_standard.empty()) {
        out << "cpp_standard = " << toml_quote(r.cpp_standard) << "\n";
    }

    if (!r.c_standard.empty()) {
        out << "c_standard = " << toml_quote(r.c_standard) << "\n";
    }

    if (!r.binary_type.empty()) {
        out << "binary_type = " << toml_quote(r.binary_type) << "\n";
    }

    out << "\n";

    // -------------------------------------------------------------------------
    // [build]
    // -------------------------------------------------------------------------
    bool has_build_content = !r.source_dirs.empty() || !r.include_dirs.empty();
    if (has_build_content) {
        out << "[build]\n";

        if (!r.source_dirs.empty()) {
            out << "source_dirs = " << toml_array(r.source_dirs) << "\n";
        }

        if (!r.include_dirs.empty()) {
            out << "include_dirs = " << toml_array(r.include_dirs) << "\n";
        }

        out << "\n";
    }

    // -------------------------------------------------------------------------
    // [build.config.debug]
    // -------------------------------------------------------------------------
    if (!r.compile_definitions.empty()) {
        out << "[build.config.debug]\n";
        out << "defines = " << toml_array(r.compile_definitions) << "\n";
        out << "\n";
    }

    // -------------------------------------------------------------------------
    // [dependencies]
    // -------------------------------------------------------------------------
    // Separate find_package deps from FetchContent deps
    std::vector<const cforge::cmake_dependency *> registry_deps;
    std::vector<const cforge::cmake_dependency *> git_deps;

    for (const auto &dep : r.dependencies) {
        if (dep.is_fetch_content) {
            git_deps.push_back(&dep);
        } else if (dep.is_find_package) {
            registry_deps.push_back(&dep);
        }
        // add_subdirectory deps are written as comments (no direct cforge mapping)
    }

    bool has_deps_section = !registry_deps.empty();
    if (has_deps_section) {
        out << "[dependencies]\n";

        for (const auto *dep : registry_deps) {
            std::string ver = dep->version.empty() ? "*" : dep->version;
            out << dep->name << " = " << toml_quote(ver) << "\n";
        }

        out << "\n";
    }

    // FetchContent deps as [dependencies.<name>] inline tables
    for (const auto *dep : git_deps) {
        out << "[dependencies." << dep->name << "]\n";
        if (!dep->git_url.empty()) {
            out << "git = " << toml_quote(dep->git_url) << "\n";
        }
        if (!dep->git_tag.empty()) {
            out << "tag = " << toml_quote(dep->git_tag) << "\n";
        }
        out << "\n";
    }

    // Subdirectory deps as comments
    bool has_subdir_deps = false;
    for (const auto &dep : r.dependencies) {
        if (dep.is_subdirectory) {
            has_subdir_deps = true;
            break;
        }
    }
    if (has_subdir_deps) {
        out << "# add_subdirectory dependencies detected (no automatic cforge mapping):\n";
        for (const auto &dep : r.dependencies) {
            if (dep.is_subdirectory) {
                out << "# subdirectory: " << dep.name << "\n";
            }
        }
        out << "\n";
    }

    // -------------------------------------------------------------------------
    // Comment blocks for things cforge doesn't map directly
    // -------------------------------------------------------------------------
    if (!r.link_libraries.empty()) {
        out << "# Additional link libraries detected (review and add manually if needed):\n";
        out << "# link_libraries = " << toml_array(r.link_libraries) << "\n";
        out << "\n";
    }

    if (!r.compile_options.empty()) {
        out << "# Compiler options detected (add to build.config sections manually if needed):\n";
        out << "# compile_options = " << toml_array(r.compile_options) << "\n";
        out << "\n";
    }

    // -------------------------------------------------------------------------
    // Known limitations footer
    // -------------------------------------------------------------------------
    out << "# ---- Migration Notes ----\n";
    out << "# Variables (${VAR}) were not expanded; paths may contain variable references.\n";
    out << "# Generator expressions ($<...>) were not parsed.\n";
    out << "# Conditional if()/else() blocks were skipped.\n";
    out << "# Only the first add_executable/add_library target was captured.\n";
    out << "# ExternalProject_Add is not handled (only FetchContent_Declare).\n";
    out << "# Review and adjust this file before running 'cforge build'.\n";

    return out.str();
}

// ============================================================================
// cforge_cmd_migrate
// ============================================================================

/**
 * @brief Handle the 'migrate' command to import CMakeLists.txt into cforge.toml
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_migrate(const cforge_context_t *ctx) {
    // -------------------------------------------------------------------------
    // Parse arguments
    // -------------------------------------------------------------------------
    std::string target_path = ".";
    bool dry_run = false;
    bool backup = false;
    std::string output_file = "cforge.toml";

    for (cforge_int_t i = 0; i < ctx->args.arg_count; ++i) {
        std::string arg = ctx->args.args[i];

        if (arg == "-h" || arg == "--help") {
            cforge::command_registry::instance().print_command_help("migrate");
            return 0;
        } else if (arg == "--dry-run" || arg == "-n") {
            dry_run = true;
        } else if (arg == "--backup" || arg == "-b") {
            backup = true;
        } else if ((arg == "--output" || arg == "-o") && i + 1 < ctx->args.arg_count) {
            output_file = ctx->args.args[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            target_path = arg;
        }
    }

    // -------------------------------------------------------------------------
    // Resolve paths
    // -------------------------------------------------------------------------
    std::filesystem::path working_dir(ctx->working_dir);
    std::filesystem::path target_fs(target_path);

    // If target_path is relative, resolve from working_dir
    if (target_fs.is_relative()) {
        target_fs = working_dir / target_fs;
    }

    // Resolve the CMakeLists.txt path
    std::filesystem::path cmake_path;
    if (std::filesystem::is_directory(target_fs)) {
        cmake_path = target_fs / "CMakeLists.txt";
    } else {
        cmake_path = target_fs;
    }

    if (!std::filesystem::exists(cmake_path)) {
        cforge::logger::print_error("CMakeLists.txt not found: " + cmake_path.string());
        return 1;
    }

    // Resolve the output path
    std::filesystem::path output_fs(output_file);
    if (output_fs.is_relative()) {
        // Relative output goes in the same directory as CMakeLists.txt
        output_fs = cmake_path.parent_path() / output_fs;
    }

    // -------------------------------------------------------------------------
    // Start migration
    // -------------------------------------------------------------------------
    cforge::logger::print_action("Migrating",
        cmake_path.string() + " -> " + output_fs.string());
    cforge::logger::print_blank();

    // -------------------------------------------------------------------------
    // Parse CMakeLists.txt
    // -------------------------------------------------------------------------
    cforge::cmake_parse_result parse_result =
        cforge::parse_cmake_file(cmake_path.string());

    // -------------------------------------------------------------------------
    // Print extraction summary
    // -------------------------------------------------------------------------
    if (!parse_result.project_name.empty()) {
        cforge::logger::print_status("Extracted project name: " + parse_result.project_name);
    }
    if (!parse_result.version.empty()) {
        cforge::logger::print_status("Extracted version: " + parse_result.version);
    }
    if (!parse_result.cpp_standard.empty()) {
        cforge::logger::print_status("Extracted C++ standard: " + parse_result.cpp_standard);
    }
    if (!parse_result.c_standard.empty()) {
        cforge::logger::print_status("Extracted C standard: " + parse_result.c_standard);
    }
    if (!parse_result.binary_type.empty()) {
        cforge::logger::print_status("Extracted binary type: " + parse_result.binary_type);
    }
    if (!parse_result.source_dirs.empty()) {
        cforge::logger::print_status(
            "Extracted " + std::to_string(parse_result.source_dirs.size()) +
            " source " + (parse_result.source_dirs.size() == 1 ? "directory" : "directories"));
    }
    if (!parse_result.include_dirs.empty()) {
        cforge::logger::print_status(
            "Extracted " + std::to_string(parse_result.include_dirs.size()) +
            " include " + (parse_result.include_dirs.size() == 1 ? "directory" : "directories"));
    }
    if (!parse_result.dependencies.empty()) {
        cforge::logger::print_status(
            "Extracted " + std::to_string(parse_result.dependencies.size()) +
            (parse_result.dependencies.size() == 1 ? " dependency" : " dependencies"));
    }
    if (!parse_result.compile_definitions.empty()) {
        cforge::logger::print_status(
            "Extracted " + std::to_string(parse_result.compile_definitions.size()) +
            " compiler " +
            (parse_result.compile_definitions.size() == 1 ? "definition" : "definitions"));
    }

    // Print warnings
    if (!parse_result.warnings.empty()) {
        cforge::logger::print_blank();
        for (const auto &w : parse_result.warnings) {
            cforge::logger::print_warning(w);
        }
    }

    // -------------------------------------------------------------------------
    // Generate TOML
    // -------------------------------------------------------------------------
    std::string toml_content = generate_cforge_toml(parse_result);

    // -------------------------------------------------------------------------
    // Dry run: print and exit
    // -------------------------------------------------------------------------
    if (dry_run) {
        cforge::logger::print_blank();
        cforge::logger::print_action("DryRun", "Generated cforge.toml content:");
        cforge::logger::print_blank();
        cforge::logger::print_plain(toml_content);
        return 0;
    }

    // -------------------------------------------------------------------------
    // Backup existing file if requested
    // -------------------------------------------------------------------------
    if (backup && std::filesystem::exists(output_fs)) {
        std::filesystem::path backup_path = std::filesystem::path(output_fs.string() + ".bak");
        try {
            std::filesystem::copy_file(output_fs, backup_path,
                std::filesystem::copy_options::overwrite_existing);
            cforge::logger::print_action("Backup", backup_path.string());
        } catch (const std::exception &e) {
            cforge::logger::print_error(
                "Failed to create backup: " + std::string(e.what()));
            return 1;
        }
    }

    // -------------------------------------------------------------------------
    // Write cforge.toml
    // -------------------------------------------------------------------------
    std::ofstream out_file(output_fs);
    if (!out_file.is_open()) {
        cforge::logger::print_error("Failed to open output file: " + output_fs.string());
        return 1;
    }

    out_file << toml_content;
    out_file.close();

    if (out_file.bad()) {
        cforge::logger::print_error("Failed to write output file: " + output_fs.string());
        return 1;
    }

    cforge::logger::print_blank();
    cforge::logger::created(output_fs.string());

    return 0;
}
