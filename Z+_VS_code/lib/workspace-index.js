"use strict";

const fs = require("node:fs/promises");

class WorkspaceIndex {
    constructor(options = {}) {
        this.readFile = options.readFile || (uri => fs.readFile(uri.fsPath, "utf8"));
        this.cache = new Map();
        this.maxConcurrency = Math.max(1, Math.min(32, options.maxConcurrency || 16));
    }

    invalidate(uri) {
        this.cache.delete(uri.toString());
    }

    async documents(vscode, currentDocument, token) {
        const folders = vscode.workspace.workspaceFolders || [];
        const open = new Map((vscode.workspace.textDocuments || [])
            .filter(document => document.languageId === "zplus")
            .map(document => [document.uri.toString(), document]));
        open.set(currentDocument.uri.toString(), currentDocument);
        if (folders.length && vscode.workspace.findFiles) {
            for (const folder of folders) {
                if (token && token.isCancellationRequested) return [];
                const uris = await vscode.workspace.findFiles(
                    new vscode.RelativePattern(folder, "**/*.{zp,zh}"),
                    "**/{.git,node_modules,build,dist,z+_packages}/**",
                    10000,
                    token
                );
                for (const uri of uris) {
                    if (!open.has(uri.toString())) open.set(uri.toString(), uri);
                }
            }
        }
        const entries = [...open.entries()];
        const present = new Set(entries.map(([key]) => key));
        const result = new Array(entries.length);
        let next = 0;
        const worker = async () => {
            while (next < entries.length && !(token && token.isCancellationRequested)) {
                const index = next++;
                const [key, source] = entries[index];
                if (source.getText) {
                    result[index] = source;
                    continue;
                }
                try {
                    const stat = await fs.stat(source.fsPath);
                    const cached = this.cache.get(key);
                    const text = cached && cached.mtimeMs === stat.mtimeMs &&
                        cached.ctimeMs === stat.ctimeMs && cached.ino === stat.ino && cached.size === stat.size
                        ? cached.text : await this.readFile(source);
                    this.cache.set(key, {
                        mtimeMs: stat.mtimeMs,
                        ctimeMs: stat.ctimeMs,
                        ino: stat.ino,
                        size: stat.size,
                        text
                    });
                    const lineOffsets = [0];
                    for (let offset = 0; offset < text.length; offset++) {
                        if (text[offset] === "\n") lineOffsets.push(offset + 1);
                    }
                    result[index] = {
                        uri: source,
                        languageId: "zplus",
                        getText: () => text,
                        offsetAt(position) {
                            const start = lineOffsets[Math.min(position.line, lineOffsets.length - 1)];
                            return Math.min(text.length, start + position.character);
                        }
                    };
                } catch (error) {
                    if (error.code !== "ENOENT" && error.code !== "EACCES") throw error;
                }
            }
        };

        await Promise.all(Array.from(
            { length: Math.min(this.maxConcurrency, entries.length) }, worker
        ));
        if (token && token.isCancellationRequested) return [];
        for (const key of this.cache.keys()) {
            if (!present.has(key)) this.cache.delete(key);
        }
        return result.filter(Boolean);
    }
}

module.exports = { WorkspaceIndex };
