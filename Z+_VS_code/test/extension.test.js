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
    class MarkdownString {
        constructor(value) { this.value = value; }
    }
    const registrations = { commands: [], providers: [], diagnosticWrites: [] };
    const configuration = {};
    const event = () => ({ dispose() {} });
    const vscode = {
        Position,
        Range,
        CompletionItem,
        DocumentSymbol,
        Diagnostic,
        SignatureInformation,
        ParameterInformation,
        SignatureHelp,
        MarkdownString,
        SnippetString: class SnippetString { constructor(value) { this.value = value; } },
        Hover: class Hover { constructor(contents, range) { Object.assign(this, { contents, range }); } },
        Location: class Location { constructor(uri, position) { Object.assign(this, { uri, position }); } },
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
            registerDocumentSymbolProvider: (_, provider) => {
                registrations.providers.push({ type: "symbols", provider });
                return event();
            },
            registerSignatureHelpProvider: (_, provider) => {
                registrations.providers.push({ type: "signature", provider });
                return event();
            }
        },
        workspace: {
            getConfiguration: () => ({
                get: (name, fallback) => Object.hasOwn(configuration, name) ? configuration[name] : fallback,
                update: async (name, value) => { configuration[name] = value; }
            }),
            onDidChangeTextDocument: event,
            onDidSaveTextDocument: event,
            onDidCloseTextDocument: event,
            getWorkspaceFolder: () => undefined
        },
        window: {
            activeTextEditor: undefined,
            createOutputChannel: () => ({ appendLine() {}, show() {}, dispose() {} }),
            createStatusBarItem: () => ({ show() {}, hide() {}, dispose() {} }),
            onDidChangeActiveTextEditor: event,
            withProgress: async (_, action) => action(),
            showWarningMessage: async () => {},
            showErrorMessage: async () => {},
            showInformationMessage: async () => {},
            showOpenDialog: async () => undefined
        },
        registrations,
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
    assert.equal(manifest.version, "1.1.4");
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
        "zplus.zbuildBuildProject", "zplus.zbuildRunProject", "zplus.zbuildTestProject"
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
            "zplus.zbuildBuildProject", "zplus.zbuildRunProject", "zplus.zbuildTestProject"
        ]
    );
    assert.deepEqual(
        vscodeMock.registrations.providers.map(item => item.type),
        ["completion", "hover", "definition", "symbols", "signature"]
    );
    extension.deactivate();
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

test("bare zgraphics import provides qualified API completions and navigation", () => {
    const source = "use zgraphics\nstart:\n    zgraphics.createWindow(\"demo\", 400, 300)\nstop";
    const document = documentFrom(source, path.join(os.tmpdir(), "zgraphics-main.zp"));
    const completions = extension.provideCompletions(document, new vscodeMock.Position(2, 14))
        .map(item => String(item.label));
    assert.ok(completions.includes("zgraphics.createWindow"));
    assert.ok(completions.includes("zgraphics.waitEvent"));

    const signature = extension.provideSignatureHelp(document, new vscodeMock.Position(2, 37));
    assert.equal(signature.signatures[0].parameters.length, 3);

    const hover = extension.provideHover(document, new vscodeMock.Position(2, 19));
    assert.match(hover.contents.value, /createWindow/);
    const definition = extension.provideDefinition(document, new vscodeMock.Position(2, 19));
    assert.equal(
        definition.uri.fsPath,
        path.join(projectRoot, "stdlib", "zgraphics", "zgraphics.zh")
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
    const root = documentFrom("", path.join(os.tmpdir(), "module root.zp"));
    const modulePath = path.join(os.tmpdir(), "module source.zh");
    const compiledTemporaryRoot = path.join(os.tmpdir(), ".module root.check.zp");
    extension.publishDiagnostics(
        JSON.stringify({
            severity: "error", message: "module error", file: modulePath, line: 4, column: 7
        }),
        root,
        compiledTemporaryRoot
    );
    const write = vscodeMock.registrations.diagnosticWrites.at(-1);
    assert.equal(write.uri.fsPath, modulePath);
    assert.equal(write.diagnostics[0].range.start.line, 3);
    assert.equal(write.diagnostics[0].range.start.character, 6);
});

test("compiler path resolution preserves explicit paths and finds PATH executables", () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "zplus compiler path "));
    try {
        const executable = path.join(directory, "zp.exe");
        fs.writeFileSync(executable, "");
        assert.equal(resolveCompilerPath(executable), executable);
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

test("real compiler reports diagnostics from imported .zh files", {
    skip: !hasCompiler
}, async () => {
    const directory = await fs.promises.mkdtemp(path.join(os.tmpdir(), "zplus module diagnostics "));
    try {
        const moduleFile = path.join(directory, "bad module.zh");
        const entryFile = path.join(directory, "main.zp");
        await fs.promises.writeFile(moduleFile,
            "fn broken() -> number:\n    return notDeclared\nend\n");
        await fs.promises.writeFile(entryFile,
            'use "bad module.zh"\nstart:\nstop\n');

        const result = await executeCompiler(compiler, ["check", entryFile, "--diagnostics=json"]);
        assert.notEqual(result.code, 0);
        const diagnostic = parseDiagnostics(result.stderr).find(item =>
            item.file && path.resolve(item.file) === path.resolve(moduleFile));
        assert.ok(diagnostic, result.stderr);
        assert.equal(diagnostic.line, 2);
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
            'fn main() -> number:\n    say("native output")\n    return 0\nend\n');
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
