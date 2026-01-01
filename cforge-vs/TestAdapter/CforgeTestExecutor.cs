using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.VisualStudio.TestPlatform.ObjectModel;
using Microsoft.VisualStudio.TestPlatform.ObjectModel.Adapter;
using Microsoft.VisualStudio.TestPlatform.ObjectModel.Logging;

namespace CforgeVS.TestAdapter
{
    /// <summary>
    /// Executes cforge tests and reports results to VS Test Explorer
    /// </summary>
    [ExtensionUri(ExecutorUri)]
    public class CforgeTestExecutor : ITestExecutor
    {
        public const string ExecutorUri = "executor://CforgeTestExecutor/v1";

        private bool _cancelled;

        public void Cancel()
        {
            _cancelled = true;
        }

        public void RunTests(IEnumerable<TestCase>? tests, IRunContext? runContext, IFrameworkHandle? frameworkHandle)
        {
            if (tests == null) return;
            _cancelled = false;

            var testsByProject = tests.GroupBy(t => Path.GetDirectoryName(t.Source));

            foreach (var projectTests in testsByProject)
            {
                if (_cancelled) break;

                var projectDir = projectTests.Key;
                if (string.IsNullOrEmpty(projectDir)) continue;

                RunTestsForProject(projectDir, projectTests.ToList(), frameworkHandle);
            }
        }

        public void RunTests(IEnumerable<string>? sources, IRunContext? runContext, IFrameworkHandle? frameworkHandle)
        {
            if (sources == null) return;
            _cancelled = false;

            foreach (var source in sources)
            {
                if (_cancelled) break;

                if (!source.EndsWith("cforge.toml", StringComparison.OrdinalIgnoreCase))
                    continue;

                var projectDir = Path.GetDirectoryName(source);
                if (string.IsNullOrEmpty(projectDir)) continue;

                // Discover tests first
                var discoverer = new CforgeTestDiscoverer();
                var sink = new TestCaseCollector();
                discoverer.DiscoverTests(new[] { source }, null!, null!, sink);

                RunTestsForProject(projectDir, sink.TestCases, frameworkHandle);
            }
        }

        private void RunTestsForProject(string projectDir, List<TestCase> tests, IFrameworkHandle? frameworkHandle)
        {
            if (tests.Count == 0) return;

            frameworkHandle?.SendMessage(TestMessageLevel.Informational, $"CForge: Running {tests.Count} tests in {projectDir}");

            // Mark all tests as started
            foreach (var test in tests)
            {
                frameworkHandle?.RecordStart(test);
            }

            var cforgePath = FindCforgeExecutable();
            if (string.IsNullOrEmpty(cforgePath))
            {
                foreach (var test in tests)
                {
                    var result = new TestResult(test)
                    {
                        Outcome = TestOutcome.Failed,
                        ErrorMessage = "Could not find cforge executable"
                    };
                    frameworkHandle?.RecordResult(result);
                }
                return;
            }

            // Run cforge test
            var startTime = DateTime.Now;
            var psi = new ProcessStartInfo
            {
                FileName = cforgePath,
                Arguments = "test --verbose",
                WorkingDirectory = projectDir,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };

            var output = new StringBuilder();
            var errors = new StringBuilder();

            try
            {
                using var process = Process.Start(psi);
                if (process == null)
                {
                    RecordAllFailed(tests, frameworkHandle, "Failed to start cforge process");
                    return;
                }

                process.OutputDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        output.AppendLine(e.Data);
                        frameworkHandle?.SendMessage(TestMessageLevel.Informational, e.Data);
                    }
                };

                process.ErrorDataReceived += (s, e) =>
                {
                    if (e.Data != null)
                    {
                        errors.AppendLine(e.Data);
                    }
                };

                process.BeginOutputReadLine();
                process.BeginErrorReadLine();

                // Wait with timeout
                if (!process.WaitForExit(300000)) // 5 minute timeout
                {
                    process.Kill();
                    RecordAllFailed(tests, frameworkHandle, "Test execution timed out after 5 minutes");
                    return;
                }

                var endTime = DateTime.Now;
                var duration = endTime - startTime;

                // Parse results
                var results = ParseTestOutput(output.ToString(), tests, duration);

                foreach (var result in results)
                {
                    frameworkHandle?.RecordResult(result);
                }

                // Handle any tests that weren't in the output (consider them passed if exit code is 0)
                var reportedTests = new HashSet<string>(results.Select(r => r.TestCase.FullyQualifiedName));
                foreach (var test in tests.Where(t => !reportedTests.Contains(t.FullyQualifiedName)))
                {
                    var result = new TestResult(test)
                    {
                        Outcome = process.ExitCode == 0 ? TestOutcome.Passed : TestOutcome.Skipped,
                        Duration = TimeSpan.FromMilliseconds(duration.TotalMilliseconds / tests.Count)
                    };
                    frameworkHandle?.RecordResult(result);
                }
            }
            catch (Exception ex)
            {
                RecordAllFailed(tests, frameworkHandle, $"Exception running tests: {ex.Message}");
            }
        }

        private List<TestResult> ParseTestOutput(string output, List<TestCase> tests, TimeSpan totalDuration)
        {
            var results = new List<TestResult>();
            var testDict = tests.ToDictionary(t => t.DisplayName, t => t);
            var durationPerTest = TimeSpan.FromMilliseconds(totalDuration.TotalMilliseconds / Math.Max(1, tests.Count));

            // Parse various test framework output formats
            var lines = output.Split(new[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries);

            // Catch2 format: "test_name: PASSED" or "test_name: FAILED"
            var catch2Pattern = @"^(.+?):\s*(PASSED|FAILED|SKIPPED)";

            // Google Test format: "[       OK ] TestSuite.TestName (123 ms)"
            var gtestPassPattern = @"\[\s*OK\s*\]\s*(\S+)\s*\((\d+)\s*ms\)";
            var gtestFailPattern = @"\[\s*FAILED\s*\]\s*(\S+)";

            // Summary lines
            var summaryPattern = @"(\d+)\s+test(?:s|case)?\s+(?:passed|ran|executed)";
            var failedSummaryPattern = @"(\d+)\s+(?:test(?:s|case)?|assertion)?\s*failed";

            bool? allPassed = null;
            int passedCount = 0;
            int failedCount = 0;

            foreach (var line in lines)
            {
                // Try Catch2 format
                var match = Regex.Match(line, catch2Pattern, RegexOptions.IgnoreCase);
                if (match.Success)
                {
                    var testName = match.Groups[1].Value.Trim();
                    var status = match.Groups[2].Value.ToUpperInvariant();

                    var testCase = FindTestCase(testDict, testName);
                    if (testCase != null)
                    {
                        var result = new TestResult(testCase)
                        {
                            Outcome = status switch
                            {
                                "PASSED" => TestOutcome.Passed,
                                "FAILED" => TestOutcome.Failed,
                                "SKIPPED" => TestOutcome.Skipped,
                                _ => TestOutcome.None
                            },
                            Duration = durationPerTest
                        };
                        results.Add(result);
                    }
                    continue;
                }

                // Try Google Test pass format
                match = Regex.Match(line, gtestPassPattern);
                if (match.Success)
                {
                    var testName = match.Groups[1].Value;
                    var duration = int.Parse(match.Groups[2].Value);

                    var testCase = FindTestCase(testDict, testName);
                    if (testCase != null)
                    {
                        var result = new TestResult(testCase)
                        {
                            Outcome = TestOutcome.Passed,
                            Duration = TimeSpan.FromMilliseconds(duration)
                        };
                        results.Add(result);
                    }
                    continue;
                }

                // Try Google Test fail format
                match = Regex.Match(line, gtestFailPattern);
                if (match.Success)
                {
                    var testName = match.Groups[1].Value;

                    var testCase = FindTestCase(testDict, testName);
                    if (testCase != null)
                    {
                        var result = new TestResult(testCase)
                        {
                            Outcome = TestOutcome.Failed,
                            Duration = durationPerTest
                        };
                        results.Add(result);
                    }
                    continue;
                }

                // Check summary lines
                match = Regex.Match(line, summaryPattern, RegexOptions.IgnoreCase);
                if (match.Success)
                {
                    passedCount = int.Parse(match.Groups[1].Value);
                }

                match = Regex.Match(line, failedSummaryPattern, RegexOptions.IgnoreCase);
                if (match.Success)
                {
                    failedCount = int.Parse(match.Groups[1].Value);
                }

                // Check for "All tests passed" or similar
                if (line.IndexOf("All tests passed", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    line.IndexOf("0 failures", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    line.IndexOf("test cases passed", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    allPassed = true;
                }
                else if (line.IndexOf("FAILED", StringComparison.OrdinalIgnoreCase) >= 0 ||
                         line.IndexOf("failures", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    if (line.IndexOf("0 failure", StringComparison.OrdinalIgnoreCase) < 0)
                        allPassed = false;
                }
            }

            // If we couldn't parse individual results but have summary info
            if (results.Count == 0 && allPassed.HasValue)
            {
                foreach (var test in tests)
                {
                    var result = new TestResult(test)
                    {
                        Outcome = allPassed.Value ? TestOutcome.Passed : TestOutcome.Failed,
                        Duration = durationPerTest
                    };
                    results.Add(result);
                }
            }

            return results;
        }

        private TestCase? FindTestCase(Dictionary<string, TestCase> testDict, string testName)
        {
            // Try exact match
            if (testDict.TryGetValue(testName, out var test))
                return test;

            // Try fuzzy match (case insensitive, partial match)
            foreach (var kvp in testDict)
            {
                if (kvp.Key.IndexOf(testName, StringComparison.OrdinalIgnoreCase) >= 0 ||
                    testName.IndexOf(kvp.Key, StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    return kvp.Value;
                }
            }

            return null;
        }

        private void RecordAllFailed(List<TestCase> tests, IFrameworkHandle? frameworkHandle, string errorMessage)
        {
            foreach (var test in tests)
            {
                var result = new TestResult(test)
                {
                    Outcome = TestOutcome.Failed,
                    ErrorMessage = errorMessage
                };
                frameworkHandle?.RecordResult(result);
            }
        }

        private string? FindCforgeExecutable()
        {
            var paths = new[]
            {
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "cforge", "bin", "cforge.exe"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "cforge", "bin", "cforge.exe"),
                "cforge.exe",
                "cforge"
            };

            foreach (var path in paths)
            {
                if (File.Exists(path))
                    return path;
            }

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

        private class TestCaseCollector : ITestCaseDiscoverySink
        {
            public List<TestCase> TestCases { get; } = new List<TestCase>();

            public void SendTestCase(TestCase discoveredTest)
            {
                TestCases.Add(discoveredTest);
            }
        }
    }
}
