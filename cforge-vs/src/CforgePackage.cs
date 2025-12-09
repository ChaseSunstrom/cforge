using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Community.VisualStudio.Toolkit;
using Microsoft.VisualStudio;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CforgeVS
{
    [PackageRegistration(UseManagedResourcesOnly = true, AllowsBackgroundLoading = true)]
    [Guid(PackageGuidString)]
    [ProvideMenuResource("Menus.ctmenu", 1)]
    [ProvideToolWindow(typeof(DependencyToolWindow), Style = VsDockStyle.Tabbed, Window = "3ae79031-e1bc-11d0-8f78-00a0c9110057")]
    [ProvideToolWindow(typeof(OutputToolWindow), Style = VsDockStyle.Tabbed, Window = "34e76e81-ee4a-11d0-ae2e-00a0c90fffc3")]
    [ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExists_string, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideAutoLoad(VSConstants.UICONTEXT.FolderOpened_string, PackageAutoLoadFlags.BackgroundLoad)]
    public sealed class CforgePackage : ToolkitPackage
    {
        public const string PackageGuidString = "7a8b9c0d-1e2f-3a4b-5c6d-7e8f9a0b1c2d";

        public static CforgePackage? Instance { get; private set; }

        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);

            Instance = this;

            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);

            // Register commands
            await BuildCommand.InitializeAsync(this);
            await BuildReleaseCommand.InitializeAsync(this);
            await CleanCommand.InitializeAsync(this);
            await RunCommand.InitializeAsync(this);
            await TestCommand.InitializeAsync(this);
            await AddDependencyCommand.InitializeAsync(this);
            await UpdatePackagesCommand.InitializeAsync(this);
            await OpenDependencyWindowCommand.InitializeAsync(this);
            await WatchCommand.InitializeAsync(this);
            await FormatCommand.InitializeAsync(this);
            await LintCommand.InitializeAsync(this);

            // Detect cforge projects
            await CforgeProjectDetector.InitializeAsync();

            // Initialize build interceptor (hooks F5, Ctrl+Shift+B, etc.)
            await BuildInterceptor.InitializeAsync();

            // Initialize file watcher for cforge.toml changes
            await CforgeFileWatcher.InitializeAsync();

            // Initialize Open Folder support (tasks.vs.json, launch.vs.json)
            await OpenFolderSupport.InitializeAsync();

            // Initialize vcxproj generator (generates .vcxproj from cforge.toml)
            await VcxprojGenerator.InitializeAsync();
        }
    }
}
