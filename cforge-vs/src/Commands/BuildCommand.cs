using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    // ==================== BUILD COMMANDS ====================

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.BuildCommandId)]
    internal sealed class BuildCommand : BaseCommand<BuildCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("build");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.BuildReleaseCommandId)]
    internal sealed class BuildReleaseCommand : BaseCommand<BuildReleaseCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("build -c Release");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.RebuildCommandId)]
    internal sealed class RebuildCommand : BaseCommand<RebuildCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("clean");
            await CforgeRunner.RunAsync("build");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CleanCommandId)]
    internal sealed class CleanCommand : BaseCommand<CleanCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("clean");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.RunCommandId)]
    internal sealed class RunCommand : BaseCommand<RunCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.BuildAndRunInConsoleAsync();
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DebugCommandId)]
    internal sealed class DebugCommand : BaseCommand<DebugCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.BuildAndDebugAsync();
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.TestCommandId)]
    internal sealed class TestCommand : BaseCommand<TestCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("test");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.BenchCommandId)]
    internal sealed class BenchCommand : BaseCommand<BenchCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("bench");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.WatchCommandId)]
    internal sealed class WatchCommand : BaseCommand<WatchCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunWatchAsync();
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.StopWatchCommandId)]
    internal sealed class StopWatchCommand : BaseCommand<StopWatchCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            CforgeRunner.StopWatch();
        }
    }

    // ==================== TOOL COMMANDS ====================

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.FormatCommandId)]
    internal sealed class FormatCommand : BaseCommand<FormatCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("fmt");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.LintCommandId)]
    internal sealed class LintCommand : BaseCommand<LintCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("lint");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DocCommandId)]
    internal sealed class DocCommand : BaseCommand<DocCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("doc");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DoctorCommandId)]
    internal sealed class DoctorCommand : BaseCommand<DoctorCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("doctor");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.ProjectInfoCommandId)]
    internal sealed class ProjectInfoCommand : BaseCommand<ProjectInfoCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("info");
        }
    }

    // ==================== PROJECT/TEMPLATE COMMANDS ====================

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.InitProjectCommandId)]
    internal sealed class InitProjectCommand : BaseCommand<InitProjectCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? projectName = InputDialog.Show(
                "Initialize cforge Project",
                "Enter project name:");

            if (!string.IsNullOrWhiteSpace(projectName))
            {
                await CforgeRunner.RunAsync($"init {projectName}");
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.NewClassCommandId)]
    internal sealed class NewClassCommand : BaseCommand<NewClassCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? className = InputDialog.Show(
                "New Class",
                "Enter class name (e.g., MyClass, utils/Helper):");

            if (!string.IsNullOrWhiteSpace(className))
            {
                await CforgeRunner.RunAsync($"new class {className}");
                await RefreshProjectAsync();
            }
        }

        private async Task RefreshProjectAsync()
        {
            // Regenerate vcxproj to include new files
            string? projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            if (!string.IsNullOrEmpty(projectDir))
            {
                await VcxprojGenerator.GenerateProjectFilesAsync(projectDir);
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.NewHeaderCommandId)]
    internal sealed class NewHeaderCommand : BaseCommand<NewHeaderCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? headerName = InputDialog.Show(
                "New Header",
                "Enter header name (e.g., MyHeader, utils/constants):");

            if (!string.IsNullOrWhiteSpace(headerName))
            {
                await CforgeRunner.RunAsync($"new header {headerName}");
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.NewTestCommandId)]
    internal sealed class NewTestCommand : BaseCommand<NewTestCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? testName = InputDialog.Show(
                "New Test",
                "Enter test name (e.g., MyClassTests):");

            if (!string.IsNullOrWhiteSpace(testName))
            {
                await CforgeRunner.RunAsync($"new test {testName}");
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.PackageProjectCommandId)]
    internal sealed class PackageProjectCommand : BaseCommand<PackageProjectCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("package");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.InstallProjectCommandId)]
    internal sealed class InstallProjectCommand : BaseCommand<InstallProjectCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("install");
        }
    }

    // ==================== SETTINGS COMMANDS ====================

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.OpenCforgeTomlCommandId)]
    internal sealed class OpenCforgeTomlCommand : BaseCommand<OpenCforgeTomlCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            string? projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            if (!string.IsNullOrEmpty(projectDir))
            {
                string tomlPath = Path.Combine(projectDir, "cforge.toml");
                if (File.Exists(tomlPath))
                {
                    await VS.Documents.OpenAsync(tomlPath);
                }
                else
                {
                    await VS.MessageBox.ShowErrorAsync("cforge", "No cforge.toml found in the current project.");
                }
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.ConfigureToolsCommandId)]
    internal sealed class ConfigureToolsCommand : BaseCommand<ConfigureToolsCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("tools");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.OpenSettingsCommandId)]
    internal sealed class OpenSettingsCommand : BaseCommand<OpenSettingsCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            // Open VS Options to our extension settings page
            await VS.Settings.OpenAsync("Tools.Options.Cforge");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.OpenProjectPropertiesCommandId)]
    internal sealed class OpenProjectPropertiesCommand : BaseCommand<OpenProjectPropertiesCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ProjectPropertiesWindow.ShowAsync();
        }
    }

    // ==================== TOOLBAR COMMANDS ====================

    [Command(PackageGuids.CforgeToolbarGuidString, PackageIds.ToolbarBuildCommandId)]
    internal sealed class ToolbarBuildCommand : BaseCommand<ToolbarBuildCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("build");
        }
    }

    [Command(PackageGuids.CforgeToolbarGuidString, PackageIds.ToolbarRunCommandId)]
    internal sealed class ToolbarRunCommand : BaseCommand<ToolbarRunCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.BuildAndRunInConsoleAsync();
        }
    }

    [Command(PackageGuids.CforgeToolbarGuidString, PackageIds.ToolbarDebugCommandId)]
    internal sealed class ToolbarDebugCommand : BaseCommand<ToolbarDebugCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.BuildAndDebugAsync();
        }
    }

    [Command(PackageGuids.CforgeToolbarGuidString, PackageIds.ToolbarTestCommandId)]
    internal sealed class ToolbarTestCommand : BaseCommand<ToolbarTestCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("test");
        }
    }

    [Command(PackageGuids.CforgeToolbarGuidString, PackageIds.ToolbarCleanCommandId)]
    internal sealed class ToolbarCleanCommand : BaseCommand<ToolbarCleanCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("clean");
        }
    }

    [Command(PackageGuids.CforgeToolbarGuidString, PackageIds.ToolbarStopCommandId)]
    internal sealed class ToolbarStopCommand : BaseCommand<ToolbarStopCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            CforgeRunner.StopCurrentProcess();
        }
    }

    // ==================== TOOL WINDOW COMMANDS ====================

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.OpenCforgeExplorerCommandId)]
    internal sealed class OpenCforgeExplorerCommand : BaseCommand<OpenCforgeExplorerCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            try
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                var pane = await CforgeExplorerWindow.ShowAsync();
                if (pane == null)
                {
                    await VS.MessageBox.ShowErrorAsync("cforge Explorer", "Failed to create tool window pane");
                    return;
                }
                if (pane.Frame is Microsoft.VisualStudio.Shell.Interop.IVsWindowFrame frame)
                {
                    frame.Show();
                }
                else
                {
                    await VS.MessageBox.ShowErrorAsync("cforge Explorer", "Tool window has no frame");
                }
            }
            catch (Exception ex)
            {
                await VS.MessageBox.ShowErrorAsync("cforge Explorer", $"Error: {ex.Message}");
            }
        }
    }

    // ==================== CLEANUP COMMANDS ====================

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CleanGeneratedFilesCommandId)]
    internal sealed class CleanGeneratedFilesCommand : BaseCommand<CleanGeneratedFilesCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            var pane = await CforgeRunner.GetOutputPaneAsync();
            await pane.ActivateAsync();

            string? projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
            {
                await pane.WriteLineAsync("Error: No cforge project found.");
                return;
            }

            await pane.WriteLineAsync("[cforge] Cleaning generated VS files...");

            try
            {
                // Clean the generated vcxproj/sln files
                VcxprojGenerator.CleanupGeneratedFiles(projectDir);

                // Also clean the .vs folder config files we generated
                string vsDir = Path.Combine(projectDir, ".vs");
                string[] filesToDelete = new[]
                {
                    Path.Combine(vsDir, "tasks.vs.json"),
                    Path.Combine(vsDir, "launch.vs.json"),
                    Path.Combine(vsDir, "CppProperties.json"),
                    Path.Combine(vsDir, "cforge.runsettings"),
                    Path.Combine(vsDir, "testsettings.json"),
                    Path.Combine(vsDir, "ProjectSettings.json"),
                    Path.Combine(projectDir, "CMakeSettings.json")
                };

                int cleanedCount = 0;
                foreach (var file in filesToDelete)
                {
                    if (File.Exists(file))
                    {
                        File.Delete(file);
                        cleanedCount++;
                        await pane.WriteLineAsync($"  Deleted: {Path.GetFileName(file)}");
                    }
                }

                string tempDir = VcxprojGenerator.GetTempProjectDir(projectDir);
                if (!Directory.Exists(tempDir))
                {
                    await pane.WriteLineAsync($"  Deleted: Generated solution directory");
                    cleanedCount++;
                }

                await pane.WriteLineAsync($"[cforge] Cleanup complete. Removed {cleanedCount} generated file(s).");
                await pane.WriteLineAsync($"  Temp directory location: {Path.GetDirectoryName(tempDir)}");
            }
            catch (Exception ex)
            {
                await pane.WriteLineAsync($"Error cleaning files: {ex.Message}");
            }
        }
    }
}
