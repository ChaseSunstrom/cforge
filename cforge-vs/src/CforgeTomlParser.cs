using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Tomlyn;
using Tomlyn.Model;

namespace CforgeVS
{
    /// <summary>
    /// Parses cforge.toml files and extracts project configuration.
    /// </summary>
    public class CforgeProject
    {
        public string Name { get; set; } = "";
        public string Version { get; set; } = "0.1.0";
        public string Type { get; set; } = "executable"; // executable, static_library, shared_library, header_only
        public string CppStandard { get; set; } = "20";
        public string CStandard { get; set; } = "17";
        public List<string> Sources { get; set; } = new List<string>();
        public List<string> IncludeDirs { get; set; } = new List<string>();
        public List<string> Defines { get; set; } = new List<string>();
        public List<string> DebugDefines { get; set; } = new List<string>();
        public List<string> ReleaseDefines { get; set; } = new List<string>();
        public List<CforgeDependency> Dependencies { get; set; } = new List<CforgeDependency>();
        public string OutputDir { get; set; } = "build";
        public List<string> CompilerFlags { get; set; } = new List<string>();
        public List<string> LinkerFlags { get; set; } = new List<string>();
    }

    public class CforgeDependency
    {
        public string Name { get; set; } = "";
        public string Version { get; set; } = "";
        public string Source { get; set; } = ""; // registry, git, local
    }

    public class CforgeWorkspace
    {
        public string Name { get; set; } = "";
        public List<string> Members { get; set; } = new List<string>();
    }

    public static class CforgeTomlParser
    {
        public static CforgeProject? ParseProject(string projectDir)
        {
            string tomlPath = Path.Combine(projectDir, "cforge.toml");
            if (!File.Exists(tomlPath))
                return null;

            try
            {
                string content = File.ReadAllText(tomlPath);
                var doc = Toml.ToModel(content);

                var project = new CforgeProject();

                // Parse [project] section
                if (doc.TryGetValue("project", out var projectSection) && projectSection is TomlTable projectTable)
                {
                    project.Name = GetString(projectTable, "name") ?? Path.GetFileName(projectDir);
                    project.Version = GetString(projectTable, "version") ?? "0.1.0";
                    project.Type = GetString(projectTable, "binary_type") ?? GetString(projectTable, "type") ?? "executable";
                    project.CppStandard = GetString(projectTable, "cpp_standard") ??
                                          GetString(projectTable, "cxx_standard") ?? "20";
                    project.CStandard = GetString(projectTable, "c_standard") ?? "17";
                    project.Sources = GetStringList(projectTable, "sources");
                    project.IncludeDirs = GetStringList(projectTable, "include_dirs");
                    project.Defines = GetStringList(projectTable, "defines");
                    project.CompilerFlags = GetStringList(projectTable, "compiler_flags");
                    project.LinkerFlags = GetStringList(projectTable, "linker_flags");
                }

                // Parse [build] section
                if (doc.TryGetValue("build", out var buildSection) && buildSection is TomlTable buildTable)
                {
                    // cforge uses "directory" for output dir, not "output_dir"
                    var outputDir = GetString(buildTable, "directory") ?? GetString(buildTable, "output_dir");
                    if (!string.IsNullOrEmpty(outputDir))
                        project.OutputDir = outputDir;

                    // Merge any additional settings
                    var extraIncludes = GetStringList(buildTable, "include_dirs");
                    project.IncludeDirs.AddRange(extraIncludes);

                    var extraSources = GetStringList(buildTable, "source_dirs");
                    foreach (var src in extraSources)
                    {
                        project.Sources.Add($"{src}/**/*.cpp");
                        project.Sources.Add($"{src}/**/*.c");
                    }

                    var extraDefines = GetStringList(buildTable, "defines");
                    project.Defines.AddRange(extraDefines);
                }

                // Parse [build.debug] section
                if (doc.TryGetValue("build", out var buildSectionForDebug) && buildSectionForDebug is TomlTable buildTableForDebug)
                {
                    if (buildTableForDebug.TryGetValue("debug", out var debugSection) && debugSection is TomlTable debugTable)
                    {
                        project.DebugDefines = GetStringList(debugTable, "defines");
                    }
                }

                // Parse [build.release] section
                if (doc.TryGetValue("build", out var buildSectionForRelease) && buildSectionForRelease is TomlTable buildTableForRelease)
                {
                    if (buildTableForRelease.TryGetValue("release", out var releaseSection) && releaseSection is TomlTable releaseTable)
                    {
                        project.ReleaseDefines = GetStringList(releaseTable, "defines");
                    }
                }

                // Parse [dependencies] section
                if (doc.TryGetValue("dependencies", out var depsSection) && depsSection is TomlTable depsTable)
                {
                    foreach (var kvp in depsTable)
                    {
                        var dep = new CforgeDependency { Name = kvp.Key };

                        if (kvp.Value is string versionStr)
                        {
                            dep.Version = versionStr;
                            dep.Source = "registry";
                        }
                        else if (kvp.Value is TomlTable depTable)
                        {
                            dep.Version = GetString(depTable, "version") ?? "";
                            dep.Source = GetString(depTable, "git") != null ? "git" :
                                        GetString(depTable, "path") != null ? "local" : "registry";
                        }

                        project.Dependencies.Add(dep);
                    }
                }

                // Add default include dirs if not specified
                if (project.IncludeDirs.Count == 0)
                {
                    project.IncludeDirs.Add("include");
                    project.IncludeDirs.Add("src");
                }

                // Add default sources if not specified
                if (project.Sources.Count == 0)
                {
                    project.Sources.Add("src/**/*.cpp");
                    project.Sources.Add("src/**/*.c");
                }

                return project;
            }
            catch (Exception)
            {
                return null;
            }
        }

        public static CforgeWorkspace? ParseWorkspace(string projectDir)
        {
            string tomlPath = Path.Combine(projectDir, "cforge.workspace.toml");
            if (!File.Exists(tomlPath))
                return null;

            try
            {
                string content = File.ReadAllText(tomlPath);
                var doc = Toml.ToModel(content);

                var workspace = new CforgeWorkspace();

                if (doc.TryGetValue("workspace", out var wsSection) && wsSection is TomlTable wsTable)
                {
                    workspace.Name = GetString(wsTable, "name") ?? Path.GetFileName(projectDir);
                    workspace.Members = GetStringList(wsTable, "members");
                }

                return workspace;
            }
            catch (Exception)
            {
                return null;
            }
        }

        /// <summary>
        /// Gets all include paths for IntelliSense, including dependency paths.
        /// </summary>
        public static List<string> GetAllIncludePaths(string projectDir, CforgeProject project)
        {
            var paths = new List<string>();

            // Add project include dirs
            foreach (var dir in project.IncludeDirs)
            {
                paths.Add($"${{workspaceRoot}}/{dir}");
            }

            // Add src directory
            paths.Add("${workspaceRoot}/src");

            // Add build directory for generated files
            paths.Add("${workspaceRoot}/build");
            paths.Add("${workspaceRoot}/build/Debug");
            paths.Add("${workspaceRoot}/build/Release");

            // Add dependency include paths
            string depsDir = Path.Combine(projectDir, "build", "_deps");
            if (Directory.Exists(depsDir))
            {
                foreach (var depDir in Directory.GetDirectories(depsDir))
                {
                    string includeDir = Path.Combine(depDir, "include");
                    if (Directory.Exists(includeDir))
                    {
                        string relativePath = GetRelativePath(projectDir, includeDir);
                        paths.Add($"${{workspaceRoot}}/{relativePath.Replace('\\', '/')}");
                    }

                    // Also check for src directory in deps (some deps have headers there)
                    string srcDir = Path.Combine(depDir, "src");
                    if (Directory.Exists(srcDir))
                    {
                        string relativePath = GetRelativePath(projectDir, srcDir);
                        paths.Add($"${{workspaceRoot}}/{relativePath.Replace('\\', '/')}");
                    }
                }
            }

            // Add FetchContent style deps
            paths.Add("${workspaceRoot}/build/_deps/**/include");
            paths.Add("${workspaceRoot}/build/Debug/_deps/**/include");
            paths.Add("${workspaceRoot}/build/Release/_deps/**/include");

            return paths.Distinct().ToList();
        }

        /// <summary>
        /// Gets the executable/library output path.
        /// cforge puts executables in {outputDir}/bin/{config}/ and libraries in {outputDir}/lib/{config}/
        /// </summary>
        public static string GetOutputPath(CforgeProject project, string configuration)
        {
            string extension = project.Type switch
            {
                "executable" => ".exe",
                "static_library" => ".lib",
                "shared_library" => ".dll",
                _ => ".exe"
            };

            string subdir = project.Type switch
            {
                "executable" => "bin",
                "static_library" => "lib",
                "shared_library" => "bin",
                _ => "bin"
            };

            return $"{project.OutputDir}/{subdir}/{configuration}/{project.Name}{extension}";
        }

        /// <summary>
        /// Finds the actual executable path by checking multiple possible locations.
        /// </summary>
        public static string? FindExecutable(string projectDir, CforgeProject project, string configuration)
        {
            string extension = project.Type switch
            {
                "executable" => ".exe",
                "static_library" => ".lib",
                "shared_library" => ".dll",
                _ => ".exe"
            };

            // Possible locations where cforge might put the executable
            var possiblePaths = new[]
            {
                // Standard cforge layout: build/bin/Config/name.exe
                Path.Combine(projectDir, project.OutputDir, "bin", configuration, project.Name + extension),
                // Alternative: build/Config/name.exe
                Path.Combine(projectDir, project.OutputDir, configuration, project.Name + extension),
                // CMake multi-config: build/Config/bin/name.exe
                Path.Combine(projectDir, project.OutputDir, configuration, "bin", project.Name + extension),
                // Single-config build: build/bin/name.exe
                Path.Combine(projectDir, project.OutputDir, "bin", project.Name + extension),
                // Flat build: build/name.exe
                Path.Combine(projectDir, project.OutputDir, project.Name + extension),
            };

            foreach (var path in possiblePaths)
            {
                if (File.Exists(path))
                {
                    return path;
                }
            }

            return null;
        }

        private static string? GetString(TomlTable table, string key)
        {
            if (table.TryGetValue(key, out var value))
            {
                return value?.ToString();
            }
            return null;
        }

        private static List<string> GetStringList(TomlTable table, string key)
        {
            var result = new List<string>();
            if (table.TryGetValue(key, out var value))
            {
                if (value is TomlArray array)
                {
                    foreach (var item in array)
                    {
                        if (item != null)
                            result.Add(item.ToString()!);
                    }
                }
                else if (value is string str)
                {
                    result.Add(str);
                }
            }
            return result;
        }

        private static string GetRelativePath(string basePath, string fullPath)
        {
            Uri baseUri = new Uri(basePath.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar);
            Uri fullUri = new Uri(fullPath);
            return Uri.UnescapeDataString(baseUri.MakeRelativeUri(fullUri).ToString());
        }
    }
}
