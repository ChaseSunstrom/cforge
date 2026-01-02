namespace CforgeVS
{
    internal static class PackageGuids
    {
        public const string CforgePackageGuidString = "7a8b9c0d-1e2f-3a4b-5c6d-7e8f9a0b1c2d";
        public const string CforgeMenuGroupGuidString = "7a8b9c0d-2e3f-4a5b-6c7d-8e9f0a1b2c3d";
        public const string CforgeToolbarGuidString = "7a8b9c0d-3e4f-5a6b-7c8d-9e0f1a2b3c4d";
    }

    internal static class PackageIds
    {
        // Build commands
        public const int BuildCommandId = 0x0100;
        public const int BuildReleaseCommandId = 0x0101;
        public const int CleanCommandId = 0x0102;
        public const int RunCommandId = 0x0103;
        public const int TestCommandId = 0x0104;
        public const int WatchCommandId = 0x0105;
        public const int RebuildCommandId = 0x0106;
        public const int DebugCommandId = 0x0107;
        public const int BenchCommandId = 0x0108;
        public const int StopWatchCommandId = 0x0109;

        // Dependency commands
        public const int AddDependencyCommandId = 0x0200;
        public const int UpdatePackagesCommandId = 0x0201;
        public const int OpenDependencyWindowCommandId = 0x0202;
        public const int RemoveDependencyCommandId = 0x0203;
        public const int DepsOutdatedCommandId = 0x0205;
        public const int DepsLockCommandId = 0x0206;
        public const int VcpkgInstallCommandId = 0x0207;

        // Tool commands
        public const int FormatCommandId = 0x0300;
        public const int LintCommandId = 0x0301;
        public const int DocCommandId = 0x0302;
        public const int DoctorCommandId = 0x0303;
        public const int ProjectInfoCommandId = 0x0304;
        public const int CleanGeneratedFilesCommandId = 0x0305;

        // Project/Template commands
        public const int InitProjectCommandId = 0x0400;
        public const int NewClassCommandId = 0x0401;
        public const int NewHeaderCommandId = 0x0402;
        public const int NewTestCommandId = 0x0403;
        public const int PackageProjectCommandId = 0x0404;
        public const int InstallProjectCommandId = 0x0405;
        public const int OpenProjectPropertiesCommandId = 0x0406;

        // Settings/Config commands
        public const int OpenSettingsCommandId = 0x0500;
        public const int ConfigureToolsCommandId = 0x0501;
        public const int OpenCforgeTomlCommandId = 0x0502;

        // Menu groups
        public const int CforgeMenuGroup = 0x1000;
        public const int CforgeBuildMenuGroup = 0x1001;
        public const int CforgeDependencyMenuGroup = 0x1002;
        public const int CforgeToolMenuGroup = 0x1003;
        public const int CforgeProjectMenuGroup = 0x1004;
        public const int CforgeSettingsMenuGroup = 0x1005;

        // Toolbar
        public const int CforgeToolbar = 0x2000;
        public const int CforgeToolbarGroup = 0x2001;
        public const int CforgeToolbarBuildGroup = 0x2002;
        public const int CforgeToolbarRunGroup = 0x2003;
        public const int CforgeToolbarTestGroup = 0x2004;

        // Toolbar-specific command IDs (separate from menu commands)
        public const int ToolbarBuildCommandId = 0x3000;
        public const int ToolbarRunCommandId = 0x3001;
        public const int ToolbarDebugCommandId = 0x3002;
        public const int ToolbarTestCommandId = 0x3003;
        public const int ToolbarCleanCommandId = 0x3004;
        public const int ToolbarStopCommandId = 0x3005;

        // Context menu
        public const int CforgeContextMenuGroup = 0x4000;

        // Tool windows
        public const int DependencyToolWindowId = 0x5000;
        public const int OutputToolWindowId = 0x5001;
        public const int ProjectPropertiesWindowId = 0x5002;
        public const int TestExplorerWindowId = 0x5003;
        public const int CforgeExplorerWindowId = 0x5004;
        public const int OpenCforgeExplorerCommandId = 0x5005;
    }
}
