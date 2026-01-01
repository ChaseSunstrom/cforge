using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    /// <summary>
    /// Provides VS Open Folder support for cforge projects.
    /// Generates tasks.vs.json and launch.vs.json for build/debug integration.
    /// </summary>
    public static class OpenFolderSupport
    {
        public static async Task InitializeAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Subscribe to folder open events
            VS.Events.SolutionEvents.OnAfterOpenFolder += OnFolderOpened;

            // Check if a folder is already open
            await CheckCurrentFolderAsync();
        }

        private static async Task CheckCurrentFolderAsync()
        {
            var solution = await VS.Solutions.GetCurrentSolutionAsync();
            if (solution?.FullPath != null)
            {
                string? folder = Path.GetDirectoryName(solution.FullPath);
                if (!string.IsNullOrEmpty(folder) && CforgeRunner.IsCforgeProject(folder))
                {
                    await EnsureVsConfigFilesAsync(folder);
                }
            }
        }

        private static void OnFolderOpened(string? folder)
        {
            if (!string.IsNullOrEmpty(folder) && CforgeRunner.IsCforgeProject(folder))
            {
                _ = EnsureVsConfigFilesAsync(folder);
            }
        }

        public static async Task EnsureVsConfigFilesAsync(string projectDir)
        {
            string vsDir = Path.Combine(projectDir, ".vs");
            if (!Directory.Exists(vsDir))
            {
                Directory.CreateDirectory(vsDir);
            }

            // Parse the cforge.toml
            var project = CforgeTomlParser.ParseProject(projectDir);
            var workspace = CforgeTomlParser.ParseWorkspace(projectDir);

            if (project == null && workspace == null)
            {
                return;
            }

            // Generate tasks.vs.json for build commands
            GenerateTasksFile(projectDir, project, workspace);

            // Generate launch.vs.json for debugging
            GenerateLaunchFile(projectDir, project, workspace);

            // Generate CppProperties.json for IntelliSense
            GenerateCppProperties(projectDir, project, workspace);

            // Disable CMake integration - cforge handles the build
            DisableCMakeIntegration(projectDir);

            var pane = await CforgeRunner.GetOutputPaneAsync();
            await pane.WriteLineAsync($"[cforge] VS configuration files generated from cforge.toml");
            if (project != null)
            {
                await pane.WriteLineAsync($"  Project: {project.Name} ({project.Type})");
                await pane.WriteLineAsync($"  C++ Standard: {project.CppStandard}");
                await pane.WriteLineAsync($"  Dependencies: {project.Dependencies.Count}");
            }
        }

        private static void GenerateTasksFile(string projectDir, CforgeProject? project, CforgeWorkspace? workspace)
        {
            string tasksPath = Path.Combine(projectDir, ".vs", "tasks.vs.json");
            string projectName = project?.Name ?? workspace?.Name ?? Path.GetFileName(projectDir);

            var sb = new StringBuilder();
            sb.AppendLine("{");
            sb.AppendLine("  \"version\": \"0.2.1\",");
            sb.AppendLine("  \"tasks\": [");

            // Build Debug
            sb.AppendLine("    {");
            sb.AppendLine($"      \"taskLabel\": \"cforge: Build {projectName} (Debug)\",");
            sb.AppendLine("      \"appliesTo\": \"/\",");
            sb.AppendLine("      \"type\": \"launch\",");
            sb.AppendLine("      \"command\": \"cforge\",");
            sb.AppendLine("      \"args\": [\"build\"],");
            sb.AppendLine("      \"contextType\": \"build\"");
            sb.AppendLine("    },");

            // Build Release
            sb.AppendLine("    {");
            sb.AppendLine($"      \"taskLabel\": \"cforge: Build {projectName} (Release)\",");
            sb.AppendLine("      \"appliesTo\": \"/\",");
            sb.AppendLine("      \"type\": \"launch\",");
            sb.AppendLine("      \"command\": \"cforge\",");
            sb.AppendLine("      \"args\": [\"build\", \"-c\", \"Release\"],");
            sb.AppendLine("      \"contextType\": \"build\"");
            sb.AppendLine("    },");

            // Clean
            sb.AppendLine("    {");
            sb.AppendLine("      \"taskLabel\": \"cforge: Clean\",");
            sb.AppendLine("      \"appliesTo\": \"/\",");
            sb.AppendLine("      \"type\": \"launch\",");
            sb.AppendLine("      \"command\": \"cforge\",");
            sb.AppendLine("      \"args\": [\"clean\"],");
            sb.AppendLine("      \"contextType\": \"clean\"");
            sb.AppendLine("    },");

            // Run (only for executables)
            if (project?.Type == "executable" || project?.Type == null)
            {
                sb.AppendLine("    {");
                sb.AppendLine($"      \"taskLabel\": \"cforge: Run {projectName}\",");
                sb.AppendLine("      \"appliesTo\": \"/\",");
                sb.AppendLine("      \"type\": \"launch\",");
                sb.AppendLine("      \"command\": \"cforge\",");
                sb.AppendLine("      \"args\": [\"run\"]");
                sb.AppendLine("    },");
            }

            // Test
            sb.AppendLine("    {");
            sb.AppendLine("      \"taskLabel\": \"cforge: Run Tests\",");
            sb.AppendLine("      \"appliesTo\": \"/\",");
            sb.AppendLine("      \"type\": \"launch\",");
            sb.AppendLine("      \"command\": \"cforge\",");
            sb.AppendLine("      \"args\": [\"test\"]");
            sb.AppendLine("    },");

            // Watch
            sb.AppendLine("    {");
            sb.AppendLine("      \"taskLabel\": \"cforge: Watch\",");
            sb.AppendLine("      \"appliesTo\": \"/\",");
            sb.AppendLine("      \"type\": \"launch\",");
            sb.AppendLine("      \"command\": \"cforge\",");
            sb.AppendLine("      \"args\": [\"watch\"]");
            sb.AppendLine("    },");

            // Format
            sb.AppendLine("    {");
            sb.AppendLine("      \"taskLabel\": \"cforge: Format Code\",");
            sb.AppendLine("      \"appliesTo\": \"*.cpp\",");
            sb.AppendLine("      \"type\": \"launch\",");
            sb.AppendLine("      \"command\": \"cforge\",");
            sb.AppendLine("      \"args\": [\"fmt\"]");
            sb.AppendLine("    },");

            // Lint
            sb.AppendLine("    {");
            sb.AppendLine("      \"taskLabel\": \"cforge: Lint\",");
            sb.AppendLine("      \"appliesTo\": \"*.cpp\",");
            sb.AppendLine("      \"type\": \"launch\",");
            sb.AppendLine("      \"command\": \"cforge\",");
            sb.AppendLine("      \"args\": [\"lint\"]");
            sb.AppendLine("    }");

            sb.AppendLine("  ]");
            sb.AppendLine("}");

            File.WriteAllText(tasksPath, sb.ToString());
        }

        private static void GenerateLaunchFile(string projectDir, CforgeProject? project, CforgeWorkspace? workspace)
        {
            string launchPath = Path.Combine(projectDir, ".vs", "launch.vs.json");

            var sb = new StringBuilder();
            sb.AppendLine("{");
            sb.AppendLine("  \"version\": \"0.2.1\",");
            sb.AppendLine("  \"defaults\": {},");
            sb.AppendLine("  \"configurations\": [");

            if (project != null && project.Type == "executable")
            {
                string outputDir = project.OutputDir;
                string exeName = project.Name + ".exe";

                // Debug configuration - cforge uses build/bin/Config layout
                sb.AppendLine("    {");
                sb.AppendLine("      \"type\": \"default\",");
                sb.AppendLine($"      \"project\": \"{outputDir}/bin/Debug/{exeName}\",");
                sb.AppendLine("      \"projectTarget\": \"\",");
                sb.AppendLine($"      \"name\": \"Debug: {project.Name}\",");
                sb.AppendLine("      \"cwd\": \"${workspaceRoot}\",");
                sb.AppendLine("      \"buildBeforeLaunch\": false,");
                sb.AppendLine($"      \"preLaunchTask\": \"cforge: Build {project.Name} (Debug)\"");
                sb.AppendLine("    },");

                // Release configuration
                sb.AppendLine("    {");
                sb.AppendLine("      \"type\": \"default\",");
                sb.AppendLine($"      \"project\": \"{outputDir}/bin/Release/{exeName}\",");
                sb.AppendLine("      \"projectTarget\": \"\",");
                sb.AppendLine($"      \"name\": \"Release: {project.Name}\",");
                sb.AppendLine("      \"cwd\": \"${workspaceRoot}\",");
                sb.AppendLine("      \"buildBeforeLaunch\": false,");
                sb.AppendLine($"      \"preLaunchTask\": \"cforge: Build {project.Name} (Release)\"");
                sb.AppendLine("    }");
            }
            else if (workspace != null)
            {
                // For workspaces, generate configs for each executable member
                bool first = true;
                foreach (var member in workspace.Members)
                {
                    string memberDir = Path.Combine(projectDir, member);
                    var memberProject = CforgeTomlParser.ParseProject(memberDir);

                    if (memberProject != null && memberProject.Type == "executable")
                    {
                        if (!first) sb.AppendLine(",");
                        first = false;

                        string outputDir = memberProject.OutputDir;
                        string exeName = memberProject.Name + ".exe";

                        sb.AppendLine("    {");
                        sb.AppendLine("      \"type\": \"default\",");
                        sb.AppendLine($"      \"project\": \"{member}/{outputDir}/bin/Debug/{exeName}\",");
                        sb.AppendLine("      \"projectTarget\": \"\",");
                        sb.AppendLine($"      \"name\": \"Debug: {memberProject.Name}\",");
                        sb.AppendLine($"      \"cwd\": \"${{workspaceRoot}}/{member}\",");
                        sb.AppendLine("      \"buildBeforeLaunch\": false,");
                        sb.AppendLine($"      \"preLaunchTask\": \"cforge: Build {memberProject.Name} (Debug)\"");
                        sb.Append("    }");
                    }
                }
                if (!first) sb.AppendLine();
            }
            else
            {
                // Fallback for unknown project type - cforge uses build/bin/Config layout
                string projectName = Path.GetFileName(projectDir);
                sb.AppendLine("    {");
                sb.AppendLine("      \"type\": \"default\",");
                sb.AppendLine($"      \"project\": \"build/bin/Debug/{projectName}.exe\",");
                sb.AppendLine("      \"projectTarget\": \"\",");
                sb.AppendLine($"      \"name\": \"Debug: {projectName}\",");
                sb.AppendLine("      \"cwd\": \"${workspaceRoot}\",");
                sb.AppendLine("      \"buildBeforeLaunch\": false,");
                sb.AppendLine($"      \"preLaunchTask\": \"cforge: Build {projectName} (Debug)\"");
                sb.AppendLine("    }");
            }

            sb.AppendLine("  ]");
            sb.AppendLine("}");

            File.WriteAllText(launchPath, sb.ToString());
        }

        private static void GenerateCppProperties(string projectDir, CforgeProject? project, CforgeWorkspace? workspace)
        {
            string cppPropsPath = Path.Combine(projectDir, ".vs", "CppProperties.json");

            // Get include paths from actual project
            var includePaths = project != null
                ? CforgeTomlParser.GetAllIncludePaths(projectDir, project)
                : new System.Collections.Generic.List<string> { "${workspaceRoot}/**" };

            // Get defines from actual project
            var defines = project?.Defines ?? new System.Collections.Generic.List<string>();
            var debugDefines = project?.DebugDefines ?? new System.Collections.Generic.List<string>();
            var releaseDefines = project?.ReleaseDefines ?? new System.Collections.Generic.List<string>();

            // Get C++ standard
            string cppStandard = project?.CppStandard ?? "20";
            string compilerSwitch = $"-std:c++{cppStandard}";

            var sb = new StringBuilder();
            sb.AppendLine("{");
            sb.AppendLine("  \"configurations\": [");

            // Debug configuration
            sb.AppendLine("    {");
            sb.AppendLine($"      \"name\": \"cforge-Debug\",");
            sb.AppendLine("      \"includePath\": [");
            for (int i = 0; i < includePaths.Count; i++)
            {
                sb.Append($"        \"{includePaths[i]}\"");
                sb.AppendLine(i < includePaths.Count - 1 ? "," : "");
            }
            sb.AppendLine("      ],");
            sb.AppendLine("      \"defines\": [");
            sb.Append("        \"_DEBUG\"");
            foreach (var def in defines.Concat(debugDefines))
            {
                sb.AppendLine(",");
                sb.Append($"        \"{EscapeJsonString(def)}\"");
            }
            sb.AppendLine();
            sb.AppendLine("      ],");
            sb.AppendLine("      \"intelliSenseMode\": \"windows-msvc-x64\",");
            sb.AppendLine($"      \"compilerSwitches\": \"{compilerSwitch}\"");
            sb.AppendLine("    },");

            // Release configuration
            sb.AppendLine("    {");
            sb.AppendLine($"      \"name\": \"cforge-Release\",");
            sb.AppendLine("      \"includePath\": [");
            for (int i = 0; i < includePaths.Count; i++)
            {
                sb.Append($"        \"{includePaths[i]}\"");
                sb.AppendLine(i < includePaths.Count - 1 ? "," : "");
            }
            sb.AppendLine("      ],");
            sb.AppendLine("      \"defines\": [");
            sb.Append("        \"NDEBUG\"");
            foreach (var def in defines.Concat(releaseDefines))
            {
                sb.AppendLine(",");
                sb.Append($"        \"{EscapeJsonString(def)}\"");
            }
            sb.AppendLine();
            sb.AppendLine("      ],");
            sb.AppendLine("      \"intelliSenseMode\": \"windows-msvc-x64\",");
            sb.AppendLine($"      \"compilerSwitches\": \"{compilerSwitch}\"");
            sb.AppendLine("    }");

            sb.AppendLine("  ]");
            sb.AppendLine("}");

            File.WriteAllText(cppPropsPath, sb.ToString());
        }

        private static string EscapeJsonString(string s)
        {
            return s.Replace("\\", "\\\\").Replace("\"", "\\\"");
        }

        /// <summary>
        /// Disables VS CMake integration for cforge projects by creating CMakeSettings.json
        /// with a configuration that indicates cforge handles the build.
        /// </summary>
        private static void DisableCMakeIntegration(string projectDir)
        {
            // Create CMakeSettings.json that tells VS not to auto-configure
            string cmakeSettingsPath = Path.Combine(projectDir, "CMakeSettings.json");

            // Only create if it doesn't exist - don't overwrite user's settings
            if (File.Exists(cmakeSettingsPath))
            {
                return;
            }

            // CMakeSettings.json with empty configuration tells VS not to use CMake automatically
            var sb = new StringBuilder();
            sb.AppendLine("{");
            sb.AppendLine("  \"_comment\": \"This file is generated by cforge VS extension to disable automatic CMake configuration. cforge handles the build system.\",");
            sb.AppendLine("  \"configurations\": [");
            sb.AppendLine("    {");
            sb.AppendLine("      \"name\": \"cforge-managed\",");
            sb.AppendLine("      \"generator\": \"Ninja\",");
            sb.AppendLine("      \"configurationType\": \"Debug\",");
            sb.AppendLine("      \"buildRoot\": \"${projectDir}\\\\build\",");
            sb.AppendLine("      \"installRoot\": \"${projectDir}\\\\install\",");
            sb.AppendLine("      \"cmakeCommandArgs\": \"\",");
            sb.AppendLine("      \"buildCommandArgs\": \"\",");
            sb.AppendLine("      \"ctestCommandArgs\": \"\",");
            sb.AppendLine("      \"inheritEnvironments\": [ \"msvc_x64_x64\" ],");
            sb.AppendLine("      \"variables\": []");
            sb.AppendLine("    }");
            sb.AppendLine("  ]");
            sb.AppendLine("}");

            try
            {
                File.WriteAllText(cmakeSettingsPath, sb.ToString());
            }
            catch
            {
                // Ignore write errors - not critical
            }

            // Also create .vs/ProjectSettings.json to set Open Folder view mode
            string vsDir = Path.Combine(projectDir, ".vs");
            string projectSettingsPath = Path.Combine(vsDir, "ProjectSettings.json");

            if (!File.Exists(projectSettingsPath))
            {
                var psb = new StringBuilder();
                psb.AppendLine("{");
                psb.AppendLine("  \"CurrentProjectSetting\": \"cforge-managed\"");
                psb.AppendLine("}");

                try
                {
                    if (!Directory.Exists(vsDir))
                    {
                        Directory.CreateDirectory(vsDir);
                    }
                    File.WriteAllText(projectSettingsPath, psb.ToString());
                }
                catch
                {
                    // Ignore write errors - not critical
                }
            }
        }
    }
}
