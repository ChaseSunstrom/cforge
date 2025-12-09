using System;
using System.ComponentModel.Design;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.BuildCommandId)]
    internal sealed class BuildCommand : BaseCommand<BuildCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("build");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.BuildReleaseCommandId)]
    internal sealed class BuildReleaseCommand : BaseCommand<BuildReleaseCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("build -c Release");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.CleanCommandId)]
    internal sealed class CleanCommand : BaseCommand<CleanCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("clean");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.RunCommandId)]
    internal sealed class RunCommand : BaseCommand<RunCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            // Build and run in a console window (like regular VS behavior)
            await CforgeRunner.BuildAndRunInConsoleAsync();
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.TestCommandId)]
    internal sealed class TestCommand : BaseCommand<TestCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("test");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.WatchCommandId)]
    internal sealed class WatchCommand : BaseCommand<WatchCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("watch");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.FormatCommandId)]
    internal sealed class FormatCommand : BaseCommand<FormatCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("fmt");
        }
    }

    [Command(PackageGuids.CforgeMenuGroupGuidString, PackageIds.LintCommandId)]
    internal sealed class LintCommand : BaseCommand<LintCommand>
    {
        protected override async Task ExecuteAsync(OleMenuCmdEventArgs e)
        {
            await CforgeRunner.RunAsync("lint");
        }
    }
}
