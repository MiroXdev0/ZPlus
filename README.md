# Z+

Z+ is a statically typed programming language implemented in C++20. The compiler includes a lexer, parser, semantic analyzer, shared intermediate representation, interpreter, and an x86-64 native backend. The compiler and first-party materials in this repository are licensed under Apache License 2.0; third-party components retain their own licenses. See [LICENSE](./LICENSE) and [NOTICE](./NOTICE).

The compiler source version in this checkout is **1.4.0**. The latest stable release metadata may describe an earlier version; consult the official [release page](https://github.com/MiroXdev0/ZPluse-release/releases) before downloading binaries.

## Capabilities

- Typed variables, functions, control flow, arrays, classes, visibility modifiers, enums, structs, and constrained generics.
- Interpreter execution and native x86-64 compilation. Supported features differ by operating system and backend; see [NATIVE_BACKEND.md](./NATIVE_BACKEND.md) and [SYNTAX.md](./SYNTAX.md).
- File-based `.zh` modules and `use` imports.
- Standard library modules for text, files, paths, math, time, JSON, HTTP, SQLite, graphics, audio, physics, and other integrations. Availability depends on platform and dependencies.
- ZPBuild project build orchestration and ZPM package tooling.

## Build from source

Requirements: CMake 3.20 or newer, a C++20 compiler, Git, and platform development libraries. Linux builds require libcurl and X11 development files. CMake fetches pinned Box2D and miniaudio source versions during configuration, so the first configure requires network access. Windows builds use the Visual Studio C++ toolchain. Optional installer and distribution tools have their own requirements documented under `installer/`.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On single-configuration generators, omit `--config Release` from build and test commands. The compiler is `build\Release\zp.exe` on Windows Visual Studio builds and `build/zp` on Linux. ZPM and ZPBuild targets are `zpm` and `zpbuild`; `zbuild` is retained as a compatibility alias.

## Run a program

```zplus
start {
    say("Hello, Z+!")
} stop
```

Save as `hello.zp`, then:

```text
zp check hello.zp
zp run hello.zp
zp build hello.zp -o hello
```

`check` validates the module graph, `run` uses the interpreter, and `build` emits a native executable only for the selected platform’s supported subset. Native compilation does not fall back to interpretation.

## Modules and tools

Import relative `.zh` modules with `use`, for example `use "helpers.zh" as helpers`. Module paths resolve relative to the importing file. Public exports use `pub`. See [MODULES.md](./MODULES.md).

- [Language syntax](./SYNTAX.md) and [standard library](./stdlib/README.md)
- [ZPBuild](./docs/ZBUILD.md) and [ZPM](./docs/ZPM.md)
- [ZGraphics](./ZGraphics/README.md)
- [Windows installer](./installer/inno/README.md)

## Contributing

Read [CONTRIBUTING.md](./CONTRIBUTING.md) before opening a pull request. Bug reports and feature proposals should include a minimal source example, the exact command, operating system, backend, compiler version, and observed versus expected result. Security issues should follow [SECURITY.md](./SECURITY.md).

## License

First-party Z+ source, documentation, tests, examples, and tools in this repository are offered under Apache License 2.0. Third-party dependencies, embedded resources, and historical release payloads may carry separate terms; consult [NOTICE](./NOTICE) and the applicable per-component notices. Z+ names and logos remain trademarks and are not licensed by Apache-2.0.
