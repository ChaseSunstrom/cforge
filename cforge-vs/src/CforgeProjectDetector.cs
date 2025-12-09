using System;
using System.IO;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    public static class CforgeProjectDetector
    {
        private static bool _isCforgeProject = false;
        public static bool IsCforgeProject => _isCforgeProject;

        public static async Task InitializeAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Check on initialization
            await DetectCforgeProjectAsync();

            // Listen for solution events
            VS.Events.SolutionEvents.OnAfterOpenSolution += OnSolutionOpened;
            VS.Events.SolutionEvents.OnAfterCloseSolution += OnSolutionClosed;
            VS.Events.SolutionEvents.OnAfterOpenFolder += OnFolderOpened;
        }

        private static void OnSolutionOpened(Solution? solution)
        {
            _ = DetectCforgeProjectAsync();
        }

        private static void OnSolutionClosed()
        {
            _isCforgeProject = false;
        }

        private static void OnFolderOpened(string? folderPath)
        {
            _ = DetectCforgeProjectAsync();
        }

        private static async Task DetectCforgeProjectAsync()
        {
            string? projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            _isCforgeProject = CforgeRunner.IsCforgeProject(projectDir);

            if (_isCforgeProject)
            {
                var pane = await CforgeRunner.GetOutputPaneAsync();
                await pane.WriteLineAsync($"cforge project detected: {projectDir}");
            }
        }
    }
}
