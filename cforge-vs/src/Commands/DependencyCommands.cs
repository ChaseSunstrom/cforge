using System;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.AddDependencyCommandId)]
    internal sealed class AddDependencyCommand : BaseCommand<AddDependencyCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? packageName = InputDialog.Show(
                "Add Dependency",
                "Enter package name (e.g., fmt, spdlog, fmt@11.1.4):");

            if (!string.IsNullOrWhiteSpace(packageName))
            {
                await CforgeRunner.RunAsync($"add {packageName}");

                // Refresh dependency window if open
                var window = await DependencyToolWindow.ShowAsync();
                if (window?.Content is DependencyToolWindowControl control)
                {
                    await control.RefreshDependenciesAsync();
                }
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.UpdatePackagesCommandId)]
    internal sealed class UpdatePackagesCommand : BaseCommand<UpdatePackagesCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("update --packages");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.OpenDependencyWindowCommandId)]
    internal sealed class OpenDependencyWindowCommand : BaseCommand<OpenDependencyWindowCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await DependencyToolWindow.ShowAsync();
        }
    }
}
