using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using CforgeVS.Models;
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
        private static Process? _currentProcess;
        private static Process? _watchProcess;
        private static BuildResult? _currentBuildResult;
        private static TestRunResult? _currentTestResult;

        // Events for build/test progress notifications
        public static event EventHandler<BuildProgressEventArgs>? BuildProgress;
        public static event EventHandler<BuildCompletedEventArgs>? BuildCompleted;
        public static event EventHandler<TestProgressEventArgs>? TestProgress;
        public static event EventHandler<TestCompletedEventArgs>? TestCompleted;

        /// <summary>
        /// Gets the last build result.
        /// </summary>
        public static BuildResult? LastBuildResult => _currentBuildResult;

        /// <summary>
        /// Gets the last test run result.
        /// </summary>
        public static TestRunResult? LastTestResult => _currentTestResult;

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
        /// Runs a cforge command and captures the output as a string.
        /// </summary>
        public static async Task<string?> RunAndCaptureAsync(string arguments, string? workingDirectory = null)
        {
            string? projectDir = workingDirectory ?? await GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
            {
                return null;
            }

            string cforgeExe = GetCforgeExecutable();

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

                using (var process = new Process { StartInfo = startInfo })
                {
                    var output = new StringBuilder();

                    process.OutputDataReceived += (s, e) =>
                    {
                        if (e.Data != null)
                        {
                            string cleanLine = StripAnsiCodes(e.Data);
                            output.AppendLine(cleanLine);
                        }
                    };

                    process.ErrorDataReceived += (s, e) =>
                    {
                        if (e.Data != null)
                        {
                            string cleanLine = StripAnsiCodes(e.Data);
                            output.AppendLine(cleanLine);
                        }
                    };

                    process.Start();
                    process.BeginOutputReadLine();
                    process.BeginErrorReadLine();

                    // .NET Framework 4.8 doesn't have WaitForExitAsync
                    await Task.Run(() => process.WaitForExit());

                    return output.ToString();
                }
            }
            catch (Exception)
            {
                return null;
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
            bool buildSuccess = await RunAsync(buildArgs, projectDir!);

            if (!buildSuccess)
            {
                return false;
            }

            // Find and run the executable
            var project = CforgeTomlParser.ParseProject(projectDir!);
            if (project != null && project.Type == "executable")
            {
                // Use FindExecutable to check multiple possible paths
                string? exePath = CforgeTomlParser.FindExecutable(projectDir!, project, config);
                if (exePath != null && File.Exists(exePath))
                {
                    return await RunExecutableInConsoleAsync(exePath, projectDir!);
                }
                else
                {
                    string expectedPath = Path.Combine(projectDir!, project.OutputDir, "bin", config, project.Name + ".exe");
                    await pane.WriteLineAsync($"Error: Executable not found at {expectedPath}");
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
                string? sourceDir = VcxprojGenerator.GetSourceDirForCurrentSolution(solution.FullPath!);
                if (!string.IsNullOrEmpty(sourceDir) && IsCforgeProject(sourceDir!))
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
            // Remove all ANSI escape sequences (colors, cursor movement, clearing, etc.)
            // Pattern matches: ESC[ followed by any params and ending with a letter
            // Also matches: ESC] (OSC), ESC( (charset), and other escape sequences
            return System.Text.RegularExpressions.Regex.Replace(input,
                @"\x1b(?:\[[0-9;?]*[a-zA-Z]|\][^\x07]*\x07|\([0-9A-Za-z]|[=>])", "");
        }

        /// <summary>
        /// Builds and then launches the debugger.
        /// </summary>
        public static async Task<bool> BuildAndDebugAsync(string? workingDirectory = null, string config = "Debug")
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
            bool buildSuccess = await RunAsync(buildArgs, projectDir!);

            if (!buildSuccess)
            {
                await pane.WriteLineAsync("Build failed. Cannot start debugger.");
                return false;
            }

            // Find the executable
            var project = CforgeTomlParser.ParseProject(projectDir!);
            if (project != null && project.Type == "executable")
            {
                // Use FindExecutable to check multiple possible locations
                string? exePath = CforgeTomlParser.FindExecutable(projectDir!, project, config);

                if (exePath != null && File.Exists(exePath))
                {
                    await pane.WriteLineAsync($"Starting debugger: {exePath}");
                    return await LaunchDebuggerAsync(exePath, projectDir!);
                }
                else
                {
                    // Show all checked paths for debugging
                    string expectedPath = CforgeTomlParser.GetOutputPath(project, config);
                    await pane.WriteLineAsync($"Error: Executable not found.");
                    await pane.WriteLineAsync($"Expected location: {Path.Combine(projectDir, expectedPath)}");
                    await pane.WriteLineAsync($"Make sure the project has been built successfully.");
                    return false;
                }
            }
            else if (project != null)
            {
                await pane.WriteLineAsync($"Warning: Project type is '{project.Type}', cannot debug directly.");
                return false;
            }
            else
            {
                await pane.WriteLineAsync("Error: Could not parse cforge.toml");
                return false;
            }
        }

        /// <summary>
        /// Launches the VS debugger for the specified executable.
        /// </summary>
        private static async Task<bool> LaunchDebuggerAsync(string exePath, string workingDirectory)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var pane = await GetOutputPaneAsync();

            try
            {
                await pane.WriteLineAsync($"Launching debugger for: {exePath}");

                // Use IVsDebugger with VsDebugTargetInfo for proper native debugging
                var debugger = await VS.GetServiceAsync<SVsShellDebugger, IVsDebugger>();
                if (debugger == null)
                {
                    await pane.WriteLineAsync("Error: Could not get IVsDebugger service");
                    return false;
                }

                var targetInfo = new VsDebugTargetInfo();
                targetInfo.cbSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf(targetInfo);
                targetInfo.dlo = DEBUG_LAUNCH_OPERATION.DLO_CreateProcess;
                targetInfo.bstrExe = exePath;
                targetInfo.bstrCurDir = workingDirectory;
                targetInfo.bstrArg = "";
                targetInfo.bstrRemoteMachine = null;
                targetInfo.fSendStdoutToOutputWindow = 0;
                targetInfo.grfLaunch = (uint)__VSDBGLAUNCHFLAGS.DBGLAUNCH_StopDebuggingOnEnd;

                // Use the Native debug engine
                targetInfo.clsidPortSupplier = Guid.Empty;
                targetInfo.bstrPortName = "";

                // Native Only debug engine GUID - set via clsidCustom
                targetInfo.clsidCustom = new Guid("3B476D35-A401-11D2-AAD4-00C04F990171");

                IntPtr pInfo = System.Runtime.InteropServices.Marshal.AllocCoTaskMem((int)targetInfo.cbSize);
                try
                {
                    System.Runtime.InteropServices.Marshal.StructureToPtr(targetInfo, pInfo, false);
                    int hr = debugger.LaunchDebugTargets(1, pInfo);

                    if (hr == 0)
                    {
                        await pane.WriteLineAsync("Debugger launched successfully.");
                        return true;
                    }
                    else
                    {
                        await pane.WriteLineAsync($"LaunchDebugTargets failed with HRESULT: 0x{hr:X8}");
                    }
                }
                finally
                {
                    System.Runtime.InteropServices.Marshal.FreeCoTaskMem(pInfo);
                }

                // Fallback: Try mixed mode (native + managed)
                await pane.WriteLineAsync("Trying mixed mode debugging...");
                targetInfo.clsidCustom = Guid.Empty; // Let VS choose

                pInfo = System.Runtime.InteropServices.Marshal.AllocCoTaskMem((int)targetInfo.cbSize);
                try
                {
                    System.Runtime.InteropServices.Marshal.StructureToPtr(targetInfo, pInfo, false);
                    int hr = debugger.LaunchDebugTargets(1, pInfo);

                    if (hr == 0)
                    {
                        await pane.WriteLineAsync("Debugger launched successfully (mixed mode).");
                        return true;
                    }
                    else
                    {
                        await pane.WriteLineAsync($"Mixed mode launch failed with HRESULT: 0x{hr:X8}");
                    }
                }
                finally
                {
                    System.Runtime.InteropServices.Marshal.FreeCoTaskMem(pInfo);
                }
            }
            catch (Exception ex)
            {
                await pane.WriteLineAsync($"Error launching debugger: {ex.Message}");
            }

            await pane.WriteLineAsync("Falling back to console execution...");
            await RunExecutableInConsoleAsync(exePath, workingDirectory);
            return false;
        }

        /// <summary>
        /// Runs the watch command in the background.
        /// </summary>
        public static async Task RunWatchAsync(string? workingDirectory = null)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Stop any existing watch process
            StopWatch();

            var pane = await GetOutputPaneAsync();
            await pane.ActivateAsync();
            await pane.ClearAsync();

            string? projectDir = workingDirectory ?? await GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
            {
                await pane.WriteLineAsync("Error: No cforge project found.");
                return;
            }

            string cforgeExe = GetCforgeExecutable();
            await pane.WriteLineAsync($"> cforge watch");
            await pane.WriteLineAsync("Watching for file changes... (use Stop Watch to terminate)");
            await pane.WriteLineAsync("");

            try
            {
                var startInfo = new ProcessStartInfo
                {
                    FileName = cforgeExe,
                    Arguments = "watch",
                    WorkingDirectory = projectDir,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    StandardOutputEncoding = Encoding.UTF8,
                    StandardErrorEncoding = Encoding.UTF8
                };

                _watchProcess = new Process { StartInfo = startInfo };

                _watchProcess.OutputDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        string cleanLine = StripAnsiCodes(e.Data);
                        _ = pane.WriteLineAsync(cleanLine);
                    }
                };

                _watchProcess.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        string cleanLine = StripAnsiCodes(e.Data);
                        _ = pane.WriteLineAsync(cleanLine);
                    }
                };

                _watchProcess.Start();
                _watchProcess.BeginOutputReadLine();
                _watchProcess.BeginErrorReadLine();

                await VS.StatusBar.ShowMessageAsync("cforge watch started");
            }
            catch (Exception ex)
            {
                await pane.WriteLineAsync($"Error starting watch: {ex.Message}");
            }
        }

        /// <summary>
        /// Stops the watch process if running.
        /// </summary>
        public static void StopWatch()
        {
            if (_watchProcess != null && !_watchProcess.HasExited)
            {
                try
                {
                    _watchProcess.Kill();
                    _watchProcess.Dispose();
                    _ = VS.StatusBar.ShowMessageAsync("cforge watch stopped");
                }
                catch { }
                finally
                {
                    _watchProcess = null;
                }
            }
        }

        /// <summary>
        /// Stops the current running cforge process.
        /// </summary>
        public static void StopCurrentProcess()
        {
            if (_currentProcess != null && !_currentProcess.HasExited)
            {
                try
                {
                    _currentProcess.Kill();
                    _currentProcess.Dispose();
                    _ = VS.StatusBar.ShowMessageAsync("Process stopped");
                }
                catch { }
                finally
                {
                    _currentProcess = null;
                }
            }
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
                    infoBar.ActionItemClicked += (sender, args) =>
                    {
                        _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
                        {
                            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
                            if (args.ActionItem.Text == "Show Error List")
                            {
                                var provider = await GetErrorListProviderAsync();
                                provider.Show();
                                provider.BringToFront();
                            }
                        });
                    };

                    await infoBar.TryShowInfoBarUIAsync();
                }
            }
            catch
            {
                // InfoBar not available, that's okay - we still have status bar and Error List
            }
        }

        #region Test Running

        // Regex patterns for parsing test output
        // Catch2 patterns
        private static readonly Regex Catch2TestCasePattern = new Regex(
            @"^(?<file>.+?)\((?<line>\d+)\):\s*(?:PASSED|FAILED):\s*(?<name>.+)$",
            RegexOptions.Compiled);
        private static readonly Regex Catch2SummaryPattern = new Regex(
            @"^(?:All tests passed|test cases?:\s*(?<total>\d+)\s*\|\s*(?<passed>\d+)\s*passed(?:\s*\|\s*(?<failed>\d+)\s*failed)?)",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);
        private static readonly Regex Catch2TestPassedPattern = new Regex(
            @"^(?<name>.+?)\s+passed\s*$",
            RegexOptions.Compiled);
        private static readonly Regex Catch2TestFailedPattern = new Regex(
            @"^(?<file>.+?)\((?<line>\d+)\):\s*FAILED:",
            RegexOptions.Compiled);
        private static readonly Regex Catch2AssertionPattern = new Regex(
            @"^\s*REQUIRE\(?\s*(?<expr>.+?)\s*\)?\s*$",
            RegexOptions.Compiled);
        private static readonly Regex Catch2RunningTestPattern = new Regex(
            @"^-------------------------------------------------------------------------------$",
            RegexOptions.Compiled);
        private static readonly Regex Catch2TestNamePattern = new Regex(
            @"^(?<name>[^-].+)$",
            RegexOptions.Compiled);

        // Google Test patterns
        private static readonly Regex GTestRunPattern = new Regex(
            @"^\[\s*RUN\s*\]\s*(?<suite>\w+)\.(?<name>\w+)",
            RegexOptions.Compiled);
        private static readonly Regex GTestOkPattern = new Regex(
            @"^\[\s*OK\s*\]\s*(?<suite>\w+)\.(?<name>\w+)\s*\((?<time>\d+)\s*ms\)",
            RegexOptions.Compiled);
        private static readonly Regex GTestFailedPattern = new Regex(
            @"^\[\s*FAILED\s*\]\s*(?<suite>\w+)\.(?<name>\w+)",
            RegexOptions.Compiled);
        private static readonly Regex GTestSummaryPattern = new Regex(
            @"^\[=+\]\s*(?<total>\d+)\s*tests?\s*from\s*(?<suites>\d+)\s*test\s*suites?\s*ran",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);
        private static readonly Regex GTestPassedSummaryPattern = new Regex(
            @"^\[\s*PASSED\s*\]\s*(?<count>\d+)\s*tests?",
            RegexOptions.Compiled);
        private static readonly Regex GTestFailedSummaryPattern = new Regex(
            @"^\[\s*FAILED\s*\]\s*(?<count>\d+)\s*tests?",
            RegexOptions.Compiled);

        // doctest patterns
        private static readonly Regex DoctestSummaryPattern = new Regex(
            @"^\[doctest\]\s*test cases:\s*(?<total>\d+)\s*\|\s*(?<passed>\d+)\s*passed\s*\|\s*(?<failed>\d+)\s*failed",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        // cforge builtin test framework patterns
        // [RUN] TestName or [RUN] Category.TestName
        private static readonly Regex BuiltinRunPattern = new Regex(
            @"^\[RUN\]\s+(?<name>.+)$",
            RegexOptions.Compiled);
        // [PASS] TestName
        private static readonly Regex BuiltinPassPattern = new Regex(
            @"^\[PASS\]\s+(?<name>.+)$",
            RegexOptions.Compiled);
        // [FAIL] TestName
        private static readonly Regex BuiltinFailPattern = new Regex(
            @"^\[FAIL\]\s+(?<name>.+)$",
            RegexOptions.Compiled);
        // Assertion failed: expr at file:line
        private static readonly Regex BuiltinAssertionPattern = new Regex(
            @"^Assertion failed:\s*(?<expr>.+)\s+at\s+(?<file>.+):(?<line>\d+)$",
            RegexOptions.Compiled);
        // cforge output formatter: "Testing test_name ... ok" or "test test_name ... FAILED"
        // Matches both "Testing" (colored output) and "test" (plain output)
        // Handles both three dots (...) and Unicode ellipsis (…)
        private static readonly Regex CforgeTestResultPattern = new Regex(
            @"^\s*(?:Testing|test)\s+(?<name>\S+)\s+(?:\.\.\.|…)\s+(?<result>ok|FAILED|ignored|TIMEOUT)",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);
        // cforge summary: "Finished X passed" or "test result: ok. X passed; Y failed"
        private static readonly Regex CforgeSummaryPattern = new Regex(
            @"^test result:\s*(?<status>ok|FAILED)\.\s*(?<passed>\d+)\s*passed;\s*(?<failed>\d+)\s*failed",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);
        // cforge Finished line: "Finished 40 passed in 2.60s" or "Failed 5 passed, 2 failed in 1.00s"
        private static readonly Regex CforgeFinishedPattern = new Regex(
            @"^\s*(?:Finished|Failed)\s+(?<passed>\d+)\s*passed(?:,\s*(?<failed>\d+)\s*failed)?",
            RegexOptions.IgnoreCase | RegexOptions.Compiled);

        /// <summary>
        /// Runs the test command and captures results for the Test Results window.
        /// </summary>
        public static async Task<TestRunResult> RunTestAsync(string? workingDirectory = null)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var pane = await GetOutputPaneAsync();
            await pane.ActivateAsync();
            await pane.ClearAsync();

            // Clear previous errors from Error List
            await ClearErrorListAsync();

            string? projectDir = workingDirectory ?? await GetProjectDirectoryAsync();
            var result = new TestRunResult();
            var startTime = DateTime.Now;

            // Auto-open Test Results window and show running state
            var windowPane = await TestResultsWindow.ShowWindowAsync();
            TestResultsWindowControl? windowControl = null;
            if (windowPane?.Content is TestResultsWindowControl control)
            {
                windowControl = control;
                windowControl.ShowTestsRunning();
            }

            if (string.IsNullOrEmpty(projectDir))
            {
                await pane.WriteLineAsync("Error: No cforge project found. Open a folder containing cforge.toml.");
                result.ExitCode = 1;
                return result;
            }

            string cforgeExe = GetCforgeExecutable();
            await pane.WriteLineAsync($"> cforge test");
            await pane.WriteLineAsync("");

            try
            {
                var startInfo = new ProcessStartInfo
                {
                    FileName = cforgeExe,
                    Arguments = "test",
                    WorkingDirectory = projectDir,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true,
                    StandardOutputEncoding = Encoding.UTF8,
                    StandardErrorEncoding = Encoding.UTF8
                };

                var outputLines = new List<string>();
                var currentSuite = new TestSuiteResult { Name = "Tests" };
                result.Suites.Add(currentSuite);
                TestCaseResult? currentTest = null;
                string? pendingTestName = null;
                bool inTestOutput = false;
                var testOutput = new StringBuilder();

                using var process = new Process { StartInfo = startInfo };
                _currentProcess = process;

                process.OutputDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        string cleanLine = StripAnsiCodes(e.Data);
                        outputLines.Add(cleanLine);
                        result.OutputLog.Add(cleanLine);
                        _ = pane.WriteLineAsync(cleanLine);

                        // Parse test output
                        ParseTestLine(cleanLine, result, currentSuite, ref currentTest, ref pendingTestName, ref inTestOutput, testOutput, projectDir, windowControl);
                    }
                };

                process.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        string cleanLine = StripAnsiCodes(e.Data);
                        outputLines.Add(cleanLine);
                        result.OutputLog.Add(cleanLine);
                        _ = pane.WriteLineAsync(cleanLine);

                        ParseTestLine(cleanLine, result, currentSuite, ref currentTest, ref pendingTestName, ref inTestOutput, testOutput, projectDir, windowControl);
                    }
                };

                process.Start();
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();

                await Task.Run(() => process.WaitForExit());

                result.ExitCode = process.ExitCode;
                result.Duration = DateTime.Now - startTime;

                // Finalize any pending test
                if (currentTest != null && currentTest.Outcome == TestOutcome.Running)
                {
                    currentTest.Outcome = result.ExitCode == 0 ? TestOutcome.Passed : TestOutcome.Failed;
                    if (inTestOutput)
                    {
                        currentTest.StdOut = testOutput.ToString();
                    }
                }

                // Recalculate totals
                result.RecalculateTotals();

                // If we couldn't parse individual tests, create a summary
                if (result.TotalTests == 0 && outputLines.Count > 0)
                {
                    // Try to parse summary from output
                    ParseTestSummary(outputLines, result, currentSuite);
                }

                await pane.WriteLineAsync("");
                bool success = result.ExitCode == 0;

                if (success)
                {
                    await pane.WriteLineAsync($"All tests passed! ({result.Passed} passed)");
                    await VS.StatusBar.ShowMessageAsync($"Tests passed: {result.Passed}");
                }
                else
                {
                    await pane.WriteLineAsync($"Tests completed with failures: {result.Failed} failed, {result.Passed} passed");
                    await VS.StatusBar.ShowMessageAsync($"Tests failed: {result.Failed} of {result.TotalTests}");
                }

                // Update Test Results window with final results
                if (windowControl != null)
                {
                    windowControl.ShowResult(result);
                }

                // Fire completion event
                OnTestCompleted(result);

                _currentProcess = null;
                return result;
            }
            catch (Exception ex)
            {
                await pane.WriteLineAsync($"Error running tests: {ex.Message}");
                result.ExitCode = -1;
                result.Duration = DateTime.Now - startTime;

                if (windowControl != null)
                {
                    windowControl.ShowResult(result);
                }

                _currentProcess = null;
                return result;
            }
        }

        private static void ParseTestLine(string line, TestRunResult result, TestSuiteResult currentSuite,
            ref TestCaseResult? currentTest, ref string? pendingTestName, ref bool inTestOutput,
            StringBuilder testOutput, string? projectDir, TestResultsWindowControl? windowControl)
        {
            // Google Test patterns
            var gtestRunMatch = GTestRunPattern.Match(line);
            if (gtestRunMatch.Success)
            {
                // Finalize previous test
                if (currentTest != null && currentTest.Outcome == TestOutcome.Running)
                {
                    currentTest.Outcome = TestOutcome.Passed;
                    if (inTestOutput)
                    {
                        currentTest.StdOut = testOutput.ToString();
                        testOutput.Clear();
                    }
                }

                string suiteName = gtestRunMatch.Groups["suite"].Value;
                string testName = gtestRunMatch.Groups["name"].Value;

                // Find or create suite
                var suite = result.Suites.FirstOrDefault(s => s.Name == suiteName);
                if (suite == null)
                {
                    suite = new TestSuiteResult { Name = suiteName };
                    result.Suites.Add(suite);
                }

                currentTest = new TestCaseResult
                {
                    Name = testName,
                    FullName = $"{suiteName}.{testName}",
                    Outcome = TestOutcome.Running
                };
                suite.TestCases.Add(currentTest);
                inTestOutput = true;
                result.Framework = "Google Test";

                // Update progress
                UpdateTestProgress(windowControl, currentTest.FullName, result);
                return;
            }

            var gtestOkMatch = GTestOkPattern.Match(line);
            if (gtestOkMatch.Success)
            {
                string suiteName = gtestOkMatch.Groups["suite"].Value;
                string testName = gtestOkMatch.Groups["name"].Value;
                int.TryParse(gtestOkMatch.Groups["time"].Value, out int timeMs);

                var test = FindOrCreateTest(result, suiteName, testName);
                test.Outcome = TestOutcome.Passed;
                test.Duration = TimeSpan.FromMilliseconds(timeMs);
                if (inTestOutput)
                {
                    test.StdOut = testOutput.ToString();
                    testOutput.Clear();
                }
                inTestOutput = false;
                currentTest = null;

                UpdateTestProgress(windowControl, null, result);
                return;
            }

            var gtestFailedMatch = GTestFailedPattern.Match(line);
            if (gtestFailedMatch.Success)
            {
                string suiteName = gtestFailedMatch.Groups["suite"].Value;
                string testName = gtestFailedMatch.Groups["name"].Value;

                var test = FindOrCreateTest(result, suiteName, testName);
                test.Outcome = TestOutcome.Failed;
                if (inTestOutput)
                {
                    test.StdOut = testOutput.ToString();
                    testOutput.Clear();
                }
                inTestOutput = false;
                currentTest = null;

                UpdateTestProgress(windowControl, null, result);
                return;
            }

            // Catch2 patterns - detect test separator
            if (line.StartsWith("-------------------------------------------------------------------------------"))
            {
                // Finalize previous test if running
                if (currentTest != null && currentTest.Outcome == TestOutcome.Running)
                {
                    currentTest.Outcome = TestOutcome.Passed; // Assume passed if no failure seen
                    if (inTestOutput)
                    {
                        currentTest.StdOut = testOutput.ToString();
                        testOutput.Clear();
                    }
                }
                pendingTestName = null;
                inTestOutput = false;
                result.Framework = "Catch2";
                return;
            }

            // Catch2 test name (line after separator)
            if (pendingTestName == null && !string.IsNullOrWhiteSpace(line) && !line.StartsWith(" ") &&
                !line.Contains("test case") && !line.Contains("assertion") && !line.Contains("FAILED") &&
                !line.Contains("PASSED") && !line.Contains("All tests") && result.Framework == "Catch2")
            {
                // This might be a test name
                pendingTestName = line.Trim();
                return;
            }

            // Catch2 source location line
            if (pendingTestName != null && line.Contains("..............."))
            {
                // Previous line was test name, now we have location
                currentTest = new TestCaseResult
                {
                    Name = pendingTestName,
                    FullName = pendingTestName,
                    Outcome = TestOutcome.Running
                };
                currentSuite.TestCases.Add(currentTest);
                pendingTestName = null;
                inTestOutput = true;
                testOutput.Clear();

                UpdateTestProgress(windowControl, currentTest.FullName, result);
                return;
            }

            // Catch2 FAILED assertion
            var catch2FailedMatch = Catch2TestFailedPattern.Match(line);
            if (catch2FailedMatch.Success && currentTest != null)
            {
                currentTest.Outcome = TestOutcome.Failed;
                string file = catch2FailedMatch.Groups["file"].Value;
                int.TryParse(catch2FailedMatch.Groups["line"].Value, out int lineNum);
                currentTest.SourceFile = NormalizeFilePath(file, projectDir);
                currentTest.SourceLine = lineNum;
                return;
            }

            // doctest summary
            var doctestMatch = DoctestSummaryPattern.Match(line);
            if (doctestMatch.Success)
            {
                int.TryParse(doctestMatch.Groups["total"].Value, out int total);
                int.TryParse(doctestMatch.Groups["passed"].Value, out int passed);
                int.TryParse(doctestMatch.Groups["failed"].Value, out int failed);

                result.TotalTests = total;
                result.Passed = passed;
                result.Failed = failed;
                result.Framework = "doctest";
                return;
            }

            // cforge builtin test framework: [RUN] TestName
            var builtinRunMatch = BuiltinRunPattern.Match(line);
            if (builtinRunMatch.Success)
            {
                // Finalize previous test
                if (currentTest != null && currentTest.Outcome == TestOutcome.Running)
                {
                    currentTest.Outcome = TestOutcome.Passed;
                    if (inTestOutput)
                    {
                        currentTest.StdOut = testOutput.ToString();
                        testOutput.Clear();
                    }
                }

                string testName = builtinRunMatch.Groups["name"].Value.Trim();
                string suiteName = "Tests";
                string localTestName = testName;

                // Check for Category.TestName format
                int dotIndex = testName.IndexOf('.');
                if (dotIndex > 0)
                {
                    suiteName = testName.Substring(0, dotIndex);
                    localTestName = testName.Substring(dotIndex + 1);
                }

                // Find or create suite
                var suite = result.Suites.FirstOrDefault(s => s.Name == suiteName);
                if (suite == null)
                {
                    suite = new TestSuiteResult { Name = suiteName };
                    result.Suites.Add(suite);
                }

                currentTest = new TestCaseResult
                {
                    Name = localTestName,
                    FullName = testName,
                    Outcome = TestOutcome.Running
                };
                suite.TestCases.Add(currentTest);
                inTestOutput = true;
                testOutput.Clear();
                result.Framework = "cforge builtin";

                UpdateTestProgress(windowControl, testName, result);
                return;
            }

            // cforge builtin: [PASS] TestName
            var builtinPassMatch = BuiltinPassPattern.Match(line);
            if (builtinPassMatch.Success)
            {
                string testName = builtinPassMatch.Groups["name"].Value.Trim();
                var test = FindTestByFullName(result, testName);
                if (test != null)
                {
                    test.Outcome = TestOutcome.Passed;
                    if (inTestOutput)
                    {
                        test.StdOut = testOutput.ToString();
                        testOutput.Clear();
                    }
                }
                inTestOutput = false;
                currentTest = null;

                UpdateTestProgress(windowControl, null, result);
                return;
            }

            // cforge builtin: [FAIL] TestName
            var builtinFailMatch = BuiltinFailPattern.Match(line);
            if (builtinFailMatch.Success)
            {
                string testName = builtinFailMatch.Groups["name"].Value.Trim();
                var test = FindTestByFullName(result, testName);
                if (test != null)
                {
                    test.Outcome = TestOutcome.Failed;
                    if (inTestOutput)
                    {
                        test.StdOut = testOutput.ToString();
                        testOutput.Clear();
                    }
                }
                inTestOutput = false;
                currentTest = null;

                UpdateTestProgress(windowControl, null, result);
                return;
            }

            // cforge builtin assertion failure: Assertion failed: expr at file:line
            var builtinAssertMatch = BuiltinAssertionPattern.Match(line);
            if (builtinAssertMatch.Success && currentTest != null)
            {
                currentTest.Outcome = TestOutcome.Failed;
                currentTest.ErrorMessage = $"Assertion failed: {builtinAssertMatch.Groups["expr"].Value}";
                string file = builtinAssertMatch.Groups["file"].Value;
                int.TryParse(builtinAssertMatch.Groups["line"].Value, out int lineNum);
                currentTest.SourceFile = NormalizeFilePath(file, projectDir);
                currentTest.SourceLine = lineNum;
                return;
            }

            // cforge output formatter style: "test test_name ... ok" or "test test_name ... FAILED"
            var cforgeTestMatch = CforgeTestResultPattern.Match(line);
            if (cforgeTestMatch.Success)
            {
                string testName = cforgeTestMatch.Groups["name"].Value;
                string resultStr = cforgeTestMatch.Groups["result"].Value;

                // Find or create the test
                string suiteName = "Tests";
                string localTestName = testName;
                int dotIndex = testName.IndexOf('.');
                if (dotIndex > 0)
                {
                    suiteName = testName.Substring(0, dotIndex);
                    localTestName = testName.Substring(dotIndex + 1);
                }

                var test = FindOrCreateTest(result, suiteName, localTestName);
                test.FullName = testName;

                switch (resultStr.ToLower())
                {
                    case "ok":
                        test.Outcome = TestOutcome.Passed;
                        break;
                    case "failed":
                    case "timeout":
                        test.Outcome = TestOutcome.Failed;
                        break;
                    case "ignored":
                        test.Outcome = TestOutcome.Skipped;
                        break;
                }

                result.Framework = "cforge";
                UpdateTestProgress(windowControl, null, result);
                return;
            }

            // cforge summary: "test result: ok. X passed; Y failed"
            var cforgeSummaryMatch = CforgeSummaryPattern.Match(line);
            if (cforgeSummaryMatch.Success)
            {
                int.TryParse(cforgeSummaryMatch.Groups["passed"].Value, out int passed);
                int.TryParse(cforgeSummaryMatch.Groups["failed"].Value, out int failed);
                result.Passed = passed;
                result.Failed = failed;
                result.TotalTests = passed + failed;
                return;
            }

            // cforge Finished line: "Finished 40 passed in 2.60s" or "Failed 5 passed, 2 failed in 1.00s"
            var cforgeFinishedMatch = CforgeFinishedPattern.Match(line);
            if (cforgeFinishedMatch.Success)
            {
                int.TryParse(cforgeFinishedMatch.Groups["passed"].Value, out int passed);
                int failed = 0;
                if (cforgeFinishedMatch.Groups["failed"].Success)
                {
                    int.TryParse(cforgeFinishedMatch.Groups["failed"].Value, out failed);
                }
                result.Passed = passed;
                result.Failed = failed;
                result.TotalTests = passed + failed;
                result.Framework = "cforge";
                return;
            }

            // Capture test output
            if (inTestOutput && currentTest != null)
            {
                testOutput.AppendLine(line);
            }
        }

        private static TestCaseResult? FindTestByFullName(TestRunResult result, string fullName)
        {
            foreach (var suite in result.Suites)
            {
                var test = suite.TestCases.FirstOrDefault(t => t.FullName == fullName || t.Name == fullName);
                if (test != null) return test;
            }
            return null;
        }

        private static TestCaseResult FindOrCreateTest(TestRunResult result, string suiteName, string testName)
        {
            var suite = result.Suites.FirstOrDefault(s => s.Name == suiteName);
            if (suite == null)
            {
                suite = new TestSuiteResult { Name = suiteName };
                result.Suites.Add(suite);
            }

            var test = suite.TestCases.FirstOrDefault(t => t.Name == testName);
            if (test == null)
            {
                test = new TestCaseResult
                {
                    Name = testName,
                    FullName = $"{suiteName}.{testName}"
                };
                suite.TestCases.Add(test);
            }
            return test;
        }

        private static void ParseTestSummary(List<string> lines, TestRunResult result, TestSuiteResult defaultSuite)
        {
            foreach (var line in lines)
            {
                // Catch2 summary
                var catch2Match = Catch2SummaryPattern.Match(line);
                if (catch2Match.Success)
                {
                    if (line.Contains("All tests passed"))
                    {
                        // Count from other output
                        result.Framework = "Catch2";
                    }
                    else
                    {
                        int.TryParse(catch2Match.Groups["total"].Value, out int total);
                        int.TryParse(catch2Match.Groups["passed"].Value, out int passed);
                        int.TryParse(catch2Match.Groups["failed"].Value, out int failed);

                        result.TotalTests = total;
                        result.Passed = passed;
                        result.Failed = failed;
                        result.Framework = "Catch2";
                    }
                    continue;
                }

                // Google Test summary
                var gtestSummaryMatch = GTestSummaryPattern.Match(line);
                if (gtestSummaryMatch.Success)
                {
                    int.TryParse(gtestSummaryMatch.Groups["total"].Value, out int total);
                    result.TotalTests = total;
                    result.Framework = "Google Test";
                    continue;
                }

                var gtestPassedMatch = GTestPassedSummaryPattern.Match(line);
                if (gtestPassedMatch.Success)
                {
                    int.TryParse(gtestPassedMatch.Groups["count"].Value, out int passed);
                    result.Passed = passed;
                    continue;
                }

                var gtestFailedSummaryMatch = GTestFailedSummaryPattern.Match(line);
                if (gtestFailedSummaryMatch.Success)
                {
                    int.TryParse(gtestFailedSummaryMatch.Groups["count"].Value, out int failed);
                    result.Failed = failed;
                    continue;
                }

                // doctest summary
                var doctestMatch = DoctestSummaryPattern.Match(line);
                if (doctestMatch.Success)
                {
                    int.TryParse(doctestMatch.Groups["total"].Value, out int total);
                    int.TryParse(doctestMatch.Groups["passed"].Value, out int passed);
                    int.TryParse(doctestMatch.Groups["failed"].Value, out int failed);

                    result.TotalTests = total;
                    result.Passed = passed;
                    result.Failed = failed;
                    result.Framework = "doctest";
                    continue;
                }

                // cforge summary: "test result: ok. X passed; Y failed"
                var cforgeSummaryMatch = CforgeSummaryPattern.Match(line);
                if (cforgeSummaryMatch.Success)
                {
                    int.TryParse(cforgeSummaryMatch.Groups["passed"].Value, out int passed);
                    int.TryParse(cforgeSummaryMatch.Groups["failed"].Value, out int failed);
                    result.Passed = passed;
                    result.Failed = failed;
                    result.TotalTests = passed + failed;
                    result.Framework = "cforge";
                    continue;
                }

                // cforge Finished line: "Finished 40 passed in 2.60s"
                var cforgeFinishedMatch = CforgeFinishedPattern.Match(line);
                if (cforgeFinishedMatch.Success)
                {
                    int.TryParse(cforgeFinishedMatch.Groups["passed"].Value, out int passed);
                    int failed = 0;
                    if (cforgeFinishedMatch.Groups["failed"].Success)
                    {
                        int.TryParse(cforgeFinishedMatch.Groups["failed"].Value, out failed);
                    }
                    result.Passed = passed;
                    result.Failed = failed;
                    result.TotalTests = passed + failed;
                    result.Framework = "cforge";
                    continue;
                }
            }

            // If we parsed totals but no individual tests, create placeholder tests
            if (result.TotalTests > 0 && defaultSuite.TestCases.Count == 0)
            {
                for (int i = 0; i < result.Passed; i++)
                {
                    defaultSuite.TestCases.Add(new TestCaseResult
                    {
                        Name = $"Test {i + 1}",
                        FullName = $"Tests.Test {i + 1}",
                        Outcome = TestOutcome.Passed
                    });
                }
                for (int i = 0; i < result.Failed; i++)
                {
                    defaultSuite.TestCases.Add(new TestCaseResult
                    {
                        Name = $"Failed Test {i + 1}",
                        FullName = $"Tests.Failed Test {i + 1}",
                        Outcome = TestOutcome.Failed
                    });
                }
            }
        }

        private static void UpdateTestProgress(TestResultsWindowControl? windowControl, string? currentTest, TestRunResult result)
        {
            if (windowControl == null) return;

            int passed = 0, failed = 0, total = 0;
            foreach (var suite in result.Suites)
            {
                foreach (var test in suite.TestCases)
                {
                    total++;
                    if (test.Outcome == TestOutcome.Passed) passed++;
                    else if (test.Outcome == TestOutcome.Failed) failed++;
                }
            }

            var args = new TestProgressEventArgs
            {
                CurrentTest = currentTest,
                CompletedCount = passed + failed,
                TotalCount = total,
                PassedCount = passed,
                FailedCount = failed
            };

            windowControl.UpdateProgress(args);
            OnTestProgress(args);
        }

        #endregion

        #region Workspace Support

        /// <summary>
        /// Runs a cforge command for a specific workspace member.
        /// </summary>
        public static async Task<bool> RunForMemberAsync(string command, string? memberPath)
        {
            // If we have a specific member, build in that directory
            if (!string.IsNullOrEmpty(memberPath))
            {
                string? workspaceDir = await GetProjectDirectoryAsync();
                if (!string.IsNullOrEmpty(workspaceDir))
                {
                    string memberDir = Path.Combine(workspaceDir, memberPath);
                    return await RunAsync(command, memberDir);
                }
            }

            // Otherwise run in workspace root
            return await RunAsync(command);
        }

        /// <summary>
        /// Gets the executable path for the active project (workspace-aware).
        /// </summary>
        public static async Task<string?> GetActiveProjectExecutableAsync(string configuration = "Debug")
        {
            // Check if we're in a workspace with an active member
            if (WorkspaceState.Instance.IsWorkspace)
            {
                return WorkspaceState.Instance.GetActiveProjectExecutable(configuration);
            }

            // Single project mode
            string? projectDir = await GetProjectDirectoryAsync();
            if (string.IsNullOrEmpty(projectDir))
                return null;

            var project = CforgeTomlParser.ParseProject(projectDir);
            if (project == null || project.Type != "executable")
                return null;

            return CforgeTomlParser.FindExecutable(projectDir, project, configuration);
        }

        /// <summary>
        /// Builds the active project (workspace-aware).
        /// </summary>
        public static async Task<bool> BuildActiveProjectAsync(string configuration = "Debug")
        {
            string buildArgs = configuration == "Release" ? "build -c Release" : "build";

            if (WorkspaceState.Instance.IsWorkspace)
            {
                string? memberPath = WorkspaceState.Instance.ActiveMemberPath;
                return await RunForMemberAsync(buildArgs, memberPath);
            }

            return await RunAsync(buildArgs);
        }

        /// <summary>
        /// Runs the active project (workspace-aware).
        /// </summary>
        public static async Task<bool> RunActiveProjectAsync(string configuration = "Debug")
        {
            if (WorkspaceState.Instance.IsWorkspace)
            {
                string? targetDir = WorkspaceState.Instance.GetBuildTargetDir();
                if (!string.IsNullOrEmpty(targetDir))
                {
                    return await BuildAndRunInConsoleAsync(targetDir, configuration);
                }
            }

            return await BuildAndRunInConsoleAsync(null, configuration);
        }

        /// <summary>
        /// Debugs the active project (workspace-aware).
        /// </summary>
        public static async Task<bool> DebugActiveProjectAsync(string configuration = "Debug")
        {
            if (WorkspaceState.Instance.IsWorkspace)
            {
                string? targetDir = WorkspaceState.Instance.GetBuildTargetDir();
                if (!string.IsNullOrEmpty(targetDir))
                {
                    return await BuildAndDebugAsync(targetDir, configuration);
                }
            }

            return await BuildAndDebugAsync(null, configuration);
        }

        #endregion

        #region Event Helpers

        private static void OnBuildProgress(BuildProgressEventArgs args)
        {
            BuildProgress?.Invoke(null, args);
        }

        private static void OnBuildCompleted(BuildResult result)
        {
            _currentBuildResult = result;
            BuildCompleted?.Invoke(null, new BuildCompletedEventArgs(result));
        }

        private static void OnTestProgress(TestProgressEventArgs args)
        {
            TestProgress?.Invoke(null, args);
        }

        private static void OnTestCompleted(TestRunResult result)
        {
            _currentTestResult = result;
            TestCompleted?.Invoke(null, new TestCompletedEventArgs(result));
        }

        #endregion
    }
}
