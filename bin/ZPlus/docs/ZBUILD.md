# ZBuild

ZBuild is the standalone project CLI and build-engine library for Z+. It delegates parsing, semantic analysis, module loading, interpretation, native code generation, and linking to `zp`; it does not duplicate compiler functionality.

## Build and installation

Build both tools with the repository's CMake project:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executable is `build\Release\zbuild.exe` with Visual Studio and `build\zbuild` with a single-configuration Linux generator. A successful Release build copies it to `bin\zbuild.exe` or `bin/zbuild` respectively. This is separate from the `zp` copy step. `zbuild --version` reports the ZBuild CLI version.

ZBuild locates `zp` beside itself, in the project's `bin` directory, in the current directory, or on `PATH`. A project can specify an explicit compiler path with `compiler.path`.

## Commands

Run commands from a project directory or one of its descendants:

```text
zbuild init MyApp
zbuild init MyGame -template Game
zbuild init MyServer --template WebServer
zbuild init MyConsole --template Console
zbuild init MyLibrary -template Library
zbuild init MyDesktop --template Desktop
zbuild init MyEmpty --template Empty
zbuild templates
zbuild template info Game
zbuild configure
zbuild build [--debug | --release] [--target NAME] [--jobs N] [--verbose] [--clean-first]
zbuild run [--target NAME] [--release] [-- program-arguments]
zbuild test [--target TEST_TARGET] [--verbose]
zbuild clean [--debug | --release]
zbuild rebuild [--target NAME] [--release]
zbuild info [--json]
zbuild targets [--json]
zbuild graph [--dot]
zbuild package [--target NAME] [--release]
```

`build` without `--target` builds every configured target. `run` and `package` choose the first executable target when none is selected. `test` runs targets configured under `[tests].targets`, or all targets of type `test` when that list is empty. Program arguments after `--` are passed to `run`; test arguments come from `[tests].arguments`. `--jobs` accepts 1 through 1024; the default is the configured value or available hardware concurrency. Nonzero program and test exit codes are returned to the caller.

`info --json` and `targets --json` provide machine-readable project metadata. `graph --dot` writes Graphviz DOT to standard output.

## Project templates

`zbuild init [ProjectName]` keeps the standard runnable Console starter as its default. Specify a template with either `-template NAME` or `--template NAME`; names and aliases are case-insensitive. Use `--path DIR` to choose another destination. With no name or path, `zbuild init -template Empty` initializes the current directory. Existing files are never overwritten; a conflict leaves those files untouched. Project names must start with an ASCII letter, digit, or `_` and contain only ASCII letters, digits, `_`, or `-` (up to 64 characters). Omit `--author` unless you want to supply a single-line author value.

`zbuild templates` lists name, description, target type, required Z+ modules, and supported platforms. `zbuild template info NAME` shows a template's version, required Z+ version, aliases, and availability. Unknown names produce the available alternatives.

| Template | Aliases | Target / modules | Platforms and notes |
|---|---|---|---|
| `Game` | `2d` | Executable; `zgraphics` | Windows interpreter/native where supported; Linux interpreter with X11. Includes a moving, keyboard-controlled player, collectible, collision, and score HUD. |
| `WebServer` | `server`, `web` | Unavailable | Z+ currently has no HTTP listener/server API. It is intentionally not generated; the HTTP client is not presented as server functionality. |
| `Console` | `executable`, `exe`, `default` | Executable; `zargs`, `zstring` | Windows and Linux. Demonstrates imported modules, argument handling, string helpers, and `printz()`. |
| `Library` | `lib` | Source `.zh` modules plus a test executable | Windows and Linux. The compiler has no library/archive/link target, so this template does not claim to produce a linkable library artifact. |
| `Desktop` | `gui` | Executable; `zgraphics` | Windows interpreter/native where supported; Linux interpreter with X11. Demonstrates a window, text, mouse input, and Escape-to-quit. |
| `Empty` | `minimal` | Executable; no extra modules | Windows and Linux. Generates only `zproject.toml`, `src/main.zp`, and `README.md`. |

Game and Desktop use only the currently supported ZGraphics API. Their windows are interactive programs and require a graphical display to run; their generated projects can still be build-validated separately. See each generated README for controls and asset guidance.

Template values available in file contents are `{{PROJECT_NAME}}`, `{{PROJECT_DIRECTORY}}`, `{{PROJECT_VERSION}}`, `{{AUTHOR}}`, and `{{YEAR}}`; `{{BUILD_BACKEND}}` and `{{BUILD_MODE}}` are set by the manifest's `configuration` values. The initializer rejects unresolved placeholders. `--version` sets the project version (numeric dotted form, default `0.1.0`); `--author` is optional.

### Custom templates and manifest

Custom templates are directories beneath `%APPDATA%\ZBuild\templates` on Windows, `$XDG_CONFIG_HOME/zbuild/templates` on Linux, or `~/.config/zbuild/templates` when `XDG_CONFIG_HOME` is unset. Set `ZBUILD_TEMPLATES_DIR` to use a different template root. Each child directory contains a `template.toml` manifest and the files listed by it. Built-ins take precedence over a custom template with the same name or alias; collisions among custom names/aliases are errors. Remote downloads are not supported.

The manifest is a flat TOML subset (quoted strings, arrays of quoted strings, and booleans; no TOML tables). Required keys are `name`, `description`, `version`, `required_zplus_version`, and `default_target_type`. The target type is descriptive metadata: `executable`, `test`, or `source_module`. Optional keys are `platforms`, `required_modules`, `aliases`, `files`, `directories`, `configuration`, `available`, and (when unavailable) `unavailable_reason`. Paths are relative to the template directory, normalized, and may not escape it. Listed files must exist; symlinked template files are rejected. Generated paths cannot overwrite existing files or traverse symlinked project directories.

For example:

```toml
name = "HelloCustom"
description = "A small custom executable."
version = "1.0.0"
required_zplus_version = "0.5.0"
default_target_type = "executable"
platforms = ["Windows", "Linux"]
required_modules = []
aliases = ["hello"]
configuration = ["backend=interpreter"]
directories = ["src"]
files = ["zproject.toml", "README.md", "src/main.zp"]
```

Keep the manifest's file and directory lists complete. Put placeholders in text file contents where project-specific values should appear. Use only APIs supported by the declared modules and set `available = false` with an explanatory `unavailable_reason` when a template cannot yet be generated honestly. The built-in `WebServer` manifest demonstrates this for missing HTTP server support.

## `zproject.toml`

The parser intentionally supports the following TOML subset: single-line tables, `[[target]]`, quoted strings, integer and boolean values, and arrays of quoted strings. It rejects unknown tables and keys. Multiline strings, inline tables, date/time values, and arbitrary TOML constructs are not supported.

```toml
[project]
name = "MyGame"
version = "1.0.0"
description = "A game written in Z+"
entry = "src/main.zp"
sources = ["src"]
include_dirs = []

[build]
backend = "native" # native or interpreter
mode = "debug" # debug or release
output_dir = "build"
optimization = 0
jobs = 0

[compiler]
path = "" # optional absolute or project-relative zp path
warnings = true
debug_symbols = false

[platform]
windows = true
linux = true

[assets]
directories = ["assets"]
preserve_structure = true

[tests]
targets = ["game_tests"]
arguments = []

[[target]]
name = "game"
type = "executable"
entry = "src/main.zp"
sources = ["src/**/*.zh"]
dependencies = []
backend = "native"
mode = "debug"
output_dir = "bin"

[[target]]
name = "game_tests"
type = "test"
entry = "tests/main.zp"
dependencies = ["game"]
```

`project.name` and each target's `name` and `entry` are required. Target names are case-insensitively unique and may contain only ASCII letters, digits, `_`, and `-`. Paths for entries, source patterns, the output directory, and asset directories must be project-relative and cannot escape the project root. Every target requires a `.zp` entry because the current compiler emits one complete program per invocation. `project.sources` supplies source discovery for a single implicit target; for explicit multi-target projects, use `target.sources` for per-target cache inputs. Because `zp` has no separate compilation-unit or linking interface, these patterns do not cause unrelated `.zp` files to be merged into the entry. Imported `.zh` modules are always discovered from the target entry, following Z+'s relative `use` semantics. Only configured target inputs and reachable imports invalidate that target's cache.

`optimization` currently accepts only `0`; the present `zp` CLI has no native optimization-level or debug-symbol switches. `compiler.warnings` must remain `true` because `zp` has no warning-disable option. Debug and release currently select separate output/cache directories, not different compiler optimization behavior. Custom profile tables are rejected until the compiler exposes profile-specific build controls.

`include_dirs` is accepted only when empty. The compiler resolves modules relative to the importing source. Platform flags can disable the current host, in which case building is rejected.

## Targets and module dependencies

The current compiler can produce standalone executable outputs. ZBuild therefore supports `executable` and `test` targets only; static/shared libraries and application bundles are rejected rather than simulated. Target dependencies form a validated DAG, are built before dependents, and participate in dependent cache signatures. They are build-order/artifact dependencies only: the current compiler has no object-file or library-link interface, so target outputs are not linked into one another. Share Z+ declarations through `.zh` modules and `use`.

`[dependencies]` accepts local project paths, either as keys or `[dependencies.NAME].path`. `zbuild configure` validates each dependency manifest and writes `zproject.lock` with its version and manifest SHA-256. These entries are currently lock/validation metadata; they are not automatically imported, compiled, or linked into target programs. No registry, version solver, external downloads, or package scripts are implemented.

The `Library` template demonstrates reusable source `.zh` modules and tests, not a separately linkable library artifact.

## Incremental builds and cache

Outputs are organized as `build/<debug|release>/bin/`, `build/<debug|release>/assets/`, and `build/cache/`. Cache signatures include compiler version and executable hash, target settings, source and imported `.zh` contents, target dependency output hashes, and asset contents. A cache entry is accepted only when its signature matches and the output's SHA-256 matches the recorded output digest. Corrupt or incomplete records rebuild or fail explicitly. Failed compiler invocations do not publish a successful cache record.

Target DAG scheduling runs independent targets in bounded parallel batches. Each target has a distinct temporary output and is published only after the compiler succeeds. Builds discover configured target sources and reachable imports again each invocation, so added, removed, and renamed inputs affect the appropriate target signatures without invalidating unrelated targets.

Assets are copied into the selected profile's `assets` directory. Only changed files are copied; stale staged files are removed. `preserve_structure = true` preserves paths within each configured source directory; `false` flattens files by name and reports collisions. `package` copies the selected executable and staged assets into `build/<profile>/package/`; on Windows it also includes the graphics runtime DLL when found beside the compiler. Native targets that import `zaudio` automatically stage the matching audio runtime beside the executable and include it in packages. Projects that do not use `zaudio` do not stage an audio runtime.

## Testing and troubleshooting

Test targets are regular Z+ programs; a successful process exit is a pass. Use `--verbose` to show successful test output. Compiler diagnostics are forwarded unchanged. `zbuild graph --dot` can help diagnose dependency cycles; missing targets and cycles are errors.

The repository's ZBuild unit and integration tests run through CTest. The build integration fixture is [zbuild_multi](../test_program/zbuild_multi/), and validates native compilation, imported modules, parallel target ordering, content-cache reuse/invalidation, test execution, asset staging/packaging, and cycle diagnostics. The audio integration test builds and runs a `zaudio` target and verifies runtime and asset packaging. The template integration test generates and validates every available built-in template, checks custom discovery/aliases/variables and overwrite protection, and builds or runs generated projects where practical.

### Current compiler-driven limitations

- The compiler has no machine-readable project/build API, object files, archive creation, or library linking. ZBuild invokes its stable CLI once per executable/test target.
- There are no compiler optimization/debug-symbol controls, so those requests are validated and rejected rather than silently ignored.
- Local dependencies are locked and validated but are not consumed as source packages or linked artifacts.
- Asset configuration is project-wide; target-specific asset rules are not supported.
- Native output is for the host platform. Cross-compilation and target-specific platform matrices are not implemented.
- `configure` supports local manifests only; it does not access a package registry.

These are compiler/build-system integration limits, not claims of supported features. Existing `zp` commands and standalone source builds remain unchanged.
