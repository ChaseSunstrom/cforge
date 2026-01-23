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

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CacheListCommandId)]
    internal sealed class CacheListCommand : BaseCommand<CacheListCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("cache list");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CacheStatsCommandId)]
    internal sealed class CacheStatsCommand : BaseCommand<CacheStatsCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("cache stats");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CacheCleanCommandId)]
    internal sealed class CacheCleanCommand : BaseCommand<CacheCleanCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("cache clean");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.UpgradeCommandId)]
    internal sealed class UpgradeCommand : BaseCommand<UpgradeCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("upgrade");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CircularCommandId)]
    internal sealed class CircularCommand : BaseCommand<CircularCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("circular");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DepsSearchCommandId)]
    internal sealed class DepsSearchCommand : BaseCommand<DepsSearchCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? query = InputDialog.Show(
                "Search Packages",
                "Enter search query (e.g., json, logging, http):");

            if (!string.IsNullOrWhiteSpace(query))
            {
                await CforgeRunner.RunAsync($"deps search {query}");
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DepsListCommandId)]
    internal sealed class DepsListCommand : BaseCommand<DepsListCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("deps list");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.DepsInfoCommandId)]
    internal sealed class DepsInfoCommand : BaseCommand<DepsInfoCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            string? packageName = InputDialog.Show(
                "Package Info",
                "Enter package name (e.g., fmt, spdlog):");

            if (!string.IsNullOrWhiteSpace(packageName))
            {
                await CforgeRunner.RunAsync($"deps info {packageName}");
            }
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

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CachePruneCommandId)]
    internal sealed class CachePruneCommand : BaseCommand<CachePruneCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            var result = await VS.MessageBox.ShowConfirmAsync(
                "Prune Cache",
                "This will remove unused packages from the cache. Continue?");

            if (result)
            {
                await CforgeRunner.RunAsync("cache prune");
            }
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CachePathCommandId)]
    internal sealed class CachePathCommand : BaseCommand<CachePathCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await ThreadHelper.JoinableTaskFactory.SwitchToMainThreadAsync();

            // Get cache path
            string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            string cachePath = System.IO.Path.Combine(localAppData, "cforge", "cache");

            var pane = await CforgeRunner.GetOutputPaneAsync();
            await pane.ActivateAsync();
            await pane.WriteLineAsync($"cforge cache path: {cachePath}");

            // Offer to open in Explorer
            var openInExplorer = await VS.MessageBox.ShowConfirmAsync(
                "Cache Path",
                $"Cache location:\n{cachePath}\n\nOpen in Explorer?");

            if (openInExplorer && System.IO.Directory.Exists(cachePath))
            {
                System.Diagnostics.Process.Start("explorer.exe", cachePath);
            }
        }
    }

    // Tool Window Commands
    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.OpenTestResultsCommandId)]
    internal sealed class OpenTestResultsCommand : BaseCommand<OpenTestResultsCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await TestResultsWindow.ShowWindowAsync();
        }
    }
}
