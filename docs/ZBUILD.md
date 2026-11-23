# ZPBuild

ZPBuild is the standalone project CLI and build-engine library for Z+. It delegates parsing, semantic analysis, module loading, interpretation, native code generation, and linking to `zp`; it does not duplicate compiler functionality. The `zbuild` executable remains available as a compatibility alias.

## Build and installation

Build both tools with the repository's CMake project:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

The executables are `build\Release\zpbuild.exe` and `build\Release\zbuild.exe` with Visual Studio, or `build\zpbuild` and `build\zbuild` with a single-configuration Linux generator. Release builds copy both to `bin`. `zpbuild --version` reports ZPBuild version `1.0.0`; `zbuild` remains supported.

ZBuild locates `zp` beside itself, in the project's `bin` directory, in the current directory, or on `PATH`. A project can specify an explicit compiler path with `compiler.path`.

## Commands

Run commands from a project directory or one of its descendants:

```text
zpbuild init MyApp
zpbuild new MyOtherApp
zbuild init MyGame -template Game
zbuild init MyServer --template WebServer
zbuild init MyConsole --template Console
zbuild init MyLibrary -template Library
zbuild init MyDesktop --template Desktop
zbuild init MyEmpty --template Empty
zbuild templates
zbuild template info Game
zbuild configure
zpbuild build [TARGET] [--debug | --release] [--profile NAME] [--backend NAME] [--jobs N] [--parallel] [--no-cache] [--dry-run] [--verbose] [--clean-first]
zpbuild plan [TARGET] [--profile NAME] [--backend NAME] [--jobs N]
zpbuild cache info
zpbuild cache verify
zpbuild cache clean
zpbuild run [TARGET] [--release] [-- program-arguments]
zpbuild check [TARGET]
zpbuild test [--target TEST_TARGET] [--verbose]
zpbuild clean [--debug | --release]
zpbuild rebuild [TARGET] [--release]
zpbuild info [--json]
zpbuild targets [--json]
zpbuild graph [--dot]
zpbuild package [TARGET] [--release]
```

`build` without a target builds every configured target. A target can be selected positionally or with `--target`. `run` and `package` select `[targets].default` when it names an executable, otherwise the first executable target. `test` runs targets configured under `[tests].targets`, or all targets of type `test` when that list is empty. Program arguments after `--` are passed to `run`; test arguments come from `[tests].arguments`. `--jobs` accepts 1 through 1024. The default uses `[build].jobs` when nonzero, otherwise automatic hardware concurrency capped at 32 workers. `--parallel` selects that automatic worker count even when the project configures a smaller default; `--jobs N` takes precedence. Jobs execute ready graph nodes concurrently and dependencies always finish first. Nonzero program and test exit codes are returned to the caller.

`info --json` and `targets --json` provide machine-readable project metadata. `graph --dot` writes Graphviz DOT to standard output.

## Project templates

`zpbuild init [ProjectName]` keeps the standard runnable Console starter as its default. `zpbuild new NAME` creates a project directory. Specify a template with either `-template NAME` or `--template NAME`; names and aliases are case-insensitive. Use `--path DIR` to choose another destination. With no name or path, `zpbuild init -template Empty` initializes the current directory. Existing files are never overwritten; a conflict leaves those files untouched. Project names must start with an ASCII letter, digit, or `_` and contain only ASCII letters, digits, `_`, or `-` (up to 64 characters). Omit `--author` unless you want to supply a single-line author value.

`zpbuild templates` lists name, description, target type, required Z+ modules, and supported platforms. `zpbuild template info NAME` shows a template's version, required Z+ version, aliases, and availability. Unknown names produce the available alternatives.

| Template | Aliases | Target / modules | Platforms and notes |
|---|---|---|---|
| `Game` | `2d` | Executable; `zp.graphics` | Windows interpreter/native where supported; Linux interpreter with X11. Includes a moving, keyboard-controlled player, collectible, collision, and score HUD. |
| `WebServer` | `server`, `web` | Unavailable | Z+ currently has no HTTP listener/server API. It is intentionally not generated; the HTTP client is not presented as server functionality. |
| `Console` | `executable`, `exe`, `default` | Executable; `zp.args`, `zp.string` | Windows and Linux. Demonstrates imported modules, argument handling, string helpers, and `printz()`. |
| `Library` | `lib` | Source `.zh` modules plus a test executable | Windows and Linux. The compiler has no library/archive/link target, so this template does not claim to produce a linkable library artifact. |
| `Desktop` | `gui` | Executable; `zp.graphics` | Windows interpreter/native where supported; Linux interpreter with X11. Demonstrates a window, text, mouse input, and Escape-to-quit. |
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
include_dirs = []

[build]
backend = "native" # native or interpreter
default_profile = "debug" # debug or release
output = "build"
optimization = 0
jobs = 0

[build.profiles.debug]
optimization = 0
debug_symbols = false

[build.profiles.release]
optimization = 0
debug_symbols = false

[sources]
directories = ["src"]
exclude = ["build", "vendor", "tests"]

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

[targets]
default = "game"

[target.game]
type = "executable"
entry = "src/main.zp"
sources = ["src/**/*.zh"]
dependencies = []
backend = "native"
output = "artifacts"

[target.game_tests]
type = "test"
entry = "tests/main.zp"
dependencies = ["game"]
```

`[targets].default` names the default executable target. Named `[target.NAME]` tables are supported alongside legacy `[[target]]` arrays. `project.name` and each target's `name` and `entry` are required. Target names are case-insensitively unique and may contain only ASCII letters, digits, `_`, and `-`. Paths for entries, source patterns, the output directory, and asset directories must be project-relative and cannot escape the project root. Every executable/test target requires a `.zp` entry because the current compiler emits one complete program per invocation. `[sources].directories` (or legacy `project.sources`) configures source discovery; `exclude` prevents generated or vendored trees from being scanned. For explicit multi-target projects, use `target.sources` for per-target cache inputs. Because `zp` has no separate compilation-unit or linking interface, these patterns do not cause unrelated `.zp` files to be merged into the entry. Imported `.zh` modules are always discovered from the target entry, following Z+'s relative `use` semantics. Only configured target inputs and reachable imports invalidate that target's cache.

`optimization` currently accepts only `0`; the present `zp` CLI has no native optimization-level or debug-symbol switches. `compiler.warnings` must remain `true` because `zp` has no warning-disable option. Debug and release select separate output and metadata directories, not different compiler optimization behavior. Only the `debug` and `release` profiles are supported; unsupported optimization or debug-symbol requests are rejected.

`include_dirs` is accepted only when empty. The compiler resolves modules relative to the importing source. Platform flags can disable the current host, in which case building is rejected.

## Targets and module dependencies

The current compiler can produce standalone executable outputs. ZPBuild therefore supports `executable` and `test` targets only for Z+ sources; static/shared libraries and application bundles are rejected rather than simulated. Custom targets can run an argv command without shell concatenation. Target dependencies form a validated DAG, are cycle-checked and traversed deterministically, are built before dependents, and participate in dependent cache signatures. They are build-order/artifact dependencies only: the current compiler has no object-file or library-link interface, so target outputs are not linked into one another. Share Z+ declarations through `.zh` modules and `use`.

ZPBuild also detects a project's `zpm.json` and resolves its local or cached package dependencies before build, check, and test operations. ZPM records the selected versions and source integrity in `zpm-lock.json`; `--locked` behavior is used automatically when that lockfile is present. Installed packages live under `z+_packages/<name>/` and can be imported by package name; the compiler resolves each import to the manifest's `.zh` entry. Native and interpreter compilation use the same module loader. Conflicting constraints fail rather than installing multiple versions under one package name. See [ZPM](ZPM.md) for the manifest, cache, and archive format.

The legacy `[dependencies]` section in `zproject.toml` accepts local project paths, either as keys or `[dependencies.NAME].path`. `zbuild configure` validates each dependency manifest and writes `zproject.lock` with its version and manifest SHA-256. These entries remain lock/validation metadata; `zpm.json` is the package dependency format.

The `Library` template demonstrates reusable source `.zh` modules and tests, not a separately linkable library artifact.

## Incremental builds and cache

By default, outputs are organized as `build/<debug|release>/artifacts/`, `build/<debug|release>/metadata/`, and `build/<debug|release>/assets/`. Persistent cache records and operation statistics live under each profile's `metadata/cache/`; each target also has metadata recording the compiler, backend, target, profile, signature, and output. A target's `output`/legacy `output_dir` can override its artifact subdirectory.

ZPBuild asks `zp dependencies ENTRY` for the compiler-resolved transitive module graph. This uses the same lexer, parser, relative-path resolution, standard-library lookup, missing-module checks, and cycle checks as compilation; ZPBuild does not parse `use` statements itself. The command emits a JSON array of canonical source paths and is backward-compatible with existing `zp` commands.

A target fingerprint includes ZPBuild/compiler versions, compiler executable hash and resolved path, target configuration, backend/profile/platform, relevant runtime hashes, dependency-target signatures/output hashes, and content hashes of configured target sources plus all resolved imported modules. Asset content is intentionally excluded from compile fingerprints because assets are staged separately and are not compiler inputs. A cache entry is reusable only when its fingerprint, recorded output path, output hash, and staged runtimes all match. Records are atomically replaced and outputs are validated before records are published. Missing/corrupt cache records, changed/deleted imports, modified sources, changed dependencies, missing outputs, or tampered outputs cause a rebuild; failed dependency discovery invalidates the affected target's stale output. Debug and Release remain isolated.

`zpbuild plan [TARGET]` and `zpbuild build [TARGET] --dry-run` report up-to-date targets, rebuild decisions, reasons, and dependency-first order without creating directories, compiling, staging assets, or updating cache statistics. `--no-cache` skips cache reads, cache-record writes, and persistent cache-stat updates for that invocation. `--clean-first` removes the selected profile's generated outputs before building and cannot be combined with a dry run.

`zpbuild cache info` reports cache entry validity, storage size, and persisted hit/miss/rebuild counts. `cache verify` checks each record against its stored output path and SHA-256 and exits nonzero if any record is invalid. `cache clean` removes only profile cache directories; it leaves compiled artifacts and source files in place. Successful and failed build attempts update actual operation counters when cache use is enabled. A per-project OS-level lock prevents concurrent ZPBuild processes from racing on the same outputs.

Custom targets have no declared input/output model in the current configuration, so they always execute, are not cache hits, and are serialized against other targets because their arbitrary commands may modify shared files. Independent compiler targets in the DAG run concurrently in bounded batches; compiler/runtime publication uses target-specific temporary outputs and atomic promotion. The engine does not parallelize compilation within a single executable target because `zp` exposes no object-file or incremental-module compilation API.

Assets are copied into the selected profile's `assets` directory. Only changed files are copied; stale staged files are removed. `preserve_structure = true` preserves paths within each configured source directory; `false` flattens files by name and reports collisions. `package` copies the selected executable and staged assets into `build/<profile>/package/`; on Windows it also includes the graphics runtime DLL when found beside the compiler. Native targets that import `zp.audio` automatically stage the matching audio runtime beside the executable and include it in packages. Projects that do not use `zp.audio` do not stage an audio runtime.

## Testing and troubleshooting

Test targets are regular Z+ programs; a successful process exit is a pass. Use `--verbose` to show successful test output. Compiler diagnostics are forwarded unchanged. `zbuild graph --dot` can help diagnose dependency cycles; missing targets and cycles are errors.

The repository's ZPBuild unit and integration tests run through CTest. The build integration fixture is [zbuild_multi](../test_program/zbuild_multi/), and validates native compilation, compiler-reported transitive imports, parallel target ordering, cache reuse/invalidation/verification/cleaning, dry-run non-mutation, `--no-cache`, profile separation, test execution, asset staging/packaging, and cycle diagnostics. The audio integration test builds and runs a `zp.audio` target and verifies runtime and asset packaging. The template integration test generates and validates every available built-in template, checks custom discovery/aliases/variables and overwrite protection, and builds or runs generated projects where practical.

### Current compiler-driven limitations

- The compiler has no machine-readable project/build API, object files, archive creation, or library linking. ZBuild invokes its stable CLI once per executable/test target.
- Incrementality is at executable/test target granularity. A changed `.zh` module rebuilds only targets whose resolved import graph contains it, but recompiles the complete affected executable; module-level artifact reuse awaits compiler support.
- Custom targets are intentionally always run because their configuration does not declare inputs or outputs. Target dependency edges order them but do not imply file linking.
- Cache use is enabled by default and has no project-level disable/directory setting yet; `--no-cache` is the per-invocation bypass. Cache records remain under the fixed profile-local `metadata/cache/` path.
- Windows is the currently validated host for this release. Linux-specific process/file-lock code remains implemented, but Linux execution and benchmarking must be reported separately from Windows validation.
- There are no compiler optimization/debug-symbol controls, so those requests are validated and rejected rather than silently ignored.
- Legacy `zproject.toml` local dependencies are lock/validation metadata only. ZPM package imports are source modules, not separately linked artifacts.
- Asset configuration is project-wide; target-specific asset rules are not supported.
- Native output is for the host platform. Cross-compilation and target-specific platform matrices are not implemented.
- ZPM resolves local and cached sources only; it does not access a package registry.

These are compiler/build-system integration limits, not claims of supported features. Existing `zp` commands and standalone source builds remain unchanged.
