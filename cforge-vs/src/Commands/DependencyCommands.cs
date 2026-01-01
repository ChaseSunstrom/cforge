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
                await CforgeRunner.RunAsync($"deps add {packageName}");

                // Refresh dependency window if open
                var window = await DependencyToolWindow.ShowAsync();
                if (window?.Content is DependencyToolWindowControl control)
                {
                    await control.RefreshDependenciesAsync();
                }
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.RemoveDependencyCommandId)]
    internal sealed class RemoveDependencyCommand : BaseCommand<RemoveDependencyCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? packageName = InputDialog.Show(
                "Remove Dependency",
                "Enter package name to remove:");

            if (!string.IsNullOrWhiteSpace(packageName))
            {
                await CforgeRunner.RunAsync($"deps remove {packageName}");

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
            await CforgeRunner.RunAsync("deps update");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DepsTreeCommandId)]
    internal sealed class DepsTreeCommand : BaseCommand<DepsTreeCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("deps tree");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DepsOutdatedCommandId)]
    internal sealed class DepsOutdatedCommand : BaseCommand<DepsOutdatedCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("deps outdated");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DepsLockCommandId)]
    internal sealed class DepsLockCommand : BaseCommand<DepsLockCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("deps lock");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.VcpkgInstallCommandId)]
    internal sealed class VcpkgInstallCommand : BaseCommand<VcpkgInstallCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("vcpkg install");
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
