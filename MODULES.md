# Z+ Modules

Entry programs use `.zp`; reusable source modules use `.zh`. A `use` statement is parsed as a language declaration and resolved by the compiler's module loader:

```zp
use "math.zh"
use "modules/network.zh" as net

start:
    say(math.add(10, 20))
    say(net.status())
stop
```

`use path.zh` accepts an unquoted path made from identifier/number segments, dots, hyphens, and slash separators. Quoted paths also support spaces. Both `/` and `\` are normalized on the host platform.

## Resolution and dependency graph

- Each path is resolved from the directory of the file that contains the `use`, including nested `.zh` modules. The working directory does not affect relative resolution.
- Modules must exist and use the `.zh` extension. Absolute, drive-prefixed, and UNC paths are rejected; there is no override for this policy.
- Canonicalized paths are module identities. The loader parses each identity once and traverses dependencies depth-first in source order before processing the importing module.
- Repeated uses of the same file, including through a transitive dependency, do not duplicate declarations. Cycles fail with a diagnostic containing the dependency chain.
- A default module name is derived from the filename stem. Non-identifier characters are replaced with `_`; directory names do not add qualification. Use `as alias` to choose another identifier.
- Different files that derive the same module name are ambiguous and rejected. A single file requested under conflicting aliases and duplicate aliases are also rejected.

## Names and visibility

Declarations in a `.zh` module are private by default. `pub` makes functions, variables/constants, structs, classes, and enums accessible outside their declaring module:

```zp
pub fn add(left: number, right: number) -> number:
    return left + right
end

fn internalHelper() -> number:
    return 1
end
```

An importer can reference public functions, constants, and variables with the module name or alias, such as `math.add(1, 2)` or `math.answer`. Public declarations also remain available by unqualified name for backward-compatible convenience when that name is unambiguous. Private declarations are only accessible inside their declaring module; external qualified or unqualified access is diagnosed. Public types can be used by their unqualified type name. Type syntax does not currently support dotted/qualified custom type names.

Public structs can be constructed by their unqualified type name after importing the declaring module. Struct construction, fields, and runtime type checks currently execute in the interpreter only; native builds reject the generated member operations. Private type names and ambiguous public type names remain subject to the same module visibility checks.

The loader preserves the source location and owning module on declarations. Duplicate names and ambiguous unqualified public names are diagnosed rather than silently selected. `export` and legacy `import` are older parser constructs; they do not replace `pub` or load a source file.

## Module initialization and backends

Modules are declaration-only. A `.zh` file containing a `start` block (including one nested in another declaration) or executable top-level statements is rejected. Imported modules do not get independent entry points.

Module-scope variables and constants are initialized once in deterministic dependency-first order before the entry function. They share storage with imported functions in the interpreter and are emitted as native globals when their initializer and use fit the native backend's supported scalar subset. Unsupported native IR is an error; `zp build` never falls back to interpretation. See [NATIVE_BACKEND.md](./NATIVE_BACKEND.md).

## Standard-library modules

The compiler resolves standard-library names from its configured `stdlib/`
directory, not relative to the process working directory:

```zp
use zmath as math
use zstring as text

start:
    let result: number = math.sqrt(144)
    say(text.upper("ready"))
stop
```

The aliases qualify public declarations and are resolved by the analyzer.
Aliases currently share the flattened program namespace: duplicate aliases
conflict, and the same file cannot be imported under different aliases. Alias
scope is not isolated per importing module. See
[stdlib/README.md](./stdlib/README.md) for the actual module APIs and their
backend differences. The original source-level `stdlib/math/math.zh` remains
available for its `abs`, `min`, `max`, and `clamp` functions.

## Editor and CLI

The compiler discovers the graph for all entry commands:

```powershell
zp check main.zp
zp run main.zp
zp build main.zp
```

JSON diagnostics identify the original `.zp` or `.zh` file, line, and column. The VS Code extension registers both extensions, routes compiler diagnostics to the owning file, and provides public imported-symbol completions, module-qualified completions, and navigation to imported declarations.

## Complete project layout

```text
Project/
  main.zp
  header.zh
  modules/
    math.zh
```

`header.zh`:

```zp
pub fn greet(name: string):
    say("Hello " + name)
end
```

`modules/math.zh`:

```zp
pub fn add(left: number, right: number) -> number:
    return left + right
end
```

`main.zp`:

```zp
use header.zh
use "modules/math.zh" as math

start:
    greet("Z+")
    say(math.add(10, 20))
stop
```
