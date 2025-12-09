namespace CforgeVS
{
    internal static class PackageGuids
    {
        public const string CforgePackageGuidString = "7a8b9c0d-1e2f-3a4b-5c6d-7e8f9a0b1c2d";
        public const string CforgeMenuGroupGuidString = "7a8b9c0d-2e3f-4a5b-6c7d-8e9f0a1b2c3d";
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

        // Dependency commands
        public const int AddDependencyCommandId = 0x0200;
        public const int UpdatePackagesCommandId = 0x0201;
        public const int OpenDependencyWindowCommandId = 0x0202;

        // Tool commands
        public const int FormatCommandId = 0x0300;
        public const int LintCommandId = 0x0301;

        // Menu groups
        public const int CforgeMenuGroup = 0x1000;
        public const int CforgeBuildMenuGroup = 0x1001;
        public const int CforgeDependencyMenuGroup = 0x1002;
        public const int CforgeToolMenuGroup = 0x1003;
    }
}
