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
    public class OutputToolWindow : BaseToolWindow<OutputToolWindow>
    {
        public override string GetTitle(int toolWindowId) => "cforge Output";

        public override Type PaneType => typeof(Pane);

        public override async Task<FrameworkElement> CreateAsync(int toolWindowId, CancellationToken cancellationToken)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            return new OutputToolWindowControl();
        }

        [Guid("9B8C7D6E-5F4A-3B2C-1D0E-9F8A7B6C5D4E")]
        internal class Pane : ToolWindowPane
        {
            public Pane()
            {
                BitmapImageMoniker = KnownMonikers.Console;
            }
        }
    }
}
