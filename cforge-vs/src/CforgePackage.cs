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
    [ProvideToolWindow(typeof(DependencyToolWindow.Pane), Style = VsDockStyle.Tabbed, Window = "3ae79031-e1bc-11d0-8f78-00a0c9110057")]
    [ProvideToolWindow(typeof(OutputToolWindow.Pane), Style = VsDockStyle.Tabbed, Window = "34e76e81-ee4a-11d0-ae2e-00a0c90fffc3")]
    [ProvideToolWindow(typeof(ProjectPropertiesWindow.Pane), Style = VsDockStyle.Tabbed, Window = "3ae79031-e1bc-11d0-8f78-00a0c9110057")]
    [ProvideToolWindow(typeof(CforgeExplorerWindow.Pane), Style = VsDockStyle.Float)]
    [ProvideAutoLoad(VSConstants.UICONTEXT.SolutionExists_string, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideAutoLoad(VSConstants.UICONTEXT.FolderOpened_string, PackageAutoLoadFlags.BackgroundLoad)]
    [ProvideOptionPage(typeof(CforgeOptionsPage), "cforge", "General", 0, 0, true)]
    public sealed class CforgePackage : ToolkitPackage
    {
        public const string PackageGuidString = "7a8b9c0d-1e2f-3a4b-5c6d-7e8f9a0b1c2d";

        public static CforgePackage? Instance { get; private set; }

        protected override async Task InitializeAsync(CancellationToken cancellationToken, IProgress<ServiceProgressData> progress)
        {
            await base.InitializeAsync(cancellationToken, progress);

            Instance = this;

            // Register all tool windows - REQUIRED for BaseToolWindow to work
            this.RegisterToolWindows();

            await JoinableTaskFactory.SwitchToMainThreadAsync(cancellationToken);

            // Register build commands
            await BuildCommand.InitializeAsync(this);
            await BuildReleaseCommand.InitializeAsync(this);
            await RebuildCommand.InitializeAsync(this);
            await CleanCommand.InitializeAsync(this);
            await RunCommand.InitializeAsync(this);
            await DebugCommand.InitializeAsync(this);
            await TestCommand.InitializeAsync(this);
            await BenchCommand.InitializeAsync(this);
            await WatchCommand.InitializeAsync(this);
            await StopWatchCommand.InitializeAsync(this);

            // Register dependency commands
            await AddDependencyCommand.InitializeAsync(this);
            await RemoveDependencyCommand.InitializeAsync(this);
            await UpdatePackagesCommand.InitializeAsync(this);
            await DepsOutdatedCommand.InitializeAsync(this);
            await DepsLockCommand.InitializeAsync(this);
            await VcpkgInstallCommand.InitializeAsync(this);
            await OpenDependencyWindowCommand.InitializeAsync(this);

            // Register tool commands
            await FormatCommand.InitializeAsync(this);
            await LintCommand.InitializeAsync(this);
            await DocCommand.InitializeAsync(this);
            await DoctorCommand.InitializeAsync(this);
            await ProjectInfoCommand.InitializeAsync(this);
            await CleanGeneratedFilesCommand.InitializeAsync(this);

            // Register project/template commands
            await InitProjectCommand.InitializeAsync(this);
            await NewClassCommand.InitializeAsync(this);
            await NewHeaderCommand.InitializeAsync(this);
            await NewTestCommand.InitializeAsync(this);
            await PackageProjectCommand.InitializeAsync(this);
            await InstallProjectCommand.InitializeAsync(this);
            await OpenProjectPropertiesCommand.InitializeAsync(this);

            // Register settings commands
            await OpenCforgeTomlCommand.InitializeAsync(this);
            await ConfigureToolsCommand.InitializeAsync(this);
            await OpenSettingsCommand.InitializeAsync(this);

            // Register tool window commands
            await OpenCforgeExplorerCommand.InitializeAsync(this);

            // Register toolbar commands
            await ToolbarBuildCommand.InitializeAsync(this);
            await ToolbarRunCommand.InitializeAsync(this);
            await ToolbarDebugCommand.InitializeAsync(this);
            await ToolbarTestCommand.InitializeAsync(this);
            await ToolbarCleanCommand.InitializeAsync(this);
            await ToolbarStopCommand.InitializeAsync(this);

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
