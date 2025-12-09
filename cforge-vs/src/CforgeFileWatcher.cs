using System;
using System.IO;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    /// <summary>
    /// Watches for changes to cforge.toml files and triggers project updates.
    /// </summary>
    public sealed class CforgeFileWatcher : IDisposable
    {
        private static CforgeFileWatcher? _instance;
        private FileSystemWatcher? _watcher;
        private string? _watchedDirectory;
        private DateTime _lastChange = DateTime.MinValue;
        private readonly object _lockObject = new object();

        public static CforgeFileWatcher Instance => _instance ??= new CforgeFileWatcher();

        public static async Task InitializeAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Subscribe to solution events
            VS.Events.SolutionEvents.OnAfterOpenSolution += OnSolutionOpened;
            VS.Events.SolutionEvents.OnAfterCloseSolution += OnSolutionClosed;
            VS.Events.SolutionEvents.OnAfterOpenFolder += OnFolderOpened;

            // Check if a solution/folder is already open
            var solution = await VS.Solutions.GetCurrentSolutionAsync();
            if (solution != null && !string.IsNullOrEmpty(solution.FullPath))
            {
                string? dir = Path.GetDirectoryName(solution.FullPath);
                if (!string.IsNullOrEmpty(dir))
                {
                    Instance.StartWatching(dir);
                }
            }
        }

        private static void OnSolutionOpened(Solution? solution)
        {
            if (solution?.FullPath != null)
            {
                string? dir = Path.GetDirectoryName(solution.FullPath);
                if (!string.IsNullOrEmpty(dir))
                {
                    Instance.StartWatching(dir);
                }
            }
        }

        private static void OnSolutionClosed()
        {
            Instance.StopWatching();
        }

        private static void OnFolderOpened(string? folder)
        {
            if (!string.IsNullOrEmpty(folder))
            {
                Instance.StartWatching(folder);
            }
        }

        public void StartWatching(string directory)
        {
            StopWatching();

            if (!Directory.Exists(directory))
                return;

            _watchedDirectory = directory;
            _watcher = new FileSystemWatcher(directory)
            {
                Filter = "*.toml",
                NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.FileName | NotifyFilters.CreationTime,
                IncludeSubdirectories = true,
                EnableRaisingEvents = true
            };

            _watcher.Changed += OnFileChanged;
            _watcher.Created += OnFileChanged;
            _watcher.Deleted += OnFileChanged;
        }

        public void StopWatching()
        {
            if (_watcher != null)
            {
                _watcher.EnableRaisingEvents = false;
                _watcher.Changed -= OnFileChanged;
                _watcher.Created -= OnFileChanged;
                _watcher.Deleted -= OnFileChanged;
                _watcher.Dispose();
                _watcher = null;
            }
            _watchedDirectory = null;
        }

        private void OnFileChanged(object sender, FileSystemEventArgs e)
        {
            // Only care about cforge.toml and cforge.workspace.toml
            string fileName = Path.GetFileName(e.FullPath);
            if (!fileName.Equals("cforge.toml", StringComparison.OrdinalIgnoreCase) &&
                !fileName.Equals("cforge.workspace.toml", StringComparison.OrdinalIgnoreCase))
            {
                return;
            }

            // Debounce - ignore changes within 1 second
            lock (_lockObject)
            {
                if ((DateTime.Now - _lastChange).TotalSeconds < 1)
                    return;
                _lastChange = DateTime.Now;
            }

            // Handle the change on the UI thread
            _ = HandleFileChangedAsync(e.FullPath, e.ChangeType);
        }

        private async Task HandleFileChangedAsync(string filePath, WatcherChangeTypes changeType)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var pane = await CforgeRunner.GetOutputPaneAsync();
            string fileName = Path.GetFileName(filePath);
            string? projectDir = Path.GetDirectoryName(filePath);

            if (string.IsNullOrEmpty(projectDir))
                return;

            switch (changeType)
            {
                case WatcherChangeTypes.Changed:
                    await pane.WriteLineAsync($"[cforge] {fileName} was modified. Regenerating VS configuration...");
                    await RegenerateVsConfigAsync(projectDir);
                    break;

                case WatcherChangeTypes.Created:
                    await pane.WriteLineAsync($"[cforge] {fileName} was created. Generating VS configuration...");
                    await RegenerateVsConfigAsync(projectDir);
                    break;

                case WatcherChangeTypes.Deleted:
                    await pane.WriteLineAsync($"[cforge] {fileName} was deleted.");
                    break;
            }
        }

        private async Task RegenerateVsConfigAsync(string projectDir)
        {
            // Re-parse cforge.toml and regenerate all VS config files
            var project = CforgeTomlParser.ParseProject(projectDir);
            var workspace = CforgeTomlParser.ParseWorkspace(projectDir);

            if (project == null && workspace == null)
            {
                var pane = await CforgeRunner.GetOutputPaneAsync();
                await pane.WriteLineAsync("[cforge] Warning: Could not parse cforge.toml");
                return;
            }

            // Use OpenFolderSupport to regenerate config files
            await OpenFolderSupport.EnsureVsConfigFilesAsync(projectDir);

            await VS.StatusBar.ShowMessageAsync("cforge.toml updated - VS configuration regenerated");
        }

        public void Dispose()
        {
            StopWatching();
        }
    }
}
