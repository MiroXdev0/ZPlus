"use strict";

function parseDiagnostics(output) {
    const diagnostics = [];

    for (const line of String(output || "").split(/\r?\n/)) {
        if (!line.trim()) {
            continue;
        }

        try {
            const value = JSON.parse(line);
            if (
                value &&
                typeof value.message === "string" &&
                Number.isInteger(value.line) &&
                Number.isInteger(value.column)
            ) {
                diagnostics.push({
                    severity: value.severity || "error",
                    code: typeof value.code === "string" ? value.code : "",
                    message: value.message,
                    file: typeof value.file === "string" ? value.file : "",
                    line: value.line,
                    column: value.column
                });
            }
            continue;
        } catch {
            // Older compiler builds may still emit human-readable locations.
        }

        const match = line.match(/^(error|warning|info):\s*(.*?)\s+in\s+'([^']+)'\s+at\s+(\d+):(\d+)\s*$/) ||
            line.match(/^(error|warning|info):\s*(.*?)\s+at\s+(\d+):(\d+)\s*$/);
        if (match) {
            diagnostics.push({
                severity: match[1],
                code: "",
                message: match[2],
                file: match.length === 6 ? match[3] : "",
                line: Number(match.length === 6 ? match[4] : match[3]),
                column: Number(match.length === 6 ? match[5] : match[4])
            });
        }
    }

    const unique = new Map();
    for (const diagnostic of diagnostics) {
        const key = `${diagnostic.file}\0${diagnostic.severity}\0${diagnostic.code}\0${diagnostic.line}\0${diagnostic.column}\0${diagnostic.message}`;
        if (!unique.has(key)) {
            unique.set(key, diagnostic);
        }
    }
    return [...unique.values()];
}

module.exports = { parseDiagnostics };
