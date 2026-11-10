# Z+ Language Support for Visual Studio Code

Z+ Language Support provides syntax highlighting and editor tools for `.zp` programs and `.zh` modules, backed by the actual Z+ compiler.

## Installation

Install the packaged `zplus-language-*.vsix` file with **Extensions: Install from VSIX...**, or run:

```powershell
code --install-extension .\zplus-language-0.6.0.vsix
```

## Requirements and compiler setup

Install/build the Z+ compiler from this repository and make `zp` (or `zp.exe`) available on `PATH`. The extension uses the compiler directly; it does not simulate checking, interpretation, or native compilation.

If the executable is not on `PATH`, set **Z+: Compiler Path** (`zplus.compilerPath`) to its full path, or run **Z+: Select Compiler**. Paths containing spaces are supported.

**Z+: Output Path** (`zplus.outputPath`) optionally sets the native build output. It supports `${file}`, `${fileDirname}`, `${fileBasename}`, `${fileBasenameNoExtension}`, and `${workspaceFolder}`. A relative path is resolved from the source file's directory. If empty, the compiler chooses its platform default.

ZBuild project commands use `zbuild` from `PATH`, or the executable selected by **Z+: ZBuild Path** (`zplus.zbuildPath`).

## Commands

Open a `.zp` program or `.zh` module, then use the Command Palette:

- **Z+: Check Current File** — checks the current editor contents and displays compiler diagnostics in Problems. A `.zh` module is checked as a temporary entry source, with its own relative dependencies resolved normally.
- **Z+: Run Current File** — saves and runs a `.zp` entry file with the interpreter using `zp run`.
- **Z+: Build Current File** — saves a `.zp` entry file and requests a native executable using `zp build`.
- **Z+: Select Compiler** — choose the compiler executable.
- **Z+: Show Compiler Version** — runs the compiler's `version` command.
- **Z+: ZBuild Build Project** — runs `zbuild build` from the nearest directory containing `zproject.toml`.
- **Z+: ZBuild Run Project** — runs `zbuild run` from that project root.
- **Z+: ZBuild Test Project** — runs `zbuild test` from that project root.

Compiler output is available in **View: Output**, **Z+**. Checks run automatically while typing and on save by default. Change `zplus.checkWhileTyping`, `zplus.checkOnSave`, and `zplus.checkDelay` to adjust this behavior.

ZBuild project commands require a `zproject.toml` in the active file's directory or an ancestor (or a workspace folder ancestor). See the repository's [ZBuild documentation](../docs/ZBUILD.md) for the supported project schema and current compiler limitations.

## Editor features

- TextMate highlighting for implemented language tokens, strings, characters, line/block comments, types, declarations, built-ins, and operators.
- Optional **Z+ File Icons** theme: select it with **Preferences: File Icon Theme** to show the Z+ logo on `.zp` and `.zh` files. Other extensions continue using VS Code's default file icons.
- Completion for keywords, built-ins, local declarations and public imported declarations. After typing `module.`, completion offers that module's public members.
- Signature help, hover information, and Go to Definition for local and imported symbols, including qualified module references.
- Imported private declarations are excluded from suggestions; compiler diagnostics remain authoritative for inaccessible references.
- Go to Definition for declarations in the current document and directly/nested `use` dependencies.
- Public ZGraphics module functions participate in completion, signature help, hover, and definition navigation for `use zgraphics` when the standard-library source is present in the workspace or beside this extension's project.
- Document outline, snippets, bracket matching, and indentation/folding rules.
- Compiler-generated diagnostics with source positions; successful checks clear old diagnostics.

The compiler resolves `.zh` modules relative to the file containing each `use` statement and reports header diagnostics against their actual source files. Module rules and current limitations are documented in the project's `MODULES.md`.

Run and Build require a `.zp` entry file; `.zh` files are declaration-only modules and cannot be executed or built directly.

## Language and native support

The extension follows the syntax described in the project's `SYNTAX.md`. This includes accepted constructs that are incomplete at runtime; highlighting and snippets do not imply that every construct executes successfully.

`zp run` uses the interpreter. `zp build` uses the x86-64 native backend and supports only a subset of Z+. Unsupported IR produces the compiler's actual error; the extension never falls back to the interpreter. See the project's `NATIVE_BACKEND.md` for the subset and platform validation status.

The checker uses the compiler's `--diagnostics=json` option. This option is supported by the compiler version in this repository; use a current compiler build if diagnostics are missing or shown only in the output channel.

ZGraphics editor suggestions describe the source wrappers, not native-link support. Run graphics examples with `zp run`; `zp build` currently rejects graphics calls. The compiler source repository's `../ZGraphics/README.md` documents platform requirements and runtime limitations.

## Troubleshooting

- **Compiler not found:** check `zplus.compilerPath`, or verify `zp --help` works in the VS Code environment's `PATH`.
- **No diagnostics:** run **Z+: Check Current File**, then inspect the **Z+** Output channel. The compiler must support `--diagnostics=json`.
- **Native build fails:** inspect its compiler message in the Output channel. The native backend intentionally rejects unsupported features.
- **File paths contain spaces:** use a quoted compiler path in settings only as a plain path (do not add shell quotes). File/process execution does not invoke a shell.

## Development and tests

From this directory, run:

```powershell
npm test
```

Compiler integration tests use the compiler from the parent project's `build` directory when available; set `ZPLUS_COMPILER` to use another executable. The VS Code feature parser follows the implemented declaration syntax and does not replace compiler semantic analysis.
