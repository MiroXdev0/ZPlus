# Native backend

`zp build` compiles the existing lexer, parser, semantic analysis, and shared IR into a host-native executable. `zp run` remains the interpreter path; `zp check` still validates without executing.

```powershell
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

build\Release\zp.exe build program.zp
build\Release\zp.exe build program.zp -o program.exe
program.exe
```

The default output is `program.exe` on Windows and `program.out` on Linux. `-o`/`--output` overrides that path. Native compilation errors are reported; the compiler does not fall back to the interpreter.

On Linux, the corresponding commands are `cmake --build build`, `ctest --test-dir build --output-on-failure`, `build/zp build program.zp`, then `./program.out`.

## Current supported subset

The backend emits actual x86-64 machine code and executable containers. Its supported IR subset includes:

- IEEE-754 binary64 `number` values, including literal loads/stores, SSE2 addition, subtraction, multiplication, division, negation, and comparisons. Numeric output, runtime-library conversions, and fractional physics/audio/math calls are supported through the Windows x64 runtime. Floating remainder uses `fmod` through that runtime. Bitwise operations and shifts convert operands to signed integers and are intended for integer-valued numbers.
- Boolean values, local and module variables, function parameters and return values, and calls to defined Z+ functions. Windows internal calls use positional XMM registers for floating-point arguments and XMM0 for floating-point returns; integer/pointer values use the Microsoft x64 convention. Linux internal calls use separate System V integer and SSE argument classes, but Linux execution has not been validated on this host.
- Conditional branches, `while`, `for`, `repeat`, `switch`, `break`, and `continue` when lowered to the supported IR branch instructions.
- Windows x64 arrays: fixed and dynamically growing array literals (including empty and nested arrays), indexed reads/writes, compound indexed assignment, `length`, `push`, `pop`, `clear`, `insert`, `remove`, `contains`, and array arguments/returns with shared mutation semantics. Append growth is geometric; capacity is retained when logical length shrinks. Runtime checks reject invalid indices and popping an empty array.
- Windows x64 structs: managed references with positional construction, typed/defaulted fields, reads/writes, nested struct and array references, function arguments/returns, identity comparisons, and `is` checks. Struct and array references share the native tagged-object tracing heap; native regression tests force collection and verify that reachable nested references survive.
- Windows x64 classes: managed instances with positional construction, typed fields, literal defaults, field reads/writes, instance methods, `this`, and function arguments/returns. Inheritance and explicit constructors are not implemented.
- Generic functions, structs, and classes: used type combinations are specialized into concrete IR before native code generation. Nested generic types, generic class methods, inferred/explicit arguments, and the generic collection classes are covered by interpreter/native parity tests. Expanding recursive specialization is rejected. Linux native generic execution is not claimed or verified.
- Windows x64 enums: typed numeric members with equality/comparison and function argument/return support; explicit member values must be numeric literals.
- `say(boolean)` prints `true` or `false`, matching interpreter output.
- `to_string(number)` uses the native runtime's round-trip decimal formatter and returns a managed string.
- String/character literals and local string values passed to `say()`, `printz()`, or the deprecated `printm()` alias. Windows native output accepts strings and numbers; `say()` adds a line feed after its final value. On Windows, string concatenation is supported for two string operands through the native runtime.

Every local and IR temporary currently receives a stack slot; this is a simple reliable spill-all strategy rather than a register allocator. Numeric stack slots contain the 64-bit IEEE-754 payload. Native formatting uses round-trip decimal conversion and normalizes NaN and infinity spellings.

The backend rejects unsupported IR explicitly. In particular, pointers/references, arbitrary external functions, and floating-point output through the Linux syscall-only runtime are not implemented. Struct and class construction/member access execute in the interpreter and Windows x64 native backend; Linux native aggregate operations are rejected rather than falling back to interpretation. Windows native arrays, structs, and classes use the packaged runtime's non-moving tracing heap. Tagged handles remain stable; generated functions register active local slots, and the entry registers module globals. The collector traces nested array, aggregate, and managed-string references; numeric array elements remain untagged double payloads. Array/aggregate creation and array capacity growth temporarily root input values. Bounds failures and invalid array operations terminate with a descriptive native runtime error. Linux native array and managed-memory operations are rejected explicitly. Module variables/constants have one 64-bit native storage slot each; initialization runs through a generated module-initializer function before the entry function.

Native `try`, `catch`, `throw`, and `defer` are supported for direct Z+ function
calls, including nested handlers, caller-side propagation, rethrowing, early
returns, and LIFO cleanup on normal and exceptional exits. Native runtime API
failures from the Windows graphics/standard-library runtime enter the same
exception path. The shared regression fixture
`tests/runtime/error_handling.zp` is executed by both interpreter and Windows
native backends and checks matching output. Uncaught native exceptions are
reported and exit with status 1. Linux exception/defer execution has not been
validated on a Linux host.

On Windows x64, the complete `zgraphics` module is callable from native Z+ programs. Native builds import the real ZGraphics C++ implementation through a small C ABI adapter; the interpreter remains unchanged. Each output directory receives `zgraphics_runtime.dll` beside the generated `.exe`. Keep the two files together when running or distributing the application. The DLL statically links the MSVC C++ runtime and uses only Windows system libraries (Kernel32, User32, and GDI32). The executable is a native PE console-subsystem program that opens actual desktop windows; it does not interpret source at runtime. Errors from this runtime are converted to native Z+ exceptions when a generated exception slot is available; otherwise the runtime reports them and exits. Uncaught Z+ exceptions are reported by the generated executable and exit with status 1. Audio and physics runtime errors still use their runtime-specific failure reporting. The backend does not silently fall back to `zp run`.

The Windows x64 runtime also implements the standard-library calls documented
in [stdlib/README.md](./stdlib/README.md), including numeric math, random
values, collections, JSON numbers, and numeric output. Runtime imports pass
numeric arguments as their IEEE-754 bit patterns; APIs that require integer
indices or counts validate and convert those values explicitly. `zargs` excludes
the executable name and reads the native process command line; the interpreter
form is `zp run file.zp -- arg...`. The DLL uses Shell32 for Windows command
line parsing in addition to the documented system libraries.

The canonical Z+ `number` representation is IEEE-754 binary64 in the interpreter
and in native numeric slots. Native `say`, `printz`, and `printm` format numeric
arguments on Windows. NaN comparisons follow IEEE-754 unordered-comparison
rules, including `NaN != NaN`. Native runtime imports, managed arrays, and
numeric output are not yet available on Linux; Linux platform parity has not
been verified. Linux native ELF generation and dynamic runtime linking exist,
but actual Linux execution has not been verified in this Windows environment.
Structural ELF checks alone are not runtime validation.

Native ZGraphics is Windows x64 only. Linux `zp build` continues to reject ZGraphics and native string concatenation with an explicit diagnostic. The graphics module itself still has the same limited controls, event loop, and drawing features on both interpreter and native paths.

If a zero-parameter Z+ function named `main` is declared, it is the native process entry and its integer return value becomes the process exit code. Otherwise, the generated top-level entry runs and returns zero by default. The interpreter retains its own execution path and prefers the same user `main` when present.

## Executable formats and calling conventions

### Windows x86-64

The backend writes a PE32+ console executable with `.text` and writable `.data` sections. Kernel32 imports (`ExitProcess`, `GetStdHandle`, and `WriteFile`) provide process exit and text output. The image is fixed at its preferred base and has no relocation table; dynamic-base/ASLR is disabled. Internal user calls follow the Microsoft x64 convention: the first four argument positions use `RCX`, `RDX`, `R8`, and `R9` for integer/pointer values and the corresponding `XMM0`-`XMM3` registers for `number`; floating returns use `XMM0`, integer/pointer returns use `RAX`. Call sites reserve shadow space and maintain 16-byte stack alignment. Standard-library native calls are provided by `zgraphics_runtime.dll`.

### Linux x86-64

The backend writes an ELF64 `ET_EXEC` image with separate read-only header/data
mappings and an executable code mapping. Programs without runtime imports use
Linux `write` and `exit` syscalls and need no dynamic loader or libc. Audio and
physics imports use the host's supported x86-64 dynamic loader and the
`libzaudio_runtime.so` / `libzphysics_runtime.so` shared libraries, copied
beside the executable and resolved through `$ORIGIN`. User calls follow the
System V AMD64 convention, with integer/pointer and SSE argument registers
allocated independently. Linux native execution is not validated on this host.

## Validation status

The CTest suite builds and executes Windows PE fixtures for floating-point arithmetic, formatting, comparisons, special values, generic functions/structs/classes/collections, function arguments/returns, arrays and collections, standard-library runtime bindings, physics/audio, branching and loops, module initialization, and interpreter/native exception-defer parity. It also checks runtime error reporting, unsupported IR, runtime packaging, and PE/ELF structure. Linux ELF execution must be tested separately.

Linux ELF execution must be tested on a Linux host. Structural ELF checks alone
do not claim that Linux runtime execution has been validated.
