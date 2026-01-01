import * as vscode from 'vscode';
import * as fs from 'fs';
import * as path from 'path';

interface Dependency {
    name: string;
    version: string;
    type: 'index' | 'git' | 'vcpkg';
    url?: string;
}

interface WorkspaceMember {
    name: string;
    path: string;
}

export class DependencyTreeProvider implements vscode.TreeDataProvider<DependencyItem> {
    private _onDidChangeTreeData: vscode.EventEmitter<DependencyItem | undefined | null | void> = new vscode.EventEmitter<DependencyItem | undefined | null | void>();
    readonly onDidChangeTreeData: vscode.Event<DependencyItem | undefined | null | void> = this._onDidChangeTreeData.event;

    refresh(): void {
        this._onDidChangeTreeData.fire();
    }

    getTreeItem(element: DependencyItem): vscode.TreeItem {
        return element;
    }

    async getChildren(element?: DependencyItem): Promise<DependencyItem[]> {
        if (element) {
            // Return children of a category
            return element.children || [];
        }

        // Root level - return categories
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
        if (!workspaceFolder) {
            return [];
        }

        // Check for cforge.toml or cforge.workspace.toml
        const cforgeTomlPath = path.join(workspaceFolder.uri.fsPath, 'cforge.toml');
        const workspaceTomlPath = path.join(workspaceFolder.uri.fsPath, 'cforge.workspace.toml');

        let configPath = '';
        let isWorkspace = false;

        if (fs.existsSync(cforgeTomlPath)) {
            configPath = cforgeTomlPath;
            // Check if it's a workspace (has [workspace] section)
            const content = fs.readFileSync(cforgeTomlPath, 'utf-8');
            isWorkspace = content.includes('[workspace]');
        } else if (fs.existsSync(workspaceTomlPath)) {
            configPath = workspaceTomlPath;
            isWorkspace = true;
        }

        if (!configPath) {
            return [];
        }

        try {
            const content = fs.readFileSync(configPath, 'utf-8');
            const categories: DependencyItem[] = [];

            // If this is a workspace, show workspace members first
            if (isWorkspace) {
                const members = this.parseWorkspaceMembers(content, workspaceFolder.uri.fsPath);
                if (members.length > 0) {
                    const memberChildren = members.map(m => new DependencyItem(
                        m.name,
                        m.path,
                        vscode.TreeItemCollapsibleState.None,
                        'project',
                        undefined
                    ));
                    categories.push(new DependencyItem(
                        'Workspace Members',
                        `${members.length} projects`,
                        vscode.TreeItemCollapsibleState.Expanded,
                        'category',
                        undefined,
                        memberChildren
                    ));
                }
            }

            // Parse dependencies
            const deps = this.parseDependencies(content);

            // Index dependencies
            const indexDeps = deps.filter(d => d.type === 'index');
            if (indexDeps.length > 0) {
                const indexChildren = indexDeps.map(d => new DependencyItem(
                    d.name,
                    d.version,
                    vscode.TreeItemCollapsibleState.None,
                    'package',
                    d
                ));
                categories.push(new DependencyItem(
                    'Registry',
                    `${indexDeps.length} packages`,
                    vscode.TreeItemCollapsibleState.Expanded,
                    'category',
                    undefined,
                    indexChildren
                ));
            }

            // Git dependencies
            const gitDeps = deps.filter(d => d.type === 'git');
            if (gitDeps.length > 0) {
                const gitChildren = gitDeps.map(d => new DependencyItem(
                    d.name,
                    d.version || d.url || '',
                    vscode.TreeItemCollapsibleState.None,
                    'git',
                    d
                ));
                categories.push(new DependencyItem(
                    'Git',
                    `${gitDeps.length} repositories`,
                    vscode.TreeItemCollapsibleState.Expanded,
                    'category',
                    undefined,
                    gitChildren
                ));
            }

            // vcpkg dependencies
            const vcpkgDeps = deps.filter(d => d.type === 'vcpkg');
            if (vcpkgDeps.length > 0) {
                const vcpkgChildren = vcpkgDeps.map(d => new DependencyItem(
                    d.name,
                    d.version,
                    vscode.TreeItemCollapsibleState.None,
                    'vcpkg',
                    d
                ));
                categories.push(new DependencyItem(
                    'vcpkg',
                    `${vcpkgDeps.length} packages`,
                    vscode.TreeItemCollapsibleState.Expanded,
                    'category',
                    undefined,
                    vcpkgChildren
                ));
            }

            return categories;
        } catch (error) {
            console.error('Error parsing cforge.toml:', error);
            return [];
        }
    }

    private parseDependencies(content: string): Dependency[] {
        const deps: Dependency[] = [];
        const lines = content.split('\n');
        let inDependencies = false;
        let inGitDependencies = false;
        let inVcpkgDependencies = false;

        for (const line of lines) {
            const trimmed = line.trim();

            // Check for section headers
            if (trimmed === '[dependencies]') {
                inDependencies = true;
                inGitDependencies = false;
                inVcpkgDependencies = false;
                continue;
            } else if (trimmed === '[dependencies.git]') {
                inDependencies = false;
                inGitDependencies = true;
                inVcpkgDependencies = false;
                continue;
            } else if (trimmed === '[dependencies.vcpkg]') {
                inDependencies = false;
                inGitDependencies = false;
                inVcpkgDependencies = true;
                continue;
            } else if (trimmed.startsWith('[')) {
                inDependencies = false;
                inGitDependencies = false;
                inVcpkgDependencies = false;
                continue;
            }

            // Skip empty lines and comments
            if (!trimmed || trimmed.startsWith('#')) {
                continue;
            }

            // Parse dependencies
            if (inDependencies) {
                // Skip config keys
                if (trimmed.startsWith('fetch_content') || trimmed.startsWith('directory')) {
                    continue;
                }

                // Parse: name = "version" or name = { version = "..." }
                const match = trimmed.match(/^(\w+)\s*=\s*"([^"]+)"/);
                if (match) {
                    deps.push({
                        name: match[1],
                        version: match[2],
                        type: 'index'
                    });
                } else {
                    // Complex format with table
                    const tableMatch = trimmed.match(/^(\w+)\s*=\s*\{/);
                    if (tableMatch) {
                        const versionMatch = trimmed.match(/version\s*=\s*"([^"]+)"/);
                        deps.push({
                            name: tableMatch[1],
                            version: versionMatch ? versionMatch[1] : '*',
                            type: 'index'
                        });
                    }
                }
            } else if (inGitDependencies) {
                // Git dependencies are in sub-tables like [dependencies.git.name]
                const tableMatch = trimmed.match(/^\[dependencies\.git\.(\w+)\]/);
                if (tableMatch) {
                    deps.push({
                        name: tableMatch[1],
                        version: '',
                        type: 'git'
                    });
                } else {
                    // Update last git dep with URL/tag info
                    const lastGitDep = deps.filter(d => d.type === 'git').pop();
                    if (lastGitDep) {
                        const urlMatch = trimmed.match(/url\s*=\s*"([^"]+)"/);
                        const tagMatch = trimmed.match(/tag\s*=\s*"([^"]+)"/);
                        if (urlMatch) {
                            lastGitDep.url = urlMatch[1];
                        }
                        if (tagMatch) {
                            lastGitDep.version = tagMatch[1];
                        }
                    }
                }
            }
        }

        return deps;
    }

    private parseWorkspaceMembers(content: string, workspacePath: string): WorkspaceMember[] {
        const members: WorkspaceMember[] = [];
        const lines = content.split('\n');
        let inWorkspace = false;
        let inMembers = false;

        for (const line of lines) {
            const trimmed = line.trim();

            // Check for workspace section
            if (trimmed === '[workspace]') {
                inWorkspace = true;
                continue;
            } else if (trimmed.startsWith('[') && trimmed !== '[workspace]' && !trimmed.startsWith('[workspace.')) {
                inWorkspace = false;
                inMembers = false;
                continue;
            }

            if (inWorkspace) {
                // Check for members array start
                if (trimmed.startsWith('members')) {
                    inMembers = true;
                    // Handle inline array: members = ["a", "b"]
                    const inlineMatch = trimmed.match(/members\s*=\s*\[([^\]]+)\]/);
                    if (inlineMatch) {
                        const items = inlineMatch[1].split(',').map(s => s.trim().replace(/"/g, ''));
                        for (const item of items) {
                            if (item) {
                                members.push({
                                    name: path.basename(item),
                                    path: item
                                });
                            }
                        }
                        inMembers = false;
                    }
                    continue;
                }

                // Parse multiline array items
                if (inMembers) {
                    if (trimmed === ']') {
                        inMembers = false;
                        continue;
                    }
                    const match = trimmed.match(/"([^"]+)"/);
                    if (match) {
                        members.push({
                            name: path.basename(match[1]),
                            path: match[1]
                        });
                    }
                }
            }
        }

        return members;
    }
}

export class DependencyItem extends vscode.TreeItem {
    children?: DependencyItem[];

    constructor(
        public readonly label: string,
        private version: string,
        public readonly collapsibleState: vscode.TreeItemCollapsibleState,
        private itemType: 'category' | 'package' | 'git' | 'vcpkg' | 'project',
        public readonly dependency?: Dependency,
        children?: DependencyItem[]
    ) {
        super(label, collapsibleState);
        this.children = children;

        this.description = version;

        switch (itemType) {
            case 'category':
                this.iconPath = new vscode.ThemeIcon('folder');
                break;
            case 'package':
                this.iconPath = new vscode.ThemeIcon('package');
                this.contextValue = 'dependency';
                break;
            case 'git':
                this.iconPath = new vscode.ThemeIcon('git-branch');
                this.contextValue = 'dependency';
                break;
            case 'vcpkg':
                this.iconPath = new vscode.ThemeIcon('extensions');
                this.contextValue = 'dependency';
                break;
            case 'project':
                this.iconPath = new vscode.ThemeIcon('project');
                this.contextValue = 'workspaceMember';
                this.tooltip = `Workspace member: ${label}\nPath: ${version}`;
                break;
        }

        if (dependency?.url) {
            this.tooltip = `${dependency.name}\n${dependency.url}`;
        }
    }
}
