using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using EnvDTE;
using EnvDTE80;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CforgeVS
{
    /// <summary>
    /// Intercepts VS build/run/debug commands and redirects them to cforge.
    /// </summary>
    public class BuildInterceptor : IVsUpdateSolutionEvents2, IVsUpdateSolutionEvents3
    {
        private static BuildInterceptor? _instance;
        private IVsSolutionBuildManager2? _buildManager;
        private uint _cookie;
        private DTE2? _dte;
        private CommandEvents? _buildCommandEvents;
        private CommandEvents? _debugCommandEvents;
        private CommandEvents? _runCommandEvents;

        // Flags to prevent re-entry and loops
        private static bool _isBuilding = false;
        private static bool _bypassInterception = false;

        public static async Task InitializeAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            _instance = new BuildInterceptor();
            await _instance.RegisterAsync();
        }

        private async Task RegisterAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Get build manager for solution events
            _buildManager = await VS.Services.GetSolutionBuildManagerAsync() as IVsSolutionBuildManager2;
            if (_buildManager != null)
            {
                _buildManager.AdviseUpdateSolutionEvents(this, out _cookie);
            }

            // Get DTE for command interception
            _dte = await VS.GetServiceAsync<DTE, DTE2>();
            if (_dte != null)
            {
                // Hook Debug.Start (F5)
                _debugCommandEvents = _dte.Events.CommandEvents["{5EFC7975-14BC-11CF-9B2B-00AA00573819}", 295];
                _debugCommandEvents.BeforeExecute += OnBeforeDebugStart;

                // Hook Debug.StartWithoutDebugging (Ctrl+F5)
                _runCommandEvents = _dte.Events.CommandEvents["{5EFC7975-14BC-11CF-9B2B-00AA00573819}", 368];
                _runCommandEvents.BeforeExecute += OnBeforeStartWithoutDebugging;
            }
        }

        private void OnBeforeDebugStart(string Guid, int ID, object CustomIn, object CustomOut, ref bool CancelDefault)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            // Skip if we're bypassing interception (e.g., launching debugger after build)
            if (_bypassInterception || _isBuilding)
            {
                return;
            }

            string? projectDir = GetActiveProjectDirectory();
            if (projectDir != null && CforgeRunner.IsCforgeProject(projectDir))
            {
                // Cancel default behavior
                CancelDefault = true;

                // Run cforge build then debug
                _ = BuildAndDebugAsync(projectDir);
            }
        }

        private void OnBeforeStartWithoutDebugging(string Guid, int ID, object CustomIn, object CustomOut, ref bool CancelDefault)
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            // Skip if we're already building
            if (_isBuilding)
            {
                return;
            }

            string? projectDir = GetActiveProjectDirectory();
            if (projectDir != null && CforgeRunner.IsCforgeProject(projectDir))
            {
                // Cancel default behavior
                CancelDefault = true;

                // Determine configuration
                string config = "Debug";
                if (_dte?.Solution?.SolutionBuild != null)
                {
                    var activeConfig = _dte.Solution.SolutionBuild.ActiveConfiguration;
                    if (activeConfig?.Name?.Contains("Release") == true)
                    {
                        config = "Release";
                    }
                }

                // Build and run in a console window (like regular VS behavior)
                _ = BuildAndRunAsync(projectDir, config);
            }
        }

        private async Task BuildAndRunAsync(string projectDir, string config)
        {
            if (_isBuilding) return;

            _isBuilding = true;
            try
            {
                await CforgeRunner.BuildAndRunInConsoleAsync(projectDir, config);
            }
            finally
            {
                _isBuilding = false;
            }
        }

        private async Task BuildAndDebugAsync(string projectDir)
        {
            if (_isBuilding) return;

            _isBuilding = true;
            try
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

                var pane = await CforgeRunner.GetOutputPaneAsync();
                await pane.WriteLineAsync("[cforge] Building before debug...");

                // Determine configuration
                string config = "Debug";
                if (_dte?.Solution?.SolutionBuild != null)
                {
                    var activeConfig = _dte.Solution.SolutionBuild.ActiveConfiguration;
                    if (activeConfig?.Name?.Contains("Release") == true)
                    {
                        config = "Release";
                    }
                }

                string args = config == "Release" ? "build -c Release" : "build";
                bool success = await CforgeRunner.RunAsync(args, projectDir);

                if (success)
                {
                    await pane.WriteLineAsync("[cforge] Build succeeded, starting...");

                    // Find the executable
                    var project = CforgeTomlParser.ParseProject(projectDir);
                    if (project != null && project.Type == "executable")
                    {
                        string exePath = Path.Combine(projectDir, project.OutputDir, config, project.Name + ".exe");

                        if (File.Exists(exePath))
                        {
                            // Start the executable in Windows Terminal
                            await CforgeRunner.RunExecutableInConsoleAsync(exePath, projectDir);
                        }
                        else
                        {
                            await pane.WriteLineAsync($"[cforge] Error: Could not find executable at {exePath}");
                            await pane.WriteLineAsync("[cforge] Make sure cforge build completed successfully.");
                        }
                    }
                    else
                    {
                        await pane.WriteLineAsync("[cforge] Warning: Project is not an executable, cannot run.");
                    }
                }
                else
                {
                    await pane.WriteLineAsync("[cforge] Build failed.");
                }
            }
            finally
            {
                _isBuilding = false;
            }
        }

        private string? GetActiveProjectDirectory()
        {
            ThreadHelper.ThrowIfNotOnUIThread();

            try
            {
                // First check if current solution is a generated one (in temp dir)
                if (_dte?.Solution?.FullName != null)
                {
                    string? sourceDir = VcxprojGenerator.GetSourceDirForCurrentSolution(_dte.Solution.FullName);
                    if (!string.IsNullOrEmpty(sourceDir) && CforgeRunner.IsCforgeProject(sourceDir))
                    {
                        return sourceDir;
                    }
                }

                if (_dte?.ActiveDocument?.Path != null)
                {
                    string? docDir = _dte.ActiveDocument.Path;
                    while (!string.IsNullOrEmpty(docDir))
                    {
                        if (File.Exists(Path.Combine(docDir, "cforge.toml")) ||
                            File.Exists(Path.Combine(docDir, "cforge.workspace.toml")))
                        {
                            return docDir;
                        }
                        docDir = Path.GetDirectoryName(docDir);
                    }
                }

                // Try solution directory
                if (_dte?.Solution?.FullName != null)
                {
                    string? slnDir = Path.GetDirectoryName(_dte.Solution.FullName);
                    if (slnDir != null && CforgeRunner.IsCforgeProject(slnDir))
                    {
                        return slnDir;
                    }
                }

                // Try startup project
                if (_dte?.Solution?.SolutionBuild?.StartupProjects != null)
                {
                    var startupProjects = (Array)_dte.Solution.SolutionBuild.StartupProjects;
                    if (startupProjects.Length > 0)
                    {
                        string? projectName = startupProjects.GetValue(0) as string;
                        if (projectName != null)
                        {
                            foreach (EnvDTE.Project proj in _dte.Solution.Projects)
                            {
                                if (proj.UniqueName == projectName && proj.FullName != null)
                                {
                                    string? projDir = Path.GetDirectoryName(proj.FullName);
                                    if (projDir != null && CforgeRunner.IsCforgeProject(projDir))
                                    {
                                        return projDir;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            catch { }

            return null;
        }

        public void Dispose()
        {
            ThreadHelper.ThrowIfNotOnUIThread();
            if (_buildManager != null && _cookie != 0)
            {
                _buildManager.UnadviseUpdateSolutionEvents(_cookie);
            }
        }

        // IVsUpdateSolutionEvents2 implementation
        public int UpdateSolution_Begin(ref int pfCancelUpdate)
        {
            return VSConstants.S_OK;
        }

        public int UpdateSolution_Done(int fSucceeded, int fModified, int fCancelCommand)
        {
            return VSConstants.S_OK;
        }

        public int UpdateSolution_StartUpdate(ref int pfCancelUpdate)
        {
            return VSConstants.S_OK;
        }

        public int UpdateSolution_Cancel()
        {
            return VSConstants.S_OK;
        }

        public int OnActiveProjectCfgChange(IVsHierarchy pIVsHierarchy)
        {
            return VSConstants.S_OK;
        }

        public int UpdateProjectCfg_Begin(IVsHierarchy pHierProj, IVsCfg pCfgProj, IVsCfg pCfgSln, uint dwAction, ref int pfCancel)
        {
            // The vcxproj already has cforge targets, so we don't need to intercept here
            return VSConstants.S_OK;
        }

        public int UpdateProjectCfg_Done(IVsHierarchy pHierProj, IVsCfg pCfgProj, IVsCfg pCfgSln, uint dwAction, int fSuccess, int fCancel)
        {
            return VSConstants.S_OK;
        }

        // IVsUpdateSolutionEvents3 implementation
        public int OnBeforeActiveSolutionCfgChange(IVsCfg pOldActiveSlnCfg, IVsCfg pNewActiveSlnCfg)
        {
            return VSConstants.S_OK;
        }

        public int OnAfterActiveSolutionCfgChange(IVsCfg pOldActiveSlnCfg, IVsCfg pNewActiveSlnCfg)
        {
            return VSConstants.S_OK;
        }
    }
}
