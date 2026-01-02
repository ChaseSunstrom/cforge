using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using EnvDTE;
using EnvDTE80;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    /// <summary>
    /// Generates .vcxproj files from cforge.toml configuration.
    /// Files are stored in a temp directory to avoid polluting source control.
    /// </summary>
    public static class VcxprojGenerator
    {
        private static bool _isOpeningSolution = false;
        private static string? _currentGeneratedSlnPath;

        /// <summary>
        /// Gets the temp directory for storing generated VS project files.
        /// Uses %LOCALAPPDATA%\cforge-vs\projects\{hash}\ to persist between sessions.
        /// </summary>
        public static string GetTempProjectDir(string sourceDir)
        {
            // Create a hash of the source directory for uniqueness
            string hash = ComputeHash(sourceDir);
            string tempBase = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "cforge-vs",
                "projects",
                hash
            );

            // Ensure directory exists
            Directory.CreateDirectory(tempBase);

            return tempBase;
        }

        /// <summary>
        /// Gets the path to the generated solution file for a project directory.
        /// </summary>
        public static string? GetGeneratedSolutionPath(string sourceDir)
        {
            string tempDir = GetTempProjectDir(sourceDir);

            // Look for any .sln file
            var slnFiles = Directory.GetFiles(tempDir, "*.sln");
            return slnFiles.FirstOrDefault();
        }

        public static async Task InitializeAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var pane = await CforgeRunner.GetOutputPaneAsync();
            await pane.WriteLineAsync("[cforge] VcxprojGenerator initialized");

            // Subscribe to folder open events
            VS.Events.SolutionEvents.OnAfterOpenFolder += OnFolderOpened;

            // Check if a folder is already open
            await CheckCurrentFolderAsync();
        }

        private static async Task CheckCurrentFolderAsync()
        {
            var pane = await CforgeRunner.GetOutputPaneAsync();

            var solution = await VS.Solutions.GetCurrentSolutionAsync();
            await pane.WriteLineAsync($"[cforge] CheckCurrentFolder: solution={solution?.FullPath ?? "null"}");

            if (solution?.FullPath != null)
            {
                string? folder = Path.GetDirectoryName(solution.FullPath);
                await pane.WriteLineAsync($"[cforge] Folder: {folder}");

                // Don't regenerate if we're already in a generated solution
                if (folder != null && folder.Contains("cforge-vs"))
                {
                    await pane.WriteLineAsync("[cforge] Already in generated solution, skipping");
                    return;
                }

                if (!string.IsNullOrEmpty(folder) && CforgeRunner.IsCforgeProject(folder))
                {
                    await pane.WriteLineAsync($"[cforge] Found cforge project at: {folder}");
                    await GenerateAndOpenAsync(folder);
                }
                else
                {
                    await pane.WriteLineAsync($"[cforge] Not a cforge project: {folder}");
                }
            }
        }

        private static void OnFolderOpened(string? folder)
        {
            if (!string.IsNullOrEmpty(folder) && CforgeRunner.IsCforgeProject(folder))
            {
                _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                {
                    var pane = await CforgeRunner.GetOutputPaneAsync();
                    await pane.WriteLineAsync($"[cforge] OnFolderOpened: {folder}");
                    await GenerateAndOpenAsync(folder);
                });
            }
        }

        /// <summary>
        /// Generates VS project files and opens the solution.
        /// </summary>
        public static async Task GenerateAndOpenAsync(string sourceDir)
        {
            string? slnPath = await GenerateProjectFilesAsync(sourceDir);
            if (!string.IsNullOrEmpty(slnPath))
            {
                await OpenSolutionAsync(slnPath, sourceDir);
            }
        }

        /// <summary>
        /// Generates VS project files from cforge.toml or cforge.workspace.toml.
        /// Returns the path to the generated solution file.
        /// </summary>
        public static async Task<string?> GenerateProjectFilesAsync(string sourceDir)
        {
            var workspace = CforgeTomlParser.ParseWorkspace(sourceDir);
            var project = CforgeTomlParser.ParseProject(sourceDir);

            if (workspace != null)
            {
                return await GenerateWorkspaceAsync(sourceDir, workspace);
            }
            else if (project != null)
            {
                return await GenerateSingleProjectAsync(sourceDir, project);
            }

            return null;
        }

        private static async Task<string> GenerateSingleProjectAsync(string sourceDir, CforgeProject project)
        {
            string tempDir = GetTempProjectDir(sourceDir);
            string vcxprojPath = Path.Combine(tempDir, $"{project.Name}.vcxproj");
            string filtersPath = Path.Combine(tempDir, $"{project.Name}.vcxproj.filters");
            string slnPath = Path.Combine(tempDir, $"{project.Name}.sln");

            // Get source files (paths relative to source dir)
            var sourceFiles = GetSourceFiles(sourceDir, project);
            var headerFiles = GetHeaderFiles(sourceDir, project);

            // Generate vcxproj with absolute paths to source
            string vcxprojContent = GenerateVcxproj(project, sourceFiles, headerFiles, sourceDir, tempDir);
            File.WriteAllText(vcxprojPath, vcxprojContent);

            // Generate filters
            string filtersContent = GenerateFilters(sourceFiles, headerFiles, sourceDir);
            File.WriteAllText(filtersPath, filtersContent);

            // Generate solution file
            string projectGuid = GenerateGuid(project.Name);
            var projects = new List<(string name, string path, string guid, CforgeProject project)>
            {
                (project.Name, $"{project.Name}.vcxproj", projectGuid, project)
            };
            string slnContent = GenerateSolution(project.Name, projects);
            File.WriteAllText(slnPath, slnContent);

            // Write a marker file with the source directory path
            File.WriteAllText(Path.Combine(tempDir, ".cforge-source"), sourceDir);

            var pane = await CforgeRunner.GetOutputPaneAsync();
            await pane.WriteLineAsync($"[cforge] Generated VS project files for '{project.Name}'");
            await pane.WriteLineAsync($"  Location: {tempDir}");
            await pane.WriteLineAsync($"  Sources: {sourceFiles.Count}, Headers: {headerFiles.Count}");

            _currentGeneratedSlnPath = slnPath;
            return slnPath;
        }

        private static async Task<string> GenerateWorkspaceAsync(string workspaceDir, CforgeWorkspace workspace)
        {
            string tempDir = GetTempProjectDir(workspaceDir);
            string slnPath = Path.Combine(tempDir, $"{workspace.Name}.sln");

            var projects = new List<(string name, string path, string guid, CforgeProject project)>();
            var pane = await CforgeRunner.GetOutputPaneAsync();

            await pane.WriteLineAsync($"[cforge] Generating workspace '{workspace.Name}' with {workspace.Members.Count} member(s)");

            // Collect all member include paths for cross-project references
            var memberIncludePaths = new List<string>();
            foreach (var member in workspace.Members)
            {
                string memberSourceDir = Path.Combine(workspaceDir, member);
                memberIncludePaths.Add(Path.Combine(memberSourceDir, "include"));
                memberIncludePaths.Add(Path.Combine(memberSourceDir, "src"));
            }

            // Generate each member project
            foreach (var member in workspace.Members)
            {
                string memberSourceDir = Path.Combine(workspaceDir, member);
                var memberProject = CforgeTomlParser.ParseProject(memberSourceDir);

                if (memberProject != null)
                {
                    // Create subdirectory for member project in temp dir
                    string memberTempDir = Path.Combine(tempDir, member);
                    Directory.CreateDirectory(memberTempDir);

                    string vcxprojPath = Path.Combine(memberTempDir, $"{memberProject.Name}.vcxproj");
                    string guid = GenerateGuid(memberProject.Name);

                    var sourceFiles = GetSourceFiles(memberSourceDir, memberProject);
                    var headerFiles = GetHeaderFiles(memberSourceDir, memberProject);

                    // Generate vcxproj with absolute paths, including workspace-level paths
                    string vcxprojContent = GenerateVcxprojForWorkspaceMember(
                        memberProject, sourceFiles, headerFiles,
                        memberSourceDir, memberTempDir, workspaceDir, memberIncludePaths);
                    File.WriteAllText(vcxprojPath, vcxprojContent);

                    string filtersContent = GenerateFilters(sourceFiles, headerFiles, memberSourceDir);
                    File.WriteAllText(Path.Combine(memberTempDir, $"{memberProject.Name}.vcxproj.filters"), filtersContent);

                    // Path in solution is relative to solution file
                    string relativeVcxprojPath = Path.Combine(member, $"{memberProject.Name}.vcxproj");
                    projects.Add((memberProject.Name, relativeVcxprojPath, guid, memberProject));

                    await pane.WriteLineAsync($"  - {member}/{memberProject.Name}: {sourceFiles.Count} sources, {headerFiles.Count} headers");
                }
                else
                {
                    await pane.WriteLineAsync($"  - {member}: No cforge.toml found, skipping");
                }
            }

            // Generate solution file
            string slnContent = GenerateSolution(workspace.Name, projects);
            File.WriteAllText(slnPath, slnContent);

            // Write a marker file with the source directory path
            File.WriteAllText(Path.Combine(tempDir, ".cforge-source"), workspaceDir);

            await pane.WriteLineAsync($"[cforge] Generated {workspace.Name}.sln with {projects.Count} project(s)");
            await pane.WriteLineAsync($"  Solution: {slnPath}");
            await pane.WriteLineAsync($"  Temp dir: {tempDir}");
            foreach (var (name, path, _, _) in projects)
            {
                string fullProjPath = Path.Combine(tempDir, path);
                bool exists = File.Exists(fullProjPath);
                await pane.WriteLineAsync($"  Project '{name}': {path} (exists: {exists})");
            }

            _currentGeneratedSlnPath = slnPath;
            return slnPath;
        }

        private static string GenerateVcxproj(CforgeProject project, List<string> sourceFiles, List<string> headerFiles, string sourceDir, string tempDir)
        {
            string projectGuid = GenerateGuid(project.Name);
            string configType = project.Type switch
            {
                "static_library" => "StaticLibrary",
                "shared_library" => "DynamicLibrary",
                "header_only" => "StaticLibrary",
                _ => "Application"
            };
            string outputExt = project.Type switch
            {
                "static_library" => ".lib",
                "shared_library" => ".dll",
                _ => ".exe"
            };

            // Use absolute paths for source directory
            string sourceDirAbs = Path.GetFullPath(sourceDir);

            var sb = new StringBuilder();
            sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
            sb.AppendLine("<Project ToolsVersion=\"17.0\" DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");
            sb.AppendLine();

            // Project Configurations
            sb.AppendLine("  <ItemGroup Label=\"ProjectConfigurations\">");
            sb.AppendLine("    <ProjectConfiguration Include=\"Debug|x64\">");
            sb.AppendLine("      <Configuration>Debug</Configuration>");
            sb.AppendLine("      <Platform>x64</Platform>");
            sb.AppendLine("    </ProjectConfiguration>");
            sb.AppendLine("    <ProjectConfiguration Include=\"Release|x64\">");
            sb.AppendLine("      <Configuration>Release</Configuration>");
            sb.AppendLine("      <Platform>x64</Platform>");
            sb.AppendLine("    </ProjectConfiguration>");
            sb.AppendLine("  </ItemGroup>");
            sb.AppendLine();

            // Globals
            sb.AppendLine("  <PropertyGroup Label=\"Globals\">");
            sb.AppendLine($"    <ProjectGuid>{{{projectGuid}}}</ProjectGuid>");
            sb.AppendLine($"    <RootNamespace>{project.Name}</RootNamespace>");
            sb.AppendLine($"    <ProjectName>{project.Name}</ProjectName>");
            sb.AppendLine("    <Keyword>Win32Proj</Keyword>");
            sb.AppendLine("    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
            sb.AppendLine("    <VCProjectVersion>17.0</VCProjectVersion>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            sb.AppendLine("  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />");
            sb.AppendLine();

            // Configuration properties - Debug
            sb.AppendLine("  <PropertyGroup Label=\"Configuration\" Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">");
            sb.AppendLine($"    <ConfigurationType>{configType}</ConfigurationType>");
            sb.AppendLine("    <PlatformToolset>v143</PlatformToolset>");
            sb.AppendLine("    <UseDebugLibraries>true</UseDebugLibraries>");
            sb.AppendLine("    <CharacterSet>Unicode</CharacterSet>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            // Configuration properties - Release
            sb.AppendLine("  <PropertyGroup Label=\"Configuration\" Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">");
            sb.AppendLine($"    <ConfigurationType>{configType}</ConfigurationType>");
            sb.AppendLine("    <PlatformToolset>v143</PlatformToolset>");
            sb.AppendLine("    <UseDebugLibraries>false</UseDebugLibraries>");
            sb.AppendLine("    <CharacterSet>Unicode</CharacterSet>");
            sb.AppendLine("    <WholeProgramOptimization>true</WholeProgramOptimization>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            sb.AppendLine("  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />");
            sb.AppendLine();

            // Output directories - use source directory for output, not temp
            // cforge puts executables in build/bin/Config/ and libraries in build/lib/Config/
            string binDir = project.Type == "static_library" ? "lib" : "bin";
            sb.AppendLine("  <PropertyGroup>");
            sb.AppendLine($"    <OutDir>{sourceDirAbs}\\{project.OutputDir}\\{binDir}\\$(Configuration)\\</OutDir>");
            sb.AppendLine($"    <IntDir>{sourceDirAbs}\\{project.OutputDir}\\obj\\$(Configuration)\\</IntDir>");
            sb.AppendLine($"    <TargetName>{project.Name}</TargetName>");
            sb.AppendLine($"    <TargetExt>{outputExt}</TargetExt>");
            sb.AppendLine("    <!-- cforge project marker -->");
            sb.AppendLine("    <CforgeProject>true</CforgeProject>");
            sb.AppendLine($"    <CforgeSourceDir>{sourceDirAbs}</CforgeSourceDir>");
            sb.AppendLine($"    <CforgeProjectType>{project.Type}</CforgeProjectType>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            // Include directories for IntelliSense - use absolute paths
            var includeDirs = new List<string>
            {
                Path.Combine(sourceDirAbs, "include"),
                Path.Combine(sourceDirAbs, "src")
            };
            includeDirs.AddRange(project.IncludeDirs.Select(d => Path.Combine(sourceDirAbs, d)));

            // Scan for dependency include directories (CMake FetchContent structure)
            string depsDir = Path.Combine(sourceDirAbs, project.OutputDir, "_deps");
            if (Directory.Exists(depsDir))
            {
                foreach (var depDir in Directory.GetDirectories(depsDir))
                {
                    string depName = Path.GetFileName(depDir);

                    // Only process *-src directories (source directories from FetchContent)
                    if (depName.EndsWith("-src"))
                    {
                        // Add the root of the dependency (for single-header libraries)
                        includeDirs.Add(depDir);

                        // Add include/ subdirectory if it exists
                        string includeSubdir = Path.Combine(depDir, "include");
                        if (Directory.Exists(includeSubdir))
                        {
                            includeDirs.Add(includeSubdir);
                        }

                        // Add src/ subdirectory if it exists (some deps have headers there)
                        string srcSubdir = Path.Combine(depDir, "src");
                        if (Directory.Exists(srcSubdir))
                        {
                            includeDirs.Add(srcSubdir);
                        }
                    }
                }
            }

            string includePathStr = string.Join(";", includeDirs) + ";%(AdditionalIncludeDirectories)";

            // Defines
            var debugDefines = new List<string> { "_DEBUG" };
            debugDefines.AddRange(project.Defines);
            debugDefines.AddRange(project.DebugDefines);
            string debugDefinesStr = string.Join(";", debugDefines) + ";%(PreprocessorDefinitions)";

            var releaseDefines = new List<string> { "NDEBUG" };
            releaseDefines.AddRange(project.Defines);
            releaseDefines.AddRange(project.ReleaseDefines);
            string releaseDefinesStr = string.Join(";", releaseDefines) + ";%(PreprocessorDefinitions)";

            string cppStdSwitch = $"stdcpp{project.CppStandard}";

            // ItemDefinitionGroup - Debug
            sb.AppendLine("  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">");
            sb.AppendLine("    <ClCompile>");
            sb.AppendLine($"      <AdditionalIncludeDirectories>{includePathStr}</AdditionalIncludeDirectories>");
            sb.AppendLine($"      <PreprocessorDefinitions>{debugDefinesStr}</PreprocessorDefinitions>");
            sb.AppendLine($"      <LanguageStandard>{cppStdSwitch}</LanguageStandard>");
            sb.AppendLine("      <WarningLevel>Level3</WarningLevel>");
            sb.AppendLine("      <Optimization>Disabled</Optimization>");
            sb.AppendLine("      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>");
            sb.AppendLine("    </ClCompile>");
            sb.AppendLine("    <Link>");
            sb.AppendLine("      <GenerateDebugInformation>true</GenerateDebugInformation>");
            sb.AppendLine("    </Link>");
            sb.AppendLine("  </ItemDefinitionGroup>");
            sb.AppendLine();

            // ItemDefinitionGroup - Release
            sb.AppendLine("  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">");
            sb.AppendLine("    <ClCompile>");
            sb.AppendLine($"      <AdditionalIncludeDirectories>{includePathStr}</AdditionalIncludeDirectories>");
            sb.AppendLine($"      <PreprocessorDefinitions>{releaseDefinesStr}</PreprocessorDefinitions>");
            sb.AppendLine($"      <LanguageStandard>{cppStdSwitch}</LanguageStandard>");
            sb.AppendLine("      <WarningLevel>Level3</WarningLevel>");
            sb.AppendLine("      <Optimization>MaxSpeed</Optimization>");
            sb.AppendLine("      <FunctionLevelLinking>true</FunctionLevelLinking>");
            sb.AppendLine("      <IntrinsicFunctions>true</IntrinsicFunctions>");
            sb.AppendLine("    </ClCompile>");
            sb.AppendLine("    <Link>");
            sb.AppendLine("      <GenerateDebugInformation>true</GenerateDebugInformation>");
            sb.AppendLine("      <EnableCOMDATFolding>true</EnableCOMDATFolding>");
            sb.AppendLine("      <OptimizeReferences>true</OptimizeReferences>");
            sb.AppendLine("    </Link>");
            sb.AppendLine("  </ItemDefinitionGroup>");
            sb.AppendLine();

            // Source files - use absolute paths
            // Mark as ExcludedFromBuild so MSVC doesn't compile them (cforge handles compilation)
            if (sourceFiles.Count > 0)
            {
                sb.AppendLine("  <ItemGroup>");
                foreach (var file in sourceFiles)
                {
                    sb.AppendLine($"    <ClCompile Include=\"{file}\">");
                    sb.AppendLine("      <ExcludedFromBuild>true</ExcludedFromBuild>");
                    sb.AppendLine("    </ClCompile>");
                }
                sb.AppendLine("  </ItemGroup>");
                sb.AppendLine();
            }

            // Header files - use absolute paths
            if (headerFiles.Count > 0)
            {
                sb.AppendLine("  <ItemGroup>");
                foreach (var file in headerFiles)
                {
                    sb.AppendLine($"    <ClInclude Include=\"{file}\" />");
                }
                sb.AppendLine("  </ItemGroup>");
                sb.AppendLine();
            }

            // cforge.toml reference - absolute path
            sb.AppendLine("  <ItemGroup>");
            sb.AppendLine($"    <None Include=\"{Path.Combine(sourceDirAbs, "cforge.toml")}\" />");
            sb.AppendLine("  </ItemGroup>");
            sb.AppendLine();

            // Override Build/Clean/Rebuild with cforge commands
            sb.AppendLine("  <!-- Override build with cforge -->");
            sb.AppendLine("  <PropertyGroup>");
            sb.AppendLine("    <BuildDependsOn>CforgeBuild</BuildDependsOn>");
            sb.AppendLine("    <CleanDependsOn>CforgeClean</CleanDependsOn>");
            sb.AppendLine("    <RebuildDependsOn>CforgeClean;CforgeBuild</RebuildDependsOn>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            sb.AppendLine($"  <Target Name=\"CforgeBuild\" Outputs=\"$(OutDir)$(TargetName)$(TargetExt)\">");
            sb.AppendLine($"    <Message Importance=\"high\" Text=\"Building with cforge: {project.Name}\" />");
            sb.AppendLine($"    <Exec Command=\"cforge build\" Condition=\"'$(Configuration)' == 'Debug'\" WorkingDirectory=\"{sourceDirAbs}\" ConsoleToMSBuild=\"true\" />");
            sb.AppendLine($"    <Exec Command=\"cforge build -c Release\" Condition=\"'$(Configuration)' == 'Release'\" WorkingDirectory=\"{sourceDirAbs}\" ConsoleToMSBuild=\"true\" />");
            sb.AppendLine("  </Target>");
            sb.AppendLine();

            sb.AppendLine("  <Target Name=\"CforgeClean\">");
            sb.AppendLine("    <Message Importance=\"high\" Text=\"Cleaning with cforge\" />");
            sb.AppendLine($"    <Exec Command=\"cforge clean\" WorkingDirectory=\"{sourceDirAbs}\" ConsoleToMSBuild=\"true\" IgnoreExitCode=\"true\" />");
            sb.AppendLine("  </Target>");
            sb.AppendLine();

            sb.AppendLine("  <Target Name=\"Build\" DependsOnTargets=\"CforgeBuild\" />");
            sb.AppendLine("  <Target Name=\"Clean\" DependsOnTargets=\"CforgeClean\" />");
            sb.AppendLine("  <Target Name=\"Rebuild\" DependsOnTargets=\"CforgeClean;CforgeBuild\" />");
            sb.AppendLine();

            sb.AppendLine("  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />");
            sb.AppendLine();
            sb.AppendLine("</Project>");

            return sb.ToString();
        }

        /// <summary>
        /// Generates a vcxproj for a workspace member project, including workspace-level include paths.
        /// </summary>
        private static string GenerateVcxprojForWorkspaceMember(
            CforgeProject project,
            List<string> sourceFiles,
            List<string> headerFiles,
            string sourceDir,
            string tempDir,
            string workspaceDir,
            List<string> memberIncludePaths)
        {
            string projectGuid = GenerateGuid(project.Name);
            string configType = project.Type switch
            {
                "static_library" => "StaticLibrary",
                "shared_library" => "DynamicLibrary",
                "header_only" => "StaticLibrary",
                _ => "Application"
            };
            string outputExt = project.Type switch
            {
                "static_library" => ".lib",
                "shared_library" => ".dll",
                _ => ".exe"
            };

            string sourceDirAbs = Path.GetFullPath(sourceDir);
            string workspaceDirAbs = Path.GetFullPath(workspaceDir);

            var sb = new StringBuilder();
            sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
            sb.AppendLine("<Project ToolsVersion=\"17.0\" DefaultTargets=\"Build\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");
            sb.AppendLine();

            // Project Configurations
            sb.AppendLine("  <ItemGroup Label=\"ProjectConfigurations\">");
            sb.AppendLine("    <ProjectConfiguration Include=\"Debug|x64\">");
            sb.AppendLine("      <Configuration>Debug</Configuration>");
            sb.AppendLine("      <Platform>x64</Platform>");
            sb.AppendLine("    </ProjectConfiguration>");
            sb.AppendLine("    <ProjectConfiguration Include=\"Release|x64\">");
            sb.AppendLine("      <Configuration>Release</Configuration>");
            sb.AppendLine("      <Platform>x64</Platform>");
            sb.AppendLine("    </ProjectConfiguration>");
            sb.AppendLine("  </ItemGroup>");
            sb.AppendLine();

            // Globals
            sb.AppendLine("  <PropertyGroup Label=\"Globals\">");
            sb.AppendLine($"    <ProjectGuid>{{{projectGuid}}}</ProjectGuid>");
            sb.AppendLine($"    <RootNamespace>{project.Name}</RootNamespace>");
            sb.AppendLine($"    <ProjectName>{project.Name}</ProjectName>");
            sb.AppendLine("    <Keyword>Win32Proj</Keyword>");
            sb.AppendLine("    <WindowsTargetPlatformVersion>10.0</WindowsTargetPlatformVersion>");
            sb.AppendLine("    <VCProjectVersion>17.0</VCProjectVersion>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            sb.AppendLine("  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />");
            sb.AppendLine();

            // Configuration properties
            sb.AppendLine("  <PropertyGroup Label=\"Configuration\" Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">");
            sb.AppendLine($"    <ConfigurationType>{configType}</ConfigurationType>");
            sb.AppendLine("    <PlatformToolset>v143</PlatformToolset>");
            sb.AppendLine("    <UseDebugLibraries>true</UseDebugLibraries>");
            sb.AppendLine("    <CharacterSet>Unicode</CharacterSet>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            sb.AppendLine("  <PropertyGroup Label=\"Configuration\" Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">");
            sb.AppendLine($"    <ConfigurationType>{configType}</ConfigurationType>");
            sb.AppendLine("    <PlatformToolset>v143</PlatformToolset>");
            sb.AppendLine("    <UseDebugLibraries>false</UseDebugLibraries>");
            sb.AppendLine("    <CharacterSet>Unicode</CharacterSet>");
            sb.AppendLine("    <WholeProgramOptimization>true</WholeProgramOptimization>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            sb.AppendLine("  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.props\" />");
            sb.AppendLine();

            // Output directories - workspace builds to workspace/build
            sb.AppendLine("  <PropertyGroup>");
            sb.AppendLine($"    <OutDir>{workspaceDirAbs}\\build\\$(Configuration)\\</OutDir>");
            sb.AppendLine($"    <IntDir>{workspaceDirAbs}\\build\\obj\\{project.Name}\\$(Configuration)\\</IntDir>");
            sb.AppendLine($"    <TargetName>{project.Name}</TargetName>");
            sb.AppendLine($"    <TargetExt>{outputExt}</TargetExt>");
            sb.AppendLine("    <CforgeProject>true</CforgeProject>");
            sb.AppendLine($"    <CforgeSourceDir>{sourceDirAbs}</CforgeSourceDir>");
            sb.AppendLine($"    <CforgeWorkspaceDir>{workspaceDirAbs}</CforgeWorkspaceDir>");
            sb.AppendLine($"    <CforgeProjectType>{project.Type}</CforgeProjectType>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            // Include directories - project's own + other workspace members + workspace deps
            var includeDirs = new List<string>
            {
                Path.Combine(sourceDirAbs, "include"),
                Path.Combine(sourceDirAbs, "src")
            };
            includeDirs.AddRange(project.IncludeDirs.Select(d => Path.Combine(sourceDirAbs, d)));

            // Add all workspace member include paths
            includeDirs.AddRange(memberIncludePaths.Where(Directory.Exists));

            // Scan for workspace-level dependencies (build/_deps at workspace root)
            string workspaceDepsDir = Path.Combine(workspaceDirAbs, "build", "_deps");
            if (Directory.Exists(workspaceDepsDir))
            {
                foreach (var depDir in Directory.GetDirectories(workspaceDepsDir))
                {
                    string depName = Path.GetFileName(depDir);
                    if (depName.EndsWith("-src"))
                    {
                        includeDirs.Add(depDir);
                        string includeSubdir = Path.Combine(depDir, "include");
                        if (Directory.Exists(includeSubdir))
                            includeDirs.Add(includeSubdir);
                        string srcSubdir = Path.Combine(depDir, "src");
                        if (Directory.Exists(srcSubdir))
                            includeDirs.Add(srcSubdir);
                    }
                }
            }

            // Also check member's own build/_deps
            string memberDepsDir = Path.Combine(sourceDirAbs, project.OutputDir, "_deps");
            if (Directory.Exists(memberDepsDir))
            {
                foreach (var depDir in Directory.GetDirectories(memberDepsDir))
                {
                    string depName = Path.GetFileName(depDir);
                    if (depName.EndsWith("-src"))
                    {
                        includeDirs.Add(depDir);
                        string includeSubdir = Path.Combine(depDir, "include");
                        if (Directory.Exists(includeSubdir))
                            includeDirs.Add(includeSubdir);
                        string srcSubdir = Path.Combine(depDir, "src");
                        if (Directory.Exists(srcSubdir))
                            includeDirs.Add(srcSubdir);
                    }
                }
            }

            string includePathStr = string.Join(";", includeDirs.Distinct()) + ";%(AdditionalIncludeDirectories)";

            // Defines
            var debugDefines = new List<string> { "_DEBUG" };
            debugDefines.AddRange(project.Defines);
            debugDefines.AddRange(project.DebugDefines);
            string debugDefinesStr = string.Join(";", debugDefines) + ";%(PreprocessorDefinitions)";

            var releaseDefines = new List<string> { "NDEBUG" };
            releaseDefines.AddRange(project.Defines);
            releaseDefines.AddRange(project.ReleaseDefines);
            string releaseDefinesStr = string.Join(";", releaseDefines) + ";%(PreprocessorDefinitions)";

            string cppStdSwitch = $"stdcpp{project.CppStandard}";

            // ItemDefinitionGroup - Debug
            sb.AppendLine("  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Debug|x64'\">");
            sb.AppendLine("    <ClCompile>");
            sb.AppendLine($"      <AdditionalIncludeDirectories>{includePathStr}</AdditionalIncludeDirectories>");
            sb.AppendLine($"      <PreprocessorDefinitions>{debugDefinesStr}</PreprocessorDefinitions>");
            sb.AppendLine($"      <LanguageStandard>{cppStdSwitch}</LanguageStandard>");
            sb.AppendLine("      <WarningLevel>Level3</WarningLevel>");
            sb.AppendLine("      <Optimization>Disabled</Optimization>");
            sb.AppendLine("      <DebugInformationFormat>ProgramDatabase</DebugInformationFormat>");
            sb.AppendLine("    </ClCompile>");
            sb.AppendLine("    <Link>");
            sb.AppendLine("      <GenerateDebugInformation>true</GenerateDebugInformation>");
            sb.AppendLine("    </Link>");
            sb.AppendLine("  </ItemDefinitionGroup>");
            sb.AppendLine();

            // ItemDefinitionGroup - Release
            sb.AppendLine("  <ItemDefinitionGroup Condition=\"'$(Configuration)|$(Platform)'=='Release|x64'\">");
            sb.AppendLine("    <ClCompile>");
            sb.AppendLine($"      <AdditionalIncludeDirectories>{includePathStr}</AdditionalIncludeDirectories>");
            sb.AppendLine($"      <PreprocessorDefinitions>{releaseDefinesStr}</PreprocessorDefinitions>");
            sb.AppendLine($"      <LanguageStandard>{cppStdSwitch}</LanguageStandard>");
            sb.AppendLine("      <WarningLevel>Level3</WarningLevel>");
            sb.AppendLine("      <Optimization>MaxSpeed</Optimization>");
            sb.AppendLine("      <FunctionLevelLinking>true</FunctionLevelLinking>");
            sb.AppendLine("      <IntrinsicFunctions>true</IntrinsicFunctions>");
            sb.AppendLine("    </ClCompile>");
            sb.AppendLine("    <Link>");
            sb.AppendLine("      <GenerateDebugInformation>true</GenerateDebugInformation>");
            sb.AppendLine("      <EnableCOMDATFolding>true</EnableCOMDATFolding>");
            sb.AppendLine("      <OptimizeReferences>true</OptimizeReferences>");
            sb.AppendLine("    </Link>");
            sb.AppendLine("  </ItemDefinitionGroup>");
            sb.AppendLine();

            // Source files - mark as excluded from build (cforge handles compilation)
            if (sourceFiles.Count > 0)
            {
                sb.AppendLine("  <ItemGroup>");
                foreach (var file in sourceFiles)
                {
                    sb.AppendLine($"    <ClCompile Include=\"{file}\">");
                    sb.AppendLine("      <ExcludedFromBuild>true</ExcludedFromBuild>");
                    sb.AppendLine("    </ClCompile>");
                }
                sb.AppendLine("  </ItemGroup>");
                sb.AppendLine();
            }

            // Header files
            if (headerFiles.Count > 0)
            {
                sb.AppendLine("  <ItemGroup>");
                foreach (var file in headerFiles)
                {
                    sb.AppendLine($"    <ClInclude Include=\"{file}\" />");
                }
                sb.AppendLine("  </ItemGroup>");
                sb.AppendLine();
            }

            // cforge.toml reference
            sb.AppendLine("  <ItemGroup>");
            sb.AppendLine($"    <None Include=\"{Path.Combine(sourceDirAbs, "cforge.toml")}\" />");
            sb.AppendLine("  </ItemGroup>");
            sb.AppendLine();

            // Override Build with cforge - build from workspace root
            sb.AppendLine("  <!-- Override build with cforge (workspace build) -->");
            sb.AppendLine("  <PropertyGroup>");
            sb.AppendLine("    <BuildDependsOn>CforgeBuild</BuildDependsOn>");
            sb.AppendLine("    <CleanDependsOn>CforgeClean</CleanDependsOn>");
            sb.AppendLine("    <RebuildDependsOn>CforgeClean;CforgeBuild</RebuildDependsOn>");
            sb.AppendLine("  </PropertyGroup>");
            sb.AppendLine();

            // Build from workspace root directory
            sb.AppendLine($"  <Target Name=\"CforgeBuild\" Outputs=\"$(OutDir)$(TargetName)$(TargetExt)\">");
            sb.AppendLine($"    <Message Importance=\"high\" Text=\"Building workspace member: {project.Name}\" />");
            sb.AppendLine($"    <Exec Command=\"cforge build\" Condition=\"'$(Configuration)' == 'Debug'\" WorkingDirectory=\"{workspaceDirAbs}\" ConsoleToMSBuild=\"true\" />");
            sb.AppendLine($"    <Exec Command=\"cforge build -c Release\" Condition=\"'$(Configuration)' == 'Release'\" WorkingDirectory=\"{workspaceDirAbs}\" ConsoleToMSBuild=\"true\" />");
            sb.AppendLine("  </Target>");
            sb.AppendLine();

            sb.AppendLine("  <Target Name=\"CforgeClean\">");
            sb.AppendLine("    <Message Importance=\"high\" Text=\"Cleaning workspace\" />");
            sb.AppendLine($"    <Exec Command=\"cforge clean\" WorkingDirectory=\"{workspaceDirAbs}\" ConsoleToMSBuild=\"true\" IgnoreExitCode=\"true\" />");
            sb.AppendLine("  </Target>");
            sb.AppendLine();

            sb.AppendLine("  <Target Name=\"Build\" DependsOnTargets=\"CforgeBuild\" />");
            sb.AppendLine("  <Target Name=\"Clean\" DependsOnTargets=\"CforgeClean\" />");
            sb.AppendLine("  <Target Name=\"Rebuild\" DependsOnTargets=\"CforgeClean;CforgeBuild\" />");
            sb.AppendLine();

            sb.AppendLine("  <Import Project=\"$(VCTargetsPath)\\Microsoft.Cpp.targets\" />");
            sb.AppendLine();
            sb.AppendLine("</Project>");

            return sb.ToString();
        }

        private static string GenerateFilters(List<string> sourceFiles, List<string> headerFiles, string sourceDir)
        {
            var sb = new StringBuilder();
            sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
            sb.AppendLine("<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">");

            // Collect unique filters based on directory structure relative to source dir
            var filters = new HashSet<string>();
            foreach (var file in sourceFiles.Concat(headerFiles))
            {
                string? relativePath = GetRelativePath(sourceDir, file);
                string? dir = Path.GetDirectoryName(relativePath);
                while (!string.IsNullOrEmpty(dir))
                {
                    filters.Add(dir);
                    dir = Path.GetDirectoryName(dir);
                }
            }

            // Filter definitions
            if (filters.Count > 0)
            {
                sb.AppendLine("  <ItemGroup>");
                foreach (var filter in filters.OrderBy(f => f))
                {
                    string guid = GenerateGuid(filter);
                    sb.AppendLine($"    <Filter Include=\"{filter}\">");
                    sb.AppendLine($"      <UniqueIdentifier>{{{guid}}}</UniqueIdentifier>");
                    sb.AppendLine("    </Filter>");
                }
                sb.AppendLine("  </ItemGroup>");
            }

            // Source files with filters
            if (sourceFiles.Count > 0)
            {
                sb.AppendLine("  <ItemGroup>");
                foreach (var file in sourceFiles)
                {
                    string relativePath = GetRelativePath(sourceDir, file);
                    string? filter = Path.GetDirectoryName(relativePath);
                    if (!string.IsNullOrEmpty(filter))
                    {
                        sb.AppendLine($"    <ClCompile Include=\"{file}\">");
                        sb.AppendLine($"      <Filter>{filter}</Filter>");
                        sb.AppendLine("    </ClCompile>");
                    }
                    else
                    {
                        sb.AppendLine($"    <ClCompile Include=\"{file}\" />");
                    }
                }
                sb.AppendLine("  </ItemGroup>");
            }

            // Header files with filters
            if (headerFiles.Count > 0)
            {
                sb.AppendLine("  <ItemGroup>");
                foreach (var file in headerFiles)
                {
                    string relativePath = GetRelativePath(sourceDir, file);
                    string? filter = Path.GetDirectoryName(relativePath);
                    if (!string.IsNullOrEmpty(filter))
                    {
                        sb.AppendLine($"    <ClInclude Include=\"{file}\">");
                        sb.AppendLine($"      <Filter>{filter}</Filter>");
                        sb.AppendLine("    </ClInclude>");
                    }
                    else
                    {
                        sb.AppendLine($"    <ClInclude Include=\"{file}\" />");
                    }
                }
                sb.AppendLine("  </ItemGroup>");
            }

            sb.AppendLine("</Project>");
            return sb.ToString();
        }

        private static string GenerateSolution(string name, List<(string name, string path, string guid, CforgeProject project)> projects)
        {
            var sb = new StringBuilder();
            sb.AppendLine();
            sb.AppendLine("Microsoft Visual Studio Solution File, Format Version 12.00");
            sb.AppendLine("# Visual Studio Version 17");
            sb.AppendLine("VisualStudioVersion = 17.0.31903.59");
            sb.AppendLine("MinimumVisualStudioVersion = 10.0.40219.1");

            // Projects
            foreach (var (projName, path, guid, _) in projects)
            {
                sb.AppendLine($"Project(\"{{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}}\") = \"{projName}\", \"{path}\", \"{{{guid}}}\"");
                sb.AppendLine("EndProject");
            }

            // Global section
            sb.AppendLine("Global");
            sb.AppendLine("\tGlobalSection(SolutionConfigurationPlatforms) = preSolution");
            sb.AppendLine("\t\tDebug|x64 = Debug|x64");
            sb.AppendLine("\t\tRelease|x64 = Release|x64");
            sb.AppendLine("\tEndGlobalSection");

            sb.AppendLine("\tGlobalSection(ProjectConfigurationPlatforms) = postSolution");
            foreach (var (_, _, guid, _) in projects)
            {
                sb.AppendLine($"\t\t{{{guid}}}.Debug|x64.ActiveCfg = Debug|x64");
                sb.AppendLine($"\t\t{{{guid}}}.Debug|x64.Build.0 = Debug|x64");
                sb.AppendLine($"\t\t{{{guid}}}.Release|x64.ActiveCfg = Release|x64");
                sb.AppendLine($"\t\t{{{guid}}}.Release|x64.Build.0 = Release|x64");
            }
            sb.AppendLine("\tEndGlobalSection");

            sb.AppendLine("\tGlobalSection(SolutionProperties) = preSolution");
            sb.AppendLine("\t\tHideSolutionNode = FALSE");
            sb.AppendLine("\tEndGlobalSection");
            sb.AppendLine("EndGlobal");

            return sb.ToString();
        }

        private static List<string> GetSourceFiles(string projectDir, CforgeProject project)
        {
            var files = new List<string>();

            // Default patterns if not specified
            var patterns = project.Sources.Count > 0
                ? project.Sources
                : new List<string> { "src/**/*.cpp", "src/**/*.c", "src/**/*.cc", "src/**/*.cxx" };

            foreach (var pattern in patterns)
            {
                files.AddRange(ExpandGlob(projectDir, pattern));
            }

            // Return absolute paths
            return files.Distinct().Select(f => Path.GetFullPath(f)).ToList();
        }

        private static List<string> GetHeaderFiles(string projectDir, CforgeProject project)
        {
            var files = new List<string>();

            // Scan include directories
            var includeDirs = project.IncludeDirs.Count > 0
                ? project.IncludeDirs
                : new List<string> { "include", "src" };

            foreach (var dir in includeDirs)
            {
                string fullDir = Path.Combine(projectDir, dir);
                if (Directory.Exists(fullDir))
                {
                    files.AddRange(Directory.GetFiles(fullDir, "*.h", SearchOption.AllDirectories));
                    files.AddRange(Directory.GetFiles(fullDir, "*.hpp", SearchOption.AllDirectories));
                    files.AddRange(Directory.GetFiles(fullDir, "*.hxx", SearchOption.AllDirectories));
                }
            }

            // Return absolute paths
            return files.Distinct().Select(f => Path.GetFullPath(f)).ToList();
        }

        private static List<string> ExpandGlob(string baseDir, string pattern)
        {
            var files = new List<string>();

            // Simple glob expansion
            if (pattern.Contains("**"))
            {
                // Recursive pattern
                string[] parts = pattern.Split(new[] { "**" }, 2, StringSplitOptions.None);
                string prefix = parts[0].TrimEnd('/', '\\');
                string suffix = parts.Length > 1 ? parts[1].TrimStart('/', '\\') : "*";

                string searchDir = string.IsNullOrEmpty(prefix)
                    ? baseDir
                    : Path.Combine(baseDir, prefix);

                if (Directory.Exists(searchDir))
                {
                    string searchPattern = Path.GetFileName(suffix);
                    if (string.IsNullOrEmpty(searchPattern)) searchPattern = "*.*";

                    try
                    {
                        files.AddRange(Directory.GetFiles(searchDir, searchPattern, SearchOption.AllDirectories));
                    }
                    catch { }
                }
            }
            else if (pattern.Contains("*"))
            {
                // Simple wildcard
                string dir = Path.GetDirectoryName(pattern) ?? "";
                string searchPattern = Path.GetFileName(pattern);
                string searchDir = Path.Combine(baseDir, dir);

                if (Directory.Exists(searchDir))
                {
                    try
                    {
                        files.AddRange(Directory.GetFiles(searchDir, searchPattern, SearchOption.TopDirectoryOnly));
                    }
                    catch { }
                }
            }
            else
            {
                // Exact file
                string fullPath = Path.Combine(baseDir, pattern);
                if (File.Exists(fullPath))
                {
                    files.Add(fullPath);
                }
            }

            return files;
        }

        private static string GetRelativePath(string basePath, string fullPath)
        {
            if (!basePath.EndsWith(Path.DirectorySeparatorChar.ToString()))
                basePath += Path.DirectorySeparatorChar;

            try
            {
                Uri baseUri = new Uri(basePath);
                Uri fullUri = new Uri(fullPath);
                string relativePath = Uri.UnescapeDataString(baseUri.MakeRelativeUri(fullUri).ToString());
                return relativePath.Replace('/', Path.DirectorySeparatorChar);
            }
            catch
            {
                return fullPath;
            }
        }

        private static string GenerateGuid(string input)
        {
            using (var md5 = MD5.Create())
            {
                byte[] hash = md5.ComputeHash(Encoding.UTF8.GetBytes(input));
                return new Guid(hash).ToString("D").ToUpperInvariant();
            }
        }

        private static string ComputeHash(string input)
        {
            using (var sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(Encoding.UTF8.GetBytes(input.ToLowerInvariant()));
                return BitConverter.ToString(hash).Replace("-", "").Substring(0, 16).ToLowerInvariant();
            }
        }

        /// <summary>
        /// Opens the generated solution file in Visual Studio.
        /// </summary>
        private static async Task OpenSolutionAsync(string slnPath, string sourceDir)
        {
            var pane = await CforgeRunner.GetOutputPaneAsync();
            await pane.WriteLineAsync($"[cforge] OpenSolutionAsync called: {slnPath}");

            if (_isOpeningSolution)
            {
                await pane.WriteLineAsync("[cforge] Already opening a solution, skipping");
                return;
            }

            try
            {
                _isOpeningSolution = true;
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

                // Verify the solution file exists
                if (!File.Exists(slnPath))
                {
                    await pane.WriteLineAsync($"[cforge] ERROR: Solution file does not exist: {slnPath}");
                    return;
                }
                await pane.WriteLineAsync($"[cforge] Solution file exists: {slnPath}");

                // Check if we're already in this solution
                var currentSolution = await VS.Solutions.GetCurrentSolutionAsync();
                await pane.WriteLineAsync($"[cforge] Current solution: {currentSolution?.FullPath ?? "none"}");

                if (currentSolution?.FullPath != null &&
                    string.Equals(currentSolution.FullPath, slnPath, StringComparison.OrdinalIgnoreCase))
                {
                    await pane.WriteLineAsync("[cforge] Solution already loaded.");
                    return;
                }

                // Get DTE to open the solution
                var dte = await VS.GetServiceAsync<DTE, DTE2>();
                if (dte != null)
                {
                    await pane.WriteLineAsync($"[cforge] Got DTE, opening solution...");
                    await pane.WriteLineAsync($"[cforge] Source: {sourceDir}");

                    // Close current solution/folder first
                    try
                    {
                        await pane.WriteLineAsync("[cforge] Closing current solution/folder...");
                        dte.Solution.Close(false);
                        await pane.WriteLineAsync("[cforge] Closed.");
                    }
                    catch (Exception closeEx)
                    {
                        await pane.WriteLineAsync($"[cforge] Close failed (may be OK): {closeEx.Message}");
                    }

                    // Longer delay to ensure VS is fully ready after closing Open Folder
                    await Task.Delay(1000);

                    // Open the generated solution
                    await pane.WriteLineAsync($"[cforge] Opening: {slnPath}");

                    try
                    {
                        dte.Solution.Open(slnPath);
                        await Task.Delay(500); // Give VS time to process

                        // Verify it opened
                        var newSolution = await VS.Solutions.GetCurrentSolutionAsync();
                        if (newSolution?.FullPath != null &&
                            newSolution.FullPath.Equals(slnPath, StringComparison.OrdinalIgnoreCase))
                        {
                            await pane.WriteLineAsync("[cforge] Solution opened successfully!");
                            await pane.WriteLineAsync("[cforge] IntelliSense and error highlighting should now be available.");
                        }
                        else
                        {
                            await pane.WriteLineAsync("[cforge] Solution may not have opened correctly.");
                            await pane.WriteLineAsync($"[cforge] Please manually open: {slnPath}");
                            await ShowOpenSolutionInfoBarAsync(slnPath);
                        }
                    }
                    catch (Exception openEx)
                    {
                        await pane.WriteLineAsync($"[cforge] Solution.Open failed: {openEx.Message}");
                        await pane.WriteLineAsync($"[cforge] Please manually open: {slnPath}");
                        await ShowOpenSolutionInfoBarAsync(slnPath);
                    }
                }
                else
                {
                    await pane.WriteLineAsync("[cforge] ERROR: Could not get DTE service");
                }
            }
            catch (Exception ex)
            {
                await pane.WriteLineAsync($"[cforge] ERROR opening solution: {ex.GetType().Name}: {ex.Message}");
                await pane.WriteLineAsync($"[cforge] Stack: {ex.StackTrace}");
                await pane.WriteLineAsync($"[cforge] You can manually open: {slnPath}");
            }
            finally
            {
                _isOpeningSolution = false;
            }
        }

        /// <summary>
        /// Shows a notification prompting the user to open the generated solution.
        /// </summary>
        private static async Task ShowOpenSolutionInfoBarAsync(string slnPath)
        {
            try
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

                // Show in status bar
                await VS.StatusBar.ShowMessageAsync($"cforge: Solution ready - {Path.GetFileName(slnPath)}");

                // Also show a message box as a more visible notification
                var result = await VS.MessageBox.ShowAsync(
                    "cforge Solution Generated",
                    $"The solution was generated but couldn't be opened automatically.\n\nWould you like to open it now?\n\n{slnPath}",
                    Microsoft.VisualStudio.Shell.Interop.OLEMSGICON.OLEMSGICON_INFO,
                    Microsoft.VisualStudio.Shell.Interop.OLEMSGBUTTON.OLEMSGBUTTON_YESNO);

                if (result == Microsoft.VisualStudio.VSConstants.MessageBoxResult.IDYES)
                {
                    var dte = await VS.GetServiceAsync<DTE, DTE2>();
                    if (dte != null)
                    {
                        dte.Solution.Open(slnPath);
                    }
                }
            }
            catch
            {
                // Notification not available, that's okay - output pane has the info
            }
        }

        /// <summary>
        /// Gets the source directory for the current solution (if it's a cforge generated solution).
        /// </summary>
        public static string? GetSourceDirForCurrentSolution(string solutionPath)
        {
            try
            {
                string? slnDir = Path.GetDirectoryName(solutionPath);
                if (slnDir == null) return null;

                // Check for marker file
                string markerPath = Path.Combine(slnDir, ".cforge-source");
                if (File.Exists(markerPath))
                {
                    return File.ReadAllText(markerPath).Trim();
                }

                return null;
            }
            catch
            {
                return null;
            }
        }

        /// <summary>
        /// Cleans up generated files for a source directory.
        /// </summary>
        public static void CleanupGeneratedFiles(string sourceDir)
        {
            try
            {
                string tempDir = GetTempProjectDir(sourceDir);
                if (Directory.Exists(tempDir))
                {
                    Directory.Delete(tempDir, true);
                }
            }
            catch { }
        }
    }
}
