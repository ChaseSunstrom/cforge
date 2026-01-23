using System;
using System.Collections.Generic;

namespace CforgeVS.Models
{
    /// <summary>
    /// Severity level for build diagnostics.
    /// </summary>
    public enum DiagnosticSeverity
    {
        Info,
        Warning,
        Error
    }

    /// <summary>
    /// Build phase for tracking progress.
    /// </summary>
    public enum BuildPhase
    {
        NotStarted,
        Configuration,
        Compilation,
        Linking,
        Completed,
        Failed
    }

    /// <summary>
    /// Represents a single diagnostic (error, warning, or note) from the build.
    /// </summary>
    public class BuildDiagnostic
    {
        /// <summary>
        /// The source file path (can be relative or absolute).
        /// </summary>
        public string File { get; set; } = "";

        /// <summary>
        /// Line number (1-based).
        /// </summary>
        public int Line { get; set; }

        /// <summary>
        /// Column number (1-based).
        /// </summary>
        public int Column { get; set; }

        /// <summary>
        /// The diagnostic message.
        /// </summary>
        public string Message { get; set; } = "";

        /// <summary>
        /// The severity level.
        /// </summary>
        public DiagnosticSeverity Severity { get; set; } = DiagnosticSeverity.Error;

        /// <summary>
        /// Compiler-specific error code (e.g., C4996, -Wunused-variable).
        /// </summary>
        public string? Code { get; set; }

        /// <summary>
        /// Full path to the file (resolved).
        /// </summary>
        public string? FullPath { get; set; }

        /// <summary>
        /// Additional context lines from the compiler output.
        /// </summary>
        public List<string> ContextLines { get; } = new List<string>();
    }

    /// <summary>
    /// Represents a compiled file during the build.
    /// </summary>
    public class CompiledFile
    {
        /// <summary>
        /// The source file path.
        /// </summary>
        public string File { get; set; } = "";

        /// <summary>
        /// Whether compilation succeeded.
        /// </summary>
        public bool Success { get; set; } = true;

        /// <summary>
        /// Compilation duration.
        /// </summary>
        public TimeSpan Duration { get; set; }

        /// <summary>
        /// Diagnostics for this file.
        /// </summary>
        public List<BuildDiagnostic> Diagnostics { get; } = new List<BuildDiagnostic>();
    }

    /// <summary>
    /// Represents the result of a build operation.
    /// </summary>
    public class BuildResult
    {
        /// <summary>
        /// Whether the build succeeded.
        /// </summary>
        public bool Success { get; set; }

        /// <summary>
        /// Total build duration.
        /// </summary>
        public TimeSpan Duration { get; set; }

        /// <summary>
        /// The project name that was built.
        /// </summary>
        public string ProjectName { get; set; } = "";

        /// <summary>
        /// The configuration (Debug/Release).
        /// </summary>
        public string Configuration { get; set; } = "Debug";

        /// <summary>
        /// Current build phase.
        /// </summary>
        public BuildPhase Phase { get; set; } = BuildPhase.NotStarted;

        /// <summary>
        /// All errors from the build.
        /// </summary>
        public List<BuildDiagnostic> Errors { get; } = new List<BuildDiagnostic>();

        /// <summary>
        /// All warnings from the build.
        /// </summary>
        public List<BuildDiagnostic> Warnings { get; } = new List<BuildDiagnostic>();

        /// <summary>
        /// All notes/info messages from the build.
        /// </summary>
        public List<BuildDiagnostic> Notes { get; } = new List<BuildDiagnostic>();

        /// <summary>
        /// Files that were compiled.
        /// </summary>
        public List<CompiledFile> CompiledFiles { get; } = new List<CompiledFile>();

        /// <summary>
        /// Raw output log lines.
        /// </summary>
        public List<string> OutputLog { get; } = new List<string>();

        /// <summary>
        /// The exit code from the build process.
        /// </summary>
        public int ExitCode { get; set; }

        /// <summary>
        /// Path to the output artifact (executable or library).
        /// </summary>
        public string? OutputPath { get; set; }

        /// <summary>
        /// Gets the total diagnostic count.
        /// </summary>
        public int TotalDiagnostics => Errors.Count + Warnings.Count;

        /// <summary>
        /// Adds a diagnostic to the appropriate list based on severity.
        /// </summary>
        public void AddDiagnostic(BuildDiagnostic diagnostic)
        {
            switch (diagnostic.Severity)
            {
                case DiagnosticSeverity.Error:
                    Errors.Add(diagnostic);
                    break;
                case DiagnosticSeverity.Warning:
                    Warnings.Add(diagnostic);
                    break;
                default:
                    Notes.Add(diagnostic);
                    break;
            }
        }
    }

    /// <summary>
    /// Event arguments for build progress updates.
    /// </summary>
    public class BuildProgressEventArgs : EventArgs
    {
        /// <summary>
        /// The current build phase.
        /// </summary>
        public BuildPhase Phase { get; set; }

        /// <summary>
        /// Progress percentage (0-100), or -1 for indeterminate.
        /// </summary>
        public int ProgressPercent { get; set; } = -1;

        /// <summary>
        /// Status message to display.
        /// </summary>
        public string Message { get; set; } = "";

        /// <summary>
        /// Current file being compiled (if applicable).
        /// </summary>
        public string? CurrentFile { get; set; }
    }

    /// <summary>
    /// Event arguments for build completed.
    /// </summary>
    public class BuildCompletedEventArgs : EventArgs
    {
        /// <summary>
        /// The build result.
        /// </summary>
        public BuildResult Result { get; }

        public BuildCompletedEventArgs(BuildResult result)
        {
            Result = result;
        }
    }
}
