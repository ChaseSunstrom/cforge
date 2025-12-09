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
using Microsoft.VisualStudio.Shell;
using Tomlyn;
using Tomlyn.Model;

namespace CforgeVS
{
    public partial class DependencyToolWindowControl : UserControl
    {
        public ObservableCollection<DependencyItem> Dependencies { get; } = new ObservableCollection<DependencyItem>();
        public ObservableCollection<AvailablePackage> AvailablePackages { get; } = new ObservableCollection<AvailablePackage>();

        private string? _registryPath;

        public DependencyToolWindowControl()
        {
            InitializeComponent();
            DependencyTree.ItemsSource = Dependencies;
            AvailablePackagesList.ItemsSource = AvailablePackages;

            // Find registry path
            string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            _registryPath = Path.Combine(localAppData, "cforge", "registry", "cforge-index", "packages");

            _ = RefreshDependenciesAsync();
            _ = LoadAvailablePackagesAsync();
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

        private void SearchPackages(string query)
        {
            if (string.IsNullOrWhiteSpace(query))
            {
                // Show all
                foreach (var item in AvailablePackagesList.Items)
                {
                    if (AvailablePackagesList.ItemContainerGenerator.ContainerFromItem(item) is ListBoxItem container)
                    {
                        container.Visibility = Visibility.Visible;
                    }
                }
                return;
            }

            query = query.ToLowerInvariant();
            foreach (var item in AvailablePackagesList.Items)
            {
                if (item is AvailablePackage pkg)
                {
                    bool match = pkg.Name.ToLowerInvariant().Contains(query) ||
                                 pkg.Description.ToLowerInvariant().Contains(query);

                    if (AvailablePackagesList.ItemContainerGenerator.ContainerFromItem(item) is ListBoxItem container)
                    {
                        container.Visibility = match ? Visibility.Visible : Visibility.Collapsed;
                    }
                }
            }
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
            await CforgeRunner.RunAsync($"add {packageName}");
            await RefreshDependenciesAsync();
        }

        private void RemoveButton_Click(object sender, RoutedEventArgs e)
        {
            if (DependencyTree.SelectedItem is DependencyItem item && item.Type != "group")
            {
                _ = RemovePackageAsync(item.Name);
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
            await CforgeRunner.RunAsync("update --packages");
            await LoadAvailablePackagesAsync();
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            SetStatus("Registry updated");
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
            SearchPackages(query);

            // Switch to Available tab if searching
            if (!string.IsNullOrWhiteSpace(query))
            {
                MainTabControl.SelectedIndex = 1;
            }
        }

        private void DependencyTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            if (e.NewValue is DependencyItem item && item.Type != "group")
            {
                DetailsPanel.Visibility = Visibility.Visible;
                DetailName.Text = item.Name;
                DetailVersion.Text = $"Version: {item.Version}";
                DetailType.Text = $"Source: {item.Type}";
            }
            else
            {
                DetailsPanel.Visibility = Visibility.Collapsed;
            }
        }

        private void RemoveDependency_Click(object sender, RoutedEventArgs e)
        {
            if (DependencyTree.SelectedItem is DependencyItem item && item.Type != "group")
            {
                _ = RemovePackageAsync(item.Name);
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
                await CforgeRunner.RunAsync($"remove {packageName}");
                await RefreshDependenciesAsync();
            }
        }

        private void CopyName_Click(object sender, RoutedEventArgs e)
        {
            if (DependencyTree.SelectedItem is DependencyItem item)
            {
                Clipboard.SetText(item.Name);
                SetStatus($"Copied '{item.Name}'");
            }
        }

        private void CopyNameVersion_Click(object sender, RoutedEventArgs e)
        {
            if (DependencyTree.SelectedItem is DependencyItem item)
            {
                string text = $"{item.Name}@{item.Version}";
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

        private void InstallPackage_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button btn && btn.Tag is string packageName)
            {
                SetStatus($"Installing {packageName}...", true);
                _ = AddPackageAsync(packageName);
            }
        }

        private void BuildButton_Click(object sender, RoutedEventArgs e)
        {
            SetStatus("Building...", true);
            _ = RunCommandAsync("build", "Build");
        }

        private void RunButton_Click(object sender, RoutedEventArgs e)
        {
            SetStatus("Running...", true);
            _ = RunCommandAsync("run", "Run");
        }

        private void CleanButton_Click(object sender, RoutedEventArgs e)
        {
            SetStatus("Cleaning...", true);
            _ = RunCommandAsync("clean", "Clean");
        }

        private async Task RunCommandAsync(string command, string displayName)
        {
            bool success = await CforgeRunner.RunAsync(command);
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            SetStatus(success ? $"{displayName} completed" : $"{displayName} failed");
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
    }

    public class AvailablePackage
    {
        public string Name { get; set; } = "";
        public string LatestVersion { get; set; } = "";
        public string Description { get; set; } = "";
    }
}
