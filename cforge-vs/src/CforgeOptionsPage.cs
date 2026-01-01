using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.VisualStudio.Shell;

namespace CforgeVS
{
    [ComVisible(true)]
    [Guid("7a8b9c0d-5e6f-7a8b-9c0d-1e2f3a4b5c6d")]
    public class CforgeOptionsPage : DialogPage
    {
        private string _cforgeExecutablePath = "cforge";
        private string _defaultConfiguration = "Debug";
        private bool _autoGenerateVcxproj = true;
        private bool _showOutputOnBuild = true;
        private bool _interceptBuildCommands = true;
        private bool _interceptDebugCommands = true;
        private bool _parseErrorsToErrorList = true;
        private string _clangFormatStyle = "file";
        private bool _formatOnSave = false;
        private int _buildTimeout = 300;

        [Category("Executable")]
        [DisplayName("cforge Executable Path")]
        [Description("Path to the cforge executable. Leave as 'cforge' to use the one in PATH.")]
        public string CforgeExecutablePath
        {
            get => _cforgeExecutablePath;
            set => _cforgeExecutablePath = value;
        }

        [Category("Build")]
        [DisplayName("Default Configuration")]
        [Description("Default build configuration (Debug or Release).")]
        [TypeConverter(typeof(ConfigurationTypeConverter))]
        public string DefaultConfiguration
        {
            get => _defaultConfiguration;
            set => _defaultConfiguration = value;
        }

        [Category("Build")]
        [DisplayName("Build Timeout (seconds)")]
        [Description("Maximum time in seconds to wait for a build to complete.")]
        public int BuildTimeout
        {
            get => _buildTimeout;
            set => _buildTimeout = value;
        }

        [Category("Build")]
        [DisplayName("Show Output on Build")]
        [Description("Automatically show the Output window when building.")]
        public bool ShowOutputOnBuild
        {
            get => _showOutputOnBuild;
            set => _showOutputOnBuild = value;
        }

        [Category("Build")]
        [DisplayName("Parse Errors to Error List")]
        [Description("Parse compiler errors and warnings to the VS Error List.")]
        public bool ParseErrorsToErrorList
        {
            get => _parseErrorsToErrorList;
            set => _parseErrorsToErrorList = value;
        }

        [Category("Integration")]
        [DisplayName("Auto-generate VS Project Files")]
        [Description("Automatically generate .vcxproj files from cforge.toml for IntelliSense.")]
        public bool AutoGenerateVcxproj
        {
            get => _autoGenerateVcxproj;
            set => _autoGenerateVcxproj = value;
        }

        [Category("Integration")]
        [DisplayName("Intercept Build Commands")]
        [Description("Intercept Ctrl+Shift+B and redirect to cforge build.")]
        public bool InterceptBuildCommands
        {
            get => _interceptBuildCommands;
            set => _interceptBuildCommands = value;
        }

        [Category("Integration")]
        [DisplayName("Intercept Debug Commands")]
        [Description("Intercept F5/Ctrl+F5 and redirect to cforge build then run/debug.")]
        public bool InterceptDebugCommands
        {
            get => _interceptDebugCommands;
            set => _interceptDebugCommands = value;
        }

        [Category("Code Quality")]
        [DisplayName("clang-format Style")]
        [Description("Style to use for clang-format. Use 'file' to read from .clang-format.")]
        public string ClangFormatStyle
        {
            get => _clangFormatStyle;
            set => _clangFormatStyle = value;
        }

        [Category("Code Quality")]
        [DisplayName("Format on Save")]
        [Description("Automatically format C/C++ files when saving.")]
        public bool FormatOnSave
        {
            get => _formatOnSave;
            set => _formatOnSave = value;
        }

        /// <summary>
        /// Gets the singleton options instance.
        /// </summary>
        public static CforgeOptionsPage? Instance
        {
            get
            {
                try
                {
                    ThreadHelper.ThrowIfNotOnUIThread();
                    if (CforgePackage.Instance != null)
                    {
                        return CforgePackage.Instance.GetDialogPage(typeof(CforgeOptionsPage)) as CforgeOptionsPage;
                    }
                }
                catch { }
                return null;
            }
        }
    }

    /// <summary>
    /// Type converter for configuration dropdown.
    /// </summary>
    public class ConfigurationTypeConverter : StringConverter
    {
        public override bool GetStandardValuesSupported(ITypeDescriptorContext context) => true;

        public override bool GetStandardValuesExclusive(ITypeDescriptorContext context) => true;

        public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext context)
        {
            return new StandardValuesCollection(new[] { "Debug", "Release", "RelWithDebInfo", "MinSizeRel" });
        }
    }
}
