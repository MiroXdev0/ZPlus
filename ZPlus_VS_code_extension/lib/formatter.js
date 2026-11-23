"use strict";

const blockHeaders = new Set([
    "start", "fn", "if", "else", "while", "for", "repeat", "switch",
    "case", "default", "namespace", "try", "catch", "finally"
]);

function scanLine(line, state) {
    const tokens = [];
    for (let index = 0; index < line.length;) {
        if (state.blockComment) {
            const end = line.indexOf("*/", index);
            if (end < 0) return tokens;
            state.blockComment = false;
            index = end + 2;
            continue;
        }

        const character = line[index];
        if (/\s/.test(character)) {
            index++;
            continue;
        }
        if (character === "/" && line[index + 1] === "/") break;
        if (character === "/" && line[index + 1] === "*") {
            state.blockComment = true;
            index += 2;
            continue;
        }
        if (character === "\"" || character === "'") {
            const quote = character;
            index++;
            while (index < line.length) {
                if (line[index] === "\\") {
                    index += Math.min(2, line.length - index);
                } else if (line[index++] === quote) {
                    break;
                }
            }
            tokens.push({ type: "literal", value: "" });
            continue;
        }
        if (/[A-Za-z_]/.test(character)) {
            const start = index++;
            while (index < line.length && /[A-Za-z0-9_]/.test(line[index])) index++;
            tokens.push({ type: "word", value: line.slice(start, index) });
            continue;
        }
        tokens.push({ type: "symbol", value: character });
        index++;
    }
    return tokens;
}

function isColonBlock(tokens) {
    if (tokens.length < 2 || tokens.at(-1).value !== ":") return false;
    const firstWord = tokens.find(token => token.type === "word");
    return firstWord !== undefined && blockHeaders.has(firstWord.value);
}

function leadingClosures(tokens) {
    let count = 0;
    for (const token of tokens) {
        if (token.value !== "}") break;
        count++;
    }
    const firstWord = tokens.find(token => token.type === "word");
    const legacyClose = count === 0 && firstWord &&
        ["end", "stop", "else"].includes(firstWord.value) ? 1 : 0;
    return { count: count + legacyClose, legacyClose };
}

function formatText(text, options = {}) {
    const source = String(text);
    const eol = source.includes("\r\n") ? "\r\n" : "\n";
    const lines = source.split(/\r\n|\n|\r/);
    const indentSize = Math.max(1, Math.min(16, Number(options.tabSize) || 4));
    const indentation = options.insertSpaces === false ? "\t" : " ".repeat(indentSize);
    const state = { blockComment: false };
    let depth = 0;

    const formatted = lines.map(line => {
        const wasInComment = state.blockComment;
        const tokens = scanLine(line, state);
        const trimmed = line.trim();
        if (wasInComment) return line;
        if (!trimmed) return "";

        const closures = leadingClosures(tokens);
        const initialDepth = Math.max(0, depth - closures.count);
        const value = indentation.repeat(initialDepth) + trimmed;
        const braceDelta = tokens.reduce((sum, token) =>
            sum + (token.value === "{" ? 1 : token.value === "}" ? -1 : 0), 0);
        const colonDelta = isColonBlock(tokens) ? 1 : 0;
        depth = Math.max(0, depth + braceDelta + colonDelta - closures.legacyClose);
        return value;
    });

    return formatted.join(eol);
}

function formatSelectedLines(text, startLine, startCharacter, endLine, endCharacter, options = {}) {
    const source = String(text);
    const eol = source.includes("\r\n") ? "\r\n" : "\n";
    const lines = source.split(/\r\n|\n|\r/);
    if (startLine === endLine &&
        (startCharacter > 0 || endCharacter < (lines[startLine] || "").length)) return undefined;
    const first = startLine + (startCharacter > 0 ? 1 : 0);
    const last = endLine - (endLine > startLine && endCharacter < (lines[endLine] || "").length ? 1 : 0);
    if (first > last || first < 0 || last >= lines.length) return undefined;

    const formatted = formatText(source, options).split(/\r\n|\n|\r/);
    const original = lines.slice(first, last + 1).join(eol);
    return {
        startLine: first,
        endLine: last,
        original,
        text: formatted.slice(first, last + 1).join(eol)
    };
}

module.exports = { formatText, formatSelectedLines };
