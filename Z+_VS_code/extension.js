"use strict";

const vscode = require("vscode");
const fs = require("node:fs");
const path = require("node:path");
const { executeCompiler, resolveCompilerPath } = require("./lib/compiler");
const { parseDiagnostics } = require("./lib/diagnostics");

const keywords = [
    "start", "stop", "fn", "return", "let", "const", "if", "else", "end",
    "while", "for", "repeat", "break", "continue", "switch", "case", "default",
    "struct", "class", "enum", "namespace", "import", "use", "export", "pub", "as", "is",
    "and", "or", "not", "public", "private", "protected", "this"
];
const types = ["number", "string", "char", "character", "boolean", "bool", "void"];
const constants = ["true", "false", "null", "nullptr"];
const builtins = {
    say: { signature: "say(value)", description: "Writes a value followed by a newline." },
    printz: {
        signature: "printz(value, ...values)",
        description: "Writes one or more values without adding a newline."
    },
    printm: {
        signature: "printm(value, ...values)",
        description: "Deprecated compatibility alias for printz(); writes one or more values without adding a newline."
    }
};

const timers = new Map();
const runningChecks = new Map();
const diagnosticsByRoot = new Map();
let diagnosticCollection;
let outputChannel;
let statusItem;

function activate(context) {
    outputChannel = vscode.window.createOutputChannel("Z+");
    diagnosticCollection = vscode.languages.createDiagnosticCollection("zplus");
    context.subscriptions.push(outputChannel, diagnosticCollection);

    const commands = [
        ["zplus.checkFile", () => withActiveDocument(checkFile)],
        ["zplus.runFile", () => withActiveDocument(runFile)],
        ["zplus.buildFile", () => withActiveDocument(buildFile)],
        ["zplus.selectCompiler", selectCompiler],
        ["zplus.showCompilerVersion", showCompilerVersion],
        ["zplus.zbuildBuildProject", () => runZBuildProject("build")],
        ["zplus.zbuildRunProject", () => runZBuildProject("run")],
        ["zplus.zbuildTestProject", () => runZBuildProject("test")]
    ];
    for (const [command, callback] of commands) {
        context.subscriptions.push(vscode.commands.registerCommand(command, callback));
    }

    context.subscriptions.push(
        vscode.workspace.onDidChangeTextDocument(({ document }) => {
            if (document.languageId === "zplus" &&
                vscode.workspace.getConfiguration("zplus").get("checkWhileTyping", true)) {
                scheduleCheck(document);
            }
        }),
        vscode.workspace.onDidSaveTextDocument(document => {
            if (document.languageId === "zplus" &&
                vscode.workspace.getConfiguration("zplus").get("checkOnSave", true)) {
                scheduleCheck(document, 0);
            }
        }),
        vscode.workspace.onDidCloseTextDocument(document => {
            const key = document.uri.toString();
            clearDiagnosticsForRoot(document.uri);
            clearTimeout(timers.get(key));
            timers.delete(key);
            runningChecks.delete(key);
        })
    );

    const language = { language: "zplus" };
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(language, {
            provideCompletionItems: (document, position) => provideCompletions(document, position)
        }),
        vscode.languages.registerHoverProvider(language, {
            provideHover: (document, position) => provideHover(document, position)
        }),
        vscode.languages.registerDefinitionProvider(language, {
            provideDefinition: (document, position) => provideDefinition(document, position)
        }),
        vscode.languages.registerDocumentSymbolProvider(language, {
            provideDocumentSymbols: document => provideDocumentSymbols(document)
        }),
        vscode.languages.registerSignatureHelpProvider(language, {
            provideSignatureHelp: (document, position) => provideSignatureHelp(document, position)
        }, "(", ",")
    );

    statusItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Right, 100);
    statusItem.text = "$(code) Z+";
    statusItem.tooltip = "Z+ Language Support — click to check this file";
    statusItem.command = "zplus.checkFile";
    context.subscriptions.push(
        statusItem,
        vscode.window.onDidChangeActiveTextEditor(editor => {
            if (editor && editor.document.languageId === "zplus") statusItem.show();
            else statusItem.hide();
        })
    );
    if (vscode.window.activeTextEditor &&
        vscode.window.activeTextEditor.document.languageId === "zplus") {
        statusItem.show();
    } else {
        statusItem.hide();
    }

    outputChannel.appendLine("Z+ language tools activated.");
}

async function withActiveDocument(action) {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== "zplus") {
        vscode.window.showWarningMessage("Z+: Open a Z+ source file first.");
        return;
    }
    return action(editor.document);
}

function getCompilerPath() {
    const configured = vscode.workspace.getConfiguration("zplus").get("compilerPath", "");
    return resolveCompilerPath(configured);
}

function scheduleCheck(document, overrideDelay) {
    const key = document.uri.toString();
    clearTimeout(timers.get(key));
    const delay = overrideDelay === undefined
        ? vscode.workspace.getConfiguration("zplus").get("checkDelay", 500)
        : overrideDelay;
    timers.set(key, setTimeout(() => {
        timers.delete(key);
        void checkFile(document);
    }, delay));
}

function toVscodeDiagnostics(output, document) {
    return parseDiagnostics(output)
        .filter(item => item.line > 0 && item.column > 0 && document.lineCount > 0)
        .map(item => {
        const line = Math.min(document.lineCount - 1, item.line - 1);
        const text = document.lineAt(line).text;
        const column = Math.min(text.length, item.column - 1);
        const range = new vscode.Range(line, column, line, Math.min(text.length, column + 1));
        const severity = item.severity === "warning"
            ? vscode.DiagnosticSeverity.Warning
            : item.severity === "info"
                ? vscode.DiagnosticSeverity.Information
                : vscode.DiagnosticSeverity.Error;
        const diagnostic = new vscode.Diagnostic(range, item.message, severity);
        diagnostic.source = "Z+";
        if (item.code) diagnostic.code = item.code;
        return diagnostic;
        });
}

function normalizedFilePath(file) {
    const normalized = path.resolve(file);
    return process.platform === "win32" ? normalized.toLowerCase() : normalized;
}

function publishDiagnostics(output, document, compiledSourcePath = document.uri.fsPath) {
    const rootKey = document.uri.toString();
    const rootPath = normalizedFilePath(compiledSourcePath);
    const grouped = new Map();
    for (const item of parseDiagnostics(output)) {
        if (item.line <= 0 || item.column <= 0) continue;
        const destination = item.file && normalizedFilePath(item.file) !== rootPath
            ? vscode.Uri.file(item.file)
            : document.uri;
        const key = destination.toString();
        if (!grouped.has(key)) grouped.set(key, { uri: destination, diagnostics: [] });

        const range = new vscode.Range(
            item.line - 1,
            item.column - 1,
            item.line - 1,
            item.column
        );
        const severity = item.severity === "warning"
            ? vscode.DiagnosticSeverity.Warning
            : item.severity === "info"
                ? vscode.DiagnosticSeverity.Information
                : vscode.DiagnosticSeverity.Error;
        const diagnostic = new vscode.Diagnostic(range, item.message, severity);
        diagnostic.source = "Z+";
        if (item.code) diagnostic.code = item.code;
        grouped.get(key).diagnostics.push(diagnostic);
    }

    const previous = diagnosticsByRoot.get(rootKey) || new Map();
    for (const [key, uri] of previous) {
        if (!grouped.has(key)) diagnosticCollection.delete(uri);
    }
    for (const [key, item] of grouped) {
        diagnosticCollection.set(item.uri, item.diagnostics);
        previous.set(key, item.uri);
    }
    diagnosticsByRoot.set(rootKey, previous);
}

function clearDiagnosticsForRoot(rootUri) {
    const rootKey = rootUri.toString();
    const previous = diagnosticsByRoot.get(rootKey);
    if (previous) {
        for (const uri of previous.values()) diagnosticCollection.delete(uri);
        diagnosticsByRoot.delete(rootKey);
    } else {
        diagnosticCollection.delete(rootUri);
    }
}

async function checkFile(document) {
    if (!document || document.languageId !== "zplus") return;
    const key = document.uri.toString();
    const version = document.version;
    runningChecks.set(key, version);

    try {
        const result = await executeCompiler(
            getCompilerPath(),
            ["check", document.uri.fsPath, "--source-stdin", "--diagnostics=json"],
            {
                cwd: path.dirname(document.uri.fsPath),
                input: document.getText()
            }
        );
        if (runningChecks.get(key) !== version || document.version !== version) return;

        const output = `${result.stderr}\n${result.stdout}`;
        publishDiagnostics(result.stderr, document);
        if (result.code !== 0 || output.trim()) {
            outputChannel.appendLine(`---- Z+ Check (${document.uri.fsPath}) ----`);
            outputChannel.appendLine(output.trim() || `Compiler exited with code ${result.code}.`);
        } else {
            outputChannel.appendLine(`Check passed: ${document.uri.fsPath}`);
        }
        if (result.error && result.error.code === "ENOENT") {
            showCompilerMissing(getCompilerPath());
        } else if (result.error && result.error.killed) {
            outputChannel.appendLine("Compiler check timed out.");
        }
    } catch (error) {
        outputChannel.appendLine(`Z+ check failed: ${error.message}`);
        vscode.window.showErrorMessage(`Z+ check failed: ${error.message}`);
    } finally {
        if (runningChecks.get(key) === version) runningChecks.delete(key);
    }
}

async function runFile(document) {
    if (path.extname(document.uri.fsPath).toLowerCase() !== ".zp") {
        vscode.window.showWarningMessage(
            "Z+: Run requires a .zp entry file. Open the program that uses this .zh module."
        );
        return;
    }
    const saved = await document.save();
    if (!saved) return;
    await executeUserCommand(document, "run", []);
}

async function buildFile(document) {
    if (path.extname(document.uri.fsPath).toLowerCase() !== ".zp") {
        vscode.window.showWarningMessage(
            "Z+: Build requires a .zp entry file. Open the program that uses this .zh module."
        );
        return;
    }
    const saved = await document.save();
    if (!saved) return;

    const outputPath = configuredOutputPath(document);
    const args = ["build", document.uri.fsPath];
    if (outputPath) args.push("-o", outputPath);
    await executeUserCommand(document, "build", args, outputPath);
}

function configuredOutputPath(document) {
    const configured = vscode.workspace.getConfiguration("zplus").get("outputPath", "").trim();
    if (!configured) return "";
    const workspaceFolder = vscode.workspace.getWorkspaceFolder(document.uri);
    const values = {
        file: document.uri.fsPath,
        fileDirname: path.dirname(document.uri.fsPath),
        fileBasename: path.basename(document.uri.fsPath),
        fileBasenameNoExtension: path.basename(document.uri.fsPath, path.extname(document.uri.fsPath)),
        workspaceFolder: workspaceFolder ? workspaceFolder.uri.fsPath : path.dirname(document.uri.fsPath)
    };
    const expanded = configured.replace(/\$\{(file|fileDirname|fileBasename|fileBasenameNoExtension|workspaceFolder)\}/g,
        (_, name) => values[name]);
    return path.isAbsolute(expanded) ? expanded : path.resolve(path.dirname(document.uri.fsPath), expanded);
}

async function executeUserCommand(document, command, args, expectedOutput = "") {
    const compiler = getCompilerPath();
    args.push("--diagnostics=json");
    outputChannel.show(true);
    outputChannel.appendLine(`---- Z+ ${command === "run" ? "Interpreter" : "Native Build"} ----`);
    outputChannel.appendLine(`Compiler: ${compiler}`);
    outputChannel.appendLine(`File: ${document.uri.fsPath}`);
    if (expectedOutput) outputChannel.appendLine(`Output: ${expectedOutput}`);

    const progressTitle = command === "run" ? "Running Z+ with the interpreter" : "Building native Z+ executable";
    await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Notification, title: progressTitle, cancellable: false },
        async () => {
            const result = await executeCompiler(compiler, args, { cwd: path.dirname(document.uri.fsPath) });
            const output = `${result.stdout}${result.stderr ? `\n${result.stderr}` : ""}`;
            if (output.trim()) outputChannel.appendLine(output.trimEnd());
            publishDiagnostics(`${result.stderr}\n${result.stdout}`, document);

            if (result.error && result.error.code === "ENOENT") {
                showCompilerMissing(compiler);
            } else if (result.code !== 0) {
                const message = parseDiagnostics(`${result.stderr}\n${result.stdout}`)
                    .map(item => item.message).join("; ") ||
                    `Z+ ${command} failed with exit code ${result.code}. See the Z+ output channel.`;
                vscode.window.showErrorMessage(message);
            } else if (command === "build") {
                vscode.window.showInformationMessage(
                    `Z+ native executable built${expectedOutput ? `: ${expectedOutput}` : ""}.`
                );
            } else {
                outputChannel.appendLine("Z+ interpreter run finished.");
            }
        }
    );
}

async function selectCompiler() {
    const selection = await vscode.window.showOpenDialog({
        canSelectFiles: true,
        canSelectFolders: false,
        canSelectMany: false,
        openLabel: "Select Z+ compiler"
    });
    if (!selection || !selection.length) return;
    await vscode.workspace.getConfiguration("zplus").update(
        "compilerPath",
        selection[0].fsPath,
        vscode.ConfigurationTarget.Global
    );
    outputChannel.appendLine(`Compiler set to ${selection[0].fsPath}`);
}

async function showCompilerVersion() {
    const compiler = getCompilerPath();
    const result = await executeCompiler(compiler, ["version"]);
    const output = `${result.stdout}${result.stderr}`.trim();
    outputChannel.appendLine(output || `Compiler exited with code ${result.code}.`);
    if (result.error && result.error.code === "ENOENT") showCompilerMissing(compiler);
    else if (result.code !== 0) vscode.window.showErrorMessage(output || "Could not read Z+ compiler version.");
    else vscode.window.showInformationMessage(output);
}

async function findZBuildProjectRoot() {
    const starts = [];
    const editor = vscode.window.activeTextEditor;
    if (editor && editor.document.uri.fsPath) {
        starts.push(path.dirname(editor.document.uri.fsPath));
    }
    for (const folder of vscode.workspace.workspaceFolders || []) {
        starts.push(folder.uri.fsPath);
    }
    for (const start of starts) {
        let current = path.resolve(start);
        while (true) {
            try {
                const stat = await fs.promises.stat(path.join(current, "zproject.toml"));
                if (stat.isFile()) return current;
            } catch (error) {
                if (error.code !== "ENOENT" && error.code !== "ENOTDIR") throw error;
            }
            const parent = path.dirname(current);
            if (parent === current) break;
            current = parent;
        }
    }
    return undefined;
}

async function runZBuildProject(command) {
    const root = await findZBuildProjectRoot();
    if (!root) {
        vscode.window.showWarningMessage("Z+: No zproject.toml found in the active project.");
        return;
    }
    const configured = vscode.workspace.getConfiguration("zplus").get("zbuildPath", "");
    const executable = resolveCompilerPath(
        configured || (process.platform === "win32" ? "zbuild.exe" : "zbuild")
    );
    outputChannel.show(true);
    outputChannel.appendLine(`---- ZBuild ${command} ----`);
    outputChannel.appendLine(`Project: ${root}`);
    const result = await executeCompiler(executable, [command], { cwd: root });
    const output = `${result.stdout}${result.stderr ? `\n${result.stderr}` : ""}`.trim();
    if (output) outputChannel.appendLine(output);
    if (result.error && result.error.code === "ENOENT") {
        const message = `ZBuild not found: ${executable}. Set zplus.zbuildPath or add zbuild to PATH.`;
        outputChannel.appendLine(message);
        vscode.window.showErrorMessage(message);
    } else if (result.code !== 0) {
        vscode.window.showErrorMessage(
            output || `ZBuild ${command} failed with exit code ${result.code}.`);
    } else {
        vscode.window.showInformationMessage(`ZBuild ${command} completed.`);
    }
}

function showCompilerMissing(compiler) {
    const message = `Z+ compiler not found: ${compiler}. Set zplus.compilerPath or add zp to PATH.`;
    outputChannel.appendLine(message);
    vscode.window.showErrorMessage(message);
}

function parseSymbols(document) {
    const symbols = [];
    const lines = document.getText().split(/\r?\n/);
    for (let lineNumber = 0; lineNumber < lines.length; lineNumber++) {
        const raw = lines[lineNumber];
        const code = raw.replace(/\/\/.*$/, "").trim();
        const offset = document.offsetAt(new vscode.Position(lineNumber, 0));
        let match = code.match(/^(?:(pub|export)\s+)?fn\s+([A-Za-z_]\w*)\s*\(([^)]*)\)\s*(?:->\s*([A-Za-z_]\w*(?:\[\]|\*)?))?\s*:/);
        if (match) {
            const params = match[3].split(",").map(value => value.trim()).filter(Boolean);
            symbols.push(makeSymbol(match[2], "function", match[4] || "void", params, lineNumber, offset, raw,
                document.uri, raw.indexOf(match[2]), Boolean(match[1])));
        } else if ((match = code.match(/^(?:(pub|export)\s+)?(let|const)\s+([A-Za-z_]\w*)\s*(?::\s*([A-Za-z_]\w*(?:\[\]|\*)?))?/))) {
            symbols.push(makeSymbol(match[3], match[2] === "const" ? "constant" : "variable",
                match[4] || "", [], lineNumber, offset, raw, document.uri, raw.indexOf(match[3]), Boolean(match[1])));
        } else if ((match = code.match(/^(?:(pub|export)\s+)?(struct|class|enum|namespace)\s+([A-Za-z_]\w*)\s*:/))) {
            symbols.push(makeSymbol(match[3], match[2], "", [], lineNumber, offset, raw,
                document.uri, raw.indexOf(match[3]), Boolean(match[1])));
        }
    }
    return symbols;
}

function parseVisibleSymbols(document) {
    const symbols = [];
    const visited = new Set();
    const visit = (sourcePath, sourceText, sourceUri, moduleAlias, isRoot = false) => {
        const key = normalizedFilePath(sourcePath);
        if (visited.has(key)) return;
        visited.add(key);

        const moduleDocument = {
            uri: sourceUri,
            getText: () => sourceText,
            offsetAt(position) {
                const lines = sourceText.split(/\r?\n/);
                return lines.slice(0, position.line).reduce((sum, line) => sum + line.length + 1, 0) +
                    position.character;
            }
        };
        for (const symbol of parseSymbols(moduleDocument)) {
            symbol.moduleAlias = moduleAlias;
            symbol.isImported = !isRoot;
            symbols.push(symbol);
        }

        for (const line of sourceText.split(/\r?\n/)) {
            const use = line.match(/^\s*use\s+(?:"([^"]+)"|([^\s;]+))(?:\s+as\s+([A-Za-z_]\w*))?/);
            if (!use) continue;
            const relativePath = (use[1] || use[2]).replace(/\\/g, path.sep);
            if (path.isAbsolute(relativePath) || /^[A-Za-z]:/.test(relativePath)) continue;
            const localPath = path.resolve(path.dirname(sourcePath), relativePath);
            const modulePaths = [localPath];
            if (!path.extname(relativePath)) {
                modulePaths.push(`${localPath}.zh`);
                modulePaths.push(path.join(localPath, `${path.basename(localPath)}.zh`));
                modulePaths.push(path.resolve(__dirname, "..", "stdlib", relativePath,
                    `${relativePath}.zh`));
                for (const folder of vscode.workspace.workspaceFolders || []) {
                    modulePaths.push(path.resolve(folder.uri.fsPath, "stdlib", relativePath,
                        `${relativePath}.zh`));
                }
            }
            const modulePath = modulePaths.find(candidate => {
                try {
                    return fs.statSync(candidate).isFile();
                } catch {
                    return false;
                }
            });
            if (!modulePath) continue;
            let moduleText;
            try {
                moduleText = fs.readFileSync(modulePath, "utf8");
            } catch {
                continue;
            }
            const alias = moduleIdentifier(
                use[3] || path.basename(relativePath, path.extname(relativePath))
            );
            visit(modulePath, moduleText, vscode.Uri.file(modulePath), alias);
        }
    };

    visit(document.uri.fsPath, document.getText(), document.uri, undefined, true);
    return symbols;
}

function makeSymbol(name, kind, type, parameters, line, offset, sourceLine, uri, column, isPublic = false) {
    return { name, kind, type, parameters, line, offset, sourceLine, uri, column, isPublic };
}

function moduleIdentifier(name) {
    let identifier = name.replace(/[^A-Za-z0-9_]/g, "_");
    if (!identifier) identifier = "module";
    if (/^[0-9]/.test(identifier)) identifier = `_${identifier}`;
    return identifier;
}

function unqualifiedVisibleSymbols(symbols) {
    const rootNames = new Set(
        symbols.filter(symbol => !symbol.isImported).map(symbol => symbol.name)
    );
    const importedPublicCounts = new Map();
    for (const symbol of symbols) {
        if (symbol.isImported && symbol.isPublic) {
            importedPublicCounts.set(
                symbol.name,
                (importedPublicCounts.get(symbol.name) || 0) + 1
            );
        }
    }
    return symbols.filter(symbol =>
        !symbol.isImported ||
        (symbol.isPublic &&
            importedPublicCounts.get(symbol.name) === 1 &&
            !rootNames.has(symbol.name)));
}

function findSymbol(symbols, name, moduleAlias) {
    const candidates = moduleAlias
        ? symbols.filter(symbol =>
            symbol.isImported && symbol.isPublic && symbol.moduleAlias === moduleAlias)
        : unqualifiedVisibleSymbols(symbols);
    return candidates.find(symbol => symbol.name === name);
}

function qualifiedReferenceAt(document, position) {
    const line = document.lineAt(position.line).text;
    const word = wordAt(document, position);
    if (!word) return undefined;
    const before = line.slice(0, word.range.start.character);
    const module = before.match(/([A-Za-z_]\w*)\s*\.\s*$/);
    return module ? { moduleAlias: module[1], name: word.name, range: word.range } : undefined;
}

function provideCompletions(document, position) {
    const items = [];
    const add = (label, kind, detail, insertText) => {
        const item = new vscode.CompletionItem(label, kind);
        item.detail = detail;
        if (insertText) item.insertText = insertText;
        items.push(item);
    };
    for (const keyword of keywords) add(keyword, vscode.CompletionItemKind.Keyword, "Z+ keyword");
    for (const type of types) add(type, vscode.CompletionItemKind.TypeParameter, "Z+ built-in type");
    for (const constant of constants) add(constant, vscode.CompletionItemKind.Constant, "Z+ literal");
    for (const [name, info] of Object.entries(builtins)) {
        add(name, vscode.CompletionItemKind.Function, info.signature, new vscode.SnippetString(`${name}($1)`));
        if (name === "printm" && vscode.CompletionItemTag) {
            items.at(-1).tags = [vscode.CompletionItemTag.Deprecated];
        }
    }
    const symbols = parseVisibleSymbols(document);
    const linePrefix = position
        ? document.lineAt(position.line).text.slice(0, position.character)
        : "";
    const qualified = linePrefix.match(/([A-Za-z_]\w*)\s*\.\s*([A-Za-z_]\w*)?$/);
    const candidates = qualified
        ? symbols.filter(symbol => symbol.isImported && symbol.isPublic &&
            !["struct", "class", "enum"].includes(symbol.kind) &&
            symbol.moduleAlias === qualified[1] && symbol.name.startsWith(qualified[2] || ""))
        : unqualifiedVisibleSymbols(symbols);
    for (const symbol of candidates) {
        const label = qualified ? `${qualified[1]}.${symbol.name}` : symbol.name;
        const kind = symbol.kind === "function" ? vscode.CompletionItemKind.Function
            : symbol.kind === "constant" ? vscode.CompletionItemKind.Constant
                : ["struct", "class", "enum"].includes(symbol.kind)
                    ? vscode.CompletionItemKind.Class : vscode.CompletionItemKind.Variable;
        add(label, kind, symbol.type ? `${symbol.kind}: ${symbol.type}` : `Z+ ${symbol.kind}`,
            symbol.kind === "function"
                ? new vscode.SnippetString(`${label}($1)`)
                : qualified ? label : undefined);
        if (symbol.kind === "function" && !qualified) {
            for (const parameter of symbol.parameters) {
                const param = parameter.match(/^([A-Za-z_]\w*)/);
                if (param) add(param[1], vscode.CompletionItemKind.Variable, `Parameter of ${symbol.name}`);
            }
        }
    }
    return [...new Map(items.map(item => [String(item.label), item])).values()];
}

function wordAt(document, position) {
    const range = document.getWordRangeAtPosition(position);
    return range ? { name: document.getText(range), range } : undefined;
}

function provideHover(document, position) {
    const word = wordAt(document, position);
    if (!word) return undefined;
    const reference = qualifiedReferenceAt(document, position);
    let content;
    if (builtins[word.name]) {
        content = `\`\`\`zp\n${builtins[word.name].signature}\n\`\`\`\n\n${builtins[word.name].description}`;
    } else if (word.name === "printm") {
        content = `\`\`\`zp\nprintm(value, ...values)\n\`\`\`\n\nDeprecated compatibility alias for \`printz()\`.`;
    } else if (keywords.includes(word.name)) {
        content = `**Z+ keyword** \`${word.name}\``;
    } else if (types.includes(word.name)) {
        content = `**Z+ type** \`${word.name}\``;
    } else if (constants.includes(word.name)) {
        content = `**Z+ literal** \`${word.name}\``;
    } else {
        const symbol = findSymbol(parseVisibleSymbols(document), word.name, reference && reference.moduleAlias);
        if (!symbol) return undefined;
        content = symbol.kind === "function"
            ? `\`\`\`zp\n${symbol.sourceLine.trim()}\n\`\`\``
            : `**Z+ ${symbol.kind}${symbol.type ? `: ${symbol.type}` : ""}**`;
    }
    return new vscode.Hover(new vscode.MarkdownString(content), word.range);
}

function provideDefinition(document, position) {
    const word = wordAt(document, position);
    if (!word) return undefined;
    const reference = qualifiedReferenceAt(document, position);
    const symbol = findSymbol(parseVisibleSymbols(document), word.name, reference && reference.moduleAlias);
    if (!symbol) return undefined;
    const targetUri = symbol.uri || document.uri;
    const targetDocument = targetUri.toString() === document.uri.toString()
        ? document
        : undefined;
    if (targetDocument) {
        return new vscode.Location(targetUri, targetDocument.positionAt(symbol.offset + symbol.column));
    }
    return new vscode.Location(targetUri, new vscode.Position(symbol.line, symbol.column));
}

function provideDocumentSymbols(document) {
    const kinds = {
        function: vscode.SymbolKind.Function,
        variable: vscode.SymbolKind.Variable,
        constant: vscode.SymbolKind.Constant,
        struct: vscode.SymbolKind.Struct,
        class: vscode.SymbolKind.Class,
        enum: vscode.SymbolKind.Enum,
        namespace: vscode.SymbolKind.Namespace
    };
    return parseSymbols(document).map(symbol => {
        const position = document.positionAt(symbol.offset);
        const end = document.lineAt(symbol.line).range.end;
        return new vscode.DocumentSymbol(
            symbol.name,
            symbol.type || symbol.kind,
            kinds[symbol.kind] || vscode.SymbolKind.Variable,
            new vscode.Range(position, end),
            new vscode.Range(position, position.translate(0, symbol.name.length))
        );
    });
}

function currentCall(document, position) {
    const offset = document.offsetAt(position);
    const before = document.getText().slice(0, offset);
    let depth = 0;
    let open = -1;
    for (let index = before.length - 1; index >= 0; index--) {
        if (before[index] === ")") depth++;
        else if (before[index] === "(") {
            if (depth === 0) {
                open = index;
                break;
            }
            depth--;
        }
    }
    if (open < 0) return undefined;
    const nameMatch = before.slice(0, open).match(/((?:[A-Za-z_]\w*\s*\.\s*)?[A-Za-z_]\w*)\s*$/);
    if (!nameMatch) return undefined;
    const argumentsText = before.slice(open + 1);
    let nested = 0;
    let activeParameter = 0;
    for (const character of argumentsText) {
        if (character === "(" || character === "[") nested++;
        else if (character === ")" || character === "]") nested--;
        else if (character === "," && nested === 0) activeParameter++;
    }
    const qualifiedName = nameMatch[1].split(".").map(part => part.trim());
    return {
        name: qualifiedName.at(-1),
        moduleAlias: qualifiedName.length > 1 ? qualifiedName[0] : undefined,
        activeParameter
    };
}

function provideSignatureHelp(document, position) {
    const call = currentCall(document, position);
    if (!call) return undefined;
    let signature;
    if (builtins[call.name]) {
        signature = {
            label: builtins[call.name].signature,
            parameters: call.name === "say" ? ["value"] : ["value", "...values"]
        };
    } else {
        const symbol = findSymbol(parseVisibleSymbols(document), call.name, call.moduleAlias);
        if (!symbol || symbol.kind !== "function") return undefined;
        signature = {
            label: symbol.sourceLine.trim(),
            parameters: symbol.parameters.map(parameter => parameter.split(":")[0].trim())
        };
    }
    const result = new vscode.SignatureHelp();
    const information = new vscode.SignatureInformation(signature.label);
    information.parameters = signature.parameters.map(name => new vscode.ParameterInformation(name));
    result.signatures = [information];
    result.activeSignature = 0;
    result.activeParameter = Math.min(call.activeParameter, Math.max(0, signature.parameters.length - 1));
    return result;
}

function deactivate() {
    for (const timer of timers.values()) clearTimeout(timer);
    timers.clear();
    runningChecks.clear();
}

module.exports = {
    activate,
    deactivate,
    parseSymbols,
    parseVisibleSymbols,
    provideCompletions,
    provideDefinition,
    provideDocumentSymbols,
    provideHover,
    provideSignatureHelp,
    publishDiagnostics,
    toVscodeDiagnostics,
    findZBuildProjectRoot
};
