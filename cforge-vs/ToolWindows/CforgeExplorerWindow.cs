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
    public class CforgeExplorerWindow : BaseToolWindow<CforgeExplorerWindow>
    {
        public override string GetTitle(int toolWindowId) => "cforge Explorer";

        public override Type PaneType => typeof(Pane);

        public override async Task<FrameworkElement> CreateAsync(int toolWindowId, CancellationToken cancellationToken)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);
            return new CforgeExplorerWindowControl();
        }

        [Guid("8b9c0d1e-3f4a-5b6c-7d8e-9f0a1b2c3d4e")]
        internal class Pane : ToolWindowPane
        {
            public Pane()
            {
                BitmapImageMoniker = KnownMonikers.CPPProjectNode;
            }
        }
    }
}
