# Z+ language evolution: audit and implementation plan

## Scope and compatibility contract

This plan is based on the Z+ compiler, runtime, native backend, standard
library, graphics integration, and VS Code extension in this repository. It is
an incremental plan, not a rewrite or a claim that planned language or
embedding features already exist.

Existing `.zp` programs, `.zh` modules, interpreter behavior, native executable
generation, CLI commands, and ZGraphics APIs remain compatibility constraints.
Changes that expand a feature must have regression tests covering existing
programs as well as tests for the newly supported behavior. `zp build` must
continue to reject unsupported native operations explicitly; it must not fall
back to interpretation.

## Architecture and current status

| Area | Current implementation | Limits established by the audit |
|---|---|---|
| Lexer and tokens | `compiler/lexer/Lexer.cpp` and `Token.hpp` tokenize the existing keywords, operators, numbers, strings, characters, and comments. | Several reserved tokens do not have working syntax or runtime behavior. Binary numeric spellings are not decoded by the interpreter. |
| Parser and AST | `compiler/parser/Parser.cpp` and `AST.hpp` parse declarations, scalar expressions, functions, control flow, arrays, member/index access, modules, and aggregate declarations. | Some accepted syntax is not executable. `if` requires an `else`; there is no function-literal or named-argument syntax. |
| Semantic analysis | `compiler/semantic/Analyzer.cpp` checks primitive and aggregate values, generic functions/types, constrained type parameters, recursive substitution, calls, arrays, control flow, and module visibility using the shared `Type` model. | Constraints are limited to `number` and `equatable`; there is no user-defined trait system, function overloading, or closure typing. |
| Shared IR | `compiler/ir/IRBuilder.cpp` lowers programs into typed shared IR and specializes used generic functions and aggregate types before verification. The verifier checks calls, returns, result types, and control flow. | Generic templates are compile-time constructs and are not executable IR; concrete specializations are required. |
| Interpreter | `runtime/Runtime.cpp` executes the shared typed IR with numbers, strings, booleans, arrays, structs/classes, generic specializations, and managed values; tracing collection roots module globals and active frames. Runtime failures include instruction locations and function frames when available. | There is no host-function registry or general C embedding ABI. |
| Native backend | `compiler/backend/native/` emits x86-64 PE on Windows and ELF on Linux, and rejects unsupported IR rather than interpreting it. Windows native fixture execution covers generic specializations and collections. | Windows x64 generic execution is tested. Linux ELF generic execution was not validated on this Windows host. |
| Modules | `compiler/ModuleLoader.cpp` resolves relative `.zh` dependencies, aliases, visibility, cycles, and module initialization. | This is a source-module loader, not a package manager, extension loader, or capability system. |
| Standard library and graphics | The source library includes math, strings, collections (including generic `List`, `Stack`, `Queue`, `Set`, `Map`, and `Optional`), file/path/system, JSON, HTTP, SQLite, memory, and ZGraphics APIs. | Generic collections and managed arrays are supported by the interpreter and Windows x64 backend. Linux native generic/runtime execution is not verified; ZGraphics is not a general host-extension API. |
| CLI and diagnostics | `compiler/main.cpp` implements `check`, `run`, `build`, `version`, and `help`, including positioned and JSON diagnostics. | There are no scripting-runtime or extension-management commands. |
| VS Code extension | `ZPlus_VS_code_extension/` provides compiler-backed checks, completion, hover, signature help, definition navigation, snippets, and module-aware editor behavior. | Editor support follows the compiler's implemented syntax and cannot make incomplete runtime features executable. |
| Host integration | The only existing C ABI is the ZGraphics-specific adapter in `runtime/graphics/NativeApi.cpp`. | A general, stable C embedding API and host-function registration interface are not implemented. Nodren-specific SDK and host integration are explicitly out of scope. |

The public syntax reference, `docs/syntax.md`, documents accepted-but-incomplete
constructs and is the current support matrix. Do not infer executable support
from successful parsing, semantic analysis, or IR generation alone.

## Baseline recorded for this audit

Validation on Windows:

- Compiler and graphics CTest suite: 7/7 passed, including native executable,
  module, and ZGraphics tests.
- VS Code extension tests: 17/17 passed.
- `examples/hello.zp`, `examples/variables.zp`, `examples/functions.zp`, and
  `test_program/pixel_escape.zp` passed `check`; the three console examples
  passed interpreter execution.
- Installed `bin/zp.exe version`: 0.5.0.
- No garden-report fixture or Nodren project/API was found in the workspace.
- Linux ELF execution was not tested; this Windows environment cannot establish
  Linux runtime compatibility.

The baseline is a starting point for regression testing, not certification of
features documented as partial or unsupported.

## Current milestone update

The first runtime-correctness slice and interpreter arrays are implemented:

- Calls may omit trailing parameters with declared defaults. Defaults are
  type-checked, may refer to earlier parameters, are evaluated only when
  omitted, and work for forward-declared callees. Calls with a required
  argument missing remain errors; a required parameter after a default is
  rejected.
- `and`, `or`, `&&`, and `||` now lower to control flow, so side effects and
  errors on an unneeded right operand are skipped. The shared IR preserves the
  same behavior for the interpreter and native code generator.
- Native and interpreter tests cover chained/partial/explicit defaults,
  forward calls, required-argument validation, both logical spellings,
  skipped side effects, and evaluation of the right operand when required.
- IR instructions retain AST source locations. Interpreter runtime failures
  report the failing source location and call frames when available, including
  failures originating in imported modules.
- The interpreter supports nested reference-like arrays, empty typed arrays,
  indexing and mutation, array arguments and returns, and `length`. Semantic
  checks reject incompatible array elements and invalid index/member uses.
- Lexically resolved variable declarations and parameters now have unique
  binding identities in the AST/IR. Inner declarations no longer overwrite
  outer variables; references and assignments use the binding selected by
  semantic analysis. Branches and loop bodies can independently shadow names,
  and block-local names remain semantically unavailable after their scope.
- Top-level variables are initialized in the module-global environment, so
  functions can access them. A binding integration fixture covers globals,
  parameters, defaults, forward calls, recursion, return values, and repeated
  calls.
- `use zp.memory.management` exposes heap statistics, collection, and a safe
  threshold setting. The interpreter uses a non-moving tracing mark-and-sweep
  heap for arrays and dynamically concatenated strings, with module globals
  and active frames as roots. Windows x64 native programs use the same API for
  arrays and dynamic strings with explicit global and active-frame slot roots.
  Other interpreter strings remain C++ value-owned; Linux native array and
  memory integration are unsupported.
- Regression tests prove nested arrays and imported globals survive
  collection, unreachable arrays and strings are reclaimed, active call frames
  retain values, and repeated allocation triggers collection. Native tests
  check empty and initialized arrays, mutation, nested/function-passed arrays,
  invalid indices, and string references retained through collection.

Lexical binding regression tests cover nested scopes, outer assignments,
shadowed function parameters and locals, separate branches, repeated loop
execution, array mutation in nested scopes, and rejection of an out-of-scope
reference. The native backend passes equivalent scalar scoping tests.

Verification of the latest scoping change: Release build succeeded; focused
CTest passed for interpreter runtime/array/diagnostic cases and native
executable integration. The final CTest suite passed 8/8; VS Code extension
tests passed 17/17; the three existing console example commands exited
successfully (the checked-in example files are empty); Pixel Escape passed
`check`. `bin/zp.exe version` reports 0.5.0 and matches the Release executable
with SHA-256
`20745A14AA7BB5F218D26A616DA3249CAB543151A509446B5797D0BF0E0C990A`.
Linux ELF execution is unavailable on this Windows host.

## Milestones

### 0. Audit and compatibility baseline — complete

Keep the baseline tests and support matrix current. Add fixtures for existing
`.zp` and `.zh` behavior before any syntax or runtime semantic changes. The
requested garden-report program is absent; add it to the compatibility suite
only when its source is provided.

### 1. Runtime and type-system foundation — complete for the defined milestone

Make the active semantic type model and runtime value representation explicit
and testable without changing existing scalar semantics. Add focused regression
tests for error propagation, argument binding, nested scopes, and interpreter
versus native diagnostics. Keep the existing IR as the shared boundary; do not
add a second interpreter-only language pipeline. The first implementation
slice now applies trailing default arguments in both backends and lowers
`and`/`or`/`&&`/`||` using short-circuit control flow. Argument defaults and
short-circuit behavior have interpreter and native executable regression
coverage.

The milestone now includes source-aware runtime failures and call frames,
interpreter array execution and validation, Windows x64 native arrays, global
access from functions, and stable lexical binding identities. Linux native
array operations remain explicitly unsupported rather than falling back to
interpretation.

Exit criteria met for this milestone: targeted interpreter/native binding and
array regressions pass, existing tests and console output remain unchanged,
and unsupported constructs fail clearly rather than producing fabricated
values. Broader runtime features remain tracked in later milestones.

### 2. First complete collection feature — interpreter and Windows x64 native

Array literals, nested arrays, indexing, indexed assignment, typed empty
arrays, array argument/return behavior, and length are implemented and covered
in interpreter tests. Arrays use shared reference-like mutation semantics.
The Windows x64 native backend now implements these operations with managed,
tagged array handles and a tracing runtime integrated with `zp.memory.management`.
Native arrays preserve aliasing, nested references and bounds checking. Linux
native builds explicitly reject managed array operations because there is no
Linux native managed-memory runtime.

Regression coverage compares interpreter and native output, proves reachable
arrays survive collection and unreachable arrays are reclaimed, exercises
reads/writes and invalid indices, and builds/runs NEXUS as a native Windows
application. Array comparison and dynamic resizing are not supported.

### 3. Aggregate and collection library expansion

Build on the verified value model to implement only a bounded, coherent subset
at a time: dynamic lists/maps, then usable structs and enums. Specify
construction, field access, mutation, equality, and printing semantics before
adding syntax. Add collection iteration only after the runtime representation
and mutation rules are stable. Do not expose empty standard-library modules.

Exit criteria: each published API has executable interpreter tests and
documented error behavior; unsupported native cases remain explicit.

### 4. Runtime errors and diagnostics

Source locations through IR and runtime execution and call-stack diagnostics
are implemented for tested interpreter runtime errors. Continue broadening
coverage and define a deliberate error-propagation design. Choose
typed errors, `Result`, or exception syntax only after validating the existing
parser, type model, and both execution backends. Do not silently convert errors
to default values.

Exit criteria: errors identify the source operation and call chain where
available; tests cover both expected failures and cleanup after failure.

### 5. Scripting and embedding ABI

Design an opaque, versioned C ABI separate from the ZGraphics-specific
`runtime/graphics/NativeApi.cpp`. Define runtime ownership, value ownership,
callback calling conventions, thread/lifecycle rules, and error retrieval
before implementation. Add create/destroy, compile/load, execute, host
function registration, value exchange, and script function invocation as
independently testable capabilities. Add script arguments and controlled
environment access only through explicit runtime APIs.

Exit criteria: C and C++ host fixtures exercise repeated execution, callback
and value lifetimes, failures, and clean shutdown without relying on internal
C++ types.

### 6. Scripting runtime and embedding ABI

Improve repeated script execution and explicit script arguments/environment
access, then design an opaque, versioned C ABI separate from the
ZGraphics-specific adapter. Define runtime/value ownership, callback calling
conventions, thread/lifecycle rules, capability configuration, and error
retrieval before implementation. Add compile/load/execute, host-function
registration, value exchange, and script function invocation as independently
tested capabilities.

Exit criteria: C and C++ host fixtures exercise repeated execution, callback
and value lifetimes, failures, capability denial, and clean shutdown without
relying on internal C++ types.

Nodren-specific SDKs, host APIs, extension lifecycle, and packaging are out of
scope; the embedding interface must remain application-neutral.

### 7. Runtime capability controls

Give embedding hosts explicit control over filesystem, process, and network
capabilities before publishing those standard-library APIs. Add practical
resource limits and cleanup tests where supported by the runtime architecture.

Exit criteria: restricted runtimes deny ungranted operations predictably and
release resources after success or failure.

### 8. Native backend parity

Extend native code generation only for features already specified and
implemented in the interpreter. Keep explicit unsupported-operation
diagnostics and add executable Windows tests for each new operation. Run ELF
execution tests on a Linux host before claiming Linux runtime support for new
features.

Exit criteria: documented native support matches executed tests; no silent
interpreter fallback.

### 9. Standard library, editor, documentation, and release

Publish standard-library modules only after their underlying runtime APIs,
permissions, and failure modes are real. Update compiler diagnostics, VS Code
grammar/providers, examples, and documentation alongside each shipped feature.
Run full compiler, graphics, editor, compatibility, embedding, and
platform-available native tests before a release build. Replace `bin/zp.exe`
or `bin/zp` only after a successful Release build and verify the installed
version.

## Immediate next implementation scope

The array/native-heap milestone is complete for the interpreter and Windows
x64. Continue with the next bounded runtime/type feature only after selecting
and specifying it. Linux native array and managed-memory support remains
unimplemented; do not claim support until generated executables have been run
successfully. Do not add Nodren-specific features.
