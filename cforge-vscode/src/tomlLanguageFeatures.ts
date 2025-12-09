import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as path from 'path';
import * as fs from 'fs';
import * as os from 'os';

interface PackageInfo {
    name: string;
    description: string;
    version: string;
    versions: string[];
    repository: string;
    license: string;
    keywords: string[];
}

interface PackageCache {
    packages: Map<string, PackageInfo>;
    lastUpdate: number;
}

interface KeyInfo {
    key: string;
    desc: string;
    snippet: string;
    values?: string[];
}

interface SectionInfo {
    name: string;
    desc: string;
    documentation: string;
    keys: KeyInfo[];
}

// cforge.toml schema based on README documentation
const CFORGE_SCHEMA: Record<string, SectionInfo> = {
    'project': {
        name: 'project',
        desc: 'Project metadata and configuration',
        documentation: 'The `[project]` section defines your project\'s identity and basic settings.',
        keys: [
            { key: 'name', desc: 'Project name', snippet: 'name = "${1:myproject}"' },
            { key: 'version', desc: 'Project version (semver)', snippet: 'version = "${1:1.0.0}"' },
            { key: 'description', desc: 'Project description', snippet: 'description = "${1:My awesome C++ project}"' },
            { key: 'cpp_standard', desc: 'C++ standard version', snippet: 'cpp_standard = "${1|11,14,17,20,23|}"', values: ['11', '14', '17', '20', '23'] },
            { key: 'c_standard', desc: 'C standard version', snippet: 'c_standard = "${1|99,11,17|}"', values: ['99', '11', '17'] },
            { key: 'binary_type', desc: 'Output type', snippet: 'binary_type = "${1|executable,shared_library,static_library,header_only|}"', values: ['executable', 'shared_library', 'static_library', 'header_only'] },
            { key: 'authors', desc: 'Project authors', snippet: 'authors = ["${1:Your Name <you@example.com>}"]' },
            { key: 'license', desc: 'Project license', snippet: 'license = "${1:MIT}"' },
        ]
    },
    'build': {
        name: 'build',
        desc: 'Build configuration settings',
        documentation: 'The `[build]` section configures how your project is built.',
        keys: [
            { key: 'build_type', desc: 'Default build type', snippet: 'build_type = "${1|Debug,Release,RelWithDebInfo,MinSizeRel|}"', values: ['Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel'] },
            { key: 'directory', desc: 'Build output directory', snippet: 'directory = "${1:build}"' },
            { key: 'source_dirs', desc: 'Source directories', snippet: 'source_dirs = ["${1:src}"]' },
            { key: 'include_dirs', desc: 'Include directories', snippet: 'include_dirs = ["${1:include}"]' },
        ]
    },
    'build.config.debug': {
        name: 'build.config.debug',
        desc: 'Debug build configuration',
        documentation: 'Settings specific to Debug builds.',
        keys: [
            { key: 'defines', desc: 'Preprocessor definitions', snippet: 'defines = ["${1:DEBUG=1}"]' },
            { key: 'flags', desc: 'Compiler flags', snippet: 'flags = ["${1:DEBUG_INFO}", "${2:NO_OPT}"]' },
        ]
    },
    'build.config.release': {
        name: 'build.config.release',
        desc: 'Release build configuration',
        documentation: 'Settings specific to Release builds.',
        keys: [
            { key: 'defines', desc: 'Preprocessor definitions', snippet: 'defines = ["${1:NDEBUG=1}"]' },
            { key: 'flags', desc: 'Compiler flags', snippet: 'flags = ["${1:OPTIMIZE}"]' },
        ]
    },
    'test': {
        name: 'test',
        desc: 'Test configuration',
        documentation: 'The `[test]` section configures your project\'s test suite.',
        keys: [
            { key: 'enabled', desc: 'Enable testing', snippet: 'enabled = ${1|true,false|}', values: ['true', 'false'] },
            { key: 'directory', desc: 'Test source directory', snippet: 'directory = "${1:tests}"' },
            { key: 'framework', desc: 'Test framework', snippet: 'framework = "${1|catch2,gtest|}"', values: ['catch2', 'gtest'] },
        ]
    },
    'benchmark': {
        name: 'benchmark',
        desc: 'Benchmark configuration',
        documentation: 'The `[benchmark]` section configures Google Benchmark integration.',
        keys: [
            { key: 'directory', desc: 'Benchmark source directory', snippet: 'directory = "${1:bench}"' },
            { key: 'target', desc: 'Benchmark target name', snippet: 'target = "${1:benchmarks}"' },
        ]
    },
    'package': {
        name: 'package',
        desc: 'Packaging configuration',
        documentation: 'The `[package]` section configures distributable package generation.',
        keys: [
            { key: 'enabled', desc: 'Enable packaging', snippet: 'enabled = ${1|true,false|}', values: ['true', 'false'] },
            { key: 'generators', desc: 'Package generators', snippet: 'generators = ["${1|ZIP,TGZ,DEB,NSIS|}"]', values: ['ZIP', 'TGZ', 'DEB', 'NSIS'] },
            { key: 'vendor', desc: 'Package vendor name', snippet: 'vendor = "${1:Your Name}"' },
        ]
    },
    'cmake': {
        name: 'cmake',
        desc: 'CMake integration settings',
        documentation: 'The `[cmake]` section customizes CMake behavior.',
        keys: [
            { key: 'version', desc: 'Minimum CMake version', snippet: 'version = "${1:3.15}"' },
            { key: 'generator', desc: 'CMake generator', snippet: 'generator = "${1|Ninja,Unix Makefiles,Visual Studio 17 2022|}"', values: ['Ninja', 'Unix Makefiles', 'Visual Studio 17 2022', 'Visual Studio 16 2019'] },
            { key: 'includes', desc: 'Custom CMake files to include', snippet: 'includes = ["${1:cmake/custom.cmake}"]' },
            { key: 'module_paths', desc: 'Custom module search paths', snippet: 'module_paths = ["${1:cmake/modules}"]' },
            { key: 'inject_before_target', desc: 'CMake code before target', snippet: 'inject_before_target = """\n${1:# Code here}\n"""' },
            { key: 'inject_after_target', desc: 'CMake code after target', snippet: 'inject_after_target = """\n${1:# Code here}\n"""' },
        ]
    },
    'cmake.compilers': {
        name: 'cmake.compilers',
        desc: 'CMake compiler settings',
        documentation: 'Specify custom C and C++ compilers.',
        keys: [
            { key: 'c', desc: 'C compiler path', snippet: 'c = "${1:/usr/bin/gcc}"' },
            { key: 'cxx', desc: 'C++ compiler path', snippet: 'cxx = "${1:/usr/bin/g++}"' },
        ]
    },
    'cmake.visual_studio': {
        name: 'cmake.visual_studio',
        desc: 'Visual Studio settings',
        documentation: 'Visual Studio-specific CMake settings.',
        keys: [
            { key: 'platform', desc: 'VS platform', snippet: 'platform = "${1|x64,Win32,ARM,ARM64|}"', values: ['x64', 'Win32', 'ARM', 'ARM64'] },
            { key: 'toolset', desc: 'VS toolset', snippet: 'toolset = "${1:v143}"' },
        ]
    },
    'dependencies': {
        name: 'dependencies',
        desc: 'Package dependencies',
        documentation: 'The `[dependencies]` section lists packages.\n\nFormats:\n- `pkg = "1.0.0"` - Exact version\n- `pkg = "1.*"` - Any 1.x version\n- `pkg = { version = "1.0.0", features = ["async"] }`\n- `pkg = { git = "url", tag = "v1.0" }`',
        keys: [
            { key: 'fetch_content', desc: 'Use CMake FetchContent', snippet: 'fetch_content = ${1|true,false|}', values: ['true', 'false'] },
            { key: 'directory', desc: 'Directory for cloned deps', snippet: 'directory = "${1:deps}"' },
        ]
    },
    'dependencies.vcpkg': {
        name: 'dependencies.vcpkg',
        desc: 'vcpkg integration',
        documentation: 'Configure vcpkg package manager integration.',
        keys: [
            { key: 'enabled', desc: 'Enable vcpkg', snippet: 'enabled = ${1|true,false|}', values: ['true', 'false'] },
            { key: 'path', desc: 'vcpkg installation path', snippet: 'path = "${1:~/.vcpkg}"' },
            { key: 'triplet', desc: 'vcpkg triplet', snippet: 'triplet = "${1|x64-windows,x64-linux,x64-osx|}"', values: ['x64-windows', 'x64-windows-static', 'x64-linux', 'x64-osx'] },
        ]
    },
    'cross': {
        name: 'cross',
        desc: 'Cross-compilation settings',
        documentation: 'The `[cross]` section configures cross-compilation.',
        keys: [
            { key: 'enabled', desc: 'Enable cross-compilation', snippet: 'enabled = ${1|true,false|}', values: ['true', 'false'] },
        ]
    },
    'cross.target': {
        name: 'cross.target',
        desc: 'Cross-compilation target',
        documentation: 'Target system settings for cross-compilation.',
        keys: [
            { key: 'system', desc: 'Target system (CMAKE_SYSTEM_NAME)', snippet: 'system = "${1|Linux,Windows,Android,iOS|}"', values: ['Linux', 'Windows', 'Android', 'iOS', 'Darwin'] },
            { key: 'processor', desc: 'Target processor (CMAKE_SYSTEM_PROCESSOR)', snippet: 'processor = "${1|aarch64,armv7l,x86_64|}"', values: ['aarch64', 'armv7l', 'x86_64', 'arm'] },
            { key: 'toolchain', desc: 'CMake toolchain file', snippet: 'toolchain = "${1:path/to/toolchain.cmake}"' },
        ]
    },
    'cross.compilers': {
        name: 'cross.compilers',
        desc: 'Cross-compilation compilers',
        documentation: 'Specify cross-compilers.',
        keys: [
            { key: 'c', desc: 'C cross-compiler', snippet: 'c = "${1:/usr/bin/aarch64-linux-gnu-gcc}"' },
            { key: 'cxx', desc: 'C++ cross-compiler', snippet: 'cxx = "${1:/usr/bin/aarch64-linux-gnu-g++}"' },
        ]
    },
    'cross.paths': {
        name: 'cross.paths',
        desc: 'Cross-compilation paths',
        documentation: 'Sysroot and find paths for cross-compilation.',
        keys: [
            { key: 'sysroot', desc: 'Target sysroot path', snippet: 'sysroot = "${1:/path/to/sysroot}"' },
            { key: 'find_root', desc: 'Find root path', snippet: 'find_root = "${1:/path/to/find/root}"' },
        ]
    },
    'scripts': {
        name: 'scripts',
        desc: 'Custom scripts and hooks',
        documentation: 'The `[scripts]` section defines build hooks.',
        keys: [
            { key: 'pre_build', desc: 'Scripts to run before build', snippet: 'pre_build = ["${1:scripts/setup.py}"]' },
            { key: 'post_build', desc: 'Scripts to run after build', snippet: 'post_build = ["${1:scripts/deploy.py}"]' },
        ]
    },
    'workspace': {
        name: 'workspace',
        desc: 'Workspace configuration',
        documentation: 'The `[workspace]` section is used in cforge-workspace.toml files.',
        keys: [
            { key: 'name', desc: 'Workspace name', snippet: 'name = "${1:my_workspace}"' },
            { key: 'projects', desc: 'Projects in workspace', snippet: 'projects = ["${1:core}", "${2:gui}"]' },
            { key: 'default_startup_project', desc: 'Default project to build/run', snippet: 'default_startup_project = "${1:core}"' },
        ]
    },
};

// Platform-specific sections
const PLATFORM_KEYS: KeyInfo[] = [
    { key: 'defines', desc: 'Platform-specific defines', snippet: 'defines = ["${1:PLATFORM_DEF}"]' },
    { key: 'flags', desc: 'Platform-specific compiler flags', snippet: 'flags = ["${1:-Wall}"]' },
    { key: 'links', desc: 'Platform-specific libraries', snippet: 'links = ["${1:pthread}"]' },
    { key: 'frameworks', desc: 'macOS frameworks', snippet: 'frameworks = ["${1:Cocoa}", "${2:IOKit}"]' },
];

// Compiler-specific sections
const COMPILER_KEYS: KeyInfo[] = [
    { key: 'flags', desc: 'Compiler-specific flags', snippet: 'flags = ["${1:-Wall}"]' },
    { key: 'defines', desc: 'Compiler-specific defines', snippet: 'defines = ["${1:COMPILER_DEF}"]' },
];

export class CforgeTomlLanguageFeatures implements
    vscode.CompletionItemProvider,
    vscode.HoverProvider,
    vscode.CodeActionProvider {

    private packageCache: PackageCache = {
        packages: new Map(),
        lastUpdate: 0
    };
    private diagnosticCollection: vscode.DiagnosticCollection;
    private isUpdatingCache = false;

    constructor() {
        this.diagnosticCollection = vscode.languages.createDiagnosticCollection('cforge');
        this.updatePackageCache();
    }

    // ==================== Completion Provider ====================

    async provideCompletionItems(
        document: vscode.TextDocument,
        position: vscode.Position,
        token: vscode.CancellationToken
    ): Promise<vscode.CompletionItem[]> {
        const line = document.lineAt(position).text;
        const linePrefix = line.substring(0, position.character);
        const section = this.getCurrentSection(document, position);

        // Suggest section headers
        if (linePrefix.trim() === '' || linePrefix.trim() === '[') {
            return this.getSectionCompletions();
        }

        // In dependencies section - package names and versions
        if (section === 'dependencies') {
            if (this.isStartOfDependencyLine(linePrefix)) {
                return this.getPackageCompletions();
            }

            const versionMatch = linePrefix.match(/^(\w[\w-]*)\s*=\s*"([^"]*)$/);
            if (versionMatch) {
                return this.getVersionCompletions(versionMatch[1]);
            }
        }

        // Suggest keys within sections
        if (section) {
            return this.getKeyCompletions(section);
        }

        return [];
    }

    private isStartOfDependencyLine(linePrefix: string): boolean {
        const trimmed = linePrefix.trim();
        if (trimmed === '') return true;
        if (/^[a-zA-Z_][a-zA-Z0-9_-]*$/.test(trimmed)) return true;
        return false;
    }

    private async getPackageCompletions(): Promise<vscode.CompletionItem[]> {
        await this.ensureCacheUpdated();

        const items: vscode.CompletionItem[] = [];

        for (const [name, info] of this.packageCache.packages) {
            const item = new vscode.CompletionItem(name, vscode.CompletionItemKind.Module);
            item.detail = info.version ? `v${info.version}` : 'cforge package';
            item.documentation = new vscode.MarkdownString(
                `**${name}**\n\n${info.description || 'No description.'}\n\n` +
                (info.license ? `**License:** ${info.license}` : '')
            );
            item.insertText = new vscode.SnippetString(`${name} = "\${1:${info.version || '*'}}"$0`);
            item.sortText = `0_${name}`;
            items.push(item);
        }

        return items;
    }

    private async getVersionCompletions(packageName: string): Promise<vscode.CompletionItem[]> {
        await this.ensureCacheUpdated();

        const items: vscode.CompletionItem[] = [];
        const pkg = this.packageCache.packages.get(packageName);

        // Always offer wildcards
        items.push(this.createVersionItem('*', 'Latest version'));

        if (pkg && pkg.versions && pkg.versions.length > 0) {
            for (const version of pkg.versions.slice(0, 10)) {
                items.push(this.createVersionItem(version, `Version ${version}`));
            }

            // Wildcard patterns
            const latest = pkg.versions[0];
            const parts = latest.split('.');
            if (parts.length >= 2) {
                items.push(this.createVersionItem(`${parts[0]}.*`, `Any ${parts[0]}.x version`));
            }
        }

        return items;
    }

    private createVersionItem(version: string, detail: string): vscode.CompletionItem {
        const item = new vscode.CompletionItem(version, vscode.CompletionItemKind.Value);
        item.detail = detail;
        item.insertText = version;
        return item;
    }

    private getSectionCompletions(): vscode.CompletionItem[] {
        const items: vscode.CompletionItem[] = [];

        for (const [sectionName, info] of Object.entries(CFORGE_SCHEMA)) {
            const item = new vscode.CompletionItem(`[${sectionName}]`, vscode.CompletionItemKind.Class);
            item.detail = info.desc;
            item.documentation = new vscode.MarkdownString(info.documentation);
            item.insertText = new vscode.SnippetString(`[${sectionName}]\n$0`);
            items.push(item);
        }

        // Platform sections
        for (const platform of ['windows', 'linux', 'macos']) {
            const item = new vscode.CompletionItem(`[platform.${platform}]`, vscode.CompletionItemKind.Class);
            item.detail = `${platform} platform settings`;
            item.insertText = new vscode.SnippetString(`[platform.${platform}]\n$0`);
            items.push(item);
        }

        // Compiler sections
        for (const compiler of ['msvc', 'gcc', 'clang', 'mingw']) {
            const item = new vscode.CompletionItem(`[compiler.${compiler}]`, vscode.CompletionItemKind.Class);
            item.detail = `${compiler} compiler settings`;
            item.insertText = new vscode.SnippetString(`[compiler.${compiler}]\n$0`);
            items.push(item);
        }

        return items;
    }

    private getKeyCompletions(section: string): vscode.CompletionItem[] {
        // Check schema first
        const sectionInfo = CFORGE_SCHEMA[section];
        if (sectionInfo) {
            return sectionInfo.keys.map(k => {
                const item = new vscode.CompletionItem(k.key, vscode.CompletionItemKind.Property);
                item.detail = k.desc;
                item.insertText = new vscode.SnippetString(k.snippet);
                return item;
            });
        }

        // Platform sections
        if (section.startsWith('platform.')) {
            return PLATFORM_KEYS.map(k => {
                const item = new vscode.CompletionItem(k.key, vscode.CompletionItemKind.Property);
                item.detail = k.desc;
                item.insertText = new vscode.SnippetString(k.snippet);
                return item;
            });
        }

        // Compiler sections
        if (section.startsWith('compiler.')) {
            return COMPILER_KEYS.map(k => {
                const item = new vscode.CompletionItem(k.key, vscode.CompletionItemKind.Property);
                item.detail = k.desc;
                item.insertText = new vscode.SnippetString(k.snippet);
                return item;
            });
        }

        return [];
    }

    // ==================== Hover Provider ====================

    async provideHover(
        document: vscode.TextDocument,
        position: vscode.Position,
        token: vscode.CancellationToken
    ): Promise<vscode.Hover | null> {
        const line = document.lineAt(position).text;
        const section = this.getCurrentSection(document, position);

        // Section header hover
        const sectionMatch = line.match(/^\[([^\]]+)\]$/);
        if (sectionMatch) {
            const sectionName = sectionMatch[1];
            const sectionInfo = CFORGE_SCHEMA[sectionName];
            if (sectionInfo) {
                const md = new vscode.MarkdownString();
                md.appendMarkdown(`## [${sectionName}]\n\n`);
                md.appendMarkdown(sectionInfo.documentation);
                return new vscode.Hover(md);
            }
        }

        // Key hover
        const keyMatch = line.match(/^(\w+)\s*=/);
        if (keyMatch && section) {
            const keyName = keyMatch[1];
            const wordRange = document.getWordRangeAtPosition(position);

            if (wordRange && document.getText(wordRange) === keyName) {
                // Package in dependencies
                if (section === 'dependencies' && !['fetch_content', 'directory'].includes(keyName)) {
                    return this.getPackageHover(keyName);
                }

                // Schema key
                const sectionInfo = CFORGE_SCHEMA[section];
                if (sectionInfo) {
                    const keyInfo = sectionInfo.keys.find(k => k.key === keyName);
                    if (keyInfo) {
                        const md = new vscode.MarkdownString();
                        md.appendMarkdown(`**${keyName}**\n\n${keyInfo.desc}`);
                        if (keyInfo.values) {
                            md.appendMarkdown(`\n\n**Values:** ${keyInfo.values.join(', ')}`);
                        }
                        return new vscode.Hover(md);
                    }
                }
            }
        }

        return null;
    }

    private async getPackageHover(packageName: string): Promise<vscode.Hover | null> {
        await this.ensureCacheUpdated();

        const pkg = this.packageCache.packages.get(packageName);
        if (!pkg) {
            return new vscode.Hover(
                new vscode.MarkdownString(`**${packageName}**\n\n_Package info not available._`)
            );
        }

        const md = new vscode.MarkdownString();
        md.appendMarkdown(`## ${pkg.name}\n\n`);
        md.appendMarkdown(`${pkg.description || 'No description.'}\n\n`);
        md.appendMarkdown(`**Latest:** ${pkg.version || 'Unknown'}\n\n`);
        if (pkg.versions && pkg.versions.length > 0) {
            md.appendMarkdown(`**Versions:** ${pkg.versions.slice(0, 5).join(', ')}${pkg.versions.length > 5 ? '...' : ''}\n\n`);
        }
        if (pkg.license) {
            md.appendMarkdown(`**License:** ${pkg.license}\n\n`);
        }
        if (pkg.repository) {
            md.appendMarkdown(`[Repository](${pkg.repository})`);
        }
        md.isTrusted = true;

        return new vscode.Hover(md);
    }

    // ==================== Code Actions ====================

    provideCodeActions(
        document: vscode.TextDocument,
        range: vscode.Range | vscode.Selection,
        context: vscode.CodeActionContext,
        token: vscode.CancellationToken
    ): vscode.CodeAction[] {
        const actions: vscode.CodeAction[] = [];
        const line = document.lineAt(range.start.line).text;
        const section = this.getCurrentSection(document, range.start);

        if (section === 'dependencies') {
            const match = line.match(/^(\w[\w-]*)\s*=\s*"([^"]+)"/);
            if (match) {
                const packageName = match[1];
                const currentVersion = match[2];
                const pkg = this.packageCache.packages.get(packageName);

                if (pkg && pkg.version && currentVersion !== '*' && currentVersion !== pkg.version) {
                    const action = new vscode.CodeAction(
                        `Update to ${pkg.version}`,
                        vscode.CodeActionKind.QuickFix
                    );
                    action.edit = new vscode.WorkspaceEdit();
                    const versionStart = line.indexOf('"') + 1;
                    const versionEnd = line.lastIndexOf('"');
                    action.edit.replace(
                        document.uri,
                        new vscode.Range(range.start.line, versionStart, range.start.line, versionEnd),
                        pkg.version
                    );
                    actions.push(action);
                }
            }
        }

        return actions;
    }

    // ==================== Diagnostics ====================

    public async updateDiagnostics(document: vscode.TextDocument) {
        if (!document.fileName.endsWith('cforge.toml') && !document.fileName.endsWith('cforge-workspace.toml')) {
            return;
        }

        await this.ensureCacheUpdated();

        const diagnostics: vscode.Diagnostic[] = [];
        const lines = document.getText().split('\n');
        let currentSection = '';

        for (let i = 0; i < lines.length; i++) {
            const line = lines[i];
            const trimmed = line.trim();

            if (trimmed.startsWith('#') || trimmed === '') continue;

            const sectionMatch = trimmed.match(/^\[([^\]]+)\]$/);
            if (sectionMatch) {
                currentSection = sectionMatch[1];
                continue;
            }

            // Validate dependencies
            if (currentSection === 'dependencies') {
                if (trimmed.startsWith('fetch_content') || trimmed.startsWith('directory')) {
                    continue;
                }

                const match = trimmed.match(/^(\w[\w-]*)\s*=\s*"([^"]*)"/);
                if (match) {
                    const packageName = match[1];
                    const version = match[2];
                    const pkg = this.packageCache.packages.get(packageName);

                    // Only warn if cache exists and package not found
                    if (this.packageCache.packages.size > 0 && !pkg) {
                        const startChar = line.indexOf(packageName);
                        diagnostics.push({
                            range: new vscode.Range(i, startChar, i, startChar + packageName.length),
                            message: `Package '${packageName}' not found in registry`,
                            severity: vscode.DiagnosticSeverity.Warning,
                            source: 'cforge'
                        });
                    } else if (pkg && pkg.version && this.isUpdateAvailable(version, pkg)) {
                        const versionStart = line.indexOf('"') + 1;
                        diagnostics.push({
                            range: new vscode.Range(i, versionStart, i, versionStart + version.length),
                            message: `Newer version available: ${pkg.version}`,
                            severity: vscode.DiagnosticSeverity.Information,
                            source: 'cforge'
                        });
                    }
                }
            }
        }

        this.diagnosticCollection.set(document.uri, diagnostics);
    }

    private isUpdateAvailable(currentVersion: string, pkg: PackageInfo): boolean {
        if (currentVersion === '*' || currentVersion.includes('*')) return false;
        if (currentVersion === pkg.version) return false;

        if (pkg.versions && pkg.versions.length > 0) {
            const idx = pkg.versions.indexOf(currentVersion);
            if (idx > 0) return true;
        }

        return false;
    }

    // ==================== Helpers ====================

    private getCurrentSection(document: vscode.TextDocument, position: vscode.Position): string | null {
        for (let i = position.line; i >= 0; i--) {
            const line = document.lineAt(i).text.trim();
            const match = line.match(/^\[([^\]]+)\]$/);
            if (match) {
                return match[1];
            }
        }
        return null;
    }

    private async ensureCacheUpdated(): Promise<void> {
        const now = Date.now();
        if (now - this.packageCache.lastUpdate < 5 * 60 * 1000 && this.packageCache.packages.size > 0) {
            return;
        }

        if (this.isUpdatingCache) return;

        await this.updatePackageCache();
    }

    private async updatePackageCache(): Promise<void> {
        this.isUpdatingCache = true;

        try {
            const registryDir = this.getRegistryPath();

            if (registryDir && fs.existsSync(registryDir)) {
                await this.loadPackagesFromRegistry(registryDir);
            }
        } catch (error) {
            console.error('Failed to update package cache:', error);
        } finally {
            this.isUpdatingCache = false;
            this.packageCache.lastUpdate = Date.now();
        }
    }

    private getRegistryPath(): string | null {
        if (process.platform === 'win32') {
            const localAppData = process.env.LOCALAPPDATA;
            if (localAppData) {
                return path.join(localAppData, 'cforge', 'registry', 'cforge-index', 'packages');
            }
        } else {
            const home = os.homedir();
            return path.join(home, '.local', 'share', 'cforge', 'registry', 'cforge-index', 'packages');
        }
        return null;
    }

    private async loadPackagesFromRegistry(registryDir: string): Promise<void> {
        try {
            const entries = fs.readdirSync(registryDir, { withFileTypes: true });

            for (const entry of entries) {
                if (entry.isDirectory() && entry.name.length === 1) {
                    const letterDir = path.join(registryDir, entry.name);
                    await this.loadPackagesFromLetterDir(letterDir);
                }
            }
        } catch (e) {
            console.error('Failed to read registry:', e);
        }
    }

    private async loadPackagesFromLetterDir(letterDir: string): Promise<void> {
        try {
            const packages = fs.readdirSync(letterDir, { withFileTypes: true });

            for (const pkg of packages) {
                if (pkg.isFile() && pkg.name.endsWith('.toml')) {
                    const packageName = pkg.name.replace('.toml', '');
                    const tomlPath = path.join(letterDir, pkg.name);

                    try {
                        const content = fs.readFileSync(tomlPath, 'utf-8');
                        this.parsePackageToml(packageName, content);
                    } catch (e) {
                        // Skip
                    }
                }
            }
        } catch (e) {
            // Skip
        }
    }

    private parsePackageToml(name: string, content: string): void {
        const pkg: PackageInfo = {
            name,
            description: '',
            version: '',
            versions: [],
            repository: '',
            license: '',
            keywords: []
        };

        const lines = content.split('\n');

        for (const line of lines) {
            const trimmed = line.trim();
            if (trimmed.startsWith('#') || trimmed === '') continue;

            const match = trimmed.match(/^(\w+)\s*=\s*"([^"]*)"/);
            if (match) {
                switch (match[1]) {
                    case 'description': pkg.description = match[2]; break;
                    case 'version': pkg.version = match[2]; break;
                    case 'repository': pkg.repository = match[2]; break;
                    case 'license': pkg.license = match[2]; break;
                }
            }

            const versionsMatch = trimmed.match(/^versions\s*=\s*\[(.*)\]/);
            if (versionsMatch) {
                pkg.versions = versionsMatch[1]
                    .split(',')
                    .map(v => v.trim().replace(/"/g, ''))
                    .filter(v => v.length > 0);
            }
        }

        this.packageCache.packages.set(name, pkg);
    }

    public dispose() {
        this.diagnosticCollection.dispose();
    }
}

export function registerTomlLanguageFeatures(context: vscode.ExtensionContext): CforgeTomlLanguageFeatures {
    const features = new CforgeTomlLanguageFeatures();

    const selector: vscode.DocumentSelector = [
        { language: 'toml', pattern: '**/cforge.toml' },
        { language: 'toml', pattern: '**/cforge-workspace.toml' },
        { pattern: '**/cforge.toml' },
        { pattern: '**/cforge-workspace.toml' }
    ];

    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(selector, features, '.', '"', '=', '['),
        vscode.languages.registerHoverProvider(selector, features),
        vscode.languages.registerCodeActionsProvider(selector, features),
        vscode.workspace.onDidOpenTextDocument(doc => features.updateDiagnostics(doc)),
        vscode.workspace.onDidChangeTextDocument(e => features.updateDiagnostics(e.document)),
        vscode.workspace.onDidSaveTextDocument(doc => features.updateDiagnostics(doc)),
        features
    );

    vscode.workspace.textDocuments.forEach(doc => features.updateDiagnostics(doc));

    return features;
}
