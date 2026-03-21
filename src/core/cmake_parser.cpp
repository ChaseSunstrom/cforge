/**
 * @file cmake_parser.cpp
 * @brief Regex-based CMakeLists.txt parser implementation
 *
 * Each extractor function is independent and fault-tolerant. A failure in one
 * extractor does not affect the others. Variables (${VAR}) and generator
 * expressions ($<...>) are treated as opaque tokens.
 */

#include "core/cmake_parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace cforge {

// ============================================================================
// Pre-processing helpers
// ============================================================================

/// Strip inline CMake comments (everything from # onwards), but be careful
/// not to strip # inside quoted strings.
static std::string strip_comment(const std::string &line) {
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') {
            in_quotes = !in_quotes;
        } else if (line[i] == '#' && !in_quotes) {
            return line.substr(0, i);
        }
    }
    return line;
}

/// Trim trailing whitespace from a string.
static std::string rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(" \t\r\n");
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

/// Trim leading whitespace from a string.
static std::string ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    return (start == std::string::npos) ? "" : s.substr(start);
}

static std::string trim(const std::string &s) {
    return ltrim(rtrim(s));
}

/// Load the file and join continued lines (lines ending with \).
/// Returns a vector of logical lines with comments stripped.
static std::vector<std::string> preprocess_cmake(const std::string &file_path) {
    std::vector<std::string> logical_lines;

    std::ifstream f(file_path);
    if (!f.is_open()) {
        return logical_lines;
    }

    std::string current_logical;
    std::string raw_line;

    while (std::getline(f, raw_line)) {
        // Strip comment first
        std::string stripped = strip_comment(raw_line);
        stripped = rtrim(stripped);

        if (!stripped.empty() && stripped.back() == '\\') {
            // Line continuation — remove the backslash and append
            stripped.pop_back();
            current_logical += stripped + " ";
        } else {
            current_logical += stripped;
            // Only emit non-empty lines
            std::string trimmed = trim(current_logical);
            if (!trimmed.empty()) {
                logical_lines.push_back(trimmed);
            }
            current_logical.clear();
        }
    }

    // Emit any remaining content
    if (!current_logical.empty()) {
        std::string trimmed = trim(current_logical);
        if (!trimmed.empty()) {
            logical_lines.push_back(trimmed);
        }
    }

    return logical_lines;
}

/// Join all logical lines into one big string for multi-line pattern matching.
static std::string join_lines(const std::vector<std::string> &lines) {
    std::string result;
    result.reserve(lines.size() * 80);
    for (const auto &l : lines) {
        result += l;
        result += '\n';
    }
    return result;
}

/// Split a token list (whitespace-separated, respecting quotes) and filter
/// out ${...} variable references and $<...> generator expressions.
static std::vector<std::string> tokenize_cmake_args(const std::string &s) {
    std::vector<std::string> tokens;
    std::string tok;
    bool in_quotes = false;

    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if ((c == ' ' || c == '\t' || c == '\n' || c == '\r') && !in_quotes) {
            std::string t = trim(tok);
            if (!t.empty()) {
                tokens.push_back(t);
            }
            tok.clear();
        } else {
            tok += c;
        }
    }
    std::string t = trim(tok);
    if (!t.empty()) {
        tokens.push_back(t);
    }

    // Filter out ${VAR} and $<GENEX> references
    std::vector<std::string> filtered;
    for (auto &token : tokens) {
        if (token.find("${") == std::string::npos && token.find("$<") == std::string::npos) {
            filtered.push_back(token);
        }
    }
    return filtered;
}

/// Deduplicate a vector while preserving order.
static void dedup(std::vector<std::string> &v) {
    std::set<std::string> seen;
    std::vector<std::string> result;
    for (auto &s : v) {
        if (seen.insert(s).second) {
            result.push_back(s);
        }
    }
    v = std::move(result);
}

// ============================================================================
// Extractor functions
// ============================================================================

/// Extract project name and version from project() call.
static void extract_project(const std::string &content, cmake_parse_result &result) {
    try {
        // Match project( ... ) — potentially multiline (we use \n in content)
        std::regex re_project(
            R"(project\s*\(([^)]+)\))",
            std::regex::icase);

        auto it = std::sregex_iterator(content.begin(), content.end(), re_project);
        auto end = std::sregex_iterator();

        if (it == end) {
            result.warnings.push_back("project() not found in CMakeLists.txt");
            return;
        }

        std::string body = (*it)[1].str();

        // Extract project name — first identifier in the body
        std::regex re_name(R"(^\s*(\w+))");
        std::smatch m_name;
        if (std::regex_search(body, m_name, re_name)) {
            result.project_name = m_name[1].str();
        }

        // Extract VERSION
        std::regex re_version(R"(VERSION\s+([\d.]+))", std::regex::icase);
        std::smatch m_ver;
        if (std::regex_search(body, m_ver, re_version)) {
            result.version = m_ver[1].str();
        }

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse project() call");
    }
}

/// Extract C++ standard from set(CMAKE_CXX_STANDARD xx) or compile features.
static void extract_cpp_standard(const std::string &content, cmake_parse_result &result) {
    try {
        // set(CMAKE_CXX_STANDARD nn)
        std::regex re_set(
            R"(set\s*\(\s*CMAKE_CXX_STANDARD\s+(\d+)\s*\))",
            std::regex::icase);
        std::smatch m;
        if (std::regex_search(content, m, re_set)) {
            result.cpp_standard = m[1].str();
            return;
        }

        // Fallback: target_compile_features with cxx_std_nn
        std::regex re_feat(R"(cxx_std_(\d+))", std::regex::icase);
        if (std::regex_search(content, m, re_feat)) {
            result.cpp_standard = m[1].str();
        }

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse C++ standard setting");
    }
}

/// Extract C standard from set(CMAKE_C_STANDARD xx).
static void extract_c_standard(const std::string &content, cmake_parse_result &result) {
    try {
        std::regex re_set(
            R"(set\s*\(\s*CMAKE_C_STANDARD\s+(\d+)\s*\))",
            std::regex::icase);
        std::smatch m;
        if (std::regex_search(content, m, re_set)) {
            result.c_standard = m[1].str();
        }

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse C standard setting");
    }
}

/// Extract binary type (executable, static, shared, interface) and target name.
static void extract_binary_type(const std::string &content, cmake_parse_result &result) {
    try {
        bool found_executable = false;
        bool found_library = false;

        // add_executable(target ...)
        std::regex re_exe(
            R"(add_executable\s*\(\s*(\w+))",
            std::regex::icase);
        std::smatch m_exe;
        if (std::regex_search(content, m_exe, re_exe)) {
            found_executable = true;
            result.binary_type = "executable";
            result.target_name = m_exe[1].str();
        }

        // add_library(target [STATIC|SHARED|INTERFACE|MODULE] ...)
        std::regex re_lib(
            R"(add_library\s*\(\s*(\w+)\s*(STATIC|SHARED|INTERFACE|MODULE)?)",
            std::regex::icase);
        std::smatch m_lib;
        if (std::regex_search(content, m_lib, re_lib)) {
            found_library = true;

            if (!found_executable) {
                // Only use library if no executable found
                result.target_name = m_lib[1].str();
                std::string lib_type = m_lib[2].str();

                // Normalize to uppercase for comparison
                std::transform(lib_type.begin(), lib_type.end(), lib_type.begin(), ::toupper);

                if (lib_type == "SHARED") {
                    result.binary_type = "shared";
                } else if (lib_type == "INTERFACE") {
                    result.binary_type = "interface";
                } else if (lib_type == "MODULE") {
                    result.binary_type = "shared";
                    result.warnings.push_back(
                        "MODULE library mapped to binary_type = \"shared\" (closest match)");
                } else {
                    // STATIC or empty defaults to static
                    result.binary_type = "static";
                }
            } else {
                result.warnings.push_back(
                    "Both add_executable and add_library found; using executable target only");
            }
        }

        if (!found_executable && !found_library) {
            result.warnings.push_back("No add_executable() or add_library() found");
        }

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse binary type");
    }
}

/// Extract source directories from file(GLOB...) or target_sources().
static void extract_source_dirs(const std::string &content, cmake_parse_result &result,
                                const std::string &cmake_dir) {
    try {
        bool found_any = false;

        // Strategy 1: file(GLOB[_RECURSE] VAR patterns...)
        std::regex re_glob(
            R"(file\s*\(\s*GLOB(?:_RECURSE)?\s+\w+\s+([^)]+)\))",
            std::regex::icase);

        auto it = std::sregex_iterator(content.begin(), content.end(), re_glob);
        auto end_it = std::sregex_iterator();

        for (; it != end_it; ++it) {
            std::string glob_args = (*it)[1].str();
            auto tokens = tokenize_cmake_args(glob_args);

            for (auto &tok : tokens) {
                // Skip CMake keywords
                if (tok == "CONFIGURE_DEPENDS" || tok == "LIST_DIRECTORIES" ||
                    tok == "true" || tok == "false") {
                    continue;
                }

                // Find the directory portion of the glob pattern
                // e.g., "src/*.cpp" -> "src", "src/**/*.cpp" -> "src"
                size_t last_sep = tok.find_last_of("/\\");
                std::string dir_part;
                if (last_sep != std::string::npos) {
                    dir_part = tok.substr(0, last_sep);
                } else {
                    // Pattern like "*.cpp" in the same directory
                    dir_part = ".";
                }

                // Skip if contains variables or wildcards in directory part
                if (dir_part.find("${") != std::string::npos ||
                    dir_part.find("$<") != std::string::npos) {
                    continue;
                }

                // Remove trailing wildcard segments
                // e.g., "src/**" -> "src"
                while (!dir_part.empty() && (dir_part.back() == '*' || dir_part.back() == '*')) {
                    size_t sep = dir_part.find_last_of("/\\");
                    if (sep != std::string::npos) {
                        dir_part = dir_part.substr(0, sep);
                    } else {
                        dir_part = ".";
                        break;
                    }
                }

                if (!dir_part.empty()) {
                    result.source_dirs.push_back(dir_part);
                    found_any = true;
                }
            }
        }

        // Strategy 2: target_sources(target PRIVATE|PUBLIC|INTERFACE sources...)
        if (!found_any) {
            std::regex re_src(
                R"(target_sources\s*\(\s*\w+\s+(?:PRIVATE|PUBLIC|INTERFACE)\s+([^)]+)\))",
                std::regex::icase);

            auto it2 = std::sregex_iterator(content.begin(), content.end(), re_src);
            for (; it2 != end_it; ++it2) {
                std::string args = (*it2)[1].str();
                auto tokens = tokenize_cmake_args(args);

                for (auto &tok : tokens) {
                    if (tok == "PRIVATE" || tok == "PUBLIC" || tok == "INTERFACE") continue;

                    size_t last_sep = tok.find_last_of("/\\");
                    std::string dir_part;
                    if (last_sep != std::string::npos) {
                        dir_part = tok.substr(0, last_sep);
                    } else {
                        dir_part = ".";
                    }

                    if (dir_part.find("${") == std::string::npos &&
                        dir_part.find("$<") == std::string::npos &&
                        !dir_part.empty()) {
                        result.source_dirs.push_back(dir_part);
                        found_any = true;
                    }
                }
            }
        }

        // Strategy 3: default to src/ if it exists
        if (!found_any) {
            std::filesystem::path src_dir = std::filesystem::path(cmake_dir) / "src";
            if (std::filesystem::exists(src_dir) && std::filesystem::is_directory(src_dir)) {
                result.source_dirs.push_back("src");
                result.warnings.push_back(
                    "No explicit source globs found; defaulting to src/");
            }
        }

        dedup(result.source_dirs);

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse source directories");
    }
}

/// Extract include directories from target_include_directories() and include_directories().
static void extract_include_dirs(const std::string &content, cmake_parse_result &result) {
    try {
        // target_include_directories(target PUBLIC|PRIVATE|INTERFACE dirs...)
        std::regex re_tid(
            R"(target_include_directories\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)\s+([^)]+)\))",
            std::regex::icase);

        auto it = std::sregex_iterator(content.begin(), content.end(), re_tid);
        auto end_it = std::sregex_iterator();

        for (; it != end_it; ++it) {
            std::string args = (*it)[1].str();
            auto tokens = tokenize_cmake_args(args);
            for (auto &tok : tokens) {
                if (tok == "PUBLIC" || tok == "PRIVATE" || tok == "INTERFACE") continue;
                if (tok.find("${") != std::string::npos) continue;
                if (tok.find("$<") != std::string::npos) continue;
                result.include_dirs.push_back(tok);
            }
        }

        // Fallback: include_directories(dirs...)
        std::regex re_id(
            R"(include_directories\s*\(\s*([^)]+)\))",
            std::regex::icase);

        auto it2 = std::sregex_iterator(content.begin(), content.end(), re_id);
        bool found_fallback = false;
        for (; it2 != end_it; ++it2) {
            std::string args = (*it2)[1].str();
            auto tokens = tokenize_cmake_args(args);
            for (auto &tok : tokens) {
                if (tok == "BEFORE" || tok == "AFTER" || tok == "SYSTEM") continue;
                if (tok.find("${") != std::string::npos) continue;
                if (tok.find("$<") != std::string::npos) continue;
                result.include_dirs.push_back(tok);
                found_fallback = true;
            }
        }
        if (found_fallback) {
            result.warnings.push_back(
                "include_directories() is non-target-scoped; review include paths.");
        }

        dedup(result.include_dirs);

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse include directories");
    }
}

/// Extract dependencies from find_package(), FetchContent_Declare(), add_subdirectory().
static void extract_dependencies(const std::string &content, cmake_parse_result &result) {
    try {
        // --- find_package ---
        std::regex re_fp(
            R"(find_package\s*\(\s*(\w+)(?:\s+([\d.]+))?(?:\s+REQUIRED)?(?:\s+COMPONENTS\s+([^)]+))?)",
            std::regex::icase);

        auto end_it = std::sregex_iterator();
        auto it = std::sregex_iterator(content.begin(), content.end(), re_fp);
        for (; it != end_it; ++it) {
            cmake_dependency dep;
            dep.name = (*it)[1].str();
            dep.version = (*it)[2].str();
            dep.is_find_package = true;
            dep.is_fetch_content = false;
            dep.is_subdirectory = false;
            result.dependencies.push_back(dep);
        }

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse find_package() calls");
    }

    try {
        // --- FetchContent_Declare ---
        // Match FetchContent_Declare( ... ) blocks (multiline-friendly since we joined lines)
        std::regex re_fc(
            R"(FetchContent_Declare\s*\(\s*(\w+)\s+([^)]+)\))",
            std::regex::icase);

        auto end_it = std::sregex_iterator();
        auto it = std::sregex_iterator(content.begin(), content.end(), re_fc);
        for (; it != end_it; ++it) {
            cmake_dependency dep;
            dep.name = (*it)[1].str();
            dep.is_fetch_content = true;
            dep.is_find_package = false;
            dep.is_subdirectory = false;

            std::string body = (*it)[2].str();

            // GIT_REPOSITORY
            std::regex re_repo(R"(GIT_REPOSITORY\s+(\S+))", std::regex::icase);
            std::smatch m_repo;
            if (std::regex_search(body, m_repo, re_repo)) {
                dep.git_url = m_repo[1].str();
            }

            // GIT_TAG
            std::regex re_tag(R"(GIT_TAG\s+(\S+))", std::regex::icase);
            std::smatch m_tag;
            if (std::regex_search(body, m_tag, re_tag)) {
                dep.git_tag = m_tag[1].str();
            } else {
                result.warnings.push_back(
                    "FetchContent dependency '" + dep.name + "' has no GIT_TAG — using \"latest\"");
            }

            result.dependencies.push_back(dep);
        }

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse FetchContent_Declare() calls");
    }

    try {
        // --- add_subdirectory ---
        std::regex re_sub(
            R"(add_subdirectory\s*\(\s*([^)\s]+))",
            std::regex::icase);

        auto end_it = std::sregex_iterator();
        auto it = std::sregex_iterator(content.begin(), content.end(), re_sub);
        for (; it != end_it; ++it) {
            cmake_dependency dep;
            std::string dir_path = (*it)[1].str();

            // Use the basename as the dependency name
            std::filesystem::path p(dir_path);
            dep.name = p.filename().string();
            if (dep.name.empty()) {
                dep.name = dir_path;
            }

            dep.is_subdirectory = true;
            dep.is_fetch_content = false;
            dep.is_find_package = false;
            result.dependencies.push_back(dep);
        }

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse add_subdirectory() calls");
    }
}

/// Extract compile definitions from target_compile_definitions() and add_definitions().
static void extract_compile_definitions(const std::string &content, cmake_parse_result &result) {
    try {
        // target_compile_definitions(target PUBLIC|PRIVATE|INTERFACE defs...)
        std::regex re_tcd(
            R"(target_compile_definitions\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)\s+([^)]+)\))",
            std::regex::icase);

        auto end_it = std::sregex_iterator();
        auto it = std::sregex_iterator(content.begin(), content.end(), re_tcd);
        for (; it != end_it; ++it) {
            std::string args = (*it)[1].str();
            auto tokens = tokenize_cmake_args(args);
            for (auto &tok : tokens) {
                if (tok == "PUBLIC" || tok == "PRIVATE" || tok == "INTERFACE") continue;
                result.compile_definitions.push_back(tok);
            }
        }

        // Fallback: add_definitions(-DFOO ...)
        std::regex re_ad(
            R"(add_definitions\s*\(\s*([^)]+)\))",
            std::regex::icase);

        auto it2 = std::sregex_iterator(content.begin(), content.end(), re_ad);
        for (; it2 != end_it; ++it2) {
            std::string args = (*it2)[1].str();
            auto tokens = tokenize_cmake_args(args);
            for (auto &tok : tokens) {
                // Strip leading -D
                if (tok.size() > 2 && tok.substr(0, 2) == "-D") {
                    tok = tok.substr(2);
                }
                result.compile_definitions.push_back(tok);
            }
        }

        dedup(result.compile_definitions);

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse compile definitions");
    }
}

/// Extract compile options from target_compile_options().
static void extract_compile_options(const std::string &content, cmake_parse_result &result) {
    try {
        std::regex re_tco(
            R"(target_compile_options\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)\s+([^)]+)\))",
            std::regex::icase);

        auto end_it = std::sregex_iterator();
        auto it = std::sregex_iterator(content.begin(), content.end(), re_tco);
        for (; it != end_it; ++it) {
            std::string args = (*it)[1].str();
            auto tokens = tokenize_cmake_args(args);
            for (auto &tok : tokens) {
                if (tok == "PUBLIC" || tok == "PRIVATE" || tok == "INTERFACE") continue;
                result.compile_options.push_back(tok);
            }
        }

        dedup(result.compile_options);

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse compile options");
    }
}

/// Extract link libraries from target_link_libraries().
/// Filters out names already captured as dependencies.
static void extract_link_libraries(const std::string &content, cmake_parse_result &result) {
    try {
        std::regex re_tll(
            R"(target_link_libraries\s*\(\s*\w+\s+(?:PUBLIC|PRIVATE|INTERFACE)?\s*([^)]+)\))",
            std::regex::icase);

        // Build set of known dependency names
        std::set<std::string> dep_names;
        for (const auto &dep : result.dependencies) {
            dep_names.insert(dep.name);
            // Also insert lowercase variant
            std::string lower = dep.name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            dep_names.insert(lower);
        }

        auto end_it = std::sregex_iterator();
        auto it = std::sregex_iterator(content.begin(), content.end(), re_tll);
        for (; it != end_it; ++it) {
            std::string args = (*it)[1].str();
            auto tokens = tokenize_cmake_args(args);
            for (auto &tok : tokens) {
                if (tok == "PUBLIC" || tok == "PRIVATE" || tok == "INTERFACE") continue;
                if (tok.find("${") != std::string::npos) continue;
                if (tok.find("$<") != std::string::npos) continue;

                // Check if this library is already captured as a dependency
                // (exact match or with common CMake target name patterns)
                bool is_dep = false;
                for (const auto &dn : dep_names) {
                    if (tok == dn || tok.find(dn) != std::string::npos) {
                        is_dep = true;
                        break;
                    }
                }

                if (!is_dep) {
                    result.link_libraries.push_back(tok);
                }
            }
        }

        dedup(result.link_libraries);

    } catch (const std::exception &) {
        result.warnings.push_back("Failed to parse link libraries");
    }
}

/// Check for skipped constructs and emit appropriate warnings.
static void check_skipped_constructs(const std::string &content, cmake_parse_result &result) {
    try {
        // Check for if/else blocks
        std::regex re_if(R"(\bif\s*\()", std::regex::icase);
        std::smatch m;
        if (std::regex_search(content, m, re_if)) {
            result.warnings.push_back("Conditional blocks (if/else) were skipped");
        }

        // Check for function definitions
        std::regex re_func(R"(\bfunction\s*\(\s*(\w+))", std::regex::icase);
        auto it = std::sregex_iterator(content.begin(), content.end(), re_func);
        auto end_it = std::sregex_iterator();
        for (; it != end_it; ++it) {
            result.warnings.push_back(
                "Custom CMake function '" + (*it)[1].str() + "' was skipped");
        }

        // Check for macro definitions
        std::regex re_macro(R"(\bmacro\s*\(\s*(\w+))", std::regex::icase);
        auto it2 = std::sregex_iterator(content.begin(), content.end(), re_macro);
        for (; it2 != end_it; ++it2) {
            result.warnings.push_back(
                "Custom CMake macro '" + (*it2)[1].str() + "' was skipped");
        }

        // Check for include() of external modules
        std::regex re_include(R"(\binclude\s*\(\s*([^)]+)\))", std::regex::icase);
        auto it3 = std::sregex_iterator(content.begin(), content.end(), re_include);
        for (; it3 != end_it; ++it3) {
            std::string mod = trim((*it3)[1].str());
            // Skip known CMake module names that are common/harmless
            if (mod == "FetchContent" || mod == "ExternalProject" ||
                mod == "GNUInstallDirs" || mod == "CMakePackageConfigHelpers" ||
                mod == "CPack") {
                continue;
            }
            result.warnings.push_back("include(" + mod + ") was skipped");
        }

    } catch (const std::exception &) {
        // Non-fatal — just skip warning generation
    }
}

// ============================================================================
// Public API
// ============================================================================

cmake_parse_result parse_cmake_file(const std::string &cmake_path) {
    cmake_parse_result result;

    // Verify the file exists
    if (!std::filesystem::exists(cmake_path)) {
        result.warnings.push_back("File not found: " + cmake_path);
        return result;
    }

    // Get the directory containing the CMakeLists.txt
    std::string cmake_dir = std::filesystem::path(cmake_path).parent_path().string();
    if (cmake_dir.empty()) {
        cmake_dir = ".";
    }

    // Pre-process: join continuation lines and strip comments
    std::vector<std::string> lines = preprocess_cmake(cmake_path);
    std::string content = join_lines(lines);

    // Run each extractor independently
    extract_project(content, result);
    extract_cpp_standard(content, result);
    extract_c_standard(content, result);
    extract_binary_type(content, result);
    extract_source_dirs(content, result, cmake_dir);
    extract_include_dirs(content, result);
    extract_dependencies(content, result);
    extract_compile_definitions(content, result);
    extract_compile_options(content, result);
    extract_link_libraries(content, result);
    check_skipped_constructs(content, result);

    return result;
}

} // namespace cforge
