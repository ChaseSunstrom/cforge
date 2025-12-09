import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';

interface CforgeTaskDefinition extends vscode.TaskDefinition {
    command: string;
    config?: string;
    args?: string[];
}

export class CforgeTaskProvider implements vscode.TaskProvider {
    static CforgeType = 'cforge';
    private tasks: vscode.Task[] | undefined;

    public provideTasks(): vscode.ProviderResult<vscode.Task[]> {
        if (!this.tasks) {
            this.tasks = this.getTasks();
        }
        return this.tasks;
    }

    public resolveTask(task: vscode.Task): vscode.Task | undefined {
        const definition = task.definition as CforgeTaskDefinition;
        if (definition.command) {
            return this.createTask(definition);
        }
        return undefined;
    }

    private getTasks(): vscode.Task[] {
        const tasks: vscode.Task[] = [];
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0];

        if (!workspaceFolder) {
            return tasks;
        }

        const cforgeTomlPath = path.join(workspaceFolder.uri.fsPath, 'cforge.toml');
        if (!fs.existsSync(cforgeTomlPath)) {
            return tasks;
        }

        // Common tasks
        const commonTasks: CforgeTaskDefinition[] = [
            { type: 'cforge', command: 'build', config: 'Debug' },
            { type: 'cforge', command: 'build', config: 'Release' },
            { type: 'cforge', command: 'clean' },
            { type: 'cforge', command: 'run', config: 'Debug' },
            { type: 'cforge', command: 'run', config: 'Release' },
            { type: 'cforge', command: 'test' },
            { type: 'cforge', command: 'fmt' },
            { type: 'cforge', command: 'lint' },
            { type: 'cforge', command: 'watch' }
        ];

        for (const def of commonTasks) {
            const task = this.createTask(def);
            if (task) {
                tasks.push(task);
            }
        }

        return tasks;
    }

    private createTask(definition: CforgeTaskDefinition): vscode.Task | undefined {
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
        if (!workspaceFolder) {
            return undefined;
        }

        const config = vscode.workspace.getConfiguration('cforge');
        const cforgeExe = config.get<string>('executablePath') || 'cforge';

        const args: string[] = [definition.command];

        if (definition.config) {
            args.push('-c', definition.config);
        }

        if (definition.args) {
            args.push(...definition.args);
        }

        const commandLine = `${cforgeExe} ${args.join(' ')}`;

        let taskName = definition.command;
        if (definition.config) {
            taskName += ` (${definition.config})`;
        }

        const execution = new vscode.ShellExecution(commandLine, {
            cwd: workspaceFolder.uri.fsPath
        });

        const task = new vscode.Task(
            definition,
            workspaceFolder,
            taskName,
            'cforge',
            execution,
            '$cforge'
        );

        // Set group based on command
        switch (definition.command) {
            case 'build':
                task.group = vscode.TaskGroup.Build;
                break;
            case 'test':
                task.group = vscode.TaskGroup.Test;
                break;
            case 'clean':
                task.group = vscode.TaskGroup.Clean;
                break;
        }

        task.presentationOptions = {
            reveal: vscode.TaskRevealKind.Always,
            panel: vscode.TaskPanelKind.Shared,
            clear: true
        };

        return task;
    }
}
