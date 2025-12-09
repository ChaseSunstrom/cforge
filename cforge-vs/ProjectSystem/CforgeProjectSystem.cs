using System;
using System.ComponentModel.Composition;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.ProjectSystem;
using Microsoft.VisualStudio.ProjectSystem.VS;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CforgeVS.ProjectSystem
{
    /// <summary>
    /// cforge Project Type GUID
    /// </summary>
    internal static class CforgeProjectTypes
    {
        public const string CforgeProjectTypeGuid = "9A19103F-16F7-4668-BE54-9A1E7A4F7556";
        public const string CforgeProjectType = "CforgeProject";
    }

    /// <summary>
    /// Provides the cforge unconfigured project
    /// </summary>
    [Export]
    [AppliesTo(CforgeUnconfiguredProject.UniqueCapability)]
    [ProjectTypeRegistration(
        CforgeProjectTypes.CforgeProjectTypeGuid,
        "cforge",
        "#100",
        "cforgeproj",
        "cforge",
        PossibleProjectExtensions = "cforgeproj",
        ProjectTemplatesDir = @"..\..\Templates\Projects")]
    internal class CforgeUnconfiguredProject
    {
        public const string UniqueCapability = "CforgeProject";

        [ImportingConstructor]
        public CforgeUnconfiguredProject(UnconfiguredProject unconfiguredProject)
        {
            UnconfiguredProject = unconfiguredProject;
        }

        [Import]
        internal UnconfiguredProject UnconfiguredProject { get; private set; }

        [Import]
        internal IProjectThreadingService ProjectThreadingService { get; private set; } = null!;

        [Import]
        internal ActiveConfiguredProject<ConfiguredProject> ActiveConfiguredProject { get; private set; } = null!;

        [Import]
        internal IProjectLockService ProjectLockService { get; private set; } = null!;
    }

    /// <summary>
    /// Provides the cforge configured project
    /// </summary>
    [Export]
    [AppliesTo(CforgeUnconfiguredProject.UniqueCapability)]
    internal class CforgeConfiguredProject
    {
        [Import]
        internal ConfiguredProject ConfiguredProject { get; private set; } = null!;

        [Import]
        internal IProjectThreadingService ProjectThreadingService { get; private set; } = null!;
    }
}
