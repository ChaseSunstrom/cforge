import * as vscode from 'vscode';

export class StatusBarManager implements vscode.Disposable {
    private statusBarItem: vscode.StatusBarItem;
    private buildButton: vscode.StatusBarItem;
    private runButton: vscode.StatusBarItem;

    constructor() {
        // Main status item
        this.statusBarItem = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            100
        );
        this.statusBarItem.command = 'cforge.build';
        this.statusBarItem.tooltip = 'Click to build with cforge';
        this.showReady(true);
        this.statusBarItem.show();

        // Build button
        this.buildButton = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            99
        );
        this.buildButton.text = '$(tools)';
        this.buildButton.command = 'cforge.build';
        this.buildButton.tooltip = 'cforge: Build';
        this.buildButton.show();

        // Run button
        this.runButton = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Left,
            98
        );
        this.runButton.text = '$(play)';
        this.runButton.command = 'cforge.run';
        this.runButton.tooltip = 'cforge: Run';
        this.runButton.show();
    }

    public showBuilding() {
        this.statusBarItem.text = '$(sync~spin) cforge: Building...';
        this.statusBarItem.backgroundColor = undefined;
    }

    public showReady(success: boolean = true) {
        if (success) {
            this.statusBarItem.text = '$(check) cforge';
            this.statusBarItem.backgroundColor = undefined;
        } else {
            this.statusBarItem.text = '$(error) cforge: Build Failed';
            this.statusBarItem.backgroundColor = new vscode.ThemeColor('statusBarItem.errorBackground');
        }
    }

    public showWatching() {
        this.statusBarItem.text = '$(eye) cforge: Watching';
        this.statusBarItem.backgroundColor = undefined;
    }

    public dispose() {
        this.statusBarItem.dispose();
        this.buildButton.dispose();
        this.runButton.dispose();
    }
}
