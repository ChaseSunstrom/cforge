using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using Microsoft.VisualStudio.TestPlatform.ObjectModel;
using Microsoft.VisualStudio.TestPlatform.ObjectModel.Adapter;
using Microsoft.VisualStudio.TestPlatform.ObjectModel.Logging;

namespace CforgeVS.TestAdapter
{
    /// <summary>
    /// Discovers cforge tests by parsing test source files and running cforge test --list.
    /// Registers for .toml, .cpp, and .exe files to ensure test discovery works in all scenarios.
    /// </summary>
    [FileExtension(".toml")]
    [FileExtension(".cpp")]
    [FileExtension(".exe")]
    [DefaultExecutorUri(CforgeTestExecutor.ExecutorUri)]
    public class CforgeTestDiscoverer : ITestDiscoverer
    {
        // Track which projects we've already discovered to avoid duplicates
        private static readonly HashSet<string> _discoveredProjects = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

        public void DiscoverTests(IEnumerable<string> sources, IDiscoveryContext discoveryContext,
            IMessageLogger logger, ITestCaseDiscoverySink discoverySink)
        {
            _discoveredProjects.Clear();

            logger?.SendMessage(TestMessageLevel.Informational, $"CForge: DiscoverTests called with {sources.Count()} sources");

            foreach (var source in sources)
            {
                logger?.SendMessage(TestMessageLevel.Informational, $"CForge: Checking source: {source}");

                string? projectDir = null;
                string? testSource = null;

                if (source.EndsWith("cforge.toml", StringComparison.OrdinalIgnoreCase))
                {
                    projectDir = Path.GetDirectoryName(source);
                    testSource = source;
                }
                else if (source.EndsWith(".cpp", StringComparison.OrdinalIgnoreCase))
                {
                    // Check if this .cpp file is in a cforge project
                    projectDir = FindCforgeProjectDir(source);
                    if (projectDir != null)
                        testSource = Path.Combine(projectDir, "cforge.toml");
                }
                else if (source.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                {
                    // Check if this exe is in a cforge project's build directory
                    projectDir = FindCforgeProjectDirFromBuildOutput(source);
                    if (projectDir != null)
                        testSource = Path.Combine(projectDir, "cforge.toml");
                }

                if (string.IsNullOrEmpty(projectDir) || string.IsNullOrEmpty(testSource))
                {
                    logger?.SendMessage(TestMessageLevel.Informational, $"CForge: No cforge project found for {source}");
                    continue;
                }

                // Skip if we've already discovered this project
                if (_discoveredProjects.Contains(projectDir)) continue;
                _discoveredProjects.Add(projectDir);

                // Verify it's a cforge project with tests
                if (!File.Exists(testSource)) continue;

                var testsDir = Path.Combine(projectDir, "tests");
                if (!Directory.Exists(testsDir))
                {
                    logger?.SendMessage(TestMessageLevel.Informational, $"CForge: No tests directory in {projectDir}");
                    continue;
                }

                logger?.SendMessage(TestMessageLevel.Informational, $"CForge: Discovering tests in {projectDir}");

                var tests = DiscoverTestsInProject(projectDir, testSource, logger);
                logger?.SendMessage(TestMessageLevel.Informational, $"CForge: Found {tests.Count} tests");

                foreach (var test in tests)
                {
                    discoverySink.SendTestCase(test);
                }
            }
        }

        /// <summary>
        /// Finds the cforge project directory for a source file.
        /// Walks up the directory tree looking for cforge.toml.
        /// </summary>
        private string? FindCforgeProjectDir(string sourceFile)
        {
            var dir = Path.GetDirectoryName(sourceFile);
            while (!string.IsNullOrEmpty(dir))
            {
                if (File.Exists(Path.Combine(dir, "cforge.toml")))
                    return dir;

                var parent = Path.GetDirectoryName(dir);
                if (parent == dir) break; // Root reached
                dir = parent;
            }
            return null;
        }

        /// <summary>
        /// Finds the cforge project directory from a build output (exe file).
        /// Looks for cforge.toml in parent directories or checks .cforge-source marker.
        /// </summary>
        private string? FindCforgeProjectDirFromBuildOutput(string exePath)
        {
            var dir = Path.GetDirectoryName(exePath);

            // First check if there's a .cforge-source marker (for generated solutions)
            while (!string.IsNullOrEmpty(dir))
            {
                var markerPath = Path.Combine(dir, ".cforge-source");
                if (File.Exists(markerPath))
                {
                    var sourceDir = File.ReadAllText(markerPath).Trim();
                    if (Directory.Exists(sourceDir) && File.Exists(Path.Combine(sourceDir, "cforge.toml")))
                        return sourceDir;
                }

                // Also check for cforge.toml directly (for builds in source tree)
                if (File.Exists(Path.Combine(dir, "cforge.toml")))
                    return dir;

                var parent = Path.GetDirectoryName(dir);
                if (parent == dir) break;
                dir = parent;
            }

            return null;
        }

        private List<TestCase> DiscoverTestsInProject(string projectDir, string source, IMessageLogger? logger)
        {
            var tests = new List<TestCase>();
            var testsDir = Path.Combine(projectDir, "tests");

            if (!Directory.Exists(testsDir))
            {
                logger?.SendMessage(TestMessageLevel.Informational, $"CForge: No tests directory found in {projectDir}");
                return tests;
            }

            // Try running cforge test --list first
            var listedTests = TryListTestsViaCforge(projectDir, source, logger);
            if (listedTests.Count > 0)
                return listedTests;

            // Fallback: parse test files directly
            var testFiles = Directory.GetFiles(testsDir, "*.cpp", SearchOption.AllDirectories);
            foreach (var testFile in testFiles)
            {
                var relPath = GetRelativePath(projectDir, testFile);
                tests.AddRange(ParseTestFile(testFile, source, relPath));
            }

            logger?.SendMessage(TestMessageLevel.Informational, $"CForge: Found {tests.Count} tests via file parsing");
            return tests;
        }

        private List<TestCase> TryListTestsViaCforge(string projectDir, string source, IMessageLogger? logger)
        {
            var tests = new List<TestCase>();

            try
            {
                var cforgePath = FindCforgeExecutable();
                if (string.IsNullOrEmpty(cforgePath))
                    return tests;

                var psi = new ProcessStartInfo
                {
                    FileName = cforgePath,
                    Arguments = "test --list",
                    WorkingDirectory = projectDir,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };

                using var process = Process.Start(psi);
                if (process == null) return tests;

                var output = process.StandardOutput.ReadToEnd();
                process.WaitForExit(10000);

                if (process.ExitCode == 0 && !string.IsNullOrEmpty(output))
                {
                    // Parse output - expected format: "test_name (file:line)"
                    var lines = output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);
                    foreach (var line in lines)
                    {
                        var match = Regex.Match(line.Trim(), @"^(.+?)(?:\s+\((.+?):(\d+)\))?$");
                        if (match.Success)
                        {
                            var testName = match.Groups[1].Value.Trim();
                            var testCase = new TestCase(testName, new Uri(CforgeTestExecutor.ExecutorUri), source)
                            {
                                DisplayName = testName,
                                CodeFilePath = match.Groups[2].Success ? match.Groups[2].Value : null,
                                LineNumber = match.Groups[3].Success ? int.Parse(match.Groups[3].Value) : 0
                            };
                            tests.Add(testCase);
                        }
                    }
                    logger?.SendMessage(TestMessageLevel.Informational, $"CForge: Found {tests.Count} tests via cforge test --list");
                }
            }
            catch (Exception ex)
            {
                logger?.SendMessage(TestMessageLevel.Warning, $"CForge: Failed to list tests via cforge: {ex.Message}");
            }

            return tests;
        }

        private List<TestCase> ParseTestFile(string testFile, string source, string relativePath)
        {
            var tests = new List<TestCase>();

            try
            {
                var content = File.ReadAllText(testFile);
                var lineNumber = 0;

                // Common test frameworks patterns
                var patterns = new[]
                {
                    // Catch2
                    @"TEST_CASE\s*\(\s*""([^""]+)""",
                    @"SCENARIO\s*\(\s*""([^""]+)""",
                    // Google Test
                    @"TEST\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                    @"TEST_F\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                    @"TEST_P\s*\(\s*(\w+)\s*,\s*(\w+)\s*\)",
                    // Doctest
                    @"TEST_CASE\s*\(\s*""([^""]+)""",
                    @"SUBCASE\s*\(\s*""([^""]+)""",
                };

                foreach (var line in content.Split('\n'))
                {
                    lineNumber++;

                    foreach (var pattern in patterns)
                    {
                        var match = Regex.Match(line, pattern);
                        if (match.Success)
                        {
                            string testName;
                            if (match.Groups.Count > 2 && match.Groups[2].Success)
                            {
                                // Google Test format: TEST(Suite, Name)
                                testName = $"{match.Groups[1].Value}.{match.Groups[2].Value}";
                            }
                            else
                            {
                                testName = match.Groups[1].Value;
                            }

                            var testCase = new TestCase($"{relativePath}::{testName}", new Uri(CforgeTestExecutor.ExecutorUri), source)
                            {
                                DisplayName = testName,
                                CodeFilePath = testFile,
                                LineNumber = lineNumber
                            };
                            tests.Add(testCase);
                            break;
                        }
                    }
                }
            }
            catch { }

            return tests;
        }

        private string? FindCforgeExecutable()
        {
            // Check common locations
            var paths = new[]
            {
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "cforge", "bin", "cforge.exe"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "cforge", "bin", "cforge.exe"),
                "cforge.exe", // In PATH
                "cforge"
            };

            foreach (var path in paths)
            {
                if (File.Exists(path))
                    return path;
            }

            // Try to find in PATH
            try
            {
                var psi = new ProcessStartInfo
                {
                    FileName = "where",
                    Arguments = "cforge",
                    RedirectStandardOutput = true,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };

                using var process = Process.Start(psi);
                if (process != null)
                {
                    var output = process.StandardOutput.ReadLine();
                    process.WaitForExit(5000);
                    if (!string.IsNullOrEmpty(output) && File.Exists(output))
                        return output;
                }
            }
            catch { }

            return null;
        }

        private static string GetRelativePath(string basePath, string fullPath)
        {
            if (!basePath.EndsWith(Path.DirectorySeparatorChar.ToString()))
                basePath += Path.DirectorySeparatorChar;

            var baseUri = new Uri(basePath);
            var fullUri = new Uri(fullPath);

            var relativeUri = baseUri.MakeRelativeUri(fullUri);
            return Uri.UnescapeDataString(relativeUri.ToString().Replace('/', Path.DirectorySeparatorChar));
        }
    }
}
