import * as vscode from 'vscode';
import { CforgeRunner } from './cforgeRunner';
import { DependencyTreeProvider } from './dependencyTree';
import { CforgeTaskProvider } from './taskProvider';
import { StatusBarManager } from './statusBar';
import { registerTomlLanguageFeatures, CforgeTomlLanguageFeatures } from './tomlLanguageFeatures';

let runner: CforgeRunner;
let dependencyProvider: DependencyTreeProvider;
let statusBar: StatusBarManager;
let taskProvider: vscode.Disposable | undefined;
let languageFeatures: CforgeTomlLanguageFeatures | undefined;

export function activate(context: vscode.ExtensionContext) {
    console.log('cforge extension is now active');

    // Initialize components
    runner = new CforgeRunner();
    dependencyProvider = new DependencyTreeProvider();
    statusBar = new StatusBarManager();

    // Register TOML language features (autocomplete, hover, diagnostics)
    languageFeatures = registerTomlLanguageFeatures(context);

    // Check for cforge project
    detectCforgeProject();

    // Register tree view
    const treeView = vscode.window.createTreeView('cforgeDependencies', {
        treeDataProvider: dependencyProvider,
        showCollapseAll: true
    });
    context.subscriptions.push(treeView);

    // Register task provider
    taskProvider = vscode.tasks.registerTaskProvider('cforge', new CforgeTaskProvider());
    context.subscriptions.push(taskProvider);

    // Register commands
    registerCommands(context);

    // Watch for workspace changes
    context.subscriptions.push(
        vscode.workspace.onDidChangeWorkspaceFolders(() => detectCforgeProject())
    );

    // Watch for file saves (optional auto-rebuild)
    context.subscriptions.push(
        vscode.workspace.onDidSaveTextDocument((doc) => {
            const config = vscode.workspace.getConfiguration('cforge');
            if (config.get('watchOnSave') && isCppFile(doc.fileName)) {
                vscode.commands.executeCommand('cforge.build');
            }
        })
    );

    // Watch for cforge.toml changes
    const tomlWatcher = vscode.workspace.createFileSystemWatcher('**/cforge.toml');
    tomlWatcher.onDidChange(() => dependencyProvider.refresh());
    tomlWatcher.onDidCreate(() => {
        detectCforgeProject();
        dependencyProvider.refresh();
    });
    tomlWatcher.onDidDelete(() => detectCforgeProject());
    context.subscriptions.push(tomlWatcher);

    context.subscriptions.push(statusBar);
}

function registerCommands(context: vscode.ExtensionContext) {
    // Init command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.init', async () => {
            const options = await vscode.window.showQuickPick([
                { label: 'Executable', description: 'Create a new executable project' },
                { label: 'Static Library', description: 'Create a static library project' },
                { label: 'Shared Library', description: 'Create a shared/dynamic library project' },
                { label: 'Header Only', description: 'Create a header-only library project' }
            ], { placeHolder: 'Select project type' });

            if (options) {
                const typeMap: Record<string, string> = {
                    'Executable': 'executable',
                    'Static Library': 'static_lib',
                    'Shared Library': 'shared_lib',
                    'Header Only': 'header_only'
                };
                await runner.run(['init', `--type=${typeMap[options.label]}`]);
                detectCforgeProject();
                dependencyProvider.refresh();
            }
        })
    );

    // Build commands
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.build', () => {
            const config = vscode.workspace.getConfiguration('cforge');
            const buildConfig = config.get<string>('defaultBuildConfig') || 'Debug';
            statusBar.showBuilding();
            runner.run(['build', '-c', buildConfig]).then(success => {
                statusBar.showReady(success);
            });
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.buildDebug', () => {
            statusBar.showBuilding();
            runner.run(['build', '-c', 'Debug']).then(success => {
                statusBar.showReady(success);
            });
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.buildRelease', () => {
            statusBar.showBuilding();
            runner.run(['build', '-c', 'Release']).then(success => {
                statusBar.showReady(success);
            });
        })
    );

    // Clean command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.clean', () => {
            runner.run(['clean']);
        })
    );

    // Run command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.run', () => {
            const config = vscode.workspace.getConfiguration('cforge');
            const buildConfig = config.get<string>('defaultBuildConfig') || 'Debug';
            runner.run(['run', '-c', buildConfig]);
        })
    );

    // Test command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.test', () => {
            runner.run(['test']);
        })
    );

    // Add dependency command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.addDependency', async () => {
            const packageName = await vscode.window.showInputBox({
                prompt: 'Enter package name (e.g., fmt, spdlog, fmt@11.1.4)',
                placeHolder: 'package[@version]'
            });

            if (packageName) {
                await runner.run(['add', packageName]);
                dependencyProvider.refresh();
            }
        })
    );

    // Remove dependency command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.removeDependency', async (item?: any) => {
            let packageName = item?.label;

            if (!packageName) {
                packageName = await vscode.window.showInputBox({
                    prompt: 'Enter package name to remove',
                    placeHolder: 'package'
                });
            }

            if (packageName) {
                await runner.run(['remove', packageName]);
                dependencyProvider.refresh();
            }
        })
    );

    // Search packages command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.searchPackages', async () => {
            const query = await vscode.window.showInputBox({
                prompt: 'Search for packages',
                placeHolder: 'Enter search query (e.g., json, logging, http)'
            });

            if (query) {
                await runner.run(['search', query]);
            }
        })
    );

    // Update packages command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.updatePackages', () => {
            runner.run(['update', '--packages']);
        })
    );

    // Generate lock file command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.generateLock', () => {
            runner.run(['lock', '--force']);
        })
    );

    // Watch command with options
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.watch', async () => {
            const config = vscode.workspace.getConfiguration('cforge');
            const buildConfig = config.get<string>('defaultBuildConfig') || 'Debug';

            const options = await vscode.window.showQuickPick([
                { label: 'Watch Only', description: 'Rebuild on file changes' },
                { label: 'Watch & Run', description: 'Rebuild and run executable on file changes' },
                { label: 'Watch (Verbose)', description: 'Rebuild with verbose output' },
                { label: 'Watch & Run (Verbose)', description: 'Rebuild and run with verbose output' }
            ], { placeHolder: 'Select watch mode' });

            if (options) {
                const args = ['watch', '-c', buildConfig];
                if (options.label.includes('Run')) {
                    args.push('--run');
                }
                if (options.label.includes('Verbose')) {
                    args.push('--verbose');
                }
                runner.run(args);
            }
        })
    );

    // Format command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.format', () => {
            runner.run(['fmt']);
        })
    );

    // Lint command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.lint', () => {
            runner.run(['lint']);
        })
    );

    // Open terminal command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.openTerminal', () => {
            const terminal = vscode.window.createTerminal('cforge');
            terminal.show();
            terminal.sendText('cforge --help');
        })
    );

    // Refresh dependencies command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.refreshDependencies', () => {
            dependencyProvider.refresh();
        })
    );

    // New project command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.new', async () => {
            const projectName = await vscode.window.showInputBox({
                prompt: 'Enter new project name',
                placeHolder: 'my-project'
            });

            if (projectName) {
                const options = await vscode.window.showQuickPick([
                    { label: 'Executable', description: 'Create a new executable project' },
                    { label: 'Static Library', description: 'Create a static library project' },
                    { label: 'Shared Library', description: 'Create a shared/dynamic library project' },
                    { label: 'Header Only', description: 'Create a header-only library project' }
                ], { placeHolder: 'Select project type' });

                if (options) {
                    const typeMap: Record<string, string> = {
                        'Executable': 'executable',
                        'Static Library': 'static_lib',
                        'Shared Library': 'shared_lib',
                        'Header Only': 'header_only'
                    };
                    await runner.run(['new', projectName, `--type=${typeMap[options.label]}`]);
                }
            }
        })
    );

    // Show dependencies command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.deps', () => {
            runner.run(['deps']);
        })
    );

    // Dependency tree command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.tree', () => {
            runner.run(['tree']);
        })
    );

    // Install command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.install', async () => {
            const config = vscode.workspace.getConfiguration('cforge');
            const buildConfig = config.get<string>('defaultBuildConfig') || 'Release';
            await runner.run(['install', '-c', buildConfig]);
        })
    );

    // Version command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.version', () => {
            runner.run(['version']);
        })
    );

    // IDE files generation command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.ide', async () => {
            const options = await vscode.window.showQuickPick([
                { label: 'Visual Studio', description: 'Generate Visual Studio solution' },
                { label: 'CLion', description: 'Generate CLion project files' },
                { label: 'Xcode', description: 'Generate Xcode project' },
                { label: 'VS Code', description: 'Generate VS Code configuration' }
            ], { placeHolder: 'Select IDE to generate files for' });

            if (options) {
                const ideMap: Record<string, string> = {
                    'Visual Studio': 'vs',
                    'CLion': 'clion',
                    'Xcode': 'xcode',
                    'VS Code': 'vscode'
                };
                await runner.run(['ide', ideMap[options.label]]);
            }
        })
    );

    // Package command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.package', async () => {
            const config = vscode.workspace.getConfiguration('cforge');
            const buildConfig = config.get<string>('defaultBuildConfig') || 'Release';
            statusBar.showBuilding();
            runner.run(['package', '-c', buildConfig]).then(success => {
                statusBar.showReady(success);
            });
        })
    );

    // Benchmark command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.bench', () => {
            runner.run(['bench']);
        })
    );

    // Project info command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.info', () => {
            runner.run(['info']);
        })
    );

    // Circular dependency check command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.circular', () => {
            runner.run(['circular']);
        })
    );

    // Doctor command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.doctor', () => {
            runner.run(['doctor']);
        })
    );

    // Documentation generation command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.docs', () => {
            runner.run(['doc']);
        })
    );

    // Stop watch mode command
    context.subscriptions.push(
        vscode.commands.registerCommand('cforge.stopWatch', () => {
            runner.stop();
        })
    );
}

function detectCforgeProject() {
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (!workspaceFolders) {
        vscode.commands.executeCommand('setContext', 'cforge.projectDetected', false);
        return;
    }

    for (const folder of workspaceFolders) {
        const cforgeToml = vscode.Uri.joinPath(folder.uri, 'cforge.toml');
        const workspaceToml = vscode.Uri.joinPath(folder.uri, 'cforge.workspace.toml');

        vscode.workspace.fs.stat(cforgeToml).then(
            () => {
                vscode.commands.executeCommand('setContext', 'cforge.projectDetected', true);
                dependencyProvider.refresh();
            },
            () => {
                vscode.workspace.fs.stat(workspaceToml).then(
                    () => {
                        vscode.commands.executeCommand('setContext', 'cforge.projectDetected', true);
                        dependencyProvider.refresh();
                    },
                    () => {
                        vscode.commands.executeCommand('setContext', 'cforge.projectDetected', false);
                    }
                );
            }
        );
    }
}

function isCppFile(fileName: string): boolean {
    const ext = fileName.toLowerCase();
    return ext.endsWith('.cpp') || ext.endsWith('.cc') || ext.endsWith('.cxx') ||
           ext.endsWith('.c') || ext.endsWith('.h') || ext.endsWith('.hpp') ||
           ext.endsWith('.hxx');
}

export function deactivate() {
    if (taskProvider) {
        taskProvider.dispose();
    }
}
