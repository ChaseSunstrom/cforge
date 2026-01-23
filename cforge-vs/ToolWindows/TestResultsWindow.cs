using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Imaging;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    /// <summary>
    /// Tool window for displaying structured test results.
    /// </summary>
    public class TestResultsWindow : BaseToolWindow<TestResultsWindow>
    {
        public override string GetTitle(int toolWindowId) => "cforge Test Results";

        public override Type PaneType => typeof(Pane);

        public override Task<FrameworkElement> CreateAsync(int toolWindowId, CancellationToken cancellationToken)
        {
            return Task.FromResult<FrameworkElement>(new TestResultsWindowControl());
        }

        [Guid("9A0B1C2D-3E4F-5A6B-7C8D-9E0F1A2B3C4F")]
        public class Pane : ToolWindowPane
        {
            public Pane()
            {
                BitmapImageMoniker = KnownMonikers.Test;
                ToolBar = null;
            }
        }

        /// <summary>
        /// Shows the Test Results window and returns the pane.
        /// </summary>
        public static async Task<ToolWindowPane?> ShowWindowAsync()
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();
            return await ShowAsync(0, true);
        }
    }
}
