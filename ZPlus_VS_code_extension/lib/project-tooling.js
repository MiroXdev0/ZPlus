"use strict";

const path = require("node:path");

function createProjectTooling(vscode, options) {
    const { executeCompiler, getZBuildPath, outputChannel } = options;
    const runningTests = new Map();
    let controller;
    let refreshGeneration = 0;

    function register(context) {
        if (vscode.tests && typeof vscode.tests.createTestController === "function") {
            controller = vscode.tests.createTestController("zplus", "Z+");
            context.subscriptions.push(controller);
            controller.refreshHandler = () => refreshProjects();
            controller.createRunProfile(
                "Run Z+ Project Tests",
                vscode.TestRunProfileKind.Run,
                (request, token) => runTests(request, token),
                true
            );
            void refreshProjects();
            registerProjectWatchers(context);
        }

        if (vscode.tasks && typeof vscode.tasks.registerTaskProvider === "function") {
            context.subscriptions.push(vscode.tasks.registerTaskProvider(
                "zplus", createBuildTaskProvider()
            ));
        }
    }

    function registerProjectWatchers(context) {
        if (vscode.workspace.createFileSystemWatcher) {
            const watcher = vscode.workspace.createFileSystemWatcher("**/zproject.toml");
            context.subscriptions.push(watcher);
            context.subscriptions.push(
                watcher.onDidCreate(() => void refreshProjects()),
                watcher.onDidChange(() => void refreshProjects()),
                watcher.onDidDelete(() => void refreshProjects())
            );
        }
        if (vscode.workspace.onDidChangeWorkspaceFolders) {
            context.subscriptions.push(vscode.workspace.onDidChangeWorkspaceFolders(
                () => void refreshProjects()
            ));
        }
    }

    async function refreshProjects() {
        const activeController = controller;
        if (!activeController || !vscode.workspace.findFiles) return;
        const generation = ++refreshGeneration;
        let projects;
        try {
            projects = await findWorkspaceProjects();
        } catch (error) {
            if (controller === activeController) {
                outputChannel.appendLine(`Z+ project discovery failed: ${error.message}`);
            }
            return;
        }
        if (generation !== refreshGeneration || controller !== activeController) return;

        const items = new Map();
        for (const project of projects) {
            if (generation !== refreshGeneration || controller !== activeController) return;
            const key = pathKey(project.root);
            const label = path.basename(project.root) || project.root;
            if (!items.has(key)) {
                items.set(key, activeController.createTestItem(
                    `zplus-project:${key}`, label, vscode.Uri.file(project.root)
                ));
            }
        }
        if (controller === activeController) activeController.items.replace([...items.values()]);
    }

    async function findWorkspaceProjects() {
        if (!vscode.workspace.findFiles) return [];
        const projects = new Map();
        for (const folder of vscode.workspace.workspaceFolders || []) {
            const manifests = await vscode.workspace.findFiles(
                new vscode.RelativePattern(folder, "**/zproject.toml"),
                "**/{.git,node_modules,build,dist,z+_packages}/**",
                10000
            );
            for (const manifest of manifests) {
                const root = path.dirname(manifest.fsPath);
                const key = pathKey(root);
                if (!projects.has(key)) {
                    projects.set(key, {
                        root,
                        folder: vscode.workspace.getWorkspaceFolder(manifest) || folder
                    });
                }
            }
        }
        return [...projects.values()];
    }

    function createBuildTaskProvider() {
        const makeTask = (projectRoot, folder, command) => {
            const definition = { type: "zplus", command, projectRoot };
            const scope = folder || vscode.TaskScope.Workspace;
            const name = `Z+ ${command}: ${path.basename(projectRoot) || projectRoot}`;
            const execution = new vscode.ProcessExecution(getZBuildPath(), [command], {
                cwd: projectRoot
            });
            const task = new vscode.Task(definition, scope, name, "Z+", execution);
            if (command === "build") task.group = vscode.TaskGroup.Build;
            if (command === "test") task.group = vscode.TaskGroup.Test;
            return task;
        };

        return {
            async provideTasks() {
                const tasks = [];
                for (const project of await findWorkspaceProjects()) {
                    for (const command of ["build", "run", "test"]) {
                        tasks.push(makeTask(project.root, project.folder, command));
                    }
                }
                return tasks;
            },
            resolveTask(task) {
                const definition = task.definition;
                if (!definition || !["build", "run", "test"].includes(definition.command) ||
                    !definition.projectRoot) return undefined;
                const folder = vscode.workspace.getWorkspaceFolder(
                    vscode.Uri.file(definition.projectRoot)
                );
                return makeTask(definition.projectRoot, folder, definition.command);
            }
        };
    }

    async function runTests(request, token) {
        if (!controller) return;
        const run = controller.createTestRun(request);
        const included = request.include && request.include.length
            ? request.include : [...controller.items];
        const excluded = new Set(request.exclude || []);
        for (const item of included) {
            if (excluded.has(item) || token.isCancellationRequested) {
                run.skipped(item);
                continue;
            }

            run.enqueued(item);
            run.started(item);
            const projectRoot = item.uri.fsPath;
            const key = pathKey(projectRoot);
            runningTests.get(key)?.abort();
            const cancellationController = new AbortController();
            runningTests.set(key, cancellationController);
            const cancellation = token.onCancellationRequested(() => cancellationController.abort());
            try {
                outputChannel.show(true);
                outputChannel.appendLine(`---- ZPBuild tests: ${projectRoot} ----`);
                const result = await executeCompiler(getZBuildPath(), ["test"], {
                    cwd: projectRoot,
                    signal: cancellationController.signal
                });
                const output = `${result.stdout}${result.stderr ? `\n${result.stderr}` : ""}`.trim();
                if (output) {
                    run.appendOutput(`${output.replace(/\r?\n/g, "\r\n")}\r\n`, undefined, item.id);
                    outputChannel.appendLine(output);
                }

                if (token.isCancellationRequested || result.error?.code === "ABORT_ERR") {
                    run.skipped(item);
                } else if (result.error?.code === "ENOENT") {
                    run.failed(item, new vscode.TestMessage(
                        `ZPBuild not found: ${getZBuildPath()}. Configure zplus.zbuildPath or add zpbuild to PATH.`
                    ));
                } else if (result.code !== 0) {
                    run.failed(item, new vscode.TestMessage(
                        output || `ZPBuild test failed with exit code ${result.code}.`
                    ));
                } else {
                    run.passed(item);
                }
            } catch (error) {
                if (token.isCancellationRequested) run.skipped(item);
                else run.failed(item, new vscode.TestMessage(error.message));
            } finally {
                cancellation.dispose();
                if (runningTests.get(key) === cancellationController) runningTests.delete(key);
            }
        }
        run.end();
    }

    function dispose() {
        refreshGeneration++;
        for (const cancellation of runningTests.values()) cancellation.abort();
        runningTests.clear();
        controller = undefined;
    }

    return { register, refreshProjects, dispose };
}

function pathKey(value) {
    return process.platform === "win32" ? value.toLowerCase() : value;
}

module.exports = { createProjectTooling };
