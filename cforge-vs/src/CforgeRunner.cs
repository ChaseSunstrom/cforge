using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CforgeVS
{
    public static class CforgeRunner
    {
        private static OutputWindowPane? _outputPane;
        private static ErrorListProvider? _errorListProvider;
        private static List<ErrorTask> _currentErrors = new List<ErrorTask>();

        // Regex patterns for parsing compiler errors/warnings

        // cforge Cargo-style header: "error[CODE]: message" or "warning[CODE]: message"
        private static readonly Regex CforgeCargoHeaderPattern = new Regex(
            @"^\s*(?<type>error|warning)(?:\[(?<code>[^\]]+)\])?:\s*(?<msg>.+)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        // cforge Cargo-style location: "  --> file:line:col"
        private static readonly Regex CforgeCargoLocationPattern = new Regex(
            @"^\s*-->\s*(?<file>.+):(?<line>\d+):(?<col>\d+)\s*$",
            RegexOptions.Compiled);

        // cforge prefixed compiler output: "     warning file:line:col: warning: message" or "     error file:line:col: error: message"
        private static readonly Regex CforgePrefixedPattern = new Regex(
            @"^\s*(?<type>error|warning)\s+(?<file>[A-Za-z]:[\\/][^:]+|[^:]+):(?<line>\d+):(?<col>\d+):\s*(?:error|warning):\s*(?<msg>.+)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        // GCC/Clang style: file:line:col: error/warning: message
        private static readonly Regex GccErrorPattern = new Regex(
            @"^(?<file>[A-Za-z]:[\\/][^:]+|[^:]+):(?<line>\d+):(?<col>\d+):\s*(?<type>error|warning|note):\s*(?<msg>.+)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        // MSVC style: file(line,col): error/warning CODE: message
        private static readonly Regex MsvcErrorPattern = new Regex(
            @"^(?<file>[^(]+)\((?<line>\d+),?(?<col>\d+)?\):\s*(?<type>error|warning)\s*(?<code>\w+)?:\s*(?<msg>.+)$",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        // Track pending cargo-style diagnostic (header followed by location)
        private static string? _pendingDiagnosticType;
        private static string? _pendingDiagnosticCode;
        private static string? _pendingDiagnosticMessage;

        /// <summary>
        /// Gets the VS Output pane for cforge output.
        /// </summary>
        public static async Task<OutputWindowPane> GetOutputPaneAsync()
        {
            if (_outputPane == null)
            {
                _outputPane = await VS.Windows.CreateOutputWindowPaneAsync("cforge");
            }
            return _outputPane;
        }

        /// <summary>
        /// Gets or creates the Error List provider.
        /// </summary>
        private static async Task<ErrorListProvider> GetErrorListProviderAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            if (_errorListProvider == null)
            {
                _errorListProvider = new ErrorListProvider(CforgePackage.Instance!)
                {
                    ProviderName = "cforge",
                    ProviderGuid = new Guid("7A8B9C0D-1E2F-3A4B-5C6D-7E8F9A0B1C2E")
                };
            }
            return _errorListProvider;
        }

        /// <summary>
        /// Clears all errors from the Error List.
        /// </summary>
        private static async Task ClearErrorListAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var provider = await GetErrorListProviderAsync();
            provider.Tasks.Clear();
            _currentErrors.Clear();

            // Clear pending diagnostic state
            _pendingDiagnosticType = null;
            _pendingDiagnosticCode = null;
            _pendingDiagnosticMessage = null;
        }

        /// <summary>
        /// Adds an error or warning to the Error List.
        /// </summary>
        private static async Task AddErrorAsync(string file, int line, int column, string message, bool isWarning)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var provider = await GetErrorListProviderAsync();

            // Ensure file path is absolute and normalized
            if (!string.IsNullOrEmpty(file))
            {
                try
                {
                    file = Path.GetFullPath(file);
                }
                catch { }
            }

            var task = new ErrorTask
            {
                Category = TaskCategory.BuildCompile,
                ErrorCategory = isWarning ? TaskErrorCategory.Warning : TaskErrorCategory.Error,
                Document = file,
                Line = Math.Max(0, line - 1), // VS uses 0-based lines
                Column = Math.Max(0, column - 1),
                Text = message,
                Priority = isWarning ? TaskPriority.Normal : TaskPriority.High
            };

            // Navigate to file when double-clicked
            task.Navigate += (s, e) =>
            {
                ThreadHelper.ThrowIfNotOnUIThread();

                if (File.Exists(file))
                {
                    try
                    {
                        provider.Navigate(task, new Guid(EnvDTE.Constants.vsViewKindCode));
                    }
                    catch { }
                }
            };

            provider.Tasks.Add(task);
            _currentErrors.Add(task);
        }

        /// <summary>
        /// Parses a line of output for errors/warnings and adds them to Error List.
        /// </summary>
        private static async Task ParseOutputLineAsync(string line, string? projectDir)
        {
            if (string.IsNullOrWhiteSpace(line)) return;

            string cleanLine = StripAnsiCodes(line);

            // Check for cforge Cargo-style location line (follows header)
            if (_pendingDiagnosticMessage != null)
            {
                var locMatch = CforgeCargoLocationPattern.Match(cleanLine);
                if (locMatch.Success)
                {
                    string file = locMatch.Groups["file"].Value.Trim();
                    int.TryParse(locMatch.Groups["line"].Value, out int lineNum);
                    int.TryParse(locMatch.Groups["col"].Value, out int col);

                    // Normalize file path
                    file = NormalizeFilePath(file, projectDir);

                    string msg = _pendingDiagnosticMessage;
                    if (!string.IsNullOrEmpty(_pendingDiagnosticCode))
                    {
                        msg = $"[{_pendingDiagnosticCode}] {msg}";
                    }

                    bool isWarning = _pendingDiagnosticType?.ToLower() == "warning";
                    await AddErrorAsync(file, lineNum, col, msg, isWarning);

                    // Clear pending
                    _pendingDiagnosticType = null;
                    _pendingDiagnosticCode = null;
                    _pendingDiagnosticMessage = null;
                    return;
                }
            }

            // Check for cforge Cargo-style header (error[CODE]: message)
            var headerMatch = CforgeCargoHeaderPattern.Match(cleanLine);
            if (headerMatch.Success && !cleanLine.Contains("-->"))
            {
                _pendingDiagnosticType = headerMatch.Groups["type"].Value;
                _pendingDiagnosticCode = headerMatch.Groups["code"].Value;
                _pendingDiagnosticMessage = headerMatch.Groups["msg"].Value.Trim();
                return;
            }

            // Try cforge prefixed style (e.g., "     warning C:/path/file.cpp:10:5: warning: message")
            var match = CforgePrefixedPattern.Match(cleanLine);
            if (match.Success)
            {
                string file = match.Groups["file"].Value.Trim();
                int.TryParse(match.Groups["line"].Value, out int lineNum);
                int.TryParse(match.Groups["col"].Value, out int col);
                string type = match.Groups["type"].Value.ToLower();
                string msg = match.Groups["msg"].Value.Trim();

                file = NormalizeFilePath(file, projectDir);
                await AddErrorAsync(file, lineNum, col, msg, type == "warning" || type == "note");
                return;
            }

            // Try GCC/Clang style
            match = GccErrorPattern.Match(cleanLine);
            if (match.Success)
            {
                string file = match.Groups["file"].Value.Trim();
                int.TryParse(match.Groups["line"].Value, out int lineNum);
                int.TryParse(match.Groups["col"].Value, out int col);
                string type = match.Groups["type"].Value.ToLower();
                string msg = match.Groups["msg"].Value.Trim();

                file = NormalizeFilePath(file, projectDir);
                await AddErrorAsync(file, lineNum, col, msg, type == "warning" || type == "note");
                return;
            }

            // Try MSVC style
            match = MsvcErrorPattern.Match(cleanLine);
            if (match.Success)
            {
                string file = match.Groups["file"].Value.Trim();
                int.TryParse(match.Groups["line"].Value, out int lineNum);
                int.TryParse(match.Groups["col"].Value, out int col);
                string type = match.Groups["type"].Value.ToLower();
                string msg = match.Groups["msg"].Value.Trim();
                string code = match.Groups["code"].Value;

                if (!string.IsNullOrEmpty(code))
                {
                    msg = $"{code}: {msg}";
                }

                file = NormalizeFilePath(file, projectDir);
                await AddErrorAsync(file, lineNum, col, msg, type == "warning");
            }
        }

        /// <summary>
        /// Normalizes a file path - makes it absolute and converts forward slashes.
        /// </summary>
        private static string NormalizeFilePath(string file, string? projectDir)
        {
            // Convert forward slashes to backslashes for Windows
            file = file.Replace('/', '\\');

            // Make path absolute if relative
            if (!Path.IsPathRooted(file) && projectDir != null)
            {
                file = Path.Combine(projectDir, file);
            }

            // Try to get full path to normalize
            try
            {
                file = Path.GetFullPath(file);
            }
            catch { }

            return file;
        }

        public static async Task<bool> RunAsync(string arguments, string? workingDirectory = null)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var pane = await GetOutputPaneAsync();
            await pane.ActivateAsync();
            await pane.ClearAsync();

            // Clear previous errors from Error List
            await ClearErrorListAsync();

            string? projectDir = workingDirectory ?? await GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
            {
                await pane.WriteLineAsync("Error: No cforge project found. Open a folder containing cforge.toml.");
                return false;
            }

            string cforgeExe = GetCforgeExecutable();
            await pane.WriteLineAsync($"> cforge {arguments}");
            await pane.WriteLineAsync("");

            try
            {
                var startInfo = new ProcessStartInfo
                {
                    FileName = cforgeExe,
                    Arguments = arguments,
                    WorkingDirectory = projectDir,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    StandardOutputEncoding = Encoding.UTF8,
                    StandardErrorEncoding = Encoding.UTF8
                };

                using var process = new Process { StartInfo = startInfo };

                process.OutputDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        string cleanLine = StripAnsiCodes(e.Data);
                        _ = pane.WriteLineAsync(cleanLine);

                        // Parse for errors/warnings
                        _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                        {
                            await ParseOutputLineAsync(e.Data, projectDir);
                        });
                    }
                };

                process.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        string cleanLine = StripAnsiCodes(e.Data);
                        _ = pane.WriteLineAsync(cleanLine);

                        // Parse for errors/warnings
                        _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                        {
                            await ParseOutputLineAsync(e.Data, projectDir);
                        });
                    }
                };

                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();

                await Task.Run(() => process.WaitForExit());

                await pane.WriteLineAsync("");
                bool success = process.ExitCode == 0;

                if (success)
                {
                    await pane.WriteLineAsync("Command completed successfully.");

                    // Show success in status bar
                    await VS.StatusBar.ShowMessageAsync("Build succeeded");
                }
                else
                {
                    await pane.WriteLineAsync($"Command failed with exit code {process.ExitCode}");

                    // Show build failed notification
                    await ShowBuildFailedNotificationAsync();
                }

                // Show Error List if there are errors
                if (_currentErrors.Count > 0)
                {
                    var provider = await GetErrorListProviderAsync();
                    provider.Show();
                    provider.BringToFront();
                }

                return success;
            }
            catch (Exception ex)
            {
                await pane.WriteLineAsync($"Error running cforge: {ex.Message}");
                await pane.WriteLineAsync("");
                await pane.WriteLineAsync("Make sure cforge is installed and available in your PATH.");
                await pane.WriteLineAsync("Download from: https://github.com/ChaseSunstrom/cforge");
                return false;
            }
        }

        /// <summary>
        /// Runs an executable in Windows Terminal (or fallback to cmd.exe).
        /// </summary>
        public static async Task<bool> RunExecutableInConsoleAsync(string exePath, string? workingDirectory = null)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var pane = await GetOutputPaneAsync();
            string? projectDir = workingDirectory ?? await GetProjectDirectoryAsync();
            string workDir = projectDir ?? Path.GetDirectoryName(exePath) ?? "";

            if (!File.Exists(exePath))
            {
                await pane.WriteLineAsync($"Error: Executable not found: {exePath}");
                return false;
            }

            try
            {
                await pane.WriteLineAsync($"Starting: {exePath}");

                // Try Windows Terminal first (wt.exe)
                bool useWindowsTerminal = IsWindowsTerminalAvailable();

                ProcessStartInfo startInfo;
                if (useWindowsTerminal)
                {
                    // Windows Terminal with profile, stays open after program exits
                    // -d sets working directory, cmd /k keeps window open
                    startInfo = new ProcessStartInfo
                    {
                        FileName = "wt.exe",
                        Arguments = $"-d \"{workDir}\" cmd /k \"\"{exePath}\" & echo. & echo [Process exited] & pause\"",
                        UseShellExecute = true,
                        CreateNoWindow = false
                    };
                }
                else
                {
                    // Fallback to cmd.exe
                    startInfo = new ProcessStartInfo
                    {
                        FileName = "cmd.exe",
                        Arguments = $"/k \"\"{exePath}\" & echo. & echo Press any key to close... & pause > nul\"",
                        WorkingDirectory = workDir,
                        UseShellExecute = true,
                        CreateNoWindow = false
                    };
                }

                Process.Start(startInfo);
                await pane.WriteLineAsync(useWindowsTerminal ? "Launched in Windows Terminal" : "Launched in Command Prompt");
                return true;
            }
            catch (Exception ex)
            {
                await pane.WriteLineAsync($"Error starting executable: {ex.Message}");

                // Try fallback to cmd.exe if Windows Terminal failed
                try
                {
                    var fallbackInfo = new ProcessStartInfo
                    {
                        FileName = "cmd.exe",
                        Arguments = $"/k \"\"{exePath}\" & echo. & echo Press any key to close... & pause > nul\"",
                        WorkingDirectory = workDir,
                        UseShellExecute = true,
                        CreateNoWindow = false
                    };
                    Process.Start(fallbackInfo);
                    await pane.WriteLineAsync("Launched in Command Prompt (fallback)");
                    return true;
                }
                catch (Exception fallbackEx)
                {
                    await pane.WriteLineAsync($"Fallback also failed: {fallbackEx.Message}");
                    return false;
                }
            }
        }

        /// <summary>
        /// Checks if Windows Terminal (wt.exe) is available.
        /// </summary>
        private static bool IsWindowsTerminalAvailable()
        {
            try
            {
                // Check if wt.exe exists in PATH
                var startInfo = new ProcessStartInfo
                {
                    FileName = "where",
                    Arguments = "wt.exe",
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    CreateNoWindow = true
                };

                using var process = Process.Start(startInfo);
                process?.WaitForExit(1000);
                return process?.ExitCode == 0;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Builds the project and then runs the executable in a console window.
        /// </summary>
        public static async Task<bool> BuildAndRunInConsoleAsync(string? workingDirectory = null, string config = "Debug")
        {
            var pane = await GetOutputPaneAsync();

            string? projectDir = workingDirectory ?? await GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
            {
                await pane.WriteLineAsync("Error: No cforge project found.");
                return false;
            }

            // Build first
            string buildArgs = config == "Release" ? "build -c Release" : "build";
            bool buildSuccess = await RunAsync(buildArgs, projectDir);

            if (!buildSuccess)
            {
                return false;
            }

            // Find and run the executable
            var project = CforgeTomlParser.ParseProject(projectDir);
            if (project != null && project.Type == "executable")
            {
                string exePath = Path.Combine(projectDir, project.OutputDir, config, project.Name + ".exe");
                if (File.Exists(exePath))
                {
                    return await RunExecutableInConsoleAsync(exePath, projectDir);
                }
                else
                {
                    await pane.WriteLineAsync($"Error: Executable not found at {exePath}");
                    return false;
                }
            }
            else
            {
                await pane.WriteLineAsync("Warning: Project is not an executable.");
                return false;
            }
        }

        public static async Task<string?> GetProjectDirectoryAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Try to get from active solution
            var solution = await VS.Solutions.GetCurrentSolutionAsync();
            if (solution != null && !string.IsNullOrEmpty(solution.FullPath))
            {
                // First check if this is a generated solution (stored in temp)
                string? sourceDir = VcxprojGenerator.GetSourceDirForCurrentSolution(solution.FullPath);
                if (!string.IsNullOrEmpty(sourceDir) && IsCforgeProject(sourceDir))
                {
                    return sourceDir;
                }

                // Otherwise check if solution dir has cforge.toml
                string solutionDir = Path.GetDirectoryName(solution.FullPath)!;
                if (File.Exists(Path.Combine(solutionDir, "cforge.toml")))
                {
                    return solutionDir;
                }
            }

            // Try open folder scenario
            string? folderPath = await GetOpenFolderPathAsync();
            if (!string.IsNullOrEmpty(folderPath) && File.Exists(Path.Combine(folderPath, "cforge.toml")))
            {
                return folderPath;
            }

            // Try active document's directory - walk up to find cforge.toml
            var activeDoc = await VS.Documents.GetActiveDocumentViewAsync();
            if (activeDoc?.FilePath != null)
            {
                string? docDir = Path.GetDirectoryName(activeDoc.FilePath);
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

            return null;
        }

        private static async Task<string?> GetOpenFolderPathAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var solution = await VS.Solutions.GetCurrentSolutionAsync();
            if (solution != null)
            {
                // In Open Folder mode, the solution path might be the folder
                string? path = solution.FullPath;
                if (!string.IsNullOrEmpty(path))
                {
                    if (Directory.Exists(path))
                    {
                        return path;
                    }
                    return Path.GetDirectoryName(path);
                }
            }
            return null;
        }

        public static bool IsCforgeProject(string? directory)
        {
            if (string.IsNullOrEmpty(directory)) return false;
            return File.Exists(Path.Combine(directory, "cforge.toml")) ||
                   File.Exists(Path.Combine(directory, "cforge.workspace.toml"));
        }

        public static string GetCforgeExecutable()
        {
            // TODO: Add settings support for custom path
            return "cforge";
        }

        private static string StripAnsiCodes(string input)
        {
            // Remove ANSI escape sequences
            return System.Text.RegularExpressions.Regex.Replace(input, @"\x1b\[[0-9;]*m", "");
        }

        /// <summary>
        /// Shows the build failed notification using VS InfoBar.
        /// </summary>
        private static async Task ShowBuildFailedNotificationAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            try
            {
                // Show error in status bar
                await VS.StatusBar.ShowMessageAsync("Build failed - see Error List for details");

                // Count errors and warnings
                int errorCount = _currentErrors.Count(e => e.ErrorCategory == TaskErrorCategory.Error);
                int warningCount = _currentErrors.Count(e => e.ErrorCategory == TaskErrorCategory.Warning);

                string message = $"Build: {errorCount} error(s), {warningCount} warning(s)";

                // Create InfoBar in the main window
                var model = new InfoBarModel(
                    new[]
                    {
                        new InfoBarTextSpan("Build failed. "),
                        new InfoBarTextSpan($"{errorCount} error(s), {warningCount} warning(s). "),
                        new InfoBarHyperlink("Show Error List", "showErrors")
                    },
                    KnownMonikers.StatusError,
                    isCloseButtonVisible: true);

                var infoBar = await VS.InfoBar.CreateAsync(model);
                if (infoBar != null)
                {
                    infoBar.ActionItemClicked += async (sender, args) =>
                    {
                        if (args.ActionItem.Text == "Show Error List")
                        {
                            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                            var provider = await GetErrorListProviderAsync();
                            provider.Show();
                            provider.BringToFront();
                        }
                    };

                    await infoBar.TryShowInfoBarUIAsync();
                }
            }
            catch
            {
                // InfoBar not available, that's okay - we still have status bar and Error List
            }
        }
    }
}
