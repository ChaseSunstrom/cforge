using System;
using System.Collections.Generic;

namespace CforgeVS.Models
{
    /// <summary>
    /// Test outcome status.
    /// </summary>
    public enum TestOutcome
    {
        None,
        Passed,
        Failed,
        Skipped,
        NotRun,
        Running
    }

    /// <summary>
    /// Represents a single test case result.
    /// </summary>
    public class TestCaseResult
    {
        /// <summary>
        /// The test case name.
        /// </summary>
        public string Name { get; set; } = "";

        /// <summary>
        /// The full qualified name (suite + test).
        /// </summary>
        public string FullName { get; set; } = "";

        /// <summary>
        /// The test outcome.
        /// </summary>
        public TestOutcome Outcome { get; set; } = TestOutcome.None;

        /// <summary>
        /// Duration of the test run.
        /// </summary>
        public TimeSpan Duration { get; set; }

        /// <summary>
        /// Error message if the test failed.
        /// </summary>
        public string? ErrorMessage { get; set; }

        /// <summary>
        /// Stack trace if the test failed.
        /// </summary>
        public string? StackTrace { get; set; }

        /// <summary>
        /// Source file where the test is defined.
        /// </summary>
        public string? SourceFile { get; set; }

        /// <summary>
        /// Line number in the source file.
        /// </summary>
        public int SourceLine { get; set; }

        /// <summary>
        /// Standard output captured during test execution.
        /// </summary>
        public string? StdOut { get; set; }

        /// <summary>
        /// Standard error captured during test execution.
        /// </summary>
        public string? StdErr { get; set; }

        /// <summary>
        /// Individual assertion results within this test.
        /// </summary>
        public List<AssertionResult> Assertions { get; } = new List<AssertionResult>();

        /// <summary>
        /// Tags/labels for this test (e.g., from Catch2 tags).
        /// </summary>
        public List<string> Tags { get; } = new List<string>();
    }

    /// <summary>
    /// Represents a single assertion result within a test.
    /// </summary>
    public class AssertionResult
    {
        /// <summary>
        /// Whether the assertion passed.
        /// </summary>
        public bool Passed { get; set; }

        /// <summary>
        /// The assertion expression.
        /// </summary>
        public string Expression { get; set; } = "";

        /// <summary>
        /// The expanded expression with values.
        /// </summary>
        public string? ExpandedExpression { get; set; }

        /// <summary>
        /// Source file where the assertion is located.
        /// </summary>
        public string? SourceFile { get; set; }

        /// <summary>
        /// Line number of the assertion.
        /// </summary>
        public int SourceLine { get; set; }

        /// <summary>
        /// Error message if assertion failed.
        /// </summary>
        public string? Message { get; set; }
    }

    /// <summary>
    /// Represents a test suite (group of test cases).
    /// </summary>
    public class TestSuiteResult
    {
        /// <summary>
        /// The suite name.
        /// </summary>
        public string Name { get; set; } = "";

        /// <summary>
        /// Total tests in this suite.
        /// </summary>
        public int TotalTests => TestCases.Count;

        /// <summary>
        /// Number of passed tests.
        /// </summary>
        public int Passed { get; set; }

        /// <summary>
        /// Number of failed tests.
        /// </summary>
        public int Failed { get; set; }

        /// <summary>
        /// Number of skipped tests.
        /// </summary>
        public int Skipped { get; set; }

        /// <summary>
        /// Total duration for all tests in this suite.
        /// </summary>
        public TimeSpan Duration { get; set; }

        /// <summary>
        /// The test cases in this suite.
        /// </summary>
        public List<TestCaseResult> TestCases { get; } = new List<TestCaseResult>();

        /// <summary>
        /// Source file for this suite.
        /// </summary>
        public string? SourceFile { get; set; }

        /// <summary>
        /// Recalculates counts based on test case outcomes.
        /// </summary>
        public void RecalculateCounts()
        {
            Passed = 0;
            Failed = 0;
            Skipped = 0;

            foreach (var test in TestCases)
            {
                switch (test.Outcome)
                {
                    case TestOutcome.Passed:
                        Passed++;
                        break;
                    case TestOutcome.Failed:
                        Failed++;
                        break;
                    case TestOutcome.Skipped:
                    case TestOutcome.NotRun:
                        Skipped++;
                        break;
                }
            }
        }
    }

    /// <summary>
    /// Represents the result of a complete test run.
    /// </summary>
    public class TestRunResult
    {
        /// <summary>
        /// Total tests run.
        /// </summary>
        public int TotalTests { get; set; }

        /// <summary>
        /// Number of passed tests.
        /// </summary>
        public int Passed { get; set; }

        /// <summary>
        /// Number of failed tests.
        /// </summary>
        public int Failed { get; set; }

        /// <summary>
        /// Number of skipped tests.
        /// </summary>
        public int Skipped { get; set; }

        /// <summary>
        /// Total test run duration.
        /// </summary>
        public TimeSpan Duration { get; set; }

        /// <summary>
        /// Test suites in this run.
        /// </summary>
        public List<TestSuiteResult> Suites { get; } = new List<TestSuiteResult>();

        /// <summary>
        /// Raw output log.
        /// </summary>
        public List<string> OutputLog { get; } = new List<string>();

        /// <summary>
        /// Whether the test run completed successfully (all tests passed).
        /// </summary>
        public bool Success => Failed == 0;

        /// <summary>
        /// The exit code from the test process.
        /// </summary>
        public int ExitCode { get; set; }

        /// <summary>
        /// Test framework detected (e.g., "Catch2", "Google Test", "doctest").
        /// </summary>
        public string? Framework { get; set; }

        /// <summary>
        /// Name of the test executable.
        /// </summary>
        public string? ExecutableName { get; set; }

        /// <summary>
        /// Recalculates totals based on suite data.
        /// </summary>
        public void RecalculateTotals()
        {
            TotalTests = 0;
            Passed = 0;
            Failed = 0;
            Skipped = 0;

            foreach (var suite in Suites)
            {
                suite.RecalculateCounts();
                TotalTests += suite.TotalTests;
                Passed += suite.Passed;
                Failed += suite.Failed;
                Skipped += suite.Skipped;
            }
        }

        /// <summary>
        /// Gets all failed test cases across all suites.
        /// </summary>
        public IEnumerable<TestCaseResult> GetFailedTests()
        {
            foreach (var suite in Suites)
            {
                foreach (var test in suite.TestCases)
                {
                    if (test.Outcome == TestOutcome.Failed)
                        yield return test;
                }
            }
        }

        /// <summary>
        /// Finds a test case by name.
        /// </summary>
        public TestCaseResult? FindTest(string name)
        {
            foreach (var suite in Suites)
            {
                foreach (var test in suite.TestCases)
                {
                    if (test.Name == name || test.FullName == name)
                        return test;
                }
            }
            return null;
        }
    }

    /// <summary>
    /// Event arguments for test progress updates.
    /// </summary>
    public class TestProgressEventArgs : EventArgs
    {
        /// <summary>
        /// Current test being run.
        /// </summary>
        public string? CurrentTest { get; set; }

        /// <summary>
        /// Current suite being run.
        /// </summary>
        public string? CurrentSuite { get; set; }

        /// <summary>
        /// Number of tests completed so far.
        /// </summary>
        public int CompletedCount { get; set; }

        /// <summary>
        /// Total number of tests to run.
        /// </summary>
        public int TotalCount { get; set; }

        /// <summary>
        /// Tests passed so far.
        /// </summary>
        public int PassedCount { get; set; }

        /// <summary>
        /// Tests failed so far.
        /// </summary>
        public int FailedCount { get; set; }

        /// <summary>
        /// Progress percentage (0-100).
        /// </summary>
        public int ProgressPercent => TotalCount > 0 ? (CompletedCount * 100 / TotalCount) : 0;
    }

    /// <summary>
    /// Event arguments for test run completed.
    /// </summary>
    public class TestCompletedEventArgs : EventArgs
    {
        /// <summary>
        /// The test run result.
        /// </summary>
        public TestRunResult Result { get; }

        public TestCompletedEventArgs(TestRunResult result)
        {
            Result = result;
        }
    }
}
