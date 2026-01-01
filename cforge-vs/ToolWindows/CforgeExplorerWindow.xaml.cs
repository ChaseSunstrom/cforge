using System;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Imaging.Interop;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    public enum TreeItemType
    {
        Project,
        Folder,
        File,
        Executable,
        Dependency,
        Info
    }

    public partial class CforgeExplorerWindowControl : UserControl
    {
        private string _configuration = "Debug";
        private string? _projectDir;
        private ProjectTreeItem? _selectedItem;

        public CforgeExplorerWindowControl()
        {
            InitializeComponent();
            Loaded += OnLoaded;
        }

        private void AddDirectoryNode(ProjectTreeItem parent, string dirName, string displayName, ImageMoniker icon)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;

            string fullPath = Path.Combine(_projectDir, dirName);
            if (!Directory.Exists(fullPath)) return;

            var node = new ProjectTreeItem
            {
                Name = displayName,
                ImageMoniker = icon,
                FullPath = fullPath,
                ItemType = TreeItemType.Folder
            };

            try
            {
                // Add files
                var files = Directory.GetFiles(fullPath, "*.*", SearchOption.TopDirectoryOnly)
                    .Where(f => IsCppFile(f))
                    .OrderBy(f => f)
                    .ToList();

                foreach (var file in files.Take(50))
                {
                    node.Children.Add(CreateFileNode(file));
                }

                // Add subdirectories
                var dirs = Directory.GetDirectories(fullPath)
                    .OrderBy(d => d)
                    .ToList();

                foreach (var dir in dirs.Take(20))
                {
                    AddSubdirectory(node, dir);
                }

                if (files.Count > 50 || dirs.Count > 20)
                {
                    node.Badge = $"({files.Count + dirs.Count} items)";
                }
            }
            catch { }

            if (node.Children.Count > 0)
            {
                parent.Children.Add(node);
            }
        }

        private void AddSubdirectory(ProjectTreeItem parent, string dirPath)
        {
            var dirName = Path.GetFileName(dirPath);
            var node = new ProjectTreeItem
            {
                Name = dirName,
                ImageMoniker = KnownMonikers.FolderClosed,
                FullPath = dirPath,
                ItemType = TreeItemType.Folder,
                IsExpanded = false
            };

            try
            {
                var files = Directory.GetFiles(dirPath, "*.*", SearchOption.TopDirectoryOnly)
                    .Where(f => IsCppFile(f))
                    .OrderBy(f => f)
                    .ToList();

                foreach (var file in files.Take(30))
                {
                    node.Children.Add(CreateFileNode(file));
                }

                // Recurse into subdirectories (limited depth)
                var dirs = Directory.GetDirectories(dirPath).Take(10);
                foreach (var dir in dirs)
                {
                    AddSubdirectory(node, dir);
                }
            }
            catch { }

            if (node.Children.Count > 0)
            {
                parent.Children.Add(node);
            }
        }

        private async void BuildButton_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;
            SetStatus("Building...");
            ProgressBar.Visibility = Visibility.Visible;
            string configArg = _configuration == "Release" ? " -c Release" : "";
            await CforgeRunner.RunAsync($"build{configArg}", _projectDir);
            ProgressBar.Visibility = Visibility.Collapsed;
            SetStatus("Build complete");
            await LoadProjectAsync();
        }

        private async void CleanButton_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;
            SetStatus("Cleaning...");
            await CforgeRunner.RunAsync("clean", _projectDir);
            SetStatus("Cleaned");
            await LoadProjectAsync();
        }

        private void ConfigCombo_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            if (ConfigCombo.SelectedItem is ComboBoxItem item)
            {
                _configuration = item.Content?.ToString() ?? "Debug";
                // Update build status for new config
                if (!string.IsNullOrEmpty(_projectDir))
                {
                    var project = CforgeTomlParser.ParseProject(_projectDir);
                    if (project != null)
                    {
                        UpdateBuildStatus(project);
                    }
                }
            }
        }

        private int CountSourceFiles()
        {
            if (string.IsNullOrEmpty(_projectDir)) return 0;

            int count = 0;
            string[] dirs = { "src", "include", "tests", "bench" };

            foreach (var dir in dirs)
            {
                string path = Path.Combine(_projectDir, dir);
                if (Directory.Exists(path))
                {
                    try
                    {
                        count += Directory.GetFiles(path, "*.*", SearchOption.AllDirectories)
                            .Count(f => IsCppFile(f));
                    }
                    catch { }
                }
            }

            return count;
        }

        private ProjectTreeItem CreateFileNode(string filePath)
        {
            string fileName = Path.GetFileName(filePath);
            string ext = Path.GetExtension(filePath).ToLower();
            ImageMoniker icon;

            // Check for special filenames first
            if (fileName.Equals("CMakeLists.txt", StringComparison.OrdinalIgnoreCase))
            {
                icon = KnownMonikers.Makefile;
            }
            else
            {
                switch (ext)
                {
                    case ".cpp":
                    case ".cxx":
                    case ".cc":
                        icon = KnownMonikers.CPPFileNode;
                        break;

                    case ".c":
                        icon = KnownMonikers.CFile;
                        break;

                    case ".hpp":
                    case ".hxx":
                        icon = KnownMonikers.CPPHeaderFile;
                        break;

                    case ".h":
                        icon = KnownMonikers.Include;
                        break;

                    case ".toml":
                        icon = KnownMonikers.ConfigurationFile;
                        break;

                    case ".json":
                        icon = KnownMonikers.JSONScript;
                        break;

                    case ".yaml":
                    case ".yml":
                        icon = KnownMonikers.ConfigurationFile;
                        break;

                    case ".md":
                        icon = KnownMonikers.MarkdownFile;
                        break;

                    case ".txt":
                        icon = KnownMonikers.TextFile;
                        break;

                    case ".cmake":
                        icon = KnownMonikers.Makefile;
                        break;

                    default:
                        icon = KnownMonikers.Document;
                        break;
                }
            }

            return new ProjectTreeItem
            {
                Name = Path.GetFileName(filePath),
                ImageMoniker = icon,
                FullPath = filePath,
                ItemType = TreeItemType.File
            };
        }

        private async void DebugButton_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;
            SetStatus("Starting debugger...");
            await CforgeRunner.BuildAndDebugAsync(_projectDir);
            SetStatus("Ready");
        }

        private bool IsCppFile(string path)
        {
            string fileName = Path.GetFileName(path);
            string ext = Path.GetExtension(path).ToLower();

            // Include CMakeLists.txt
            if (fileName.Equals("CMakeLists.txt", StringComparison.OrdinalIgnoreCase))
                return true;

            return ext == ".cpp" || ext == ".cxx" || ext == ".cc" || ext == ".c" ||
                   ext == ".h" || ext == ".hpp" || ext == ".hxx" ||
                   ext == ".toml" || ext == ".json" || ext == ".md" || ext == ".txt" ||
                   ext == ".cmake";
        }

        private async Task LoadProjectAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            _projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(_projectDir))
            {
                ProjectNameText.Text = "No cforge project";
                ProjectVersionText.Text = "Open a folder with cforge.toml";
                SetBuildStatus("No Project", false);
                return;
            }

            var project = CforgeTomlParser.ParseProject(_projectDir);
            if (project != null)
            {
                ProjectNameText.Text = project.Name;
                ProjectVersionText.Text = $"v{project.Version}";
                UpdateBuildStatus(project);
                await PopulateTreeAsync(project);
            }
        }

        private async void NewClass_Click(object sender, RoutedEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? className = InputDialog.Show("New Class", "Enter class name (e.g., MyClass, utils/Helper):");
            if (!string.IsNullOrWhiteSpace(className))
            {
                await CforgeRunner.RunAsync($"new class {className}", _projectDir);
                await LoadProjectAsync();
            }
        }

        private async void NewHeader_Click(object sender, RoutedEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? headerName = InputDialog.Show("New Header", "Enter header name:");
            if (!string.IsNullOrWhiteSpace(headerName))
            {
                await CforgeRunner.RunAsync($"new header {headerName}", _projectDir);
                await LoadProjectAsync();
            }
        }

        private async void OnLoaded(object sender, RoutedEventArgs e)
        {
            try
            {
                await LoadProjectAsync();
            }
            catch (Exception ex)
            {
                SetStatus($"Error: {ex.Message}");
            }
        }

        private async void OpenFile_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedItem?.FullPath != null && File.Exists(_selectedItem.FullPath))
            {
                await VS.Documents.OpenAsync(_selectedItem.FullPath);
            }
        }

        private void OpenFolder_Click(object sender, RoutedEventArgs e)
        {
            if (_selectedItem?.FullPath != null)
            {
                string folder = File.Exists(_selectedItem.FullPath)
                    ? Path.GetDirectoryName(_selectedItem.FullPath)!
                    : _selectedItem.FullPath;

                if (Directory.Exists(folder))
                {
                    Process.Start("explorer.exe", folder);
                }
            }
        }

        private async void OpenTomlButton_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;
            string tomlPath = Path.Combine(_projectDir, "cforge.toml");
            if (File.Exists(tomlPath))
            {
                await VS.Documents.OpenAsync(tomlPath);
            }
        }

        private async Task PopulateTreeAsync(CforgeProject project)
        {
            await Task.Run(() =>
            {
                var root = new ProjectTreeItem
                {
                    Name = project.Name,
                    ImageMoniker = KnownMonikers.Application,
                    IsExpanded = true,
                    FullPath = _projectDir!,
                    ItemType = TreeItemType.Project
                };

                // Add cforge.toml
                root.Children.Add(new ProjectTreeItem
                {
                    Name = "cforge.toml",
                    ImageMoniker = KnownMonikers.ConfigurationFile,
                    FullPath = Path.Combine(_projectDir!, "cforge.toml"),
                    ItemType = TreeItemType.File
                });

                // Add source directories
                AddDirectoryNode(root, "src", "Source Files", KnownMonikers.CPPSourceFile);
                AddDirectoryNode(root, "include", "Headers", KnownMonikers.CPPHeaderFile);
                AddDirectoryNode(root, "tests", "Tests", KnownMonikers.TestGroup);
                AddDirectoryNode(root, "bench", "Benchmarks", KnownMonikers.Performance);

                // Add dependencies summary
                if (project.Dependencies?.Count > 0)
                {
                    var depsNode = new ProjectTreeItem
                    {
                        Name = "Dependencies",
                        ImageMoniker = KnownMonikers.Reference,
                        Badge = $"({project.Dependencies.Count})",
                        ItemType = TreeItemType.Folder
                    };

                    foreach (var dep in project.Dependencies.Take(10))
                    {
                        depsNode.Children.Add(new ProjectTreeItem
                        {
                            Name = dep.Name,
                            ImageMoniker = KnownMonikers.NuGet,
                            Badge = string.IsNullOrEmpty(dep.Version) ? "*" : dep.Version,
                            ItemType = TreeItemType.Dependency
                        });
                    }

                    if (project.Dependencies.Count > 10)
                    {
                        depsNode.Children.Add(new ProjectTreeItem
                        {
                            Name = $"... and {project.Dependencies.Count - 10} more",
                            ImageMoniker = KnownMonikers.StatusInformation,
                            ItemType = TreeItemType.Info
                        });
                    }

                    root.Children.Add(depsNode);
                }

                // Add build output
                var buildDir = Path.Combine(_projectDir!, "build");
                if (Directory.Exists(buildDir))
                {
                    var buildNode = new ProjectTreeItem
                    {
                        Name = "Build Output",
                        ImageMoniker = KnownMonikers.Output,
                        FullPath = buildDir,
                        ItemType = TreeItemType.Folder
                    };

                    // Check for executables
                    var exePath = CforgeTomlParser.FindExecutable(_projectDir!, project, _configuration);
                    if (exePath != null && File.Exists(exePath))
                    {
                        buildNode.Children.Add(new ProjectTreeItem
                        {
                            Name = Path.GetFileName(exePath),
                            ImageMoniker = KnownMonikers.Application,
                            FullPath = exePath,
                            ItemType = TreeItemType.Executable
                        });
                    }

                    root.Children.Add(buildNode);
                }

                // Count files
                int fileCount = CountSourceFiles();

                // Update UI on main thread
                _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                {
                    await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                    ProjectTree.ItemsSource = new[] { root };
                    FileCountText.Text = $"{fileCount} files";
                    SetStatus("Ready");
                });
            });
        }

        private async void ProjectTree_MouseDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (_selectedItem == null) return;

            if (_selectedItem.ItemType == TreeItemType.File && !string.IsNullOrEmpty(_selectedItem.FullPath))
            {
                await VS.Documents.OpenAsync(_selectedItem.FullPath);
            }
            else if (_selectedItem.ItemType == TreeItemType.Executable && !string.IsNullOrEmpty(_selectedItem.FullPath))
            {
                // Run the executable
                await CforgeRunner.BuildAndRunInConsoleAsync(_projectDir!, _configuration);
            }
        }

        private void ProjectTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            _selectedItem = e.NewValue as ProjectTreeItem;
        }

        private async void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            SetStatus("Refreshing...");
            await LoadProjectAsync();
        }

        private async void RunButton_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;
            SetStatus("Running...");
            await CforgeRunner.BuildAndRunInConsoleAsync(_projectDir, _configuration);
            SetStatus("Ready");
        }

        private void SetBuildStatus(string text, bool isBuilt)
        {
            BuildStatusText.Text = text;
            if (isBuilt)
            {
                BuildStatusBorder.Background = new SolidColorBrush(Color.FromRgb(76, 175, 80));
                BuildStatusText.Foreground = Brushes.White;
            }
            else
            {
                BuildStatusBorder.Background = new SolidColorBrush(Color.FromRgb(158, 158, 158));
                BuildStatusText.Foreground = Brushes.White;
            }
        }

        private void SetStatus(string message)
        {
            StatusText.Text = message;
        }

        private async void TestButton_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;
            SetStatus("Running tests...");
            ProgressBar.Visibility = Visibility.Visible;
            await CforgeRunner.RunAsync("test", _projectDir);
            ProgressBar.Visibility = Visibility.Collapsed;
            SetStatus("Tests complete");
        }

        private void UpdateBuildStatus(CforgeProject project)
        {
            if (string.IsNullOrEmpty(_projectDir)) return;

            string? exePath = CforgeTomlParser.FindExecutable(_projectDir, project, _configuration);
            if (exePath != null && File.Exists(exePath))
            {
                var fileInfo = new FileInfo(exePath);
                var elapsed = DateTime.Now - fileInfo.LastWriteTime;
                string timeStr = elapsed.TotalMinutes < 1 ? "just now" :
                                 elapsed.TotalHours < 1 ? $"{(int)elapsed.TotalMinutes}m ago" :
                                 elapsed.TotalDays < 1 ? $"{(int)elapsed.TotalHours}h ago" :
                                 fileInfo.LastWriteTime.ToString("MMM d");
                SetBuildStatus($"Built {timeStr}", true);
            }
            else
            {
                SetBuildStatus("Not built", false);
            }
        }
    }

    /// <summary>
    /// Converter for ImageMoniker (not currently used but available for custom scenarios).
    /// </summary>
    public class MonikerToImageSourceConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            return value;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }

    public class ProjectTreeItem
    {
        public string Badge { get; set; } = "";
        public ObservableCollection<ProjectTreeItem> Children { get; } = new ObservableCollection<ProjectTreeItem>();
        public string FullPath { get; set; } = "";
        public ImageMoniker ImageMoniker { get; set; } = KnownMonikers.Document;
        public bool IsExpanded { get; set; } = true;
        public TreeItemType ItemType { get; set; }
        public string Name { get; set; } = "";
    }
}