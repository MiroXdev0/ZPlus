"use strict";

const childProcess = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

function resolveCompilerPath(configuredPath, env = process.env, platform = process.platform) {
    const executable = String(configuredPath || (platform === "win32" ? "zp.exe" : "zp")).trim();
    if (!executable) {
        return platform === "win32" ? "zp.exe" : "zp";
    }

    if (path.isAbsolute(executable) || executable.includes("/") || executable.includes("\\")) {
        return executable;
    }

    const extensions = platform === "win32"
        ? (env.PATHEXT || ".COM;.EXE;.BAT;.CMD").split(";")
        : [""];
    for (const directory of (env.PATH || "").split(path.delimiter)) {
        if (!directory) {
            continue;
        }
        for (const extension of extensions) {
            const candidate = path.join(directory, path.extname(executable) ? executable : executable + extension);
            try {
                const stat = fs.statSync(candidate);
                if (stat.isFile()) {
                    return candidate;
                }
            } catch {
                // Continue searching PATH; the process launch reports missing executables.
            }
        }
    }
    return executable;
}

function executeCompiler(executable, args, options = {}) {
    return new Promise((resolve, reject) => {
        const child = childProcess.execFile(
            executable,
            args,
            {
                cwd: options.cwd,
                windowsHide: true,
                timeout: options.timeout || 120000,
                maxBuffer: options.maxBuffer || 8 * 1024 * 1024
            },
            (error, stdout, stderr) => {
                const result = {
                    stdout: stdout || "",
                    stderr: stderr || "",
                    code: error ? (typeof error.code === "number" ? error.code : 1) : 0,
                    error
                };
                resolve(result);
            }
        );
        if (options.input !== undefined) {
            child.stdin.on("error", error => {
                if (error.code !== "EPIPE") reject(error);
            });
            child.stdin.end(options.input, "utf8");
        }
    });
}

module.exports = { executeCompiler, resolveCompilerPath };
