<p align="center">
  <img src="logo/Z%2B%20Emblem%20-%20for%20ligth%20theme.png" width="460" alt="Z+ emblem">
</p>

# Z+

Z+ is a small compiled language with a lexer, parser, semantic analyzer, shared intermediate representation, interpreter, and an x86-64 native backend.

The current compiler release is **1.2.0 Stable**. The Windows x64 per-user
installer and release build instructions are documented in
[installer/inno/README.md](./installer/inno/README.md).

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The CTest suite includes the root `test.zp` feature tour. It runs under the
interpreter on every host and compares interpreter/native output on Windows x64.

The compiler executable is `build\Release\zp.exe` on Windows Visual Studio builds and `build\zp` on Linux.
Every successful Release build also synchronizes that executable to `bin\zp.exe` on Windows or `bin/zp` on Linux. Debug builds skip the copy. The synchronization runs after the compiler target succeeds and replaces the destination only with the Release artifact.

The standalone Z+ project build system is ZPBuild 0.3.0, invoked as `zpbuild` (`zpbuild.exe` on Windows); `zbuild` remains available as a compatibility alias. It supports compiler-resolved dependency tracking, incremental target builds, bounded parallel execution, build planning, and persistent cache inspection. ZPM 0.1.0 adds local package management and is copied alongside the compiler and build tools on Release builds. See [ZBUILD.md](./docs/ZBUILD.md) for project builds and [ZPM.md](./docs/ZPM.md) for the local package workflow and online-registry limitations.

## Use the compiler

```powershell
build\Release\zp.exe check main.zp
build\Release\zp.exe run main.zp
build\Release\zp.exe build main.zp -o main.exe
```

`check` validates the complete module graph, `run` executes through the interpreter, and `build` creates a native executable for the host. Native compilation supports only the subset listed in [NATIVE_BACKEND.md](./NATIVE_BACKEND.md) and never falls back to the interpreter.

The `read()`, `read_number()`, `read_integer()`, and `read_boolean()` terminal
input built-ins are available without imports in the interpreter and native
Windows programs. Linux native support is implemented but has not been
execution-tested in this environment. See
[the interactive input example](./examples/interactive_input.zp) and
[SYNTAX.md](./SYNTAX.md#10-built-in-functions) for parsing and EOF behavior.

The generic `length(array)` and numeric `to_string(number)` built-ins are
available without imports. The interpreter supports them directly; Windows
x64 native programs support both through the packaged runtime. See the
[built-in function reference](./SYNTAX.md#10-built-in-functions).

Z+ 1.2.0 supports generic functions, structs, and classes with inferred or
explicit type arguments. Concrete specializations are generated for both the
interpreter IR and Windows x64 native backend. The `zcollections` module
provides generic `List`, `Stack`, `Queue`, `Set`, `Map`, and `Optional` types;
see [the syntax reference](./SYNTAX.md#generic-declarations-and-types) and
[standard-library documentation](./stdlib/README.md#zcollections-as-collections).

## Modules

Import `.zh` declarations with a module-relative `use`. Modules are private by default; `pub` exposes declarations. Use the filename-derived module name or an explicit alias:

```zp
use "stdlib/math/math.zh" as math

start {
    say(math.clamp(22, 0, 10))
} stop
```

Paths are resolved relative to the file containing each `use`, not the process working directory. Imports can nest and repeated references to the same canonical file load it once. Absolute module paths are denied. `.zh` files must have one `start` block containing declarations only; the loader extracts those declarations without executing the block. See [MODULES.md](./MODULES.md) for graph behavior, visibility, diagnostics, and limitations.

## Standard library

The standard library in [stdlib/](./stdlib/) includes math, strings,
collections, file/path/system APIs, JSON, HTTP, SQLite, graphics, audio,
physics, and installer integrations. Backend and platform support varies by
module; see [stdlib/README.md](./stdlib/README.md) for the API inventory and
limitations.

## ZGraphics

The `zgraphics` module provides real Win32/GDI windows and drawing on Windows, and X11 windows through the interpreter on Linux. On Windows, `zp build` links calls to the packaged native runtime and places `zgraphics_runtime.dll` beside the generated executable. Distribute both files together. Linux native graphics linking, native controls, and callbacks are not implemented. See [ZGraphics/README.md](./ZGraphics/README.md) and the runnable-source examples in [examples/zgraphics/](./examples/zgraphics/).

The VS Code extension is in [Z+_VS_code](./Z+_VS_code); compiler syntax details are in [SYNTAX.md](./SYNTAX.md).
