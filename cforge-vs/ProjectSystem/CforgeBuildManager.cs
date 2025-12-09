using System;
using System.ComponentModel.Composition;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.ProjectSystem;
using Microsoft.VisualStudio.ProjectSystem.Build;
using Microsoft.VisualStudio.Shell;
using Microsoft.VisualStudio.Shell.Interop;

namespace CforgeVS.ProjectSystem
{
    /// <summary>
    /// Handles building cforge projects by invoking the cforge CLI
    /// </summary>
    [Export(typeof(IDeployProvider))]
    [AppliesTo(CforgeUnconfiguredProject.UniqueCapability)]
    internal class CforgeBuildManager : IDeployProvider
    {
        [Import]
        internal ConfiguredProject ConfiguredProject { get; private set; } = null!;

        [Import]
        internal IProjectThreadingService ThreadingService { get; private set; } = null!;

        public bool IsDeploySupported => true;

        public void Commit()
        {
            // Nothing to commit for cforge builds
        }

        public void Rollback()
        {
            // Nothing to rollback for cforge builds
        }

        public async Task DeployAsync(CancellationToken cancellationToken, TextWriter outputPaneWriter)
        {
            await ThreadingService.SwitchToUIThread();
            
            string projectDir = Path.GetDirectoryName(ConfiguredProject.UnconfiguredProject.FullPath) ?? "";
            string config = ConfiguredProject.ProjectConfiguration.Name.Contains("Release") ? "Release" : "Debug";
            
            await outputPaneWriter.WriteLineAsync($"Building cforge project: {projectDir}");
            await outputPaneWriter.WriteLineAsync($"Configuration: {config}");
            
            await RunCforgeAsync("build", config == "Release" ? "-c Release" : "", projectDir, outputPaneWriter, cancellationToken);
        }

        private async Task RunCforgeAsync(string command, string args, string workingDir, TextWriter output, CancellationToken cancellationToken)
        {
            var psi = new ProcessStartInfo
            {
                FileName = "cforge",
                Arguments = $"{command} {args}".Trim(),
                WorkingDirectory = workingDir,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };

            using var process = new Process { StartInfo = psi };

            var outputLines = new System.Collections.Concurrent.ConcurrentQueue<string>();
            var errorLines = new System.Collections.Concurrent.ConcurrentQueue<string>();

            process.OutputDataReceived += (s, e) =>
            {
                if (e.Data != null)
                    outputLines.Enqueue(e.Data);
            };

            process.ErrorDataReceived += (s, e) =>
            {
                if (e.Data != null)
                    errorLines.Enqueue($"ERROR: {e.Data}");
            };

            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            await Task.Run(() => process.WaitForExit(), cancellationToken);

            // Write all collected output
            while (outputLines.TryDequeue(out var line))
            {
                await output.WriteLineAsync(line);
            }
            while (errorLines.TryDequeue(out var line))
            {
                await output.WriteLineAsync(line);
            }

            if (process.ExitCode != 0)
            {
                await output.WriteLineAsync($"cforge {command} failed with exit code {process.ExitCode}");
            }
        }
    }
}
