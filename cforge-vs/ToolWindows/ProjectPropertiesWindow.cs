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
    public class ProjectPropertiesWindow : BaseToolWindow<ProjectPropertiesWindow>
    {
        public override string GetTitle(int toolWindowId) => "cforge Project Properties";

        public override Type PaneType => typeof(Pane);

        public override Task<FrameworkElement> CreateAsync(int toolWindowId, CancellationToken cancellationToken)
        {
            return Task.FromResult<FrameworkElement>(new ProjectPropertiesWindowControl());
        }

        [Guid("7a8b9c0d-4e5f-6a7b-8c9d-0e1f2a3b4c5d")]
        internal class Pane : ToolWindowPane
        {
            public Pane()
            {
                BitmapImageMoniker = KnownMonikers.Property;
            }
        }
    }
}
