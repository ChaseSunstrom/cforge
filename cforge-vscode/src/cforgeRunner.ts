import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';

export class CforgeRunner {
    private terminal: vscode.Terminal | undefined;
    private outputChannel: vscode.OutputChannel;
    private lastCommand: string = '';

    constructor() {
        // Output channel for quiet operations (like getting output programmatically)
        this.outputChannel = vscode.window.createOutputChannel('cforge');

        // Listen for terminal close events
        vscode.window.onDidCloseTerminal((closedTerminal) => {
            if (closedTerminal === this.terminal) {
                this.terminal = undefined;
            }
        });
    }

    private getOrCreateTerminal(): vscode.Terminal {
        if (!this.terminal || !this.isTerminalAlive()) {
            this.terminal = vscode.window.createTerminal({
                name: 'cforge',
                env: {
                    // Ensure colors are enabled
                    FORCE_COLOR: '1',
                    CLICOLOR_FORCE: '1'
                }
            });
        }
        return this.terminal;
    }

    private isTerminalAlive(): boolean {
        if (!this.terminal) return false;
        // Check if terminal is still in the list of active terminals
        return vscode.window.terminals.includes(this.terminal);
    }

    public async run(args: string[]): Promise<boolean> {
        const config = vscode.workspace.getConfiguration('cforge');
        const cforgeExe = config.get<string>('executablePath') || 'cforge';
        const autoShow = config.get<boolean>('autoShowOutput') ?? true;

        const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
        if (!workspaceFolder) {
            vscode.window.showErrorMessage('No workspace folder open');
            return false;
        }

        const terminal = this.getOrCreateTerminal();
        const command = `${cforgeExe} ${args.join(' ')}`;
        this.lastCommand = command;

        if (autoShow) {
            terminal.show(false); // false = don't take focus
        }

        // Send command to terminal
        terminal.sendText(command);

        // Since we can't easily get exit codes from sendText,
        // we return true and let the user see the output
        return true;
    }

    /**
     * Run a command and capture its output (for programmatic use)
     * This uses child_process instead of terminal
     */
    public async runWithOutput(args: string[]): Promise<{ success: boolean; output: string }> {
        const config = vscode.workspace.getConfiguration('cforge');
        const cforgeExe = config.get<string>('executablePath') || 'cforge';

        const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
        if (!workspaceFolder) {
            return { success: false, output: 'No workspace folder open' };
        }

        const cwd = workspaceFolder.uri.fsPath;

        return new Promise((resolve) => {
            cp.exec(`${cforgeExe} ${args.join(' ')}`, { cwd }, (error, stdout, stderr) => {
                if (error) {
                    resolve({ success: false, output: stderr || stdout });
                } else {
                    resolve({ success: true, output: stdout });
                }
            });
        });
    }

    /**
     * Run command using child_process with output in OutputChannel (no colors)
     * Use this for commands where you need to track success/failure
     */
    public async runWithTracking(args: string[]): Promise<boolean> {
        const config = vscode.workspace.getConfiguration('cforge');
        const cforgeExe = config.get<string>('executablePath') || 'cforge';
        const autoShow = config.get<boolean>('autoShowOutput') ?? true;

        const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
        if (!workspaceFolder) {
            vscode.window.showErrorMessage('No workspace folder open');
            return false;
        }

        const cwd = workspaceFolder.uri.fsPath;

        if (autoShow) {
            this.outputChannel.show(true);
        }

        this.outputChannel.appendLine(`> cforge ${args.join(' ')}`);
        this.outputChannel.appendLine('');

        return new Promise((resolve) => {
            const process = cp.spawn(cforgeExe, args, {
                cwd,
                shell: true
            });

            process.stdout?.on('data', (data: Buffer) => {
                this.outputChannel.append(this.stripAnsi(data.toString()));
            });

            process.stderr?.on('data', (data: Buffer) => {
                this.outputChannel.append(this.stripAnsi(data.toString()));
            });

            process.on('close', (code) => {
                this.outputChannel.appendLine('');
                if (code === 0) {
                    this.outputChannel.appendLine('✓ Command completed successfully');
                } else {
                    this.outputChannel.appendLine(`✗ Command failed with exit code ${code}`);
                }
                this.outputChannel.appendLine('');
                resolve(code === 0);
            });

            process.on('error', (err) => {
                this.outputChannel.appendLine(`Error: ${err.message}`);
                if (err.message.includes('ENOENT')) {
                    vscode.window.showErrorMessage(
                        `cforge executable not found. Please ensure cforge is installed and in your PATH, or configure the path in settings.`,
                        'Open Settings'
                    ).then(selection => {
                        if (selection === 'Open Settings') {
                            vscode.commands.executeCommand('workbench.action.openSettings', 'cforge.executablePath');
                        }
                    });
                }
                resolve(false);
            });
        });
    }

    /**
     * Strip ANSI escape codes from output
     * Used for OutputChannel which doesn't support ANSI codes
     */
    private stripAnsi(text: string): string {
        // eslint-disable-next-line no-control-regex
        return text.replace(/\x1b\[[0-9;]*m/g, '');
    }

    public stop() {
        if (this.terminal) {
            // Send Ctrl+C to the terminal
            this.terminal.sendText('\x03');
        }
    }

    public dispose() {
        this.stop();
        if (this.terminal) {
            this.terminal.dispose();
            this.terminal = undefined;
        }
        this.outputChannel.dispose();
    }
}
