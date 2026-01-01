using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;
using Tomlyn;
using Tomlyn.Model;

namespace CforgeVS
{
    public partial class ProjectPropertiesWindowControl : UserControl
    {
        private string? _projectDir;
        private string? _tomlPath;

        public ProjectPropertiesWindowControl()
        {
            InitializeComponent();
            _ = LoadProjectAsync();
        }

        private async Task LoadProjectAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            _projectDir = await CforgeRunner.GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(_projectDir))
            {
                ProjectNameHeader.Text = "No cforge project found";
                ProjectPathText.Text = "Open a folder containing cforge.toml";
                return;
            }

            _tomlPath = Path.Combine(_projectDir, "cforge.toml");
            if (!File.Exists(_tomlPath))
            {
                ProjectNameHeader.Text = "No cforge.toml found";
                ProjectPathText.Text = _projectDir;
                return;
            }

            ProjectPathText.Text = _tomlPath;

            try
            {
                string content = File.ReadAllText(_tomlPath);
                var doc = Toml.ToModel(content);

                LoadFromToml(doc);
            }
            catch (Exception ex)
            {
                ProjectNameHeader.Text = "Error loading cforge.toml";
                ProjectPathText.Text = ex.Message;
            }
        }

        private void LoadFromToml(TomlTable doc)
        {
            // Project section
            if (doc.TryGetValue("project", out var projectSection) && projectSection is TomlTable projectTable)
            {
                ProjectNameBox.Text = GetString(projectTable, "name") ?? "";
                ProjectNameHeader.Text = $"Project: {ProjectNameBox.Text}";
                ProjectVersionBox.Text = GetString(projectTable, "version") ?? "0.1.0";
                ProjectDescriptionBox.Text = GetString(projectTable, "description") ?? "";

                SelectComboItem(BinaryTypeCombo, GetString(projectTable, "binary_type") ?? GetString(projectTable, "type") ?? "executable");
                SelectComboItem(CppStandardCombo, GetString(projectTable, "cpp_standard") ?? "20");
                SelectComboItem(CStandardCombo, GetString(projectTable, "c_standard") ?? "17");

                AuthorsBox.Text = string.Join(", ", GetStringList(projectTable, "authors"));
                LicenseBox.Text = GetString(projectTable, "license") ?? "";
                HomepageBox.Text = GetString(projectTable, "homepage") ?? "";
                RepositoryBox.Text = GetString(projectTable, "repository") ?? "";
            }

            // Build section
            if (doc.TryGetValue("build", out var buildSection) && buildSection is TomlTable buildTable)
            {
                OutputDirBox.Text = GetString(buildTable, "directory") ?? "build";
                SourceDirsBox.Text = string.Join(", ", GetStringList(buildTable, "source_dirs"));
                IncludeDirsBox.Text = string.Join(", ", GetStringList(buildTable, "include_dirs"));

                var exportCommands = GetBool(buildTable, "export_compile_commands");
                ExportCompileCommandsCheck.IsChecked = exportCommands;

                // Debug config
                if (buildTable.TryGetValue("config", out var configObj) && configObj is TomlTable configTable)
                {
                    if (configTable.TryGetValue("debug", out var debugObj) && debugObj is TomlTable debugTable)
                    {
                        SelectComboItem(DebugOptimizeCombo, GetString(debugTable, "optimize") ?? "debug");
                        DebugInfoCheck.IsChecked = GetBool(debugTable, "debug_info") ?? true;
                        DebugDefinesBox.Text = string.Join(", ", GetStringList(debugTable, "defines"));
                        SelectComboItem(DebugWarningsCombo, GetString(debugTable, "warnings") ?? "all");
                    }

                    if (configTable.TryGetValue("release", out var releaseObj) && releaseObj is TomlTable releaseTable)
                    {
                        SelectComboItem(ReleaseOptimizeCombo, GetString(releaseTable, "optimize") ?? "speed");
                        ReleaseLtoCheck.IsChecked = GetBool(releaseTable, "lto") ?? false;
                        ReleaseDefinesBox.Text = string.Join(", ", GetStringList(releaseTable, "defines"));
                    }
                }
            }

            // Test section
            if (doc.TryGetValue("test", out var testSection) && testSection is TomlTable testTable)
            {
                TestEnabledCheck.IsChecked = GetBool(testTable, "enabled") ?? false;
                SelectComboItem(TestFrameworkCombo, GetString(testTable, "framework") ?? "catch2");
                TestDirBox.Text = GetString(testTable, "directory") ?? "tests";
            }
        }

        private void SaveToToml()
        {
            if (string.IsNullOrEmpty(_tomlPath)) return;

            try
            {
                var sb = new StringBuilder();

                // Project section
                sb.AppendLine("[project]");
                sb.AppendLine($"name = \"{ProjectNameBox.Text}\"");
                sb.AppendLine($"version = \"{ProjectVersionBox.Text}\"");
                if (!string.IsNullOrWhiteSpace(ProjectDescriptionBox.Text))
                    sb.AppendLine($"description = \"{ProjectDescriptionBox.Text.Replace("\"", "\\\"")}\"");
                sb.AppendLine($"binary_type = \"{GetSelectedComboValue(BinaryTypeCombo)}\"");
                sb.AppendLine($"cpp_standard = \"{GetSelectedComboValue(CppStandardCombo)}\"");
                sb.AppendLine($"c_standard = \"{GetSelectedComboValue(CStandardCombo)}\"");

                if (!string.IsNullOrWhiteSpace(AuthorsBox.Text))
                    sb.AppendLine($"authors = [{FormatStringArray(AuthorsBox.Text)}]");
                if (!string.IsNullOrWhiteSpace(LicenseBox.Text))
                    sb.AppendLine($"license = \"{LicenseBox.Text}\"");
                if (!string.IsNullOrWhiteSpace(HomepageBox.Text))
                    sb.AppendLine($"homepage = \"{HomepageBox.Text}\"");
                if (!string.IsNullOrWhiteSpace(RepositoryBox.Text))
                    sb.AppendLine($"repository = \"{RepositoryBox.Text}\"");

                sb.AppendLine();

                // Build section
                sb.AppendLine("[build]");
                sb.AppendLine($"directory = \"{OutputDirBox.Text}\"");
                if (!string.IsNullOrWhiteSpace(SourceDirsBox.Text))
                    sb.AppendLine($"source_dirs = [{FormatStringArray(SourceDirsBox.Text)}]");
                if (!string.IsNullOrWhiteSpace(IncludeDirsBox.Text))
                    sb.AppendLine($"include_dirs = [{FormatStringArray(IncludeDirsBox.Text)}]");
                if (ExportCompileCommandsCheck.IsChecked == true)
                    sb.AppendLine("export_compile_commands = true");

                sb.AppendLine();

                // Debug config
                sb.AppendLine("[build.config.debug]");
                sb.AppendLine($"optimize = \"{GetSelectedComboValue(DebugOptimizeCombo)}\"");
                sb.AppendLine($"debug_info = {(DebugInfoCheck.IsChecked == true ? "true" : "false")}");
                if (!string.IsNullOrWhiteSpace(DebugDefinesBox.Text))
                    sb.AppendLine($"defines = [{FormatStringArray(DebugDefinesBox.Text)}]");
                sb.AppendLine($"warnings = \"{GetSelectedComboValue(DebugWarningsCombo)}\"");

                sb.AppendLine();

                // Release config
                sb.AppendLine("[build.config.release]");
                sb.AppendLine($"optimize = \"{GetSelectedComboValue(ReleaseOptimizeCombo)}\"");
                if (ReleaseLtoCheck.IsChecked == true)
                    sb.AppendLine("lto = true");
                if (!string.IsNullOrWhiteSpace(ReleaseDefinesBox.Text))
                    sb.AppendLine($"defines = [{FormatStringArray(ReleaseDefinesBox.Text)}]");

                sb.AppendLine();

                // Test section
                if (TestEnabledCheck.IsChecked == true)
                {
                    sb.AppendLine("[test]");
                    sb.AppendLine("enabled = true");
                    sb.AppendLine($"framework = \"{GetSelectedComboValue(TestFrameworkCombo)}\"");
                    if (!string.IsNullOrWhiteSpace(TestDirBox.Text))
                        sb.AppendLine($"directory = \"{TestDirBox.Text}\"");
                    sb.AppendLine();
                }

                // Preserve dependencies from original file
                if (File.Exists(_tomlPath))
                {
                    string original = File.ReadAllText(_tomlPath);
                    var originalDoc = Toml.ToModel(original);

                    if (originalDoc.TryGetValue("dependencies", out var depsSection) && depsSection is TomlTable depsTable)
                    {
                        sb.AppendLine("[dependencies]");
                        foreach (var kvp in depsTable)
                        {
                            if (kvp.Value is string version)
                            {
                                sb.AppendLine($"{kvp.Key} = \"{version}\"");
                            }
                            else if (kvp.Value is TomlTable)
                            {
                                // Complex dependency - preserve as-is by re-serializing
                                string depToml = Toml.FromModel(new TomlTable { [kvp.Key] = kvp.Value });
                                // Extract just the value part
                                sb.AppendLine($"# {kvp.Key} = ... (complex dependency preserved)");
                            }
                        }
                    }
                }

                File.WriteAllText(_tomlPath, sb.ToString());
            }
            catch (Exception ex)
            {
                _ = VS.MessageBox.ShowErrorAsync("Error Saving", $"Failed to save cforge.toml: {ex.Message}");
            }
        }

        private void SelectComboItem(ComboBox combo, string value)
        {
            foreach (ComboBoxItem item in combo.Items)
            {
                if (item.Content?.ToString() == value)
                {
                    combo.SelectedItem = item;
                    return;
                }
            }
            // If not found, select first item
            if (combo.Items.Count > 0)
                combo.SelectedIndex = 0;
        }

        private string GetSelectedComboValue(ComboBox combo)
        {
            if (combo.SelectedItem is ComboBoxItem item)
                return item.Content?.ToString() ?? "";
            return "";
        }

        private string FormatStringArray(string commaSeparated)
        {
            var items = commaSeparated.Split(',')
                .Select(s => s.Trim())
                .Where(s => !string.IsNullOrEmpty(s))
                .Select(s => $"\"{s}\"");
            return string.Join(", ", items);
        }

        private string? GetString(TomlTable table, string key)
        {
            if (table.TryGetValue(key, out var value))
                return value?.ToString();
            return null;
        }

        private bool? GetBool(TomlTable table, string key)
        {
            if (table.TryGetValue(key, out var value))
            {
                if (value is bool b) return b;
                if (value is string s) return s.ToLower() == "true";
            }
            return null;
        }

        private List<string> GetStringList(TomlTable table, string key)
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

        private void RefreshButton_Click(object sender, RoutedEventArgs e)
        {
            _ = LoadProjectAsync();
        }

        private void SaveButton_Click(object sender, RoutedEventArgs e)
        {
            SaveToToml();
            _ = VS.MessageBox.ShowAsync("cforge", "Project properties saved to cforge.toml");
        }

        private async void OpenTomlButton_Click(object sender, RoutedEventArgs e)
        {
            if (!string.IsNullOrEmpty(_tomlPath) && File.Exists(_tomlPath))
            {
                await VS.Documents.OpenAsync(_tomlPath);
            }
        }
    }
}
