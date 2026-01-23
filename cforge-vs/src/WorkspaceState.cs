using System;
using System.IO;
using System.Threading.Tasks;
using Microsoft.VisualStudio.Shell;
using Newtonsoft.Json;

namespace CforgeVS
{
    /// <summary>
    /// Singleton that tracks active workspace state and the selected member project.
    /// Persists state to .vs/cforge-state.json.
    /// </summary>
    public sealed class WorkspaceState
    {
        private static readonly Lazy<WorkspaceState> _instance = new Lazy<WorkspaceState>(() => new WorkspaceState());
        public static WorkspaceState Instance => _instance.Value;

        private string? _workspaceDir;
        private string? _activeMemberPath;
        private string? _activeMemberName;
        private bool _isWorkspace;
        private CforgeWorkspace? _workspace;

        /// <summary>
        /// Event fired when the active project changes.
        /// </summary>
        public event EventHandler<ActiveProjectChangedEventArgs>? ActiveProjectChanged;

        /// <summary>
        /// Event fired when the workspace is reloaded.
        /// </summary>
        public event EventHandler<WorkspaceReloadedEventArgs>? WorkspaceReloaded;

        private WorkspaceState() { }

        /// <summary>
        /// Gets whether the current directory is a workspace.
        /// </summary>
        public bool IsWorkspace => _isWorkspace;

        /// <summary>
        /// Gets the workspace directory path.
        /// </summary>
        public string? WorkspaceDir => _workspaceDir;

        /// <summary>
        /// Gets the parsed workspace configuration.
        /// </summary>
        public CforgeWorkspace? Workspace => _workspace;

        /// <summary>
        /// Gets the active member path (relative to workspace root).
        /// </summary>
        public string? ActiveMemberPath => _activeMemberPath;

        /// <summary>
        /// Gets the active member name.
        /// </summary>
        public string? ActiveMemberName => _activeMemberName;

        /// <summary>
        /// Gets the full path to the active member directory.
        /// </summary>
        public string? ActiveMemberFullPath
        {
            get
            {
                if (string.IsNullOrEmpty(_workspaceDir) || string.IsNullOrEmpty(_activeMemberPath))
                    return null;
                return Path.Combine(_workspaceDir, _activeMemberPath);
            }
        }

        /// <summary>
        /// Initializes or refreshes workspace state for the given directory.
        /// </summary>
        public async Task RefreshAsync(string? projectDir = null)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            projectDir ??= await CforgeRunner.GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
            {
                Clear();
                return;
            }

            _workspaceDir = projectDir;
            _isWorkspace = CforgeTomlParser.IsWorkspace(projectDir);

            if (_isWorkspace)
            {
                _workspace = CforgeTomlParser.ParseWorkspace(projectDir);
                await LoadStateAsync();

                // If no active member, set default from main_project or first member
                if (string.IsNullOrEmpty(_activeMemberPath) && _workspace != null)
                {
                    SetDefaultActiveMember();
                }
            }
            else
            {
                _workspace = null;
                _activeMemberPath = null;
                _activeMemberName = null;
            }

            OnWorkspaceReloaded();
        }

        /// <summary>
        /// Sets the active member project.
        /// </summary>
        public async Task SetActiveMemberAsync(string memberPath, string? memberName = null)
        {
            if (_activeMemberPath == memberPath)
                return;

            string? oldPath = _activeMemberPath;
            _activeMemberPath = memberPath;
            _activeMemberName = memberName ?? Path.GetFileName(memberPath);

            await SaveStateAsync();
            OnActiveProjectChanged(oldPath, memberPath);
        }

        /// <summary>
        /// Gets the project to build/run. Returns member path for workspace, null for single project.
        /// </summary>
        public string? GetBuildTargetPath()
        {
            if (!_isWorkspace)
                return null;

            return _activeMemberPath;
        }

        /// <summary>
        /// Gets the full directory path for the build target.
        /// For workspace: returns active member directory.
        /// For single project: returns project directory.
        /// </summary>
        public string? GetBuildTargetDir()
        {
            if (!_isWorkspace)
                return _workspaceDir;

            if (string.IsNullOrEmpty(_workspaceDir) || string.IsNullOrEmpty(_activeMemberPath))
                return _workspaceDir;

            return Path.Combine(_workspaceDir, _activeMemberPath);
        }

        /// <summary>
        /// Finds the executable for the active project.
        /// </summary>
        public string? GetActiveProjectExecutable(string configuration = "Debug")
        {
            string? targetDir = GetBuildTargetDir();
            if (string.IsNullOrEmpty(targetDir))
                return null;

            var project = CforgeTomlParser.ParseProject(targetDir);
            if (project == null || project.Type != "executable")
                return null;

            return CforgeTomlParser.FindExecutable(targetDir, project, configuration);
        }

        /// <summary>
        /// Clears all workspace state.
        /// </summary>
        public void Clear()
        {
            _workspaceDir = null;
            _activeMemberPath = null;
            _activeMemberName = null;
            _isWorkspace = false;
            _workspace = null;
        }

        private void SetDefaultActiveMember()
        {
            if (_workspace == null) return;

            // First check for startup project
            var startupProject = _workspace.Projects.Find(p => p.IsStartup);
            if (startupProject != null)
            {
                _activeMemberPath = startupProject.Path;
                _activeMemberName = startupProject.Name;
                return;
            }

            // Check main_project setting
            if (!string.IsNullOrEmpty(_workspace.MainProject))
            {
                _activeMemberPath = _workspace.MainProject;
                _activeMemberName = _workspace.MainProject;
                return;
            }

            // Fall back to first member
            if (_workspace.Members.Count > 0)
            {
                _activeMemberPath = _workspace.Members[0];
                _activeMemberName = Path.GetFileName(_workspace.Members[0]);
            }
        }

        private async Task LoadStateAsync()
        {
            if (string.IsNullOrEmpty(_workspaceDir))
                return;

            string stateFile = GetStateFilePath();
            if (!File.Exists(stateFile))
                return;

            try
            {
                string json = await Task.Run(() => File.ReadAllText(stateFile));
                var state = JsonConvert.DeserializeObject<WorkspaceStateData>(json);

                if (state != null)
                {
                    _activeMemberPath = state.ActiveMemberPath;
                    _activeMemberName = state.ActiveMemberName;
                }
            }
            catch
            {
                // Ignore errors reading state file
            }
        }

        private async Task SaveStateAsync()
        {
            if (string.IsNullOrEmpty(_workspaceDir))
                return;

            string stateFile = GetStateFilePath();

            try
            {
                string? vsDir = Path.GetDirectoryName(stateFile);
                if (!string.IsNullOrEmpty(vsDir) && !Directory.Exists(vsDir))
                {
                    Directory.CreateDirectory(vsDir);
                }

                var state = new WorkspaceStateData
                {
                    ActiveMemberPath = _activeMemberPath,
                    ActiveMemberName = _activeMemberName
                };

                string json = JsonConvert.SerializeObject(state, Formatting.Indented);
                await Task.Run(() => File.WriteAllText(stateFile, json));
            }
            catch
            {
                // Ignore errors writing state file
            }
        }

        private string GetStateFilePath()
        {
            return Path.Combine(_workspaceDir!, ".vs", "cforge-state.json");
        }

        private void OnActiveProjectChanged(string? oldPath, string? newPath)
        {
            ActiveProjectChanged?.Invoke(this, new ActiveProjectChangedEventArgs(oldPath, newPath, _activeMemberName));
        }

        private void OnWorkspaceReloaded()
        {
            WorkspaceReloaded?.Invoke(this, new WorkspaceReloadedEventArgs(_isWorkspace, _workspace));
        }

        private class WorkspaceStateData
        {
            public string? ActiveMemberPath { get; set; }
            public string? ActiveMemberName { get; set; }
        }
    }

    public class ActiveProjectChangedEventArgs : EventArgs
    {
        public string? OldMemberPath { get; }
        public string? NewMemberPath { get; }
        public string? NewMemberName { get; }

        public ActiveProjectChangedEventArgs(string? oldPath, string? newPath, string? newName)
        {
            OldMemberPath = oldPath;
            NewMemberPath = newPath;
            NewMemberName = newName;
        }
    }

    public class WorkspaceReloadedEventArgs : EventArgs
    {
        public bool IsWorkspace { get; }
        public CforgeWorkspace? Workspace { get; }

        public WorkspaceReloadedEventArgs(bool isWorkspace, CforgeWorkspace? workspace)
        {
            IsWorkspace = isWorkspace;
            Workspace = workspace;
        }
    }
}
