using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Imaging.Interop;
using Microsoft.VisualStudio.Shell;
using Tomlyn;
using Tomlyn.Model;

namespace CforgeVS
{
    public partial class DependencyToolWindowControl : UserControl
    {
        public ObservableCollection<DependencyItem> Dependencies { get; } = new ObservableCollection<DependencyItem>();
        public ObservableCollection<AvailablePackage> AvailablePackages { get; } = new ObservableCollection<AvailablePackage>();
        public ObservableCollection<OutdatedPackage> OutdatedPackages { get; } = new ObservableCollection<OutdatedPackage>();

        private string? _registryPath;
        private DependencyItem? _selectedDependency;

        public DependencyToolWindowControl()
        {
            InitializeComponent();
            DependencyTree.ItemsSource = Dependencies;
            AvailablePackagesList.ItemsSource = AvailablePackages;
            OutdatedPackagesList.ItemsSource = OutdatedPackages;

            // Find registry path
            string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            _registryPath = Path.Combine(localAppData, "cforge", "registry", "cforge-index", "packages");

            _ = RefreshDependenciesAsync();
        }

        #region Refresh Dependencies

        public async Task RefreshDependenciesAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            SetStatus("Loading dependencies...", true);
            Dependencies.Clear();

            string? projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
            {
                SetStatus("No cforge project found");
                return;
            }

            string tomlPath = Path.Combine(projectDir, "cforge.toml");
            if (!File.Exists(tomlPath))
            {
                SetStatus("cforge.toml not found");
                return;
            }

            try
            {
                string content = File.ReadAllText(tomlPath);
                var doc = Toml.ToModel(content);

                // Parse dependencies
                if (doc.TryGetValue("dependencies", out var depsObj) && depsObj is TomlTable depsTable)
                {
                    var indexGroup = new DependencyItem { Name = "Index Packages", Type = "group" };
                    var gitGroup = new DependencyItem { Name = "Git Dependencies", Type = "group" };
                    var vcpkgGroup = new DependencyItem { Name = "vcpkg Packages", Type = "group" };
                    var systemGroup = new DependencyItem { Name = "System Libraries", Type = "group" };
                    var projectGroup = new DependencyItem { Name = "Local Projects", Type = "group" };

                    foreach (var kvp in depsTable)
                    {
                        if (kvp.Key == "fetch_content" || kvp.Key == "directory" || kvp.Key == "git")
                            continue;

                        string name = kvp.Key;
                        string version = "*";
                        string type = "index";

                        if (kvp.Value is string versionStr)
                        {
                            version = versionStr;
                        }
                        else if (kvp.Value is TomlTable depTable)
                        {
                            if (depTable.TryGetValue("version", out var v))
                                version = v?.ToString() ?? "*";

                            if (depTable.TryGetValue("git", out _))
                                type = "git";
                            else if (depTable.TryGetValue("vcpkg", out _))
                                type = "vcpkg";
                            else if (depTable.TryGetValue("system", out _))
                                type = "system";
                            else if (depTable.TryGetValue("project", out _) || depTable.TryGetValue("path", out _))
                                type = "project";
                        }

                        var item = new DependencyItem { Name = name, Version = version, Type = type };

                        switch (type)
                        {
                            case "git": gitGroup.Children.Add(item); break;
                            case "vcpkg": vcpkgGroup.Children.Add(item); break;
                            case "system": systemGroup.Children.Add(item); break;
                            case "project": projectGroup.Children.Add(item); break;
                            default: indexGroup.Children.Add(item); break;
                        }
                    }

                    // Parse old-style [dependencies.git] section
                    if (depsTable.TryGetValue("git", out var gitObj) && gitObj is TomlTable gitTable)
                    {
                        foreach (var kvp in gitTable)
                        {
                            string name = kvp.Key;
                            string version = "main";

                            if (kvp.Value is TomlTable depTable)
                            {
                                if (depTable.TryGetValue("tag", out var tag))
                                    version = tag?.ToString() ?? "main";
                                else if (depTable.TryGetValue("branch", out var branch))
                                    version = branch?.ToString() ?? "main";
                                else if (depTable.TryGetValue("commit", out var commit))
                                    version = commit?.ToString()?.Substring(0, Math.Min(7, commit.ToString()?.Length ?? 0)) ?? "HEAD";
                            }

                            gitGroup.Children.Add(new DependencyItem { Name = name, Version = version, Type = "git" });
                        }
                    }

                    // Add non-empty groups
                    if (indexGroup.Children.Count > 0) Dependencies.Add(indexGroup);
                    if (gitGroup.Children.Count > 0) Dependencies.Add(gitGroup);
                    if (vcpkgGroup.Children.Count > 0) Dependencies.Add(vcpkgGroup);
                    if (systemGroup.Children.Count > 0) Dependencies.Add(systemGroup);
                    if (projectGroup.Children.Count > 0) Dependencies.Add(projectGroup);
                }

                int totalDeps = Dependencies.Sum(g => g.Children.Count);
                SetStatus($"{totalDeps} dependencies loaded");
            }
            catch (Exception ex)
            {
                SetStatus($"Error: {ex.Message}");
            }
        }

        #endregion

        #region Available Packages

        private async Task LoadAvailablePackagesAsync()
        {
            await Task.Run(() =>
            {
                if (_registryPath == null || !Directory.Exists(_registryPath))
                    return;

                var packages = new List<AvailablePackage>();

                try
                {
                    // Scan registry directories (a-z)
                    foreach (var letterDir in Directory.GetDirectories(_registryPath))
                    {
                        foreach (var packageDir in Directory.GetDirectories(letterDir))
                        {
                            string packageName = Path.GetFileName(packageDir);
                            string? latestVersion = null;
                            string? description = null;

                            // Find latest version
                            var versionFiles = Directory.GetFiles(packageDir, "*.toml");
                            if (versionFiles.Length > 0)
                            {
                                // Get the last version file (alphabetically sorted, usually latest)
                                var latestFile = versionFiles.OrderByDescending(f => f).First();
                                latestVersion = Path.GetFileNameWithoutExtension(latestFile);

                                // Try to read description
                                try
                                {
                                    string content = File.ReadAllText(latestFile);
                                    var doc = Toml.ToModel(content);
                                    if (doc.TryGetValue("package", out var pkgObj) && pkgObj is TomlTable pkgTable)
                                    {
                                        if (pkgTable.TryGetValue("description", out var desc))
                                            description = desc?.ToString();
                                    }
                                }
                                catch { }
                            }

                            packages.Add(new AvailablePackage
                            {
                                Name = packageName,
                                LatestVersion = latestVersion ?? "unknown",
                                Description = description ?? "No description available"
                            });
                        }
                    }
                }
                catch { }

                // Update UI on main thread
                _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                {
                    await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                    AvailablePackages.Clear();
                    foreach (var pkg in packages.OrderBy(p => p.Name))
                    {
                        AvailablePackages.Add(pkg);
                    }
                    NoPackagesText.Visibility = AvailablePackages.Count > 0 ? Visibility.Collapsed : Visibility.Visible;
                });
            });
        }

        private async Task SearchPackagesAsync(string query)
        {
            if (string.IsNullOrWhiteSpace(query))
            {
                await LoadAvailablePackagesAsync();
                return;
            }

            SetStatus($"Searching for '{query}'...", true);

            // Use cforge deps search
            var result = await CforgeRunner.RunAndCaptureAsync($"deps search {query}");

            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            AvailablePackages.Clear();

            if (!string.IsNullOrWhiteSpace(result))
            {
                foreach (var line in result.Split('\n'))
                {
                    string trimmedLine = line.Trim();
                    if (string.IsNullOrEmpty(trimmedLine) ||
                        trimmedLine.StartsWith("Searching") ||
                        trimmedLine.StartsWith("Found") ||
                        trimmedLine.StartsWith("---"))
                        continue;

                    // Try to parse package info
                    var parts = trimmedLine.Split(new[] { " - ", ": " }, StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length >= 1)
                    {
                        var namePart = parts[0].Trim().Split('@', ' ');
                        AvailablePackages.Add(new AvailablePackage
                        {
                            Name = namePart[0].Trim(),
                            LatestVersion = namePart.Length > 1 ? namePart[1].Trim() : "latest",
                            Description = parts.Length > 1 ? string.Join(" - ", parts.Skip(1)) : ""
                        });
                    }
                }
            }

            NoPackagesText.Visibility = AvailablePackages.Count > 0 ? Visibility.Collapsed : Visibility.Visible;
            SetStatus($"Found {AvailablePackages.Count} packages");
        }

        #endregion

        #region Event Handlers

        private void AddButton_Click(object sender, RoutedEventArgs e)
        {
            string? packageName = InputDialog.Show(
                "Add Dependency",
                "Enter package name (e.g., fmt, spdlog, fmt@11.1.4):");

            if (!string.IsNullOrWhiteSpace(packageName))
            {
                SetStatus($"Adding {packageName}...", true);
                _ = AddPackageAsync(packageName!);
            }
        }

        private async Task AddPackageAsync(string packageName)
        {
            await CforgeRunner.RunAsync($"deps add {packageName}");
            await RefreshDependenciesAsync();
        }

        private void RemoveButton_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedDependency != null && _selectedDependency.Type != "group")
            {
                _ = RemovePackageAsync(_selectedDependency.Name);
            }
            else
            {
                _ = VS.MessageBox.ShowWarningAsync("Remove Dependency", "Please select a dependency to remove.");
            }
        }

        private void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            _ = RefreshDependenciesAsync();
        }

        private void UpdateRegistryButton_Click(object sender, RoutedEventArgs e)
        {
            SetStatus("Updating package registry...", true);
            _ = UpdateRegistryAsync();
        }

        private async Task UpdateRegistryAsync()
        {
            await CforgeRunner.RunAsync("deps update");
            await LoadAvailablePackagesAsync();
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            SetStatus("Registry updated");
        }

        private async void OutdatedButton_Click(object sender, RoutedEventArgs e)
        {
            SetStatus("Checking for outdated packages...", true);

            var result = await CforgeRunner.RunAndCaptureAsync("deps outdated");

            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            OutdatedPackages.Clear();

            if (!string.IsNullOrWhiteSpace(result))
            {
                foreach (var line in result.Split('\n'))
                {
                    if (line.Contains("->"))
                    {
                        var parts = line.Split(':');
                        if (parts.Length >= 2)
                        {
                            string name = parts[0].Trim();
                            string versionPart = parts[1].Trim();
                            var versions = versionPart.Split(new[] { "->" }, StringSplitOptions.None);
                            if (versions.Length == 2)
                            {
                                OutdatedPackages.Add(new OutdatedPackage
                                {
                                    Name = name,
                                    CurrentVersion = versions[0].Trim(),
                                    LatestVersion = versions[1].Trim()
                                });
                            }
                        }
                    }
                }
            }

            NoUpdatesPanel.Visibility = OutdatedPackages.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
            OutdatedPackagesList.Visibility = OutdatedPackages.Count > 0 ? Visibility.Visible : Visibility.Collapsed;

            SetStatus($"{OutdatedPackages.Count} outdated packages found");
            MainTabControl.SelectedIndex = 3; // Switch to Updates tab
        }

        private void SearchBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter)
            {
                SearchButton_Click(sender, e);
            }
        }

        private void SearchButton_Click(object sender, RoutedEventArgs e)
        {
            string query = SearchBox.Text;
            _ = SearchPackagesAsync(query);

            // Switch to Browse tab if searching
            if (!string.IsNullOrWhiteSpace(query))
            {
                MainTabControl.SelectedIndex = 1;
            }
        }

        private void DependencyTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            _selectedDependency = e.NewValue as DependencyItem;

            if (_selectedDependency != null && _selectedDependency.Type != "group")
            {
                DetailsPanel.Visibility = Visibility.Visible;
                DetailName.Text = _selectedDependency.Name;
                DetailVersion.Text = _selectedDependency.Version;
                DetailLatestVersion.Text = "Checking...";
                DetailType.Text = $"Source: {_selectedDependency.Type}";

                // Async check for latest version
                _ = CheckLatestVersionAsync(_selectedDependency.Name);
            }
            else
            {
                DetailsPanel.Visibility = Visibility.Collapsed;
            }
        }

        private async Task CheckLatestVersionAsync(string packageName)
        {
            var result = await CforgeRunner.RunAndCaptureAsync($"deps info {packageName}");

            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            if (!string.IsNullOrWhiteSpace(result))
            {
                foreach (var line in result.Split('\n'))
                {
                    if (line.ToLower().Contains("latest") || line.ToLower().Contains("version"))
                    {
                        var parts = line.Split(':');
                        if (parts.Length >= 2)
                        {
                            DetailLatestVersion.Text = parts[1].Trim();
                            return;
                        }
                    }
                }
            }
            DetailLatestVersion.Text = "Unknown";
        }

        private void CheckUpdate_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedDependency != null)
            {
                _ = CheckLatestVersionAsync(_selectedDependency.Name);
            }
        }

        private void UpdateDependency_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedDependency != null)
            {
                SetStatus($"Updating {_selectedDependency.Name}...", true);
                _ = UpdatePackageAsync(_selectedDependency.Name);
            }
        }

        private async Task UpdatePackageAsync(string packageName)
        {
            await CforgeRunner.RunAsync($"deps add {packageName}@latest");
            await RefreshDependenciesAsync();
        }

        private void RemoveDependency_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedDependency != null && _selectedDependency.Type != "group")
            {
                _ = RemovePackageAsync(_selectedDependency.Name);
            }
        }

        private async Task RemovePackageAsync(string packageName)
        {
            var result = await VS.MessageBox.ShowConfirmAsync(
                $"Remove '{packageName}'?",
                "This will remove the dependency from cforge.toml.");

            if (result)
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                SetStatus($"Removing {packageName}...", true);
                await CforgeRunner.RunAsync($"deps remove {packageName}");
                await RefreshDependenciesAsync();
            }
        }

        private void CopyName_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedDependency != null)
            {
                Clipboard.SetText(_selectedDependency.Name);
                SetStatus($"Copied '{_selectedDependency.Name}'");
            }
        }

        private void CopyNameVersion_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedDependency != null)
            {
                string text = $"{_selectedDependency.Name} = \"{_selectedDependency.Version}\"";
                Clipboard.SetText(text);
                SetStatus($"Copied '{text}'");
            }
        }

        private async void ViewInToml_Click(object sender, RoutedEventArgs e)
        {
            string? projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            if (!string.IsNullOrEmpty(projectDir))
            {
                string tomlPath = Path.Combine(projectDir, "cforge.toml");
                if (File.Exists(tomlPath))
                {
                    await VS.Documents.OpenAsync(tomlPath);
                }
            }
        }

        private void DetailUpdateButton_Click(object sender, RoutedEventArgs e)
        {
            UpdateDependency_Click(sender, e);
        }

        private void DetailRemoveButton_Click(object sender, RoutedEventArgs e)
        {
            RemoveDependency_Click(sender, e);
        }

        private async void DetailInfoButton_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedDependency != null)
            {
                await CforgeRunner.RunAsync($"deps info {_selectedDependency.Name}");
            }
        }

        private void InstallPackage_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button btn && btn.Tag is string packageName)
            {
                SetStatus($"Installing {packageName}...", true);
                _ = AddPackageAsync(packageName);
            }
        }

        private async void PackageInfoButton_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button btn && btn.Tag is string packageName)
            {
                await CforgeRunner.RunAsync($"deps info {packageName}");
            }
        }

        private async void RefreshTreeButton_Click(object sender, RoutedEventArgs e)
        {
            SetStatus("Loading dependency tree...", true);
            var result = await CforgeRunner.RunAndCaptureAsync("deps tree");
            DependencyTreeText.Text = result ?? "No dependency tree available";
            SetStatus("Dependency tree loaded");
        }

        private void UpdateOutdatedPackage_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button btn && btn.Tag is string packageName)
            {
                SetStatus($"Updating {packageName}...", true);
                _ = UpdateAndRefreshAsync(packageName);
            }
        }

        private async Task UpdateAndRefreshAsync(string packageName)
        {
            await CforgeRunner.RunAsync($"deps add {packageName}@latest");
            await RefreshDependenciesAsync();

            // Re-check outdated
            OutdatedButton_Click(null!, null!);
        }

        #endregion

        #region Helpers

        private void SetStatus(string message, bool showProgress = false)
        {
            StatusText.Text = message;
            ProgressBar.Visibility = showProgress ? Visibility.Visible : Visibility.Collapsed;
        }

        #endregion
    }

    public class DependencyItem
    {
        public string Name { get; set; } = "";
        public string Version { get; set; } = "";
        public string Type { get; set; } = "";
        public ObservableCollection<DependencyItem> Children { get; } = new ObservableCollection<DependencyItem>();

        public ImageMoniker ImageMoniker
        {
            get
            {
                return Type switch
                {
                    "group" => KnownMonikers.FolderOpened,
                    "git" => KnownMonikers.Git,
                    "vcpkg" => KnownMonikers.Package,
                    "system" => KnownMonikers.Library,
                    "project" => KnownMonikers.Application,
                    _ => KnownMonikers.NuGet
                };
            }
        }
    }

    public class AvailablePackage
    {
        public string Name { get; set; } = "";
        public string LatestVersion { get; set; } = "";
        public string Description { get; set; } = "";
        public string Categories { get; set; } = "";
    }

    public class OutdatedPackage
    {
        public string Name { get; set; } = "";
        public string CurrentVersion { get; set; } = "";
        public string LatestVersion { get; set; } = "";
    }
}
