using System;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using CforgeVS.Models;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Imaging.Interop;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    public partial class TestResultsWindowControl : UserControl
    {
        private TestRunResult? _currentResult;
        private TestTreeItem? _selectedItem;
        private bool _isRunning;

        public TestResultsWindowControl()
        {
            InitializeComponent();
        }

        /// <summary>
        /// Updates the window with test results.
        /// </summary>
        public void ShowResult(TestRunResult result)
        {
            _currentResult = result;
            _isRunning = false;
            UpdateUI();
        }

        /// <summary>
        /// Shows the test run in progress state.
        /// </summary>
        public void ShowTestsRunning()
        {
            _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

                _isRunning = true;
                EmptyState.Visibility = Visibility.Collapsed;
                TestTree.Visibility = Visibility.Visible;
                TestProgressBar.Visibility = Visibility.Visible;
                TestProgressBar.IsIndeterminate = true;
                StopButton.IsEnabled = true;
                RunAllButton.IsEnabled = false;
                RunFailedButton.IsEnabled = false;
                StatusText.Text = "Running tests...";
            });
        }

        /// <summary>
        /// Updates progress during test run.
        /// </summary>
        public void UpdateProgress(TestProgressEventArgs args)
        {
            _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

                if (args.TotalCount > 0)
                {
                    TestProgressBar.IsIndeterminate = false;
                    TestProgressBar.Value = args.ProgressPercent;
                }

                PassedCountText.Text = args.PassedCount.ToString();
                FailedCountText.Text = args.FailedCount.ToString();
                TotalCountText.Text = $"{args.CompletedCount}/{args.TotalCount}";

                if (!string.IsNullOrEmpty(args.CurrentTest))
                {
                    StatusText.Text = $"Running: {args.CurrentTest}";
                }
            });
        }

        private void UpdateUI()
        {
            if (_currentResult == null) return;

            _ = ThreadHelper.JoinableTaskFactory.RunAsync(async () =>
            {
                await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

                TestProgressBar.Visibility = Visibility.Collapsed;
                EmptyState.Visibility = Visibility.Collapsed;
                TestTree.Visibility = Visibility.Visible;
                StopButton.IsEnabled = false;
                RunAllButton.IsEnabled = true;
                RunFailedButton.IsEnabled = _currentResult.Failed > 0;

                // Update counts
                PassedCountText.Text = _currentResult.Passed.ToString();
                FailedCountText.Text = _currentResult.Failed.ToString();
                SkippedCountText.Text = _currentResult.Skipped.ToString();
                TotalCountText.Text = _currentResult.TotalTests.ToString();
                DurationText.Text = FormatDuration(_currentResult.Duration);

                // Populate tree
                PopulateTestTree();

                // Update status
                if (_currentResult.Success)
                {
                    StatusText.Text = $"All {_currentResult.TotalTests} tests passed";
                }
                else
                {
                    StatusText.Text = $"{_currentResult.Failed} test(s) failed";
                }
            });
        }

        private void PopulateTestTree()
        {
            var items = new ObservableCollection<TestTreeItem>();

            bool showPassed = ShowPassedToggle.IsChecked == true;
            bool showFailed = ShowFailedToggle.IsChecked == true;
            bool showSkipped = ShowSkippedToggle.IsChecked == true;

            foreach (var suite in _currentResult!.Suites)
            {
                var suiteItem = new TestTreeItem
                {
                    Name = suite.Name,
                    ImageMoniker = GetSuiteIcon(suite),
                    Badge = $"({suite.Passed}/{suite.TotalTests})",
                    IsExpanded = suite.Failed > 0,
                    Suite = suite
                };

                foreach (var test in suite.TestCases)
                {
                    bool include = (test.Outcome == TestOutcome.Passed && showPassed) ||
                                   (test.Outcome == TestOutcome.Failed && showFailed) ||
                                   ((test.Outcome == TestOutcome.Skipped || test.Outcome == TestOutcome.NotRun) && showSkipped);

                    if (include)
                    {
                        suiteItem.Children.Add(new TestTreeItem
                        {
                            Name = test.Name,
                            ImageMoniker = GetTestIcon(test.Outcome),
                            Badge = FormatDuration(test.Duration),
                            TestCase = test
                        });
                    }
                }

                if (suiteItem.Children.Count > 0)
                {
                    items.Add(suiteItem);
                }
            }

            TestTree.ItemsSource = items;
        }

        private ImageMoniker GetSuiteIcon(TestSuiteResult suite)
        {
            if (suite.Failed > 0)
                return KnownMonikers.StatusError;
            if (suite.Skipped > 0 && suite.Passed == 0)
                return KnownMonikers.StatusWarning;
            return KnownMonikers.StatusOK;
        }

        private ImageMoniker GetTestIcon(TestOutcome outcome)
        {
            return outcome switch
            {
                TestOutcome.Passed => KnownMonikers.StatusOK,
                TestOutcome.Failed => KnownMonikers.StatusError,
                TestOutcome.Skipped => KnownMonikers.StatusWarning,
                TestOutcome.NotRun => KnownMonikers.StatusInformation,
                TestOutcome.Running => KnownMonikers.Refresh,
                _ => KnownMonikers.StatusInformation
            };
        }

        private string FormatDuration(TimeSpan duration)
        {
            if (duration.TotalMilliseconds < 1)
                return "";
            if (duration.TotalSeconds < 1)
                return $"{duration.Milliseconds}ms";
            if (duration.TotalMinutes < 1)
                return $"{duration.TotalSeconds:F2}s";
            return $"{(int)duration.TotalMinutes}m {duration.Seconds}s";
        }

        private void ShowTestDetails(TestCaseResult test)
        {
            DetailsEmptyState.Visibility = Visibility.Collapsed;
            DetailsPanel.Visibility = Visibility.Visible;

            DetailTestName.Text = test.FullName ?? test.Name;
            DetailStatusIcon.Moniker = GetTestIcon(test.Outcome);
            DetailStatusText.Text = test.Outcome.ToString();
            DetailDurationText.Text = FormatDuration(test.Duration);

            // Source location
            if (!string.IsNullOrEmpty(test.SourceFile))
            {
                string fileName = Path.GetFileName(test.SourceFile);
                DetailSourceText.Text = test.SourceLine > 0 ? $"{fileName}:{test.SourceLine}" : fileName;
                DetailSourceText.Tag = test;
                DetailSourceText.Visibility = Visibility.Visible;
            }
            else
            {
                DetailSourceText.Visibility = Visibility.Collapsed;
            }

            // Error message
            if (!string.IsNullOrEmpty(test.ErrorMessage))
            {
                ErrorMessageHeader.Visibility = Visibility.Visible;
                ErrorMessagePanel.Visibility = Visibility.Visible;
                DetailErrorMessage.Text = test.ErrorMessage;
            }
            else
            {
                ErrorMessageHeader.Visibility = Visibility.Collapsed;
                ErrorMessagePanel.Visibility = Visibility.Collapsed;
            }

            // Stack trace
            if (!string.IsNullOrEmpty(test.StackTrace))
            {
                StackTraceHeader.Visibility = Visibility.Visible;
                StackTracePanel.Visibility = Visibility.Visible;
                DetailStackTrace.Text = test.StackTrace;
            }
            else
            {
                StackTraceHeader.Visibility = Visibility.Collapsed;
                StackTracePanel.Visibility = Visibility.Collapsed;
            }

            // Standard output
            if (!string.IsNullOrEmpty(test.StdOut))
            {
                OutputHeader.Visibility = Visibility.Visible;
                OutputPanel.Visibility = Visibility.Visible;
                DetailOutput.Text = test.StdOut;
            }
            else
            {
                OutputHeader.Visibility = Visibility.Collapsed;
                OutputPanel.Visibility = Visibility.Collapsed;
            }
        }

        private void ClearDetails()
        {
            DetailsPanel.Visibility = Visibility.Collapsed;
            DetailsEmptyState.Visibility = Visibility.Visible;
        }

        private async void RunAllButton_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                // RunTestAsync handles showing running state and updating results
                await CforgeRunner.RunTestAsync();
            }
            catch (Exception ex)
            {
                StatusText.Text = $"Error: {ex.Message}";
                TestProgressBar.Visibility = Visibility.Collapsed;
                StopButton.IsEnabled = false;
                RunAllButton.IsEnabled = true;
            }
        }

        private async void RunFailedButton_Click(object sender, RoutedEventArgs e)
        {
            if (_currentResult == null) return;

            try
            {
                // TODO: Pass failed test names to cforge test command
                // For now, just run all tests
                await CforgeRunner.RunTestAsync();
            }
            catch (Exception ex)
            {
                StatusText.Text = $"Error: {ex.Message}";
                TestProgressBar.Visibility = Visibility.Collapsed;
                StopButton.IsEnabled = false;
                RunAllButton.IsEnabled = true;
            }
        }

        private void StopButton_Click(object sender, RoutedEventArgs e)
        {
            CforgeRunner.StopCurrentProcess();
            _isRunning = false;
            TestProgressBar.Visibility = Visibility.Collapsed;
            StopButton.IsEnabled = false;
            RunAllButton.IsEnabled = true;
            RunFailedButton.IsEnabled = _currentResult?.Failed > 0;
            StatusText.Text = "Test run cancelled";
        }

        private void FilterToggle_Click(object sender, RoutedEventArgs e)
        {
            if (_currentResult != null)
            {
                PopulateTestTree();
            }
        }

        private void ClearButton_Click(object sender, RoutedEventArgs e)
        {
            _currentResult = null;
            TestTree.ItemsSource = null;
            EmptyState.Visibility = Visibility.Visible;
            TestTree.Visibility = Visibility.Collapsed;
            ClearDetails();

            PassedCountText.Text = "0";
            FailedCountText.Text = "0";
            SkippedCountText.Text = "0";
            TotalCountText.Text = "0";
            DurationText.Text = "";
            StatusText.Text = "Ready";
        }

        private void TestTree_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            _selectedItem = e.NewValue as TestTreeItem;

            if (_selectedItem?.TestCase != null)
            {
                ShowTestDetails(_selectedItem.TestCase);
            }
            else
            {
                ClearDetails();
            }
        }

        private async void TestTree_MouseDoubleClick(object sender, MouseButtonEventArgs e)
        {
            if (_selectedItem?.TestCase != null && !string.IsNullOrEmpty(_selectedItem.TestCase.SourceFile))
            {
                await NavigateToSource(_selectedItem.TestCase.SourceFile, _selectedItem.TestCase.SourceLine);
            }
        }

        private async void DetailSourceText_Click(object sender, MouseButtonEventArgs e)
        {
            if (DetailSourceText.Tag is TestCaseResult test && !string.IsNullOrEmpty(test.SourceFile))
            {
                await NavigateToSource(test.SourceFile, test.SourceLine);
            }
        }

        private async System.Threading.Tasks.Task NavigateToSource(string filePath, int line)
        {
            try
            {
                if (File.Exists(filePath))
                {
                    var docView = await VS.Documents.OpenAsync(filePath);
                    if (docView?.TextView != null && line > 0)
                    {
                        var textView = docView.TextView;
                        var textLine = textView.TextSnapshot.GetLineFromLineNumber(Math.Max(0, line - 1));
                        var point = new Microsoft.VisualStudio.Text.SnapshotPoint(textView.TextSnapshot, textLine.Start.Position);
                        textView.Caret.MoveTo(point);
                        textView.ViewScroller.EnsureSpanVisible(new Microsoft.VisualStudio.Text.SnapshotSpan(point, 0));
                    }
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error navigating to source: {ex.Message}");
            }
        }
    }

    /// <summary>
    /// Tree item for displaying tests.
    /// </summary>
    public class TestTreeItem
    {
        public string Name { get; set; } = "";
        public ImageMoniker ImageMoniker { get; set; } = KnownMonikers.StatusInformation;
        public string Badge { get; set; } = "";
        public bool IsExpanded { get; set; } = false;
        public TestSuiteResult? Suite { get; set; }
        public TestCaseResult? TestCase { get; set; }
        public ObservableCollection<TestTreeItem> Children { get; } = new ObservableCollection<TestTreeItem>();
    }
}
