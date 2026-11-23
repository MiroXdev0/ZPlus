# Official Z+ Developer Tools for Visual Studio Code

Z+ is a statically typed programming language and compiler developed by the Z+ project. This extension provides official developer tools for `.zp` programs and `.zh` modules, backed by the Z+ compiler. Visit the [official Z+ website](https://zpluse.vercel.app/) for product information and official resources.

This extension is licensed under Apache License 2.0. Third-party materials retain their own licenses; see `LICENSE.txt` and `NOTICE`.

## Installation

Install the packaged `zplus-language-*.vsix` file with **Extensions: Install from VSIX...**, or run:

```powershell
code --install-extension .\bin\zpluse-language-1.3.4.vsix
```

## Requirements and compiler setup

The extension requires the Z+ compiler. See the [official Z+ website](https://zpluse.vercel.app/) for current compiler distribution information, then make `zp` (or `zp.exe`) available on `PATH`. The extension uses the compiler directly; it does not simulate checking, interpretation, or native compilation.

If the executable is not on `PATH`, set **Z+: Compiler Path** (`zplus.compilerPath`) to its full path, or run **Z+: Select Compiler**. Paths containing spaces are supported.

**Z+: Output Path** (`zplus.outputPath`) optionally sets the native build output. It supports `${file}`, `${fileDirname}`, `${fileBasename}`, `${fileBasenameNoExtension}`, and `${workspaceFolder}`. A relative path is resolved from the source file's directory. If empty, the compiler chooses its platform default.

ZBuild project commands prefer `zpbuild` and fall back to the `zbuild` compatibility alias on `PATH`, or use the executable selected by **Z+: ZBuild Path** (`zplus.zbuildPath`).

## Commands

Open a `.zp` program or `.zh` module, then use the Command Palette:

- **Z+: Check Current File** — checks the current editor contents and displays compiler diagnostics in Problems. A `.zh` module is checked as a temporary entry source, with its own relative dependencies resolved normally.
- **Z+: Run Current File** — saves and runs a `.zp` entry file with the interpreter using `zp run`.
- **Z+: Build Current File** — saves a `.zp` entry file and requests a native executable using `zp build`.
- **Z+: Select Compiler** — choose the compiler executable.
- **Z+: Show Compiler Version** — runs the compiler's `version` command.
- **Z+: ZBuild Build Project** — runs `zpbuild build` from the nearest directory containing `zproject.toml`.
- **Z+: ZBuild Run Project** — runs `zpbuild run` from that project root.
- **Z+: ZBuild Test Project** — runs `zpbuild test` from that project root.
- **Z+: Create Project from Template** — creates a ZPBuild Console, Empty, Library, Game, or Desktop project in a selected parent folder.

The **Tasks: Run Task** picker also offers project-scoped ZPBuild Build, Run, and Test tasks. These run through VS Code's process task provider in the integrated terminal.

Compiler output is available in **View: Output**, **Z+**. Checks run automatically while typing and on save by default; a newer check cancels the previous compiler process for that document. Run and Build show cancellable progress and stop when cancelled or when the source changes. Stale diagnostics are cleared on edits while the next check is pending. Change `zplus.checkWhileTyping`, `zplus.checkOnSave`, and `zplus.checkDelay` to adjust this behavior.

ZBuild project commands require a `zproject.toml` in the active file's directory or an ancestor (or a workspace folder ancestor). The project initializer requires `zpbuild` (or the compatibility `zbuild` command) and uses its built-in templates. Consult the [official Z+ website](https://zpluse.vercel.app/) for current project and compiler information.

## Editor features

- TextMate highlighting for implemented language tokens, strings, characters, line/block comments, types, declarations, built-ins, and operators.
- Optional **Z+ File Icons** theme: select it with **Preferences: File Icon Theme** to show the official light-surface logo in light themes and dark-surface logo in dark themes on `.zp` and `.zh` files. Other extensions continue using VS Code's default file icons.
- Contextual completion for keywords, built-ins, local declarations and public imported declarations. After typing `module.`, completion offers that module's public members.
- Signature help, hover information, and Go to Definition for local and imported symbols, including qualified module references.
- Go to Type Definition for indexed aggregate declarations and explicit variable or function return annotations, when the referenced type resolves unambiguously.
- Workspace symbol search, Find All References and Rename Symbol for indexed declarations and importing files. The workspace index is lazy, cached by file metadata, updated for open buffers, cancellation-aware, and excludes generated/build/dependency folders. Imported symbols use a bounded source-content cache invalidated by editor changes and `.zp`/`.zh` filesystem events, so repeated completion and navigation requests do not synchronously reread modules.
- Imported private declarations are excluded from suggestions; compiler diagnostics remain authoritative for inaccessible references.
- Go to Definition for declarations in the current document and directly/nested `use` dependencies.
- Public Z+ graphics module functions participate in completion, signature help, hover, and definition navigation for `use zp.graphics` when the standard-library source is present in the workspace or beside this extension's project.
- Document outline, snippets, bracket matching, and indentation/folding rules.
- Format Document and Format Selection use a lexical scanner to indent brace-delimited and legacy colon-delimited blocks. They preserve code tokens, comments, and string contents and remove trailing whitespace; they do not rewrite expressions or move braces. Selection formatting only edits complete selected lines and leaves partial boundary lines untouched. Enable VS Code's `editor.formatOnSave` setting to format on save.
- Compiler-generated diagnostics with source positions; successful checks clear old diagnostics.
- Test Explorer discovers `zproject.toml` projects and delegates each project suite to `zpbuild test`; ZPBuild remains responsible for selecting and running test targets.

The compiler resolves `.zh` modules relative to the file containing each `use` statement and reports header diagnostics against their actual source files. Consult the [official Z+ website](https://zpluse.vercel.app/) for current module-system information.

Run and Build require a `.zp` entry file; `.zh` files are declaration-only modules and cannot be executed or built directly.

## Language and native support

The installed compiler remains authoritative for parsing and semantics; editor symbol indexing supports navigation and does not replace compiler validation. The compiler has no semantic-query or debugger protocol, so the extension does not claim type-correct IntelliSense, semantic tokens, call/type hierarchy, or Debug Adapter Protocol support. References and rename are based on the extension's parsed declaration/import graph and are most reliable for direct declarations and explicitly imported modules. Visit the [official Z+ website](https://zpluse.vercel.app/) for current language documentation.

`zp run` uses the interpreter. `zp build` uses the x86-64 native backend and supports only a subset of Z+. Unsupported IR produces the compiler's actual error; the extension never falls back to the interpreter. Consult the [official Z+ website](https://zpluse.vercel.app/) for current native-backend information.

Live checks pass the current editor contents to the compiler over standard input, so unsaved edits are analyzed without creating source snapshots in the project. The compiler uses the real source path for relative `.zh` imports and diagnostics. The checker uses the compiler's `--source-stdin` and `--diagnostics=json` options; use a current compiler build if diagnostics are missing or shown only in the output channel.

ZGraphics editor suggestions describe the available wrappers, not native-link support. Run graphics examples with `zp run`; `zp build` currently rejects graphics calls. Visit the [official Z+ website](https://zpluse.vercel.app/) for current ZGraphics platform and runtime information.

## Troubleshooting

- **Compiler not found:** check `zplus.compilerPath`, or verify `zp --help` works in the VS Code environment's `PATH`.
- **No diagnostics:** run **Z+: Check Current File**, then inspect the **Z+** Output channel. The compiler must support `--diagnostics=json`.
- **Native build fails:** inspect its compiler message in the Output channel. The native backend intentionally rejects unsupported features.
- **File paths contain spaces:** use a quoted compiler path in settings only as a plain path (do not add shell quotes). File/process execution does not invoke a shell.

## Development and tests

The release version is **1.3.4**. The package output is `bin/zpluse-language-1.3.4.vsix`; tests and development files are excluded from the VSIX.

From this directory, run:

```powershell
npm test
```

Compiler integration tests can use the executable selected by `ZPLUS_COMPILER`. Run `npm run check` for JavaScript syntax checks and `npm test` for unit, workspace, formatter, and compiler integration tests. The tests include valid and invalid Z+ diagnostic stress fixtures. The extension separates compiler execution, diagnostics, workspace indexing, formatting, and project tooling into focused modules. It uses VS Code language-provider APIs in the extension host; it does not start a separate LSP server because the compiler exposes check/run/build commands rather than a semantic-query protocol. Diagnostics always come from the compiler.
