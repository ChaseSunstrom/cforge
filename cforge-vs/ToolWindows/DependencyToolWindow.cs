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
    public class DependencyToolWindow : BaseToolWindow<DependencyToolWindow>
    {
        public override string GetTitle(int toolWindowId) => "cforge Dependencies";

        public override Type PaneType => typeof(Pane);

        public override async Task<FrameworkElement> CreateAsync(int toolWindowId, CancellationToken cancellationToken)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            return new DependencyToolWindowControl();
        }

        [Guid("8a9b0c1d-2e3f-4a5b-6c7d-8e9f0a1b2c3d")]
        internal class Pane : ToolWindowPane
        {
            public Pane()
            {
                BitmapImageMoniker = KnownMonikers.Reference;
            }
        }
    }
}
