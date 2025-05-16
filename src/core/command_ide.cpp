/**
 * @file command_ide.cpp
 * @brief Implementation of the 'ide' command to generate IDE project files
 */

#include "cforge/log.hpp"
#include "core/commands.hpp"
#include "core/constants.h"
#include "core/process_utils.hpp"
#include "core/toml_reader.hpp"
#include "core/workspace.hpp"

#include <filesystem>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <map>
#include <fstream>
#include <set>

using namespace cforge;

/**
 * @brief Generate Visual Studio project files using CMake
 *
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_vs_project(const std::filesystem::path &project_dir,
                                const std::filesystem::path &build_dir,
                                bool verbose) {
  logger::print_status("Generating Visual Studio project files...");

  // Create build directory if it doesn't exist
  if (!std::filesystem::exists(build_dir)) {
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create build directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // Run CMake to generate Visual Studio project files
  std::vector<std::string> cmake_args = {
      "-B", build_dir.string(),      "-S", project_dir.string(),
      "-G", "Visual Studio 17 2022", "-A", "x64"};

  bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);

  if (success) {
    logger::print_success("Visual Studio project files generated successfully");
    logger::print_status("Open " + build_dir.string() +
                         "/*.sln to start working with the project");
  } else {
    logger::print_error("Failed to generate Visual Studio project files");
  }

  return success;
}

/**
 * @brief Generate CodeBlocks project files using CMake
 *
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool
generate_codeblocks_project(const std::filesystem::path &project_dir,
                            const std::filesystem::path &build_dir,
                            bool verbose) {
  logger::print_status("Generating CodeBlocks project files...");

  // Create build directory if it doesn't exist
  if (!std::filesystem::exists(build_dir)) {
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create build directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // Run CMake to generate CodeBlocks project files
  std::vector<std::string> cmake_args = {"-B", build_dir.string(),
                                         "-S", project_dir.string(),
                                         "-G", "CodeBlocks - Ninja"};

  bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);

  if (success) {
    logger::print_success("CodeBlocks project files generated successfully");
    logger::print_status("Open " + build_dir.string() +
                         "/*.cbp to start working with the project");
  } else {
    logger::print_error("Failed to generate CodeBlocks project files");
  }

  return success;
}

/**
 * @brief Generate Xcode project files using CMake
 *
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_xcode_project(const std::filesystem::path &project_dir,
                                   const std::filesystem::path &build_dir,
                                   bool verbose) {
  logger::print_status("Generating Xcode project files...");

  // Create build directory if it doesn't exist
  if (!std::filesystem::exists(build_dir)) {
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create build directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // Run CMake to generate Xcode project files
  std::vector<std::string> cmake_args = {
      "-B", build_dir.string(), "-S", project_dir.string(), "-G", "Xcode"};

  bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);

  if (success) {
    logger::print_success("Xcode project files generated successfully");
    logger::print_status("Open " + build_dir.string() +
                         "/*.xcodeproj to start working with the project");
  } else {
    logger::print_error("Failed to generate Xcode project files");
  }

  return success;
}

/**
 * @brief Generate CLion project files using CMake
 *
 * @param project_dir Project directory path
 * @param build_dir Build directory path
 * @param verbose Verbose output flag
 * @return bool Success flag
 */
static bool generate_clion_project(const std::filesystem::path &project_dir,
                                   const std::filesystem::path &build_dir,
                                   bool verbose) {
  logger::print_status("Setting up project for CLion...");

  // Create build directory if it doesn't exist
  if (!std::filesystem::exists(build_dir)) {
    try {
      std::filesystem::create_directories(build_dir);
    } catch (const std::exception &ex) {
      logger::print_error("Failed to create build directory: " +
                          std::string(ex.what()));
      return false;
    }
  }

  // CLion uses CMake directly, just need to run CMake once to generate cache
  std::vector<std::string> cmake_args = {"-B", build_dir.string(), "-S",
                                         project_dir.string()};

  bool success = execute_tool("cmake", cmake_args, "", "CMake", verbose);

  if (success) {
    logger::print_success("Project set up for CLion successfully");
    logger::print_status("Open the project root directory in CLion");
  } else {
    logger::print_error("Failed to set up project for CLion");
  }

  return success;
}

// Generate a random GUID in the form XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
static std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);
    auto r = [&]() { return dist(gen); };
    std::ostringstream oss;
    oss << std::uppercase << std::hex
        << std::setw(8) << std::setfill('0') << r() << "-"
        << std::setw(4) << ((r() >> 16) & 0xFFFF) << "-"
        << std::setw(4) << (((r() >> 16) & 0x0FFF) | 0x4000) << "-"
        << std::setw(4) << (((r() >> 16) & 0x3FFF) | 0x8000) << "-"
        << std::setw(12)
           << ((((uint64_t)r() << 32) | r()) & 0x0000FFFFFFFFFFFFULL);
    return oss.str();
}

// Write a minimal .vcxproj for a project from its TOML configuration
static bool write_vcxproj(const std::filesystem::path &proj_dir,
                          const toml_reader &cfg,
                          const std::filesystem::path &out_dir,
                          const std::string &proj_guid,
                          bool verbose) {
    std::string name = cfg.get_string("project.name", proj_dir.filename().string());
    std::filesystem::path rel_out = out_dir / (name + ".vcxproj");
    if (!std::filesystem::exists(out_dir)) {
        std::filesystem::create_directories(out_dir);
    }
    std::ofstream f(rel_out);
    if (!f) {
        logger::print_error("Failed to create vcxproj: " + rel_out.string());
        return false;
    }
    // Detect current platform for platform-specific settings
    std::string cforge_platform;
#ifdef _WIN32
    cforge_platform = "windows";
#elif defined(__APPLE__)
    cforge_platform = "macos";
#else
    cforge_platform = "linux";
#endif

    std::string binary_type = cfg.get_string("project.binary_type", "executable");
    std::string configurationType;
    if (binary_type == "executable") configurationType = "Application";
    else if (binary_type == "shared_lib") configurationType = "DynamicLibrary";
    else if (binary_type == "static_lib") configurationType = "StaticLibrary";
    else if (binary_type == "header_only") configurationType = "Utility";
    std::string cpp_standard = cfg.get_string("project.cpp_standard", "17");

    f << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      << "<Project DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
      << "  <ItemGroup Label=\"ProjectConfigurations\">\n"
      << "    <ProjectConfiguration Include=\"Debug|x64\">\n"
      << "      <Configuration>Debug</Configuration>\n"
      << "      <Platform>x64</Platform>\n"
      << "    </ProjectConfiguration>\n"
      << "    <ProjectConfiguration Include=\"Release|x64\">\n"
      << "      <Configuration>Release</Configuration>\n"
      << "      <Platform>x64</Platform>\n"
      << "    </ProjectConfiguration>\n"
      << "  </ItemGroup>\n"
      << "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n"
      << "  <PropertyGroup Label=\"Globals\">\n"
      << "    <ProjectGuid>{" << proj_guid << "}</ProjectGuid>\n"
      << "    <RootNamespace>" << name << "</RootNamespace>\n"
      << "    <Keyword>Win32Proj</Keyword>\n"
      << "  </PropertyGroup>\n"
      << "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\" Label=\"Configuration\">\n"
      << "    <ConfigurationType>" << configurationType << "</ConfigurationType>\n"
      << "    <UseDebugLibraries>true</UseDebugLibraries>\n"
      << "    <PlatformToolset>v143</PlatformToolset>\n"
      << "    <LanguageStandard>stdcpp" << cpp_standard << "</LanguageStandard>\n"
      << "  </PropertyGroup>\n"
      << "  <PropertyGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\" Label=\"Configuration\">\n"
      << "    <ConfigurationType>" << configurationType << "</ConfigurationType>\n"
      << "    <UseDebugLibraries>false</UseDebugLibraries>\n"
      << "    <PlatformToolset>v143</PlatformToolset>\n"
      << "    <LanguageStandard>stdcpp" << cpp_standard << "</LanguageStandard>\n"
      << "  </PropertyGroup>\n"
      << "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n"
      << "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">\n"
      << "    <ClCompile>\n"
      << "      <CompileAs>CompileAsCpp</CompileAs>\n";
    {
        // Build preprocessor definitions
        std::vector<std::string> defs = cfg.get_string_array("build.defines");
        defs.push_back("_DEBUG");
        auto debug_defs = cfg.get_string_array("build.config.debug.defines");
        defs.insert(defs.end(), debug_defs.begin(), debug_defs.end());
        // Platform-specific defines
        auto plat_defs = cfg.get_string_array("platform." + cforge_platform + ".defines");
        defs.insert(defs.end(), plat_defs.begin(), plat_defs.end());
        // Join definitions
        std::ostringstream defoss;
        for (auto &d : defs) defoss << d << ";";
        defoss << "%(PreprocessorDefinitions)";
        f << "      <PreprocessorDefinitions>" << defoss.str() << "</PreprocessorDefinitions>\n";
    }
    f << "      <WarningLevel>Level3</WarningLevel>\n"
        << "      <Optimization>Disabled</Optimization>\n";
    {
        // Include directories
        auto incs = cfg.get_string_array("build.include_dirs");
        if (incs.empty()) incs = {"include"};
        // Include directories from workspace project dependencies
        if (cfg.has_key("dependencies")) {
            auto deps = cfg.get_table_keys("dependencies");
            deps.erase(std::remove_if(deps.begin(), deps.end(),
                [&](const std::string &k) {
                    if (k == cfg.get_string("dependencies.directory", "")) return true;
                    if (k == "git" || k == "vcpkg") return true;
                    if (cfg.has_key("dependencies." + k + ".url")) return true;
                    return false;
                }), deps.end());
            for (const auto &dep : deps) {
                bool inc = cfg.get_bool("dependencies." + dep + ".include", true);
                if (!inc) continue;
                incs.push_back(std::string("../") + dep + "/include");
            }
        }
        std::ostringstream incoss;
        for (auto &inc : incs) incoss << (proj_dir / inc).string() << ";";
        incoss << "%(AdditionalIncludeDirectories)";
        f << "      <AdditionalIncludeDirectories>" << incoss.str() << "</AdditionalIncludeDirectories>\n";
        f << "      <LanguageStandard>stdcpp" << cpp_standard << "</LanguageStandard>\n";
    }
    f << "    </ClCompile>\n"
        << "    <Link>\n"
        << "      <SubSystem>Console</SubSystem>\n"
        << "      <GenerateDebugInformation>true</GenerateDebugInformation>\n"
        << "    </Link>\n"
        << "  </ItemDefinitionGroup>\n"
        << "  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">\n"
        << "    <ClCompile>\n";
    {
        std::vector<std::string> defs = cfg.get_string_array("build.defines");
        defs.push_back("NDEBUG");
        auto rel_defs = cfg.get_string_array("build.config.release.defines");
        defs.insert(defs.end(), rel_defs.begin(), rel_defs.end());
        auto plat_defs = cfg.get_string_array("platform." + cforge_platform + ".defines");
        defs.insert(defs.end(), plat_defs.begin(), plat_defs.end());
        std::ostringstream defoss;
        for (auto &d : defs) defoss << d << ";";
        defoss << "%(PreprocessorDefinitions)";
        f << "      <PreprocessorDefinitions>" << defoss.str() << "</PreprocessorDefinitions>\n";
    }
    f << "      <WarningLevel>Level3</WarningLevel>\n"
        << "      <Optimization>MaxSpeed</Optimization>\n";
    {
        auto incs = cfg.get_string_array("build.include_dirs");
        if (incs.empty()) incs = {"include"};
        // Include directories from workspace project dependencies
        if (cfg.has_key("dependencies")) {
            auto deps = cfg.get_table_keys("dependencies");
            deps.erase(std::remove_if(deps.begin(), deps.end(),
                [&](const std::string &k) {
                    if (k == cfg.get_string("dependencies.directory", "")) return true;
                    if (k == "git" || k == "vcpkg") return true;
                    if (cfg.has_key("dependencies." + k + ".url")) return true;
                    return false;
                }), deps.end());
            for (const auto &dep : deps) {
                bool inc = cfg.get_bool("dependencies." + dep + ".include", true);
                if (!inc) continue;
                incs.push_back(std::string("../") + dep + "/include");
            }
        }
        std::ostringstream incoss;
        for (auto &inc : incs) incoss << (proj_dir / inc).string() << ";";
        incoss << "%(AdditionalIncludeDirectories)";
        f << "      <AdditionalIncludeDirectories>" << incoss.str() << "</AdditionalIncludeDirectories>\n";
        f << "      <LanguageStandard>stdcpp" << cpp_standard << "</LanguageStandard>\n";
    }
    f << "    </ClCompile>\n"
        << "    <Link>\n"
        << "      <SubSystem>Console</SubSystem>\n"
        << "      <GenerateDebugInformation>false</GenerateDebugInformation>\n"
        << "    </Link>\n"
        << "  </ItemDefinitionGroup>\n"
        << "  <ItemGroup>\n";
    auto srcs = cfg.get_string_array("build.source_dirs");
    for (const auto &sd : srcs) {
        for (auto &p : std::filesystem::recursive_directory_iterator(proj_dir / sd)) {
            if (p.path().extension() == ".cpp" || p.path().extension() == ".c") {
                f << "    <ClCompile Include=\"" << std::filesystem::relative(p.path(), out_dir).string() << "\" />\n";
            }
        }
    }
    f << "  </ItemGroup>\n";
    // Include header files so .h/.hpp show up in Solution Explorer
    auto header_dirs = cfg.get_string_array("build.include_dirs");
    if (header_dirs.empty()) header_dirs = {"include"};
    f << "  <ItemGroup>\n";
    for (const auto &hd : header_dirs) {
        for (auto &p : std::filesystem::recursive_directory_iterator(proj_dir / hd)) {
            if (p.path().extension() == ".h" || p.path().extension() == ".hpp") {
                f << "    <ClInclude Include=\"" << std::filesystem::relative(p.path(), out_dir).string() << "\" />\n";
            }
        }
    }
    f << "  </ItemGroup>\n"
      << "  <ImportGroup Label=\"ExtensionTargets\" />\n"
      << "  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n"
      << "</Project>\n";
    f.close();
    // Generate .vcxproj.filters for directory-based filters
    {
        std::filesystem::path filters_path = out_dir / (name + ".vcxproj.filters");
        std::ofstream fl(filters_path);
        if (!fl) {
            logger::print_error("Failed to create filters: " + filters_path.string());
            return false;
        }
        fl << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
           << "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
        // Collect files by directory filter
        std::map<std::string, std::vector<std::string>> files;
        // Source files
        for (auto &sd : cfg.get_string_array("build.source_dirs")) {
            for (auto &p : std::filesystem::recursive_directory_iterator(proj_dir / sd)) {
                if (p.path().extension() == ".cpp" || p.path().extension() == ".c") {
                    std::string rel = std::filesystem::relative(p.path(), out_dir).string();
                    std::string filter = std::filesystem::relative(p.path().parent_path(), proj_dir).string();
                    files[filter].push_back(rel);
                }
            }
        }
        // Header files
        auto header_dirs = cfg.get_string_array("build.include_dirs");
        if (header_dirs.empty()) header_dirs = {"include"};
        for (auto &hd : header_dirs) {
            for (auto &p : std::filesystem::recursive_directory_iterator(proj_dir / hd)) {
                if (p.path().extension() == ".h" || p.path().extension() == ".hpp") {
                    std::string rel = std::filesystem::relative(p.path(), out_dir).string();
                    std::string filter = std::filesystem::relative(p.path().parent_path(), proj_dir).string();
                    files[filter].push_back(rel);
                }
            }
        }
        // Write filters
        fl << "  <ItemGroup>\n";
        for (auto &entry : files) {
            std::string nameFilter = entry.first.empty() ? "." : entry.first;
            fl << "    <Filter Include=\"" << nameFilter << "\">\n"
               << "      <UniqueIdentifier>{" << generate_uuid() << "}</UniqueIdentifier>\n"
               << "    </Filter>\n";
        }
        fl << "  </ItemGroup>\n";
        // Write ClCompile entries
        fl << "  <ItemGroup>\n";
        for (auto &entry : files) {
            for (auto &file : entry.second) {
                if (std::filesystem::path(file).extension() == ".cpp" || std::filesystem::path(file).extension() == ".c") {
                    fl << "    <ClCompile Include=\"" << file << "\">\n"
                       << "      <Filter>" << (entry.first.empty() ? "." : entry.first) << "</Filter>\n"
                       << "    </ClCompile>\n";
                }
            }
        }
        fl << "  </ItemGroup>\n";
        // Write ClInclude entries
        fl << "  <ItemGroup>\n";
        for (auto &entry : files) {
            for (auto &file : entry.second) {
                if (std::filesystem::path(file).extension() == ".h" || std::filesystem::path(file).extension() == ".hpp") {
                    fl << "    <ClInclude Include=\"" << file << "\">\n"
                       << "      <Filter>" << (entry.first.empty() ? "." : entry.first) << "</Filter>\n"
                       << "    </ClInclude>\n";
                }
            }
        }
        fl << "  </ItemGroup>\n"
           << "</Project>\n";
    }
    return true;
}

// Write a .sln that references all generated projects
static bool write_sln(const std::filesystem::path &workspace_dir,
                      const cforge::workspace &ws,
                      const std::map<std::string, std::string> &project_guids,
                      const std::filesystem::path &out_dir,
                      bool verbose) {
    std::filesystem::path sln_path = out_dir / (ws.get_name() + ".sln");
    std::ofstream sln(sln_path);
    if (!sln) {
        logger::print_error("Failed to create solution: " + sln_path.string());
        return false;
    }
    sln << "Microsoft Visual Studio Solution File, Format Version 12.00\n"
        << "# Visual Studio Version 17\n"
        << "VisualStudioVersion = 17.0.0\n"
        << "MinimumVisualStudioVersion = 10.0.40219.1\n";
    for (auto &proj : ws.get_projects()) {
        const std::string &name = proj.name;
        std::string guid = project_guids.at(name);
        auto proj_file = workspace_dir / proj.path / (name + ".vcxproj");
        auto rel_proj = std::filesystem::relative(proj_file, workspace_dir);
        sln << "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" << name
            << "\", \"" << rel_proj.string() << "\", \"{" << guid << "}\"\n"
            << "EndProject\n";
    }
    // Set the solution startup project based on workspace.toml's startup flag
    {
        auto sp = ws.get_startup_project();
        if (!sp.name.empty()) {
            sln << "Global\n";
            sln << "    GlobalSection(ExtensibilityGlobals) = postSolution\n";
            sln << "        StartupProject = " << sp.name << "\n";
            sln << "    EndGlobalSection\n";
            sln << "EndGlobal\n";
        }
    }
    return true;
}

// Generate VS solution and projects directly from workspace TOML
static bool generate_vs_workspace_solution(const std::filesystem::path &workspace_dir,
                                           bool verbose) {
    toml_reader ws_cfg;
    auto ws_file = workspace_dir / WORKSPACE_FILE;
    if (!ws_cfg.load(ws_file.string())) {
        logger::print_error("Failed to load " + ws_file.string());
        return false;
    }
    cforge::workspace ws;
    if (!ws.load(workspace_dir)) {
        logger::print_error("Failed to parse workspace at " + workspace_dir.string());
        return false;
    }
    std::filesystem::path out_dir = workspace_dir;
    std::map<std::string,std::string> project_guids;
    for (auto &proj : ws.get_projects()) {
        std::string guid = generate_uuid();
        project_guids[proj.name] = guid;
        toml_reader proj_cfg;
        auto proj_toml = workspace_dir / proj.path / CFORGE_FILE;
        if (!proj_cfg.load(proj_toml.string())) {
            logger::print_error("Failed to load " + proj_toml.string());
            return false;
        }
        // Write each project into its own directory
        std::filesystem::path proj_out_dir = workspace_dir / proj.path;
        if (!write_vcxproj(workspace_dir / proj.path, proj_cfg, proj_out_dir, guid, verbose)) {
            return false;
        }
    }
    if (!write_sln(workspace_dir, ws, project_guids, out_dir, verbose)) {
        return false;
    }
    // Add inter-project references for workspace dependencies
    for (auto &proj : ws.get_projects()) {
        // Skip if no dependencies
        if (proj.dependencies.empty()) continue;
        // Path to this project's vcxproj
        std::filesystem::path proj_file = workspace_dir / proj.path / (proj.name + ".vcxproj");
        // Read file into lines
        std::vector<std::string> lines;
        std::ifstream in(proj_file);
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        in.close();
        // Find insertion point
        int insert_idx = -1;
        for (int i = 0; i < (int)lines.size(); ++i) {
            if (lines[i].find("<Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\"") != std::string::npos) {
                insert_idx = i;
                break;
            }
        }
        if (insert_idx < 0) continue;
        // Build reference block
        std::vector<std::string> ref_block;
        ref_block.push_back("  <ItemGroup>");
        for (auto &dep : proj.dependencies) {
            auto it = project_guids.find(dep);
            if (it == project_guids.end()) continue;
            // Reference to dependent project in sibling folder
            std::filesystem::path ref_path = std::filesystem::path("..") / dep / (dep + ".vcxproj");
            std::string dep_proj = ref_path.generic_string();
            ref_block.push_back("    <ProjectReference Include=\"" + dep_proj + "\">");
            ref_block.push_back("      <Project>{" + it->second + "}</Project>");
            ref_block.push_back("      <ReferenceOutputAssembly>true</ReferenceOutputAssembly>");
            ref_block.push_back("      <LinkLibraryDependencies>true</LinkLibraryDependencies>");
            ref_block.push_back("      <UseLibraryDependencyInputs>false</UseLibraryDependencyInputs>");
            ref_block.push_back("    </ProjectReference>");
        }
        ref_block.push_back("  </ItemGroup>");
        // Insert block before MSBuild targets import
        lines.insert(lines.begin() + insert_idx, ref_block.begin(), ref_block.end());
        // Write back
        std::ofstream out(proj_file);
        for (auto &l : lines) out << l << '\n';
    }
    logger::print_success("Visual Studio solution and project files generated successfully");
    logger::print_status("Open " + (workspace_dir / (ws.get_name() + ".sln")).string() + " to start working");
    return true;
}

// Write a single-project .sln file for a direct project
static bool write_sln_single(const std::filesystem::path &out_dir,
                              const std::string &name,
                              const std::string &guid,
                              bool verbose) {
    std::filesystem::path sln_path = out_dir / (name + ".sln");
    std::ofstream sln(sln_path);
    if (!sln) {
        logger::print_error("Failed to create solution: " + sln_path.string());
        return false;
    }
    sln << "Microsoft Visual Studio Solution File, Format Version 12.00\n"
        << "# Visual Studio Version 17\n"
        << "VisualStudioVersion = 17.0.0\n"
        << "MinimumVisualStudioVersion = 10.0.40219.1\n";
    auto proj_file = name + ".vcxproj";
    sln << "Project(\"{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}\") = \"" << name
        << "\", \"" << proj_file << "\", \"{" << guid << "}\"\n"
        << "EndProject\n";
    return true;
}

// Generate VS project and solution for a single project without CMake
static bool generate_vs_project_direct(const std::filesystem::path &project_dir,
                                       const toml_reader &cfg,
                                       bool verbose) {
    std::filesystem::path out_dir = project_dir;
    std::string name = cfg.get_string("project.name", project_dir.filename().string());
    std::string guid = generate_uuid();
    if (!write_vcxproj(project_dir, cfg, out_dir, guid, verbose)) {
        return false;
    }
    if (!write_sln_single(out_dir, name, guid, verbose)) {
        return false;
    }
    logger::print_success("Visual Studio solution and project files generated successfully");
    logger::print_status("Open " + (out_dir / (name + ".sln")).string() + " to start working");
    return true;
}

/**
 * @brief Handle the 'ide' command
 *
 * @param ctx Context containing parsed arguments
 * @return cforge_int_t Exit code (0 for success)
 */
cforge_int_t cforge_cmd_ide(const cforge_context_t *ctx) {
  // Determine project directory
  std::filesystem::path project_dir = ctx->working_dir;

  // Determine verbosity
  bool verbose = logger::get_verbosity() == log_verbosity::VERBOSITY_VERBOSE;
  // Get IDE type from arguments
  std::string ide_type;
  if (ctx->args.args && ctx->args.args[0]) {
    ide_type = ctx->args.args[0];
  }
  // If IDE type is not specified, detect based on platform
  if (ide_type.empty()) {
#ifdef _WIN32
    ide_type = "vs";
#elif defined(__APPLE__)
    ide_type = "xcode";
#else
    ide_type = "codeblocks";
#endif
  }
  // Workspace mode: bypass CMake and generate VS solution
  if (ctx->is_workspace) {
    if (ide_type != "" && ide_type != "vs" && ide_type != "visual-studio") {
      logger::print_error("Workspace IDE only supports Visual Studio (vs)");
      return 1;
    }
    return generate_vs_workspace_solution(project_dir, verbose) ? 0 : 1;
  }

  // Project mode: verify cforge.toml exists
  // Check if cforge.toml exists
  std::filesystem::path config_path = project_dir / CFORGE_FILE;
  if (!std::filesystem::exists(config_path)) {
    logger::print_error("Not a valid cforge project (missing " +
                        std::string(CFORGE_FILE) + ")");
    return 1;
  }

  // Load project configuration
  toml_reader project_config;
  if (!project_config.load(config_path.string())) {
    logger::print_error("Failed to parse " + std::string(CFORGE_FILE));
    return 1;
  }

  // Get build directory from configuration or use default
  std::string build_dir_name =
      project_config.get_string("build.build_dir", "build");
  std::filesystem::path build_dir = project_dir / build_dir_name / "ide";

  // Generate project files based on IDE type
  bool success = false;

  if (ide_type == "vs" || ide_type == "visual-studio") {
    success = generate_vs_project_direct(project_dir, project_config, verbose);
  } else if (ide_type == "cb" || ide_type == "codeblocks") {
    success = generate_codeblocks_project(project_dir, build_dir, verbose);
  } else if (ide_type == "xcode") {
    success = generate_xcode_project(project_dir, build_dir, verbose);
  } else if (ide_type == "clion") {
    success = generate_clion_project(project_dir, build_dir, verbose);
  } else {
    logger::print_error("Unknown IDE type: " + ide_type);
    logger::print_status("Available IDE types: vs (Visual Studio), cb "
                         "(CodeBlocks), xcode, clion");
    return 1;
  }

  return success ? 0 : 1;
}