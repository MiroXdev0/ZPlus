"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const Module = require("node:module");
const { test } = require("node:test");

const extensionRoot = path.resolve(__dirname, "..");
const projectRoot = path.resolve(extensionRoot, "..");
const manifest = require(path.join(extensionRoot, "package.json"));
const grammar = require(path.join(extensionRoot, "syntaxes", "zplusLanguage.json"));
const snippets = require(path.join(extensionRoot, "snippets", "zplus.json"));
const { parseDiagnostics } = require(path.join(extensionRoot, "lib", "diagnostics"));
const { executeCompiler, resolveCompilerPath } = require(path.join(extensionRoot, "lib", "compiler"));
const { formatText, formatSelectedLines } = require(path.join(extensionRoot, "lib", "formatter"));
const { WorkspaceIndex } = require(path.join(extensionRoot, "lib", "workspace-index"));

const compiler = process.env.ZPLUS_COMPILER || (process.platform === "win32"
    ? path.join(projectRoot, "build", "Release", "zp.exe")
    : path.join(projectRoot, "build", "zp"));
const hasCompiler = fs.existsSync(compiler);

function createVscodeMock() {
    class Position {
        constructor(line, character) { this.line = line; this.character = character; }
        translate(lineDelta, characterDelta) {
            return new Position(this.line + lineDelta, this.character + characterDelta);
        }
    }
    class Range {
        constructor(startLine, startCharacter, endLine, endCharacter) {
            this.start = startLine instanceof Position ? startLine : new Position(startLine, startCharacter);
            this.end = endLine instanceof Position ? endLine : new Position(endLine, endCharacter);
        }
    }
    class CompletionItem {
        constructor(label, kind) { this.label = label; this.kind = kind; }
    }
    class DocumentSymbol {
        constructor(name, detail, kind, range, selectionRange) {
            Object.assign(this, { name, detail, kind, range, selectionRange });
        }
    }
    class Diagnostic {
        constructor(range, message, severity) { Object.assign(this, { range, message, severity }); }
    }
    class SignatureInformation {
        constructor(label) { this.label = label; this.parameters = []; }
    }
    class ParameterInformation {
        constructor(label) { this.label = label; }
    }
    class SignatureHelp {
        constructor() { this.signatures = []; }
    }
    class SymbolInformation {
        constructor(name, kind, containerName, location) { Object.assign(this, { name, kind, containerName, location }); }
    }
    class WorkspaceEdit {
        constructor() { this.entries = []; }
        replace(uri, range, newText) { this.entries.push({ uri, range, newText }); }
    }
    class TextEdit {
        static replace(range, newText) { return { range, newText }; }
    }
    class ProcessExecution {
        constructor(process, args, options) { Object.assign(this, { process, args, options }); }
    }
    class Task {
        constructor(definition, scope, name, source, execution) {
            Object.assign(this, { definition, scope, name, source, execution });
        }
    }
    class MarkdownString {
        constructor(value) { this.value = value; }
    }
    const registrations = {
        commands: [], providers: [], diagnosticWrites: [], testControllers: [], testRuns: [], taskProviders: []
    };
    const configuration = {};
    const listeners = new Map();
    const event = name => handler => {
        if (typeof handler !== "function") return { dispose() {} };
        const entries = listeners.get(name) || [];
        entries.push(handler);
        listeners.set(name, entries);
        return {
            dispose() {
                const index = entries.indexOf(handler);
                if (index >= 0) entries.splice(index, 1);
            }
        };
    };
    const fireLast = (name, value) => {
        const entries = listeners.get(name) || [];
        if (entries.length) entries.at(-1)(value);
    };
    const vscode = {
        Position,
        Range,
        RelativePattern: class RelativePattern { constructor(base, pattern) { Object.assign(this, { base, pattern }); } },
        CompletionItem,
        DocumentSymbol,
        Diagnostic,
        SignatureInformation,
        ParameterInformation,
        SignatureHelp,
        SymbolInformation,
        WorkspaceEdit,
        TextEdit,
        ProcessExecution,
        Task,
        TaskScope: { Workspace: 1 },
        TaskGroup: { Build: "build", Test: "test" },
        EndOfLine: { LF: 1, CRLF: 2 },
        MarkdownString,
        TestMessage: class TestMessage { constructor(message) { this.message = message; } },
        TestRunProfileKind: { Run: 1 },
        SnippetString: class SnippetString { constructor(value) { this.value = value; } },
        Hover: class Hover { constructor(contents, range) { Object.assign(this, { contents, range }); } },
        Location: class Location {
            constructor(uri, position) {
                this.uri = uri;
                this.position = position;
                this.range = position instanceof Range ? position : new Range(position, position);
            }
        },
        DiagnosticSeverity: { Error: 0, Warning: 1, Information: 2 },
        CompletionItemTag: { Deprecated: 1 },
        CompletionItemKind: {
            Keyword: 1, TypeParameter: 2, Constant: 3, Function: 4, Class: 5, Variable: 6
        },
        SymbolKind: { Function: 1, Variable: 2, Constant: 3, Struct: 4, Class: 5, Enum: 6, Namespace: 7 },
        Uri: {
            file(filePath) {
                return { fsPath: filePath, toString: () => filePath };
            }
        },
        StatusBarAlignment: { Right: 1 },
        ProgressLocation: { Notification: 1 },
        ConfigurationTarget: { Global: 1 },
        commands: {
            registerCommand: (name, callback) => {
                registrations.commands.push({ name, callback });
                return event();
            }
        },
        languages: {
            createDiagnosticCollection: () => ({
                set(uri, diagnostics) { registrations.diagnosticWrites.push({ uri, diagnostics }); },
                delete(uri) { registrations.diagnosticWrites.push({ uri, diagnostics: [] }); },
                dispose() {}
            }),
            registerCompletionItemProvider: (_, provider) => {
                registrations.providers.push({ type: "completion", provider });
                return event();
            },
            registerHoverProvider: (_, provider) => {
                registrations.providers.push({ type: "hover", provider });
                return event();
            },
            registerDefinitionProvider: (_, provider) => {
                registrations.providers.push({ type: "definition", provider });
                return event();
            },
            registerTypeDefinitionProvider: (_, provider) => {
                registrations.providers.push({ type: "typeDefinition", provider });
                return event();
            },
            registerDocumentSymbolProvider: (_, provider) => {
                registrations.providers.push({ type: "symbols", provider });
                return event();
            },
            registerSignatureHelpProvider: (_, provider) => {
                registrations.providers.push({ type: "signature", provider });
                return event();
            },
            registerReferenceProvider: (_, provider) => {
                registrations.providers.push({ type: "references", provider });
                return event();
            },
            registerRenameProvider: (_, provider) => {
                registrations.providers.push({ type: "rename", provider });
                return event();
            },
            registerWorkspaceSymbolProvider: provider => {
                registrations.providers.push({ type: "workspaceSymbols", provider });
                return event();
            },
            registerDocumentFormattingEditProvider: (_, provider) => {
                registrations.providers.push({ type: "format", provider });
                return event();
            },
            registerDocumentRangeFormattingEditProvider: (_, provider) => {
                registrations.providers.push({ type: "formatRange", provider });
                return event();
            }
        },
        workspace: {
            getConfiguration: () => ({
                get: (name, fallback) => Object.hasOwn(configuration, name) ? configuration[name] : fallback,
                update: async (name, value) => { configuration[name] = value; }
            }),
            onDidChangeTextDocument: event("textChange"),
            onDidSaveTextDocument: event("save"),
            onDidCloseTextDocument: event("close"),
            onDidChangeWorkspaceFolders: event("workspaceFolders"),
            createFileSystemWatcher: () => ({
                onDidCreate: event("fileCreate"),
                onDidChange: event("fileChange"),
                onDidDelete: event("fileDelete"),
                dispose() {}
            }),
            workspaceFolders: [],
            textDocuments: [],
            findFiles: async () => [],
            getWorkspaceFolder: () => undefined
        },
        tests: {
            createTestController(id, label) {
                const items = [];
                const controller = {
                    id,
                    label,
                    items: {
                        replace(next) { items.splice(0, items.length, ...next); },
                        [Symbol.iterator]() { return items[Symbol.iterator](); }
                    },
                    createTestItem(itemId, itemLabel, uri) {
                        return { id: itemId, label: itemLabel, uri };
                    },
                    createRunProfile(name, kind, handler, isDefault) {
                        controller.runProfile = { name, kind, handler, isDefault };
                    },
                    createTestRun(request) {
                        const run = {
                            request,
                            results: { enqueued: [], started: [], passed: [], failed: [], skipped: [] },
                            output: [], ended: false,
                            enqueued(item) { this.results.enqueued.push(item); },
                            started(item) { this.results.started.push(item); },
                            passed(item) { this.results.passed.push(item); },
                            failed(item, message) { this.results.failed.push({ item, message }); },
                            skipped(item) { this.results.skipped.push(item); },
                            appendOutput(value, location, testId) { this.output.push({ value, location, testId }); },
                            end() { this.ended = true; }
                        };
                        registrations.testRuns.push(run);
                        return run;
                    },
                    dispose() {}
                };
                registrations.testControllers.push(controller);
                return controller;
            }
        },
        tasks: {
            registerTaskProvider(type, provider) {
                registrations.taskProviders.push({ type, provider });
                return event();
            }
        },
        window: {
            activeTextEditor: undefined,
            createOutputChannel: () => ({ appendLine() {}, show() {}, dispose() {} }),
            createStatusBarItem: () => ({ show() {}, hide() {}, dispose() {} }),
            onDidChangeActiveTextEditor: event("activeEditor"),
            withProgress: async (_, action) => action(),
            showWarningMessage: async () => {},
            showErrorMessage: async () => {},
            showInformationMessage: async () => {},
            showOpenDialog: async () => undefined,
            showQuickPick: async () => undefined,
            showInputBox: async () => undefined
        },
        registrations: Object.assign(registrations, { fireLast }),
        configuration
    };
    return vscode;
}

const vscodeMock = createVscodeMock();
const originalLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (request === "vscode") return vscodeMock;
    return originalLoad.call(this, request, parent, isMain);
};
const extension = require(path.join(extensionRoot, "extension.js"));
Module._load = originalLoad;

function documentFrom(text, filePath = path.join(os.tmpdir(), "example.zp")) {
    const lines = text.split(/\r?\n/);
    const lineOffsets = [];
    let lineOffset = 0;
    for (const line of lines) {
        lineOffsets.push(lineOffset);
        lineOffset += line.length;
        const lineEnding = text.slice(lineOffset).match(/^(?:\r\n|\n|\r)/);
        if (lineEnding) lineOffset += lineEnding[0].length;
    }
    return {
        languageId: "zplus",
        uri: { fsPath: filePath, toString: () => filePath },
        version: 1,
        lineCount: lines.length,
        getText(range) {
            if (!range) return text;
            const start = this.offsetAt(range.start);
            const end = this.offsetAt(range.end);
            return text.slice(start, end);
        },
        lineAt: line => ({ text: lines[line], range: new vscodeMock.Range(line, 0, line, lines[line].length) }),
        getWordRangeAtPosition(position) {
            const textLine = lines[position.line] || "";
            const start = textLine.slice(0, position.character + 1).search(/[A-Za-z_]\w*$/);
            if (start < 0) return undefined;
            const match = textLine.slice(start).match(/^[A-Za-z_]\w*/);
            return new vscodeMock.Range(position.line, start, position.line, start + match[0].length);
        },
        offsetAt(position) {
            return lineOffsets[position.line] + position.character;
        },
        positionAt(offset) {
            const leading = text.slice(0, offset).split(/\r?\n/);
            return new vscodeMock.Position(leading.length - 1, leading.at(-1).length);
        },
        get lineCountValue() { return lines.length; }
    };
}

test("manifest, grammar, language configuration and snippets are loadable", () => {
    assert.equal(manifest.contributes.languages[0].id, "zplus");
    assert.equal(manifest.version, "1.3.4");
    assert.equal(manifest.name, "zplus-language");
    assert.equal(manifest.publisher, "Miroxdev0");
    assert.equal(manifest.license, "Apache-2.0");
    assert.equal(manifest.homepage, "https://zpluse.vercel.app/");
    assert.equal(Object.hasOwn(manifest, "repository"), false);
    assert.equal(Object.hasOwn(manifest, "bugs"), false);
    assert.ok(manifest.description.length > 20);
    assert.doesNotMatch(manifest.description, /proprietary/i);
    const marketplaceText = [
        manifest.description,
        fs.readFileSync(path.join(extensionRoot, "README.md"), "utf8"),
        fs.readFileSync(path.join(extensionRoot, "CHANGELOG.md"), "utf8")
    ].join("\n");
    assert.match(marketplaceText, /https:\/\/zpluse\.vercel\.app\//);
    assert.doesNotMatch(marketplaceText, /github\.com|gitlab\.com|bitbucket\.org/i);
    assert.doesNotMatch(marketplaceText, /open[ -]?source|free and open/i);
    assert.ok(manifest.categories.includes("Programming Languages"));
    assert.ok(manifest.keywords.includes("compiler"));
    const extensionLicensePath = path.join(extensionRoot, "LICENSE.txt");
    assert.ok(fs.existsSync(extensionLicensePath));
    assert.ok(!fs.existsSync(path.join(extensionRoot, "LICENSE")));
    assert.deepEqual(
        fs.readFileSync(extensionLicensePath),
        fs.readFileSync(path.join(projectRoot, "LICENSE")),
        "extension terms must match the official root Z+ license exactly"
    );
    assert.match(fs.readFileSync(extensionLicensePath, "utf8"), /Apache License\s+Version 2\.0/m);
    assert.deepEqual(manifest.dependencies || {}, {}, "the runtime has no external npm dependencies");
    assert.deepEqual(manifest.contributes.taskDefinitions[0].properties.command.enum,
        ["build", "run", "test"]);
    assert.deepEqual(manifest.contributes.languages[0].extensions, [".zp", ".zh"]);
    assert.ok(fs.existsSync(path.join(extensionRoot, manifest.contributes.grammars[0].path)));
    assert.equal(grammar.scopeName, "source.zplus");
    assert.ok(grammar.repository.comments.patterns.some(pattern => pattern.begin === "/\\*"));
    assert.ok(grammar.repository.keywords.patterns.some(pattern => pattern.match.includes("pub")));
    assert.ok(snippets["Z+ Program"]);
    const iconTheme = manifest.contributes.iconThemes.find(theme => theme.id === "zplus-icon-theme");
    assert.ok(iconTheme, "Z+ file icon theme must be registered");
    const iconThemePath = path.join(extensionRoot, iconTheme.path);
    assert.ok(fs.existsSync(iconThemePath), "registered icon theme file must exist");
    const iconThemeDefinition = JSON.parse(fs.readFileSync(iconThemePath, "utf8"));
    assert.equal(iconThemeDefinition.fileExtensions.zp, "zplus-file-dark");
    assert.equal(iconThemeDefinition.fileExtensions.zh, "zplus-file-dark");
    assert.deepEqual(Object.keys(iconThemeDefinition.fileExtensions).sort(), ["zh", "zp"]);
    assert.equal(iconThemeDefinition.light.fileExtensions.zp, "zplus-file");
    assert.equal(iconThemeDefinition.light.fileExtensions.zh, "zplus-file");
    assert.ok(!iconThemeDefinition.fileNames && !iconThemeDefinition.file, "other file types should retain defaults");
    const definitions = [
        ...Object.values(iconThemeDefinition.iconDefinitions),
        ...Object.values(iconThemeDefinition.light.iconDefinitions || {})
    ];
    for (const definition of definitions) {
        assert.ok(fs.existsSync(path.resolve(path.dirname(iconThemePath), definition.iconPath)),
            `icon asset must exist: ${definition.iconPath}`);
    }
    for (const command of [
        "zplus.checkFile", "zplus.runFile", "zplus.buildFile",
        "zplus.selectCompiler", "zplus.showCompilerVersion",
        "zplus.zbuildBuildProject", "zplus.zbuildRunProject", "zplus.zbuildTestProject",
        "zplus.zbuildInitProject",
        "zplus.zpmInstall", "zplus.zpmAdd", "zplus.zpmRemove", "zplus.zpmList"
    ]) {
        assert.ok(manifest.contributes.commands.some(item => item.command === command), command);
    }
});

test("extension branding uses the official logo variants for light and dark surfaces", () => {
    const officialLight = fs.readFileSync(path.join(projectRoot, "logo", "Z+ Emblem - for ligth theme.png"));
    const officialDark = fs.readFileSync(path.join(projectRoot, "logo", "Z+ Emblem - for dark theme.png"));
    const readDimensions = png => [png.readUInt32BE(16), png.readUInt32BE(20)];
    const marketplaceIcon = fs.readFileSync(path.join(extensionRoot, manifest.icon));
    const lightFileIcon = fs.readFileSync(path.join(extensionRoot, "icons", "zplus-file.png"));
    const darkFileIcon = fs.readFileSync(path.join(extensionRoot, "icons", "zplus-file-dark.png"));

    assert.equal(manifest.icon, "icons/zplus.icon.png");
    assert.deepEqual(readDimensions(officialLight), [1536, 1024]);
    assert.deepEqual(readDimensions(officialDark), [1536, 1024]);
    for (const [name, icon] of [
        ["Marketplace", marketplaceIcon],
        ["Light-theme file", lightFileIcon],
        ["Dark-theme file", darkFileIcon]
    ]) {
        assert.deepEqual(readDimensions(icon), [512, 512], `${name} icon should use a square canvas`);
        assert.equal(icon[25], 6, `${name} icon should preserve RGBA transparency`);
    }
    assert.notDeepEqual(marketplaceIcon, lightFileIcon,
        "the Marketplace icon should be a separately sized asset from the file icon");
    assert.notDeepEqual(lightFileIcon, darkFileIcon,
        "light and dark file themes should use distinct logo variants");
});

test("activation registers commands and editor providers", () => {
    const context = { subscriptions: [] };
    extension.activate(context);
    assert.deepEqual(
        vscodeMock.registrations.commands.map(item => item.name),
        [
            "zplus.checkFile", "zplus.runFile", "zplus.buildFile",
            "zplus.selectCompiler", "zplus.showCompilerVersion",
            "zplus.zbuildBuildProject", "zplus.zbuildRunProject", "zplus.zbuildTestProject",
            "zplus.zbuildInitProject",
            "zplus.zpmInstall", "zplus.zpmAdd", "zplus.zpmRemove", "zplus.zpmList"
        ]
    );
    assert.deepEqual(
        vscodeMock.registrations.providers.map(item => item.type),
        ["completion", "hover", "definition", "typeDefinition", "symbols", "signature", "references", "rename",
            "workspaceSymbols", "format", "formatRange"]
    );
    assert.equal(vscodeMock.registrations.testControllers.at(-1).id, "zplus");
    assert.equal(vscodeMock.registrations.taskProviders.at(-1).type, "zplus");
    extension.deactivate();
});

test("editing a Z+ buffer invalidates analysis and clears stale root diagnostics", () => {
    extension.activate({ subscriptions: [] });
    const document = documentFrom("start {\n missing\n} stop\n");
    try {
        extension.publishDiagnostics(
            '{"severity":"error","message":"old error","line":2,"column":2}',
            document
        );
        const before = vscodeMock.registrations.diagnosticWrites.length;
        vscodeMock.configuration.checkWhileTyping = false;
        vscodeMock.registrations.fireLast("textChange", { document });
        const writes = vscodeMock.registrations.diagnosticWrites.slice(before);
        assert.ok(writes.some(write => write.uri.toString() === document.uri.toString() &&
            write.diagnostics.length === 0));
    } finally {
        delete vscodeMock.configuration.checkWhileTyping;
        extension.deactivate();
    }
});

test("newer Test Explorer refreshes supersede pending project scans", async () => {
    vscodeMock.workspace.workspaceFolders = [];
    vscodeMock.workspace.findFiles = async () => [];
    extension.activate({ subscriptions: [] });
    const firstRoot = path.join(os.tmpdir(), "zplus-old-project");
    const latestRoot = path.join(os.tmpdir(), "zplus-latest-project");
    const folder = { uri: vscodeMock.Uri.file(os.tmpdir()) };
    const pending = [];
    try {
        vscodeMock.workspace.workspaceFolders = [folder];
        vscodeMock.workspace.findFiles = () => new Promise(resolve => pending.push(resolve));
        const olderRefresh = extension.refreshTestProjects();
        const latestRefresh = extension.refreshTestProjects();
        assert.equal(pending.length, 2);
        pending[1]([vscodeMock.Uri.file(path.join(latestRoot, "zproject.toml"))]);
        await latestRefresh;
        pending[0]([vscodeMock.Uri.file(path.join(firstRoot, "zproject.toml"))]);
        await olderRefresh;
        const controller = vscodeMock.registrations.testControllers.at(-1);
        assert.deepEqual([...controller.items].map(item => item.uri.fsPath), [latestRoot]);
    } finally {
        vscodeMock.workspace.workspaceFolders = [];
        vscodeMock.workspace.findFiles = async () => [];
        extension.deactivate();
    }
});

test("ZBuild project discovery walks from the active file to zproject.toml", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus zbuild project "));
    try {
        await fs.promises.mkdir(path.join(directory, "src"));
        await fs.promises.writeFile(path.join(directory, "zproject.toml"), "[project]\n");
        vscodeMock.window.activeTextEditor = {
            document: { uri: { fsPath: path.join(directory, "src", "main.zp") } }
        };
        assert.equal(await extension.findZBuildProjectRoot(), directory);
    } finally {
        vscodeMock.window.activeTextEditor = undefined;
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("ZBuild project initializer builds documented, path-safe CLI arguments", () => {
    assert.deepEqual(extension.createZBuildInitArgs(
        "My-App", "Console", path.join(os.tmpdir(), "projects with spaces", "My-App")
    ), ["init", "My-App", "--template", "Console", "--path",
        path.join(os.tmpdir(), "projects with spaces", "My-App")]);
});

test("ZPBuild discovery honors configuration and prefers local zpbuild binaries", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus zpbuild path "));
    const executableName = process.platform === "win32" ? "zpbuild.exe" : "zpbuild";
    const localExecutable = path.join(directory, "bin", executableName);
    try {
        await fs.promises.mkdir(path.dirname(localExecutable));
        await fs.promises.writeFile(localExecutable, "");
        vscodeMock.workspace.workspaceFolders = [{ uri: { fsPath: directory } }];
        delete vscodeMock.configuration.zbuildPath;
        assert.equal(extension.getZBuildPath(), localExecutable);

        const configured = path.join(directory, "custom tools", executableName);
        vscodeMock.configuration.zbuildPath = configured;
        assert.equal(extension.getZBuildPath(), configured);
    } finally {
        vscodeMock.workspace.workspaceFolders = [];
        delete vscodeMock.configuration.zbuildPath;
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("Test Explorer discovers ZPBuild projects and executes their configured suite", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus testing api "));
    const manifestPath = path.join(directory, "zproject.toml");
    try {
        await fs.promises.writeFile(manifestPath, "[project]\nname = \"TestApi\"\n");
        await fs.promises.writeFile(path.join(directory, "test"),
            "process.stdout.write('configured Z+ tests passed\\n')\n");
        vscodeMock.workspace.workspaceFolders = [{ uri: vscodeMock.Uri.file(directory) }];
        vscodeMock.workspace.findFiles = async () => [vscodeMock.Uri.file(manifestPath)];
        vscodeMock.configuration.zbuildPath = process.execPath;
        extension.activate({ subscriptions: [] });
        await extension.refreshTestProjects();

        const controller = vscodeMock.registrations.testControllers.at(-1);
        const items = [...controller.items];
        assert.equal(items.length, 1);
        assert.equal(items[0].uri.fsPath, directory);
        await controller.runProfile.handler({ include: items, exclude: [] }, {
            isCancellationRequested: false,
            onCancellationRequested: () => ({ dispose() {} })
        });

        const result = vscodeMock.registrations.testRuns.at(-1);
        assert.deepEqual(result.results.passed, items);
        assert.equal(result.results.failed.length, 0);
        assert.equal(result.ended, true);
        assert.match(result.output.map(entry => entry.value).join(""), /configured Z\+ tests passed/);

        await fs.promises.writeFile(path.join(directory, "test"), "setInterval(() => {}, 1000)\n");
        let cancellationHandler;
        const cancellationToken = {
            isCancellationRequested: false,
            onCancellationRequested(handler) {
                cancellationHandler = handler;
                return { dispose() {} };
            }
        };
        const pendingRun = controller.runProfile.handler({ include: items, exclude: [] }, cancellationToken);
        setTimeout(() => {
            cancellationToken.isCancellationRequested = true;
            cancellationHandler();
        }, 50);
        await pendingRun;
        const cancelled = vscodeMock.registrations.testRuns.at(-1);
        assert.deepEqual(cancelled.results.skipped, items);
    } finally {
        extension.deactivate();
        vscodeMock.workspace.workspaceFolders = [];
        vscodeMock.workspace.findFiles = async () => [];
        delete vscodeMock.configuration.zbuildPath;
        await new Promise(resolve => setTimeout(resolve, 150));
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("workspace build tasks launch ZPBuild in project directories through the integrated task API", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus task project "));
    const manifestPath = path.join(directory, "zproject.toml");
    try {
        await fs.promises.writeFile(manifestPath, "[project]\nname = \"TaskProject\"\n");
        const folder = { uri: vscodeMock.Uri.file(directory), name: "TaskProject" };
        vscodeMock.workspace.workspaceFolders = [folder];
        vscodeMock.workspace.findFiles = async () => [vscodeMock.Uri.file(manifestPath)];
        vscodeMock.configuration.zbuildPath = process.execPath;
        extension.activate({ subscriptions: [] });

        const provider = vscodeMock.registrations.taskProviders.at(-1).provider;
        const tasks = await provider.provideTasks();
        assert.deepEqual(tasks.map(task => task.definition.command), ["build", "run", "test"]);
        for (const task of tasks) {
            assert.equal(task.execution.process, process.execPath);
            assert.deepEqual(task.execution.args, [task.definition.command]);
            assert.equal(task.execution.options.cwd, directory);
        }
        assert.equal(tasks[0].group, "build");
        assert.equal(tasks[2].group, "test");
        assert.equal(provider.resolveTask(tasks[1]).execution.options.cwd, directory);
    } finally {
        extension.deactivate();
        vscodeMock.workspace.workspaceFolders = [];
        vscodeMock.workspace.findFiles = async () => [];
        delete vscodeMock.configuration.zbuildPath;
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("formatter adjusts syntax-aware indentation without touching literals or comments", () => {
    const source = [
        "start {",
        "fn choose() {",
        "if (true) {",
        'say("{ keep } // literal") // keep comment text',
        "/* block comment starts",
        "        preserve this comment line {",
        "    */",
        "} else {",
        "say('}')",
        "}",
        "} end fn",
        "} stop",
        ""
    ].join("\n");
    const formatted = formatText(source, { tabSize: 4, insertSpaces: true });
    assert.equal(formatted, [
        "start {",
        "    fn choose() {",
        "        if (true) {",
        '            say("{ keep } // literal") // keep comment text',
        "            /* block comment starts",
        "        preserve this comment line {",
        "    */",
        "        } else {",
        "            say('}')",
        "        }",
        "    } end fn",
        "} stop",
        ""
    ].join("\n"));
    assert.equal(formatText(formatted, { tabSize: 4, insertSpaces: true }), formatted,
        "formatting should be stable across repeated runs");
});

test("formatter handles legacy blocks, CRLF input and tab indentation", () => {
    const source = "start:\r\nfn f():\r\nif true:\r\nsay(1)\r\nelse:\r\nsay(2)\r\nend\r\nend\r\nstop\r\n";
    const formatted = formatText(source, { tabSize: 2, insertSpaces: false });
    assert.ok(formatted.includes("\r\n"));
    assert.equal(formatted, "start:\r\n\tfn f():\r\n\t\tif true:\r\n\t\t\tsay(1)\r\n\t\telse:\r\n\t\t\tsay(2)\r\n\t\tend\r\n\tend\r\nstop\r\n");
    assert.equal(formatText(formatted, { tabSize: 2, insertSpaces: false }), formatted);
});

test("selection formatting only changes complete selected lines", () => {
    const source = "start {\n say(1) \n say(2) \n} stop\n";
    const partialLine = formatSelectedLines(source, 1, 2, 1, 7, { tabSize: 4 });
    assert.equal(partialLine, undefined, "a partial one-line selection must not be widened");

    const middleLine = formatSelectedLines(source, 0, 3, 3, 2, { tabSize: 4 });
    assert.equal(middleLine.startLine, 1);
    assert.equal(middleLine.endLine, 2);
    assert.equal(middleLine.text, "    say(1)\n    say(2)");
    assert.equal(source.split("\n")[0], "start {");
    assert.equal(source.split("\n")[3], "} stop");

    const fullLine = formatSelectedLines("start {\n say(1) \n} stop", 1, 0, 1, 8, { tabSize: 2 });
    assert.equal(fullLine.text, "  say(1)");
});

test("VS Code document and selection formatting providers produce workspace edits", () => {
    extension.activate({ subscriptions: [] });
    try {
        const document = documentFrom("start {\n say(1) \n} stop\n");
        const documentProvider = vscodeMock.registrations.providers
            .filter(item => item.type === "format").at(-1).provider;
        const rangeProvider = vscodeMock.registrations.providers
            .filter(item => item.type === "formatRange").at(-1).provider;
        const edits = documentProvider.provideDocumentFormattingEdits(document, {
            tabSize: 2, insertSpaces: true
        });
        assert.equal(edits.length, 1);
        assert.equal(edits[0].newText, "start {\n  say(1)\n} stop\n");

        const rangeEdit = rangeProvider.provideDocumentRangeFormattingEdits(
            document, new vscodeMock.Range(1, 0, 1, 8), { tabSize: 4, insertSpaces: true });
        assert.equal(rangeEdit.length, 1);
        assert.equal(rangeEdit[0].newText, "    say(1)");

        const partialBoundary = rangeProvider.provideDocumentRangeFormattingEdits(
            document, new vscodeMock.Range(0, 2, 2, 3), { tabSize: 4, insertSpaces: true });
        assert.equal(partialBoundary.length, 1);
        assert.equal(partialBoundary[0].range.start.line, 1);
        assert.equal(partialBoundary[0].range.end.line, 1);
        assert.equal(partialBoundary[0].newText, "    say(1)");

        const partialOnly = rangeProvider.provideDocumentRangeFormattingEdits(
            document, new vscodeMock.Range(1, 2, 1, 6), { tabSize: 4, insertSpaces: true });
        assert.deepEqual(partialOnly, []);
    } finally {
        extension.deactivate();
    }
});

test("ZPM project discovery walks from the active file to zpm.json", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus zpm project "));
    try {
        await fs.promises.mkdir(path.join(directory, "src"));
        await fs.promises.writeFile(path.join(directory, "zpm.json"), "{}");
        vscodeMock.window.activeTextEditor = {
            document: { uri: { fsPath: path.join(directory, "src", "main.zp") } }
        };
        assert.equal(await extension.findZpmProjectRoot(), directory);
    } finally {
        vscodeMock.window.activeTextEditor = undefined;
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("completion and document symbols include local declarations and built-ins", () => {
    const document = documentFrom(
        "fn greet(name: string) -> string:\n    return name\nend\nconst count: number = 1"
    );
    const completions = extension.provideCompletions(document).map(item => String(item.label));
    assert.ok(completions.includes("say"));
    assert.ok(completions.includes("printz"));
    assert.ok(completions.includes("printm"));
    const printm = extension.provideCompletions(document).find(item => item.label === "printm");
    assert.deepEqual(printm.tags, [vscodeMock.CompletionItemTag.Deprecated]);
    assert.match(
        extension.provideHover(documentFrom("printm(1)"), new vscodeMock.Position(0, 3)).contents.value,
        /Deprecated.*printz/
    );
    assert.ok(completions.includes("greet"));
    assert.ok(completions.includes("count"));
    const symbols = extension.provideDocumentSymbols(document).map(item => item.name);
    assert.deepEqual(symbols, ["greet", "count"]);
});

test("symbol indexing recognizes current brace-style declarations", () => {
    const document = documentFrom(
        "start {\n    pub fn calculate(value: number): number {\n        return value\n    } end fn\n    struct Point {\n    } end struct\n} stop"
    );
    const symbols = extension.parseSymbols(document);
    assert.deepEqual(symbols.map(symbol => symbol.name), ["calculate", "Point"]);
    assert.equal(symbols[0].type, "number");
    assert.equal(symbols[0].isPublic, true);
    assert.equal(symbols[1].kind, "struct");
});

test("Go to Type Definition resolves local and imported aggregate types", async () => {
    const local = documentFrom(
        "start {\nstruct Point {\n} end struct\nlet point: Point\n} stop"
    );
    const localTarget = extension.provideTypeDefinition(local, new vscodeMock.Position(3, 5));
    assert.equal(localTarget.uri.fsPath, local.uri.fsPath);
    assert.equal(localTarget.position.line, 1);

    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus type definition "));
    try {
        const headerPath = path.join(directory, "types.zh");
        const entryPath = path.join(directory, "main.zp");
        await fs.promises.writeFile(headerPath,
            "start {\npub struct Widget {\n} end struct\npub let current: Widget\n} stop\n");
        await fs.promises.writeFile(entryPath,
            'use "types.zh"\nstart {\nlet value: Widget\n} stop\n');
        const entry = documentFrom(await fs.promises.readFile(entryPath, "utf8"), entryPath);
        const target = extension.provideTypeDefinition(entry, new vscodeMock.Position(2, 5));
        assert.equal(target.uri.fsPath, headerPath);
        assert.equal(target.position.line, 1);
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("signature help, hover and definition resolve locally declared symbols", () => {
    const source = "fn sum(a: number, b: number) -> number:\n    return a + b\nend\nstart:\n    sum(1, 2)\nstop";
    const document = documentFrom(source);
    const signature = extension.provideSignatureHelp(document, new vscodeMock.Position(4, 12));
    assert.equal(signature.signatures[0].parameters.length, 2);
    assert.equal(signature.activeParameter, 1);
    const hover = extension.provideHover(document, new vscodeMock.Position(0, 4));
    assert.ok(hover);
    const definition = extension.provideDefinition(document, new vscodeMock.Position(4, 5));
    assert.equal(definition.position.line, 0);
});

test("imported .zh declarations support completion, signature help and definition", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus editor modules "));
    try {
        const header = path.join(directory, "common defs.zh");
        const main = path.join(directory, "main.zp");
        await fs.promises.writeFile(header,
            "pub fn fromModule(value: number) -> number:\n    return value\nend\n");
        const source = 'use "common defs.zh"\nstart:\n    fromModule(1)\nstop';
        const document = documentFrom(source, main);
        const names = extension.provideCompletions(document).map(item => String(item.label));
        assert.ok(names.includes("fromModule"));
        const signature = extension.provideSignatureHelp(document, new vscodeMock.Position(2, 16));
        assert.equal(signature.signatures[0].parameters.length, 1);
        const definition = extension.provideDefinition(document, new vscodeMock.Position(2, 5));
        assert.equal(definition.uri.fsPath, header);
        assert.equal(definition.position.line, 0);
        assert.ok(definition.position.character > 0);
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("zp.graphics import provides canonical qualified API completions and navigation", () => {
    const source = "use zp.graphics\nstart:\n    graphics.createWindow(\"demo\", 400, 300)\nstop";
    const document = documentFrom(source, path.join(os.tmpdir(), "zgraphics-main.zp"));
    const completions = extension.provideCompletions(document, new vscodeMock.Position(2, 13))
        .map(item => String(item.label));
    assert.ok(completions.includes("graphics.createWindow"));
    assert.ok(completions.includes("graphics.waitEvent"));

    const signature = extension.provideSignatureHelp(document, new vscodeMock.Position(2, 35));
    assert.equal(signature.signatures[0].parameters.length, 3);

    const hover = extension.provideHover(document, new vscodeMock.Position(2, 17));
    assert.match(hover.contents.value, /createWindow/);
    const definition = extension.provideDefinition(document, new vscodeMock.Position(2, 17));
    assert.equal(
        definition.uri.fsPath,
        path.join(projectRoot, "stdlib", "zp", "graphics", "graphics.zh")
    );
});

test("legacy standard-module names resolve the migrated source with the legacy alias", () => {
    const source = "use zgraphics\nstart:\n    zgraphics.createWindow(\"demo\", 400, 300)\nstop";
    const document = documentFrom(source, path.join(os.tmpdir(), "legacy-zgraphics-main.zp"));
    const completions = extension.provideCompletions(document, new vscodeMock.Position(2, 14))
        .map(item => String(item.label));
    assert.ok(completions.includes("zgraphics.createWindow"));
    assert.equal(
        extension.provideDefinition(document, new vscodeMock.Position(2, 18)).uri.fsPath,
        path.join(projectRoot, "stdlib", "zp", "graphics", "graphics.zh")
    );
});

test("qualified module completion, hover, signature and definition respect public visibility", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus qualified editor "));
    try {
        const modulePath = path.join(directory, "math.zh");
        const mainPath = path.join(directory, "main.zp");
        await fs.promises.writeFile(modulePath,
            "pub fn add(left: number, right: number) -> number:\n" +
            "    return left + right\nend\n" +
            "fn hidden() -> number:\n    return 0\nend\n");
        const source = 'use "math.zh" as math\nstart:\n    math.add(1, 2)\nstop';
        const document = documentFrom(source, mainPath);

        const unqualified = extension.provideCompletions(document)
            .map(item => String(item.label));
        assert.ok(unqualified.includes("add"));
        assert.ok(!unqualified.includes("hidden"));

        const qualified = extension.provideCompletions(document, new vscodeMock.Position(2, 9))
            .map(item => String(item.label));
        assert.ok(qualified.includes("math.add"));
        assert.ok(!qualified.includes("math.hidden"));

        const signature = extension.provideSignatureHelp(document, new vscodeMock.Position(2, 14));
        assert.equal(signature.signatures[0].parameters.length, 2);

        const hover = extension.provideHover(document, new vscodeMock.Position(2, 10));
        assert.match(hover.contents.value, /pub fn add/);

        const definition = extension.provideDefinition(document, new vscodeMock.Position(2, 10));
        assert.equal(definition.uri.fsPath, modulePath);
        assert.equal(definition.position.line, 0);
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("ambiguous public imports are offered only through module qualification", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus ambiguous editor "));
    try {
        await fs.promises.writeFile(path.join(directory, "left.zh"), "pub fn shared():\nend\n");
        await fs.promises.writeFile(path.join(directory, "right.zh"), "pub fn shared():\nend\n");
        const document = documentFrom(
            "use left.zh as left\nuse right.zh as right\nstart:\n    left.shared()\nstop",
            path.join(directory, "main.zp")
        );
        const unqualified = extension.provideCompletions(document).map(item => String(item.label));
        assert.ok(!unqualified.includes("shared"));
        const qualified = extension.provideCompletions(document, new vscodeMock.Position(3, 10))
            .map(item => String(item.label));
        assert.ok(qualified.includes("left.shared"));
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("imported module symbols refresh after invalidation and support two aliases", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus module cache "));
    try {
        const modulePath = path.join(directory, "shared.zh");
        await fs.promises.writeFile(modulePath, "pub fn older():\nend\n");
        const document = documentFrom(
            'use "shared.zh" as first\nuse "shared.zh" as second\n',
            path.join(directory, "main.zp")
        );
        vscodeMock.workspace.textDocuments = [];
        const first = extension.parseVisibleSymbols(document);
        assert.ok(first.some(symbol => symbol.moduleAlias === "first" && symbol.name === "older"));
        assert.ok(first.some(symbol => symbol.moduleAlias === "second" && symbol.name === "older"));

        await fs.promises.writeFile(modulePath, "pub fn newer():\nend\n");
        const cached = extension.parseVisibleSymbols(document);
        assert.ok(cached.some(symbol => symbol.name === "older"), "unchanged project results are cached");
        extension.invalidateLanguageAnalysis(vscodeMock.Uri.file(modulePath));
        const refreshed = extension.parseVisibleSymbols(document);
        assert.ok(refreshed.some(symbol => symbol.name === "newer"));
        assert.ok(!refreshed.some(symbol => symbol.name === "older"));
    } finally {
        vscodeMock.workspace.textDocuments = [];
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("workspace symbols, references and rename include importing project files", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus workspace index "));
    try {
        const modulePath = path.join(directory, "math.zh");
        const entryPath = path.join(directory, "main.zp");
        await fs.promises.writeFile(modulePath, "pub fn total(value: number):\n    return value\nend\n");
        const entryText = 'use "math.zh" as math\nstart:\n    math.total(2)\nstop\n';
        await fs.promises.writeFile(entryPath, entryText);
        const module = documentFrom(await fs.promises.readFile(modulePath, "utf8"), modulePath);
        const entry = documentFrom(entryText, entryPath);
        vscodeMock.workspace.workspaceFolders = [{ uri: vscodeMock.Uri.file(directory) }];
        vscodeMock.workspace.textDocuments = [module, entry];
        vscodeMock.workspace.findFiles = async () => [vscodeMock.Uri.file(modulePath), vscodeMock.Uri.file(entryPath)];

        const symbols = await extension.provideWorkspaceSymbols("tot");
        assert.ok(symbols.some(symbol => symbol.name === "total"));
        const references = await extension.provideReferences(
            module, new vscodeMock.Position(0, 9), { includeDeclaration: true });
        assert.equal(references.length, 2);
        assert.ok(references.some(reference => reference.uri.fsPath === entryPath));
        const edits = await extension.provideRenameEdits(module, new vscodeMock.Position(0, 9), "sumTotal");
        assert.equal(edits.entries.length, 2);
        assert.ok(edits.entries.some(edit => edit.uri.fsPath === entryPath && edit.newText === "sumTotal"));
        await assert.rejects(extension.provideRenameEdits(module, new vscodeMock.Position(0, 9), "not-valid"));
    } finally {
        vscodeMock.workspace.workspaceFolders = [];
        vscodeMock.workspace.textDocuments = [];
        vscodeMock.workspace.findFiles = async () => [];
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("references and rename keep same-named public declarations in separate modules", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus duplicate symbols "));
    try {
        const leftPath = path.join(directory, "left.zh");
        const rightPath = path.join(directory, "right.zh");
        const entryPath = path.join(directory, "main.zp");
        await fs.promises.writeFile(leftPath, "pub fn same():\nend\n");
        await fs.promises.writeFile(rightPath, "pub fn same():\nend\n");
        const entryText = 'use "left.zh" as left\nuse "right.zh" as right\nstart:\n    left.same()\n    right.same()\nstop\n';
        await fs.promises.writeFile(entryPath, entryText);
        const left = documentFrom(await fs.promises.readFile(leftPath, "utf8"), leftPath);
        const right = documentFrom(await fs.promises.readFile(rightPath, "utf8"), rightPath);
        const entry = documentFrom(entryText, entryPath);
        vscodeMock.workspace.workspaceFolders = [{ uri: vscodeMock.Uri.file(directory) }];
        vscodeMock.workspace.textDocuments = [left, right, entry];
        vscodeMock.workspace.findFiles = async () => [
            vscodeMock.Uri.file(leftPath), vscodeMock.Uri.file(rightPath), vscodeMock.Uri.file(entryPath)
        ];

        const references = await extension.provideReferences(left, new vscodeMock.Position(0, 9), {
            includeDeclaration: true
        });
        assert.equal(references.length, 2);
        assert.ok(references.some(reference => reference.uri.fsPath === leftPath));
        assert.ok(references.some(reference => reference.uri.fsPath === entryPath && reference.range.start.line === 3));

        const edits = await extension.provideRenameEdits(left, new vscodeMock.Position(0, 9), "leftOnly");
        assert.equal(edits.entries.length, 2);
        assert.ok(edits.entries.every(edit => edit.uri.fsPath !== rightPath));
        assert.ok(edits.entries.some(edit => edit.uri.fsPath === entryPath &&
            entryText.slice(entry.offsetAt(edit.range.start), entry.offsetAt(edit.range.end)) === "same"));
    } finally {
        vscodeMock.workspace.workspaceFolders = [];
        vscodeMock.workspace.textDocuments = [];
        vscodeMock.workspace.findFiles = async () => [];
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("workspace index bounds file reads, reuses unchanged content, and drops deleted files", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus index concurrency "));
    const paths = Array.from({ length: 40 }, (_, index) => path.join(directory, `module-${index}.zh`));
    let activeReads = 0;
    let maximumActiveReads = 0;
    let readCount = 0;
    const index = new WorkspaceIndex({
        maxConcurrency: 4,
        async readFile(uri) {
            activeReads++;
            maximumActiveReads = Math.max(maximumActiveReads, activeReads);
            readCount++;
            await new Promise(resolve => setTimeout(resolve, 5));
            try { return await fs.promises.readFile(uri.fsPath, "utf8"); }
            finally { activeReads--; }
        }
    });
    try {
        await Promise.all(paths.map(file => fs.promises.writeFile(file, "pub fn value(): number\n")));
        const folder = { uri: vscodeMock.Uri.file(directory) };
        vscodeMock.workspace.workspaceFolders = [folder];
        vscodeMock.workspace.findFiles = async () => paths.map(file => vscodeMock.Uri.file(file));
        const current = documentFrom("start { } stop", path.join(directory, "main.zp"));
        const first = await index.documents(vscodeMock, current);
        assert.equal(first.length, paths.length + 1);
        assert.ok(maximumActiveReads > 1);
        assert.ok(maximumActiveReads <= 4);

        await index.documents(vscodeMock, current);
        assert.equal(readCount, paths.length, "unchanged files should be served from the metadata cache");

        await new Promise(resolve => setTimeout(resolve, 5));
        await fs.promises.writeFile(paths[0], "pub fn names(): number\n");
        const updated = await index.documents(vscodeMock, current);
        assert.ok(updated.some(document => document.getText().includes("fn names")));
        assert.equal(readCount, paths.length + 1);

        await fs.promises.rm(paths.at(-1));
        vscodeMock.workspace.findFiles = async () => paths.slice(0, -1).map(file => vscodeMock.Uri.file(file));
        const afterDelete = await index.documents(vscodeMock, current);
        assert.equal(afterDelete.length, paths.length);
        assert.equal(index.cache.size, paths.length - 1);
    } finally {
        vscodeMock.workspace.workspaceFolders = [];
        vscodeMock.workspace.findFiles = async () => [];
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("workspace index handles a thousand source files with a bounded cache", async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus large workspace "));
    const paths = Array.from({ length: 1000 }, (_, index) =>
        path.join(directory, `module-${index}.zh`));
    const index = new WorkspaceIndex({ maxConcurrency: 12 });
    try {
        await Promise.all(paths.map((file, number) =>
            fs.promises.writeFile(file, `pub fn symbol_${number}():\nend\n`)));
        vscodeMock.workspace.workspaceFolders = [{ uri: vscodeMock.Uri.file(directory) }];
        vscodeMock.workspace.textDocuments = [];
        vscodeMock.workspace.findFiles = async () => paths.map(file => vscodeMock.Uri.file(file));
        const current = documentFrom("start { } stop", path.join(directory, "main.zp"));
        const documents = await index.documents(vscodeMock, current);
        assert.equal(documents.length, 1001);
        assert.equal(index.cache.size, 1000);
        assert.ok(documents.some(item => item.getText().includes("symbol_999")));
    } finally {
        vscodeMock.workspace.workspaceFolders = [];
        vscodeMock.workspace.textDocuments = [];
        vscodeMock.workspace.findFiles = async () => [];
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("diagnostics are positioned and unknown source positions are not fabricated", () => {
    const document = documentFrom("start:\n    missing\nstop");
    const diagnostics = extension.toVscodeDiagnostics(
        '{"severity":"error","code":"E2001","message":"undefined variable","line":2,"column":5}\n' +
        '{"severity":"error","message":"backend issue","line":0,"column":0}',
        document
    );
    assert.equal(diagnostics.length, 1);
    assert.equal(diagnostics[0].range.start.line, 1);
    assert.equal(diagnostics[0].range.start.character, 4);
    assert.equal(diagnostics[0].code, "E2001");
    assert.equal(parseDiagnostics('{"severity":"error","message":"x","line":2,"column":3}\n' +
        '{"severity":"error","message":"x","line":2,"column":3}').length, 1);
});

test("imported module diagnostics target the reported .zh source file", () => {
    extension.activate({ subscriptions: [] });
    const root = documentFrom("", path.join(os.tmpdir(), "module root.zp"));
    const modulePath = path.join(os.tmpdir(), "module source.zh");
    const compiledTemporaryRoot = path.join(os.tmpdir(), ".module root.check.zp");
    extension.publishDiagnostics(
        [
            { severity: "error", message: "entry error", line: 1, column: 1 },
            { severity: "error", message: "module error", file: modulePath, line: 4, column: 7 }
        ].map(item => JSON.stringify(item)).join("\n"),
        root,
        compiledTemporaryRoot
    );
    const write = vscodeMock.registrations.diagnosticWrites.at(-1);
    assert.equal(write.uri.fsPath, modulePath);
    assert.equal(write.diagnostics[0].range.start.line, 3);
    assert.equal(write.diagnostics[0].range.start.character, 6);
    const before = vscodeMock.registrations.diagnosticWrites.length;
    vscodeMock.registrations.fireLast("fileChange", vscodeMock.Uri.file(modulePath));
    const cleared = vscodeMock.registrations.diagnosticWrites.slice(before);
    assert.ok(cleared.some(item => item.uri.toString() === root.uri.toString() &&
        item.diagnostics.length === 0));
    assert.ok(cleared.some(item => item.uri.fsPath === modulePath && item.diagnostics.length === 0));
    extension.deactivate();
});

test("compiler path resolution preserves explicit paths and finds PATH executables", () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "zplus compiler path "));
    try {
        const executable = path.join(directory, "zp.exe");
        fs.writeFileSync(executable, "");
        assert.equal(resolveCompilerPath(executable), executable);
        assert.equal(resolveCompilerPath("", process.env, process.platform, [executable]), executable);
        assert.equal(
            resolveCompilerPath("zp", { PATH: directory, PATHEXT: ".EXE" }, "win32").toLowerCase(),
            executable.toLowerCase()
        );
    } finally {
        fs.rmSync(directory, { recursive: true, force: true });
    }
});

test("missing compiler is reported as a launch error", async () => {
    const missing = path.join(os.tmpdir(), "zplus missing compiler", "zp.exe");
    const result = await executeCompiler(missing, ["version"]);
    assert.equal(result.code, 1);
    assert.equal(result.error.code, "ENOENT");
});

test("compiler process cancellation terminates a superseded check", async () => {
    const controller = new AbortController();
    const pending = executeCompiler(process.execPath, ["-e", "setInterval(() => {}, 1000)"], {
        signal: controller.signal,
        timeout: 10000
    });
    setTimeout(() => controller.abort(), 50);
    const result = await pending;
    assert.equal(result.code, 1);
    assert.equal(result.error.code, "ABORT_ERR");
});

test("compiler reports its version", { skip: !hasCompiler }, async () => {
    const result = await executeCompiler(compiler, ["version"]);
    assert.equal(result.code, 0);
    assert.match(result.stdout, /Z\+/i);
});

test("real compiler checks and runs source files in paths containing spaces", {
    skip: !hasCompiler
}, async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus integration spaces "));
    const source = path.join(directory, "a file.zp");
    try {
        await fs.promises.writeFile(source,
            'start:\n    let total: number = 2 + 3\n    say("hello")\n    say(total)\nstop\n');
        const check = await executeCompiler(compiler, ["check", source, "--diagnostics=json"]);
        assert.equal(check.code, 0, check.stderr);
        const run = await executeCompiler(compiler, ["run", source, "--diagnostics=json"]);
        assert.equal(run.code, 0, run.stderr);
        assert.match(run.stdout, /hello/);
        assert.match(run.stdout, /5/);
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("live checks analyze unsaved text without creating project snapshots", {
    skip: !hasCompiler
}, async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus live check "));
    const sourcePath = path.join(directory, "main.zp");
    const document = documentFrom(
        'use "common.zh"\nstart:\n    say(importedValue)\n    say(missing)\nstop\n',
        sourcePath
    );
    try {
        await fs.promises.writeFile(sourcePath, "start:\nstop\n");
        await fs.promises.writeFile(
            path.join(directory, "common.zh"),
            "start {\npub let importedValue: number = 1\n} stop\n"
        );
        vscodeMock.configuration.compilerPath = compiler;
        vscodeMock.window.activeTextEditor = { document };
        extension.activate({ subscriptions: [] });

        const checkCommand = vscodeMock.registrations.commands
            .filter(item => item.name === "zplus.checkFile")
            .at(-1);
        await checkCommand.callback();

        const diagnostics = vscodeMock.registrations.diagnosticWrites
            .filter(write => write.uri.fsPath === sourcePath)
            .at(-1).diagnostics;
        assert.ok(diagnostics.some(item => /missing|undefined/i.test(item.message)));

        const modulePath = path.join(directory, "common.zh");
        vscodeMock.window.activeTextEditor = {
            document: documentFrom("start {\n    say(moduleMissing)\n} stop\n", modulePath)
        };
        await checkCommand.callback();
        const moduleDiagnostics = vscodeMock.registrations.diagnosticWrites
            .filter(write => write.uri.fsPath === modulePath)
            .at(-1).diagnostics;
        assert.ok(moduleDiagnostics.some(item => /moduleMissing|undefined/i.test(item.message)));

        assert.deepEqual((await fs.promises.readdir(directory)).sort(), ["common.zh", "main.zp"]);
    } finally {
        extension.deactivate();
        vscodeMock.window.activeTextEditor = undefined;
        delete vscodeMock.configuration.compilerPath;
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("real compiler emits positioned syntax and semantic diagnostics", {
    skip: !hasCompiler
}, async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus diagnostics "));
    try {
        const syntaxFile = path.join(directory, "syntax.zp");
        await fs.promises.writeFile(syntaxFile, "start:\n    @\nstop\n");
        const syntax = await executeCompiler(compiler, ["check", syntaxFile, "--diagnostics=json"]);
        assert.notEqual(syntax.code, 0);
        assert.ok(parseDiagnostics(syntax.stderr).some(item => item.line === 2 && item.column > 0));

        const semanticFile = path.join(directory, "semantic.zp");
        await fs.promises.writeFile(semanticFile, "start:\n    say(missing)\nstop\n");
        const semantic = await executeCompiler(compiler, ["check", semanticFile, "--diagnostics=json"]);
        assert.notEqual(semantic.code, 0);
        assert.ok(parseDiagnostics(semantic.stderr).some(item => /undefined/.test(item.message)));
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("Z+ diagnostic stress fixtures use compiler-accepted syntax and recover across many errors", {
    skip: !hasCompiler
}, async () => {
    const validSource = path.join(extensionRoot, "test", "fixtures", "stress-valid.zp");
    const invalidSource = path.join(extensionRoot, "test", "fixtures", "stress-invalid.zp");
    const valid = await executeCompiler(compiler, ["check", validSource, "--diagnostics=json"]);
    assert.equal(valid.code, 0, valid.stderr);

    const started = process.hrtime.bigint();
    const invalid = await executeCompiler(compiler, ["check", invalidSource, "--diagnostics=json"]);
    const elapsedMs = Number(process.hrtime.bigint() - started) / 1e6;
    const diagnostics = parseDiagnostics(invalid.stderr);
    assert.notEqual(invalid.code, 0);
    assert.ok(diagnostics.length >= 30, `expected multiple independent errors, got ${diagnostics.length}`);
    assert.ok(diagnostics.every(item => item.line >= 2 && item.line <= 37 && item.column > 0));
    assert.ok(elapsedMs < 30000, `compiler diagnostic stress check took ${elapsedMs.toFixed(0)}ms`);
});

test("real compiler reports diagnostics from imported .zh files", {
    skip: !hasCompiler
}, async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus module diagnostics "));
    try {
        const moduleFile = path.join(directory, "bad module.zh");
        const entryFile = path.join(directory, "main.zp");
        await fs.promises.writeFile(moduleFile,
            "start {\nfn broken() -> number {\n    return notDeclared\n} end fn\n} stop\n");
        await fs.promises.writeFile(entryFile,
            'use "bad module.zh"\nstart:\nstop\n');

        const result = await executeCompiler(compiler, ["check", entryFile, "--diagnostics=json"]);
        assert.notEqual(result.code, 0);
        const diagnostic = parseDiagnostics(result.stderr).find(item =>
            item.file && path.resolve(item.file) === path.resolve(moduleFile));
        assert.ok(diagnostic, result.stderr);
        assert.equal(diagnostic.line, 3);
        assert.ok(diagnostic.column > 0);
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});

test("real compiler executable and source/output paths containing spaces work", {
    skip: !hasCompiler
}, async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus tool path spaces "));
    const compilerDirectory = path.join(directory, "compiler path");
    const executable = path.join(compilerDirectory, "zp.exe");
    const source = path.join(directory, "native source.zp");
    const output = path.join(directory, "native output.exe");
    try {
        await fs.promises.mkdir(compilerDirectory);
        await fs.promises.copyFile(compiler, executable);
        if (process.platform !== "win32") {
            const compilerMode = (await fs.promises.stat(compiler)).mode;
            await fs.promises.chmod(executable, compilerMode);
        }
        await fs.promises.writeFile(source,
            'start {\nfn main(): number {\n    say("native output")\n    return 0\n} end fn\n} stop\n');
        const built = await executeCompiler(executable, ["build", source, "-o", output, "--diagnostics=json"]);
        assert.equal(built.code, 0, built.stderr);
        assert.ok((await fs.promises.stat(output)).size > 0);
        if (process.platform === "win32" || process.platform === "linux") {
            const run = await executeCompiler(output, []);
            assert.equal(run.code, 0, run.stderr);
            assert.match(run.stdout, /native output/);
        }

        const unsupported = path.join(directory, "unsupported.zp");
        await fs.promises.writeFile(unsupported,
            "start:\n    let values: number[] = [1, 2]\n    say(values[0])\nstop\n");
        const failedBuild = await executeCompiler(executable, ["build", unsupported, "-o", path.join(directory, "bad.exe")]);
        assert.notEqual(failedBuild.code, 0);
        assert.match(failedBuild.stderr, /native compilation failed/i);
    } finally {
        await fs.promises.rm(directory, { recursive: true, force: true });
    }
});
