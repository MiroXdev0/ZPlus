# Z+ Syntax Reference

## Introduction

This reference describes the language accepted by the Z+ 1.3.3 command-line compiler in this repository. It follows the lexer, parser, semantic analyzer, IR builder, and interpreter—not proposed or aspirational features.

The status labels used below mean:

- **Implemented:** accepted by parsing and semantic analysis, lowered to IR, and executable by `zp run`.
- **Partial:** some compiler stages support it, but a later stage is missing or behavior is incomplete.
- **Parsed only:** syntax is recognized, but it does not pass the complete compiler pipeline.

`zp check` runs lexing, parsing, semantic analysis, and IR generation. It does **not** execute the program. `zp run` performs those same steps and then runs the interpreter.
`zp build (file) -o (file name)` builds the file into a `.exe`.

## Diagnostics

Source diagnostics retain the file, line, and column reported by the lexer and
parser. Human-readable errors show the source line and a caret at that
location. Parser errors identify the unexpected token as well as the expected
construct. Diagnostic codes identify the compiler stage:

| Code | Stage |
|---|---|
| `E1000` | Lexing |
| `E1001` | Parsing |
| `E2001` | Semantic analysis |
| `E3001` | Module loading and imports |
| `E4001` | Intermediate representation |
| `E5001` | Native backend |
| `E6001` | Runtime execution |
| `E0001` | Command-line and other compiler errors |

Use `--diagnostics=json` to emit one JSON diagnostic per line for editor
integrations. Each record includes `severity`, `code`, `message`, `file`,
`line`, and `column`. A location is shown only when the compiler has a source
position; command-line and runtime errors without a source span do not invent
one.

## 2. Hello World

```zp
start {
    printz("Hello, world!")
} stop
```

`start {` begins the program's explicit entry block; `} stop` closes it. `printz` is the preferred output function and writes one or more values without adding a newline. `say` writes one value followed by a newline. **Implemented.**

## 3. Program Structure

Every `.zp` program has exactly one top-level `start { ... } stop` block.
Functions, namespaces, classes, enums, and structs belong directly inside that
block (or inside a namespace). Executable statements cannot appear at file
scope. `use` and legacy `import` directives remain at file scope so module
dependencies can be resolved before semantic analysis.

```zp
start {
    fn announce(message: string) {
        say(message)
    } end fn

    announce("Ready")
} stop
```

Nested functions and type declarations inside executable blocks are rejected.
Namespace members can contain declarations; namespace functions are called by
their qualified name. Direct declarations in `start` are registered before
entry statements are analyzed and lowered, so function forward references are
supported. A zero-parameter function named `main` inside `start` remains the
entry point when present; otherwise the statements in `start` execute.

Imported `.zh` files use the same declaration container: one `start` block
containing declarations only. The loader extracts those declarations as module
members; it does not execute the module's block. `use` directives stay outside
that block.

### Official brace syntax and migration

`{` opens a block and `}` closes its body. The matching terminator is required after the closing brace: functions use `end fn`, conditionals use `end if`, loops use their corresponding `end while`, `end for`, or `end repeat`, switch uses `end switch`, and `start` uses `stop`. `else` and `else if` continue the same `if` and do not have their own terminator. Semicolons are not required between ordinary statements.

```zp
start {
    fn calculate(a: number, b: number): number {
        return a + b
    } end fn

    let score = calculate(40, 50)
    if score >= 90 {
        say("Excellent")
    } else if score >= 70 {
        say("Good")
    } else {
        say("Needs improvement")
    } end if

    while score < 100 {
        score++
    } end while

    for index in 0..3 {
        say(index)
    } end for

    let values = [7, 8]
    for value in values {
        say(value)
    } end for
} stop
```

Range iteration is start-inclusive and end-exclusive (`0..3` visits 0, 1, and 2). The start and end expressions are evaluated once. Array iteration evaluates the collection once and visits its elements in index order. C-style `for (initializer; condition; increment) { ... } end for` remains supported as well. `switch` retains its existing `case`/`default` behavior; `match` is not implemented. `try`/`catch` and `defer` support braces and require `end try` and `end defer`, respectively.

Each block must use one style throughout: do not nest colon/`end` blocks inside brace blocks or vice versa. Struct, class, enum, and namespace declarations use braces and matching explicit terminators as well. Terminator mismatches and missing braces/terminators are parser errors that identify the opened block where available. Declarations outside `start` and multiple or missing top-level `start` blocks are compile-time errors.

Migration is structural, not a text replacement. For each old block, replace its colon with `{`, its matching generic `end` with `} end <block-kind>`, and keep `else` within the `if`. Replace `start:` / `stop` with `start {` / `} stop`. Keep old syntax unchanged when not migrating; both forms remain accepted, but not mixed within one block.

| Legacy form | Brace form |
|---|---|
| `fn f(): ... end` | `fn f() { ... } end fn` |
| `if condition: ... end` | `if condition { ... } end if` |
| `while condition: ... end` | `while condition { ... } end while` |
| `for (...): ... end` | `for (...) { ... } end for` |
| `switch value: ... end` | `switch value { ... } end switch` |
| `start: ... stop` | `start { ... } stop` |
| `struct Name: ... end` | `struct Name { ... } end struct` |
| `class Name: ... end` | `class Name { ... } end class` |
| `enum Name: ... end` | `enum Name { ... } end enum` |
| `namespace Name: ... end` | `namespace Name { ... } end namespace` |

## 4. Variables and Types

### Declarations and inference

```text
let name [: type] [= expression]
const name [: type] = expression
```

`let` declares a mutable variable. An initializer's type is inferred when no annotation is supplied. A declaration with neither annotation nor initializer is accepted but has the semantic type `unknown`. Unknown values remain dynamically usable in the interpreter, but cannot be assigned, passed, or returned where a concrete type is promised. Operations whose result type depends on the unknown runtime value remain `unknown`. The native backend rejects unsupported operations on unknown values rather than treating them as integers. A typed variable may omit its initializer. `const` requires an initializer, and assigning to a constant is a semantic error. Duplicate declarations in the same scope are rejected.

The analyzer creates nested scopes for the program, `start`, functions, branches, loops, switch cases, and namespaces. An inner scope can shadow an outer declaration, but same-scope duplicates are rejected. Direct top-level functions are predeclared (so they can be referenced before their definition); variable declarations are analyzed in order. Each variable and parameter reference is resolved to its declaration and given a unique IR binding identity. Consequently, shadowing creates a distinct runtime binding, assignments target the lexically resolved declaration, and a block-local name is no longer visible after leaving its scope. The interpreter may retain the inaccessible local slot until its function returns.

```zp
start {
    let score: number = 10
    let nextScore = score + 1
    const title: string = "Results"
    let pending: number
    say(title)
    say(nextScore)
} stop
```

The example declares typed and inferred mutable values, a constant, and an uninitialized typed variable. **Implemented.** The interpreter initializes an uninitialized local to numeric zero; this is a runtime default, not source-level type inference.

### Primitive and custom types

The built-in type spellings are:

| Type | Accepted spellings |
|---|---|
| Number | `number` |
| String | `string` |
| Character | `char`, `character` |
| Boolean | `boolean`, `bool` |
| No value | `void` |

Custom type annotations use an identifier, such as `Player`. Array and pointer type spellings are `elementType[]` and `elementType*`, respectively. Array suffixes can be repeated for nested arrays; the parser accepts one pointer suffix.

```zp
start {
    struct Player {
        name: string
        score: number
    } end struct

    let count: number = 3
    let active: bool = true
    let initial: character = 'A'
    say(count)
    say(active)
    say(initial)
} stop
```

Primitive annotations are checked by the analyzer. Structs and classes support
construction, typed fields, member access, and function arguments/returns in
the interpreter and Windows x64 native backend. Enums are typed numeric values
with member access and function argument/return support in those backends.
Custom aggregates and enum operations are not supported by Linux native code
generation. `void` is a recognized annotation, not a value type.

### Literals

The lexer recognizes decimal integers and fractions, scientific notation, hexadecimal (`0x` / `0X`), and binary (`0b` / `0B`) number tokens. Signs are unary operators rather than part of a number token. Boolean literals are lowercase `true` and `false`; `null` and `nullptr` are null literals.

Strings use double quotes and characters use single quotes. String escapes include `\n`, `\t`, `\r`, `\b`, `\f`, `\v`, `\a`, `\0`, `\\`, `\"`, and `\'`. Character literals must contain one Unicode scalar value (or one recognized escape). Physical newlines are not allowed inside a quoted literal.

```zp
start {
    say(12.5e2)
    say(0x10)
    say("first\nsecond")
    say('Z')
    say(false)
} stop
```

Decimal, scientific, binary (`0b...`), and hexadecimal (`0x...`) literals are parsed as IEEE-754 double values. The native backend preserves those values as 64-bit doubles. Division by zero follows IEEE-754 behavior (`inf`, `-inf`, or `nan`); output uses a consistent shortest-round-trip representation and prints special values as `inf`, `-inf`, or `nan`. The interpreter represents characters as strings and `null` as false. **Implemented for numeric execution on Windows x64; native runtime imports and Linux execution remain platform-limited.**

## 5. Operators and Expressions

### Supported expression forms

Expressions include literals, identifiers, parenthesized expressions, calls, array literals, indexing, member access, unary and binary operators, assignments, and the conditional `?:` expression.

```zp
start {
    let first: number = 6
    let second: number = 2
    let total: number = first + second * 3
    let message: string = "total=" + "8"
    let positive: boolean = total > 0 and true
    let label: string = positive ? "valid" : "invalid"
    say(message)
    say(label)
} stop
```

Arithmetic, comparisons, logical operators, string concatenation, and conditional selection are executable. `+` concatenation is semantically accepted for two strings; mixed string/number addition is not. **Implemented** for these forms.

Logical `and` / `or` and `&&` / `||` short-circuit: the right operand is evaluated only when needed.

Bitwise operators convert numeric operands to signed 64-bit integers by
truncating toward zero. Non-finite or out-of-range values convert to
`-9223372036854775808`. Shift counts use their low six bits; left shifts wrap
to 64 bits and right shifts are arithmetic. The interpreter retains dynamic
behavior for unknown values; the native backend reports an error when it
cannot determine a supported operation.

### Operators

| Category | Operators |
|---|---|
| Assignment | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` |
| Conditional | `? :` |
| Logical OR | `or`, `||` |
| Logical AND | `and`, `&&` |
| Bitwise | `&`, `|`, `^`, `~`, `<<`, `>>` |
| Equality | `==`, `!=` |
| Comparison | `<`, `<=`, `>`, `>=`, `is` |
| Arithmetic | `+`, `-`, `*`, `/`, `%` |
| Unary | `+`, `-`, `!`, `not`, `~`, prefix `++` / `--` |
| Postfix | postfix `++` / `--` |
| Access | calls `f(...)`, indexing `a[i]`, member `a.name`, pointer member `p->name` |

Assignment targets must be identifiers, member accesses, or index expressions. Assignment is right-associative. Binary operators at a given parser tier group left-to-right.

`is` performs a runtime type check for struct instances in the interpreter and
Windows x64 native backend, for example `player is Player`. It is not a
general primitive, class, or enum reflection system; Linux native aggregate
checks are unsupported.

### Strings

Strings can be printed and concatenated with `+` or `+=` when both operands are strings according to semantic analysis. There is no string interpolation or built-in string-method syntax.

```zp
start {
    let greeting: string = "Hello"
    greeting += ", Z+"
    say(greeting)
} stop
```

The interpreter also concatenates if either runtime value is a string, but the semantic analyzer only promises compatible string operands. **Implemented** for the checked string operations shown.

## 6. Functions

```text
fn name(parameter [: type] [= default], ...) [: returnType] {
    statements
} end fn
```

Parentheses are required, including for a zero-parameter function. Parameter type annotations and the return annotation are optional. Missing parameter types become `unknown`; a missing return annotation means `void`—return types are not inferred. The legacy `-> returnType` spelling remains accepted. Trailing parameters may have default expressions; later parameters must also have defaults. Defaults are evaluated when the call omits those arguments, in parameter order, and can refer to earlier parameters. `return` can provide an expression or no value and is only valid inside a function. Explicit returned values are checked against the declared return type, but the analyzer does not require every control-flow path to return.

```zp
start {
    fn add(left: number, right: number): number {
        return left + right
    } end fn

    fn greet(name: string = "friend"): string {
        return "Hello, " + name
    } end fn

    say(add(2, 3))
    say(greet("Z+"))
} stop
```

Default expressions are type-checked and evaluated only when their trailing argument is omitted. They are evaluated in parameter order and may refer to earlier parameters. Parameters without defaults cannot follow parameters with defaults. The interpreter and native backend receive the completed argument list, so both execution paths use the same defaults. **Default arguments: implemented.**

### Generic declarations and types

Functions, structs, and classes may declare type parameters. A parameter can
appear in nested parameter, return, and field types. Type arguments are
inferred from function arguments and record constructor arguments where
possible, or supplied explicitly with `<...>`:

```zp
start {
    fn identity<T>(value: T) -> T {
        return value
    } end fn

    struct Box<T> {
        value: T
    } end struct

    fn unbox<T>(box: Box<T>) -> T {
        return box.value
    } end fn

    let text = identity("inferred")
    let number = identity<number>(42)
    let box = Box<number>(number)
    say(text)
    say(unbox(box))
} stop
```

Generic parameter constraints currently support `number` and `equatable`,
written as `T: number` or `T: equatable`. Arguments and nested generic types
are checked structurally; unresolved or conflicting type parameters are
diagnosed. Generic class methods may use class and method type parameters.
Each concrete use is specialized before IR verification, interpreter
execution, or native code generation. Expanding recursive specialization is
rejected instead of generating an unbounded set of implementations.

The interpreter and Windows x64 native backend support these specializations.
Generic inheritance, user-defined constraints/traits, and generic type
inference across arbitrary function-value contexts are not implemented.
**Generic functions, structs, and classes: implemented.**

## 7. Control Flow

Conditions for `if`, `while`, and C-style `for` must be boolean (or have `unknown` type). Parentheses around a condition are optional. Braces and explicit terminators are the official block form:

```zp
start {
    let value: number = 0

    if value == 0 {
        say("zero")
    } else {
        say("nonzero")
    } end if

    while value < 2 {
        value++
    } end while

    repeat(2) {
        say("repeat")
    } end repeat

    for (let i: number = 0; i < 3; i++) {
        if i == 0 {
            continue
        } end if
        if i == 2 {
            break
        } end if
        say(i)
    } end for

    switch value {
        case 2 {
            say("matched")
        }
        default {
            say("other")
        }
    } end switch
} stop
```

- `if` supports an optional `else`, including `else if` chains. Braced `if` without `else` is valid.
- `while condition { ... } end while` repeats while true.
- `repeat(count) { ... } end repeat` requires a numeric count and repeats a fixed number of times.
- C-style `for (initializer; condition; increment) { ... } end for` requires parentheses and both semicolons; each clause may be omitted.
- `for name in start..end { ... } end for` iterates the integer values from start inclusive to end exclusive. `for name in array { ... } end for` visits array elements in order.
- `break` and `continue` are valid only inside loops according to semantic analysis. Although switch lowering has break handling, semantic analysis rejects `break` inside a switch unless it is also inside a loop.
- `switch expression { ... } end switch` supports braced `case expression { ... }` and `default { ... }` arms. A selected case runs then branches to the end; there is no fall-through.

The loop forms, `if`/`else if`/`else`, and switch dispatch are lowered through the shared AST and implemented by the interpreter and Windows x64 native backend. The old colon/`end` form remains accepted for compatibility and retains its legacy parsing constraints. **Implemented.**

### Error handling and cleanup

`throw expression` raises an error. The interpreter converts ordinary values
to their string representation and stores the result in an `Error` value with
a `message` field. Throwing a caught `Error` value again preserves its
`message`. A `try` requires one `catch` clause; the binding may be written with
or without parentheses:

```zp
fn divide(a: number, b: number): number {
    if b == 0 {
        throw "Division by zero"
    } end if
    return a / b
} end fn

start {
    try {
        let result = divide(10, 0)
        say(result)
    } catch error {
        say(error.message)
    } end try
} stop
```

An active `catch` handles explicit `throw` values and interpreter runtime
exceptions raised while executing an instruction (including errors from
standard-library calls). The caught value is a read-only `Error` struct; its
currently exposed field is `message`. If no handler in the current function
handles an error, it propagates through function calls to the nearest caller
handler. An unhandled error terminates `zp run` with a runtime diagnostic.
Nested handlers use the innermost active `try`; an error thrown by a catch body
can be handled by an enclosing `try`, but not by the already-consumed handler.

`defer` currently accepts exactly one direct named function call. The callee
and argument values are captured when the `defer` statement runs, and the call
executes when its lexical scope exits. Calls execute in last-in, first-out
order. Scope exit includes reaching the end of a block, returning from a
function, and unwinding after an error; nested scopes run their own deferred
calls before outer scopes. During cleanup all registered calls for the exiting
scope are attempted; if one or more fail, the first cleanup failure is
propagated after the remaining calls have run and may replace the error that
started unwinding.

The interpreter and Windows x64 native backend implement these behaviors.
Native parity for nested handlers, function-call propagation, rethrowing,
early returns, cleanup order, and cleanup during exception propagation is
covered by regression tests. Linux native exception/defer behavior has not
been validated on a Linux host and is not claimed as supported.

## 8. Data Structures

### Arrays and collections

Array literals use square brackets; array types use `T[]`, including nested forms such as `number[][]`. The analyzer checks array element compatibility, indexed receiver types, numeric index types, and assignment compatibility.

```zp
start {
    let values: number[] = [10, 20, 30]
    say(values[0])
    values[1] = 99
    say(length(values))
} stop
```

The interpreter executes typed and inferred arrays, empty typed arrays, nested arrays, indexed reads and writes, compound indexed assignment, and dynamic mutation. The built-ins are `push(array, value)`, `pop(array)`, `clear(array)`, `insert(array, index, value)`, `remove(array, index)`, `contains(array, value)`, and `length(array)`. `pop`/`remove` return the removed element; `push`/`clear`/`insert` return no value. Arrays grow geometrically and aliases refer to the same mutable array. `clear`, `pop`, and `remove` shrink logical length while retaining capacity for reuse. `contains` compares numbers and booleans by value, strings by contents, and nested arrays by identity.

Windows x64 native builds support the same operations, including function arguments/returns and shared mutation; arrays are tracing-GC managed. Indexes must be finite, non-negative integer numbers; insert permits the end index, while reads and removal require an existing element. Invalid indices, empty-array `pop`, incompatible element types, and non-array arguments produce errors. Interpreter failures include source locations and call frames when available; native failures report a descriptive runtime error. Linux native builds reject array operations explicitly. **Interpreter and Windows x64 native implemented; Linux native unsupported.**

### Structs, classes, and enums

```text
struct Name {
    field [: type] [= default]
} end struct

class Name {
    field [: type] [= initializer]
    fn method(...) {
        ...
    } end fn
} end class

enum Name {
    Member [= numericExpression]
} end enum
```

```zp
start {
    struct Player {
        name: string = "Ari"
        health: number = 100
    } end struct

    enum State {
        Ready
        Done = 1
    } end enum

    class Counter {
        value: number = 0
        fn current(): number {
            return this.value
        } end fn
    } end class

    let counter = Counter()
    say(counter.current())
} stop
```

Structs support positional construction, typed fields, literal defaults, field reads and writes, nested structs, arrays in fields, arrays of structs, and passing/returning instances through functions. Struct values use reference semantics in both the interpreter and Windows x64 native backend. The `is` operator recognizes struct instances in both execution modes. For example:

```zp
start {
    struct Player {
        name: string
        health: number = 100
    } end struct

    let player = Player("Ari")
    player.health -= 5
    say(player.health)
} stop
```

Fields without defaults are required constructor arguments. Constructor arguments fill fields in declaration order, so required fields must precede defaulted fields. Current field defaults must be literals. Assignment, array insertion, and function argument/return passing preserve object identity; mutating an alias changes the same instance. Interpreter structs are traced by the interpreter's managed heap. Windows native structs are tagged managed references in the existing native tracing heap; collection traces references in arrays and struct fields. Fields are public; per-field visibility is not implemented.

Classes use typed fields, literal field initializers, implicit positional
construction, instance methods, and `this` for field access. Enums assign
successive numeric values unless a member specifies a numeric literal. Enum
members retain the declared enum type for assignment, comparisons, and
function parameters/returns. Class and enum interpreter/native behavior is
covered by regression tests on Windows x64. Inheritance, explicit
constructors, member visibility modifiers, and function values are not
implemented. Linux native aggregate operations remain unsupported.

### Pointers and references

Pointer type annotations use `T*`; the parser also recognizes unary `&` and `*`, and pointer-member access `->`.

```zp
start {
    let value: number = 1
    let reference: number* = &value
    say(*reference)
} stop
```

The parser and analyzer accept the pointer type and address-of expression, but IR generation rejects unary `&` as an unsupported operator, so this example does not pass `zp check`. Unary `*` lowers to a plain `load`, not a real pointer dereference; `->` lowers to an unsupported member opcode. The runtime has no pointer/reference values. **Parsed and partially semantically analyzed; not implemented.**

## 9. Modules and Namespaces

```zp
// math.zh
start {
    pub fn twice(value: number): number {
        return value * 2
    } end fn
} stop
```

```zp
// main.zp
use "math.zh" as math

start {
    say(math.twice(21))
} stop
```

`use` is a module-loading statement. Relative paths are resolved from the directory of the file containing the statement; both `/` and `\` separators are normalized. Quoted paths support spaces. The target must exist and have a `.zh` extension. Absolute paths (including drive-qualified and UNC paths) are rejected. Dependencies are traversed depth-first in source order; canonical paths identify modules and repeated references load once. A cycle fails with a dependency-chain diagnostic.

The default module name comes from the filename stem with non-identifier characters replaced by `_`; directories do not create name components. Bare dotted standard-module names map their components to standard-library directories and use the first component as the default module name (for example, `use zmemory.management` exposes `zmemory`). `as alias` sets an explicit identifier. Distinct modules with conflicting names, duplicate aliases, and repeated use of one file under different names are errors. Public functions, variables/constants, structs, classes, and enums may be referenced as `module.name`. Unambiguous public declarations are also accessible unqualified.

Declarations in `.zh` files are private by default. Prefix declarations with `pub` to expose them outside the module. Private functions and variables cannot be referenced by an importer; private custom types cannot be used from another module. Public custom types are currently referenced by their unqualified type name because dotted type annotations are not implemented. Duplicate declarations and ambiguous public unqualified names are errors.

Imported files require one `start` block containing declarations only; the
loader extracts declarations from the block and does not execute it. Module
directives remain at file scope. Module-scope variables/constants initialize
once in deterministic dependency-first order before the entry function. They
share interpreter storage and can be emitted as native globals when the
operations fit the native scalar subset. Unsupported native IR remains an
error; `zp build` never falls back to interpretation. Legacy `import` and
`export` are parsed constructs, not file loading or the 0.5.0 visibility
mechanism.

Diagnostics from lexing, parsing, semantic analysis, and module resolution include the owning source file, line, and column in `--diagnostics=json` output. The VS Code extension recognizes both `.zp` and `.zh`, resolves unsaved entry buffers from their on-disk directory, and routes module diagnostics to the relevant header file.

See [MODULES.md](./MODULES.md) for the module loader's resolution and validation rules.

Standard module names resolve from the configured compiler `stdlib/` directory:

```zp
use zmath as math
use zstring as text

start {
    let result: number = math.sqrt(144) + math.pow(2, 3)
    say(text.upper("ready"))
} stop
```

The current implemented modules and backend limits are listed in
[stdlib/README.md](./stdlib/README.md). The module path and alias are resolved
through the compiler's module loader; imported declarations are not textually
substituted. `repeat`, `number`, and `boolean` are reserved tokens in ordinary
identifier positions, but are accepted as names after `.` and as imported
function names so APIs such as `text.repeat(...)` and `random.number(...)`
remain usable. Dynamic alias scoping and using the same source file under
multiple distinct aliases are not supported.

### Managed memory

`use zmemory.management` exposes `heapUsed()`, `heapObjects()`, `collect()`,
`collectionCount()`, and `setThreshold(bytes)`. The interpreter automatically
manages arrays and dynamically concatenated strings with a tracing collector.
Windows x64 native programs manage arrays and dynamic strings through the
packaged runtime. Linux native memory support is unavailable. See
[memory-management.md](./docs/memory-management.md) for ownership, roots,
thresholds, and limitations.

## 10. Built-in Functions

The source-level output built-ins are `say(value)`, `printz(value, ...values)`, and the deprecated compatibility alias `printm(value, ...values)`. `printz` and `printm` write each argument consecutively, without separators or a trailing newline; `say` writes one value followed by a newline. In the interpreter, printable values include strings, numbers, booleans, and arrays (recursively). Native output currently accepts strings only; Windows and Linux native builds support multiple string arguments.

```zp
start {
    printz("Age: ", 16)
    printz("This line has no newline")
    say("This line ends with a newline")
    printm("Existing projects remain compatible")
} stop
```

`printm` behaves exactly like `printz` and produces a compiler deprecation warning recommending `printz`. `printz` does not produce a warning. Array programs also have `length(array)`, `push(array, value)`, `pop(array)`, `clear(array)`, `insert(array, index, value)`, `remove(array, index)`, and `contains(array, value)` as described above.

The compiler represents generic array element types internally for built-in
signatures such as `length(array)`. This does not add user-declared generic
functions or generic type syntax. `to_string(value)` accepts `number` and
returns a `string`; other argument types are rejected during semantic analysis.

## 11. Comments and Formatting

The lexer skips `//` line comments and `/* ... */` block comments. Block comments are not nested. Spaces, tabs, and carriage returns are ignored; newlines are tokens. Indentation is not significant.

```zp
// A small complete program.
start {
    /* The comment can span
       more than one line. */
    let message: string = "Ready"
    say(message)
} stop
```

Newlines generally separate statements; semicolons can also separate statements and are mandatory between `for` header clauses. A newline cannot generally continue an expression. Keep expressions, declarations, and headers on one line unless the grammar explicitly provides a block.

The lexer reports malformed literals through source diagnostics. Character literals contain exactly one Unicode scalar value (or one recognized escape); source columns count UTF-16 code units for editor compatibility.

## 12. Keywords Reference

Keywords are case-sensitive and lowercase. Keyword spellings cannot generally be used as ordinary identifiers.

| Category | Reserved spellings |
|---|---|
| Program | `start`, `stop`, `end` |
| Declarations | `let`, `const` |
| Types | `number`, `string`, `char`, `character`, `boolean`, `bool`, `void` |
| Functions | `fn`, `return` |
| Control flow | `if`, `else`, `while`, `for`, `repeat`, `break`, `continue`, `try`, `catch`, `throw`, `defer` |
| Switch | `switch`, `case`, `default` |
| Data types | `struct`, `class`, `enum` |
| Visibility / receiver | `pub`, `public`, `private`, `protected`, `this` |
| Modules | `namespace`, `import`, `use`, `export`, `as` |
| Built-in spellings | `say`, `printz` |
| Literals | `true`, `false`, `null`, `nullptr` |
| Logical / type relation | `and`, `or`, `not`, `is` |

All listed words are lexed as keywords, but not all have a supported parser construct. `pub` precedes module declarations to make them externally visible; `this` is supported in class methods, while `public`, `private`, and `protected` do not implement member visibility. `as` supports module aliases (and the older import alias syntax). `printz` is a working output built-in; `printm` remains an identifier-based deprecated alias. `repeat`, `number`, and `boolean` are specially allowed as qualified member or function names for the standard library. `stop` is also allowed as a qualified member or function name, so `zaudio.stop(handle)` works despite `stop` closing a `start:` block. Identifiers start with a letter or `_` and continue with letters, digits, or `_`. Matching is case-sensitive (`score` and `Score` are different names).

## 13. Operator Precedence

From lowest to highest, the parser's expression tiers are:

| Precedence | Operators / forms | Associativity |
|---:|---|---|
| 1 | Assignment operators | Right |
| 2 | `?:` | Conditional; else arm recurses |
| 3 | `or`, `||` | Left |
| 4 | `and`, `&&` | Left |
| 5 | `|` (bitwise OR) | Left |
| 6 | `^` (bitwise XOR) | Left |
| 7 | `&` (bitwise AND) | Left |
| 8 | `==`, `!=` | Left |
| 9 | `<`, `<=`, `>`, `>=`, `is` | Left |
| 10 | `<<`, `>>` | Left |
| 11 | `+`, `-` | Left |
| 12 | `*`, `/`, `%` | Left |
| 13 | Prefix unary operators | Right / prefix |
| 14 | Calls, indexing, `.`, `->`, postfix `++` / `--` | Left-to-right chaining |
| 15 | Literals, identifiers, parenthesized expressions, arrays | Primary |

Bitwise operators follow the tiers shown above. The parser does not recognize `as` as a cast operator.

## 14. Runtime Support and Limitations

| Feature | Parser / semantic analysis | IR / interpreter status |
|---|---|---|
| Variables, constants, primitive values | Supported | Executable; ordinary `number` values are IEEE-754 doubles in the interpreter and native arithmetic; nested scopes are flattened at runtime |
| Functions, calls, returns | Supported | Executable; trailing default arguments are applied in interpreter and native calls |
| Arithmetic, comparisons, logical and bitwise operators | Supported | Generally executable; logical operators short-circuit; `is` checks struct instances in the interpreter and Windows x64 native backend |
| `if` / optional `else`, loops, switch | Supported with noted `break` restrictions | Executable |
| Strings and concatenation | Supported | Executable |
| Binary, hexadecimal, decimal, scientific numeric literals | Supported | Parsed and represented as doubles; Windows x64 native execution supports fractions and IEEE-754 division/comparison |
| Arrays and indexing | Parsed, semantically checked | Nested arrays, indexing, mutation, and length execute in the interpreter and Windows x64 native backend and are tracing-GC managed; Linux native compilation rejects them |
| Managed memory | `use zmemory.management` and public APIs | Interpreter arrays/dynamic strings and Windows x64 native arrays/dynamic strings use mark-and-sweep; other interpreter strings remain value-owned |
| Structs | Parsed, semantically checked | Interpreter and Windows x64 runtime values; Linux native unsupported |
| Classes | Braced declarations inside `start`; typed fields and methods checked | Interpreter and Windows x64: construction, fields, `this`, instance calls |
| Enums | Braced declarations inside `start`; numeric members and type checks | Interpreter and Windows x64: member values, comparisons, function arguments/returns |
| Namespaces | Braced declaration containers inside `start` | Qualified namespace function calls; namespace types and dynamic values are not supported |
| Member access and `->` | Struct/class fields and class methods are checked | Struct/class fields and methods execute in the interpreter and Windows x64 native backend; arbitrary member operations and `->` are unsupported |
| Pointer types and address-of | Parsed; pointer types accepted | Address-of fails IR generation; no runtime pointers |
| `use` with `.zh` declarations | Parsed by compiler and extension; imported declarations belong in the module's declaration-only `start` block | Canonical-path dependency graph; public/private visibility, aliases, qualified function/variable/constant access; globals initialize in interpreter and native supported scalar subset |
| `zaudio` standard module | `use zaudio` and its documented API pass checking | Interpreter and native Windows x64 integrations are tested; native Linux x86-64 imports and runtime staging are implemented but not validated on Linux |
| `zphysics` standard module | `use zphysics` and its documented API pass checking | Box2D engine and Windows native runtime are implemented; Linux native execution is not validated |
| `import`, `export`, namespaces | Parsed and analyzed | Legacy `import` does not load files; `export` wraps declarations; namespace functions use qualified names |
| `pub` | Parsed for module functions, variables/constants, structs, classes, and enums | Enforced for imported references |
| `public`, `private`, `protected`, `this` | `this` is supported in class methods; visibility keywords are lexed | Class-member visibility syntax is not implemented |
| Casts (`as`), scope resolution (`::`), braces, `#`, `@` | Some are tokenized | No corresponding parser implementation |
| Native `zp build` | Not a source-language feature | Generates a native executable for its supported IR subset; see [NATIVE_BACKEND.md](./NATIVE_BACKEND.md) for platform support and limitations |

`zp check file.zp` validates through IR generation, not execution. A successful check therefore does not guarantee that arrays or aggregate member operations run. `zp run file.zp` reports interpreter exceptions as runtime errors and exits unsuccessfully.

### ZGraphics module

The standard-library module `zgraphics` provides numeric window handles and module functions for creating/showing windows, drawing text and basic shapes, and receiving basic native events. The calls are accepted by the parser and semantic analyzer, then dispatched by the interpreter or, on Windows x64, the packaged native graphics runtime.

```zp
use zgraphics

start {
    let app = zgraphics.createWindow("Z+ graphics", 640, 360)
    zgraphics.background(app, "#18202B")
    zgraphics.text(app, "Close this window to exit", 24, 28, "#FFFFFF", 24)
    zgraphics.show(app)
    zgraphics.run(app)
} stop
```

This opens a real desktop window when run with `zp run` or built as a native Windows x64 program. Native builds package `zgraphics_runtime.dll` beside the output executable; both files must remain together. Linux native graphics linking is not implemented. Basic drawing calls include `line`, `rectangle`, and `circle`. `pollEvent` returns immediately with an event code or `0`; `waitEvent` blocks. Event details are read through `eventX`, `eventY`, `eventKey`, and `eventText`. Event codes are `1` for close, `2`/`3` for mouse down/up, `4` for mouse move, `5` for key down, and `6` for text input. See [ZGraphics/README.md](./ZGraphics/README.md) for the complete API and platform limitations. There are no object methods, native controls, or function callbacks.

### Audio module

The standard-library module `zaudio` provides music and sound-effect playback through miniaudio. Music streams from WAV, OGG Vorbis, or MP3 files; sound effects support overlapping instances. Handles are numeric IDs rather than ZMemory-managed objects, so release them explicitly with `unload()` or `unloadAll()`.

```zp
use zaudio

zaudio.initialize()
let music = zaudio.loadMusic("assets/theme.ogg")
let effect = zaudio.loadSound("assets/jump.wav")

zaudio.setLooping(music, true)
zaudio.setVolume(music, 0.7)
zaudio.play(music)
zaudio.play(effect)
zaudio.pause(music)
zaudio.resume(music)
zaudio.stop(effect)
zaudio.stop(music)
zaudio.unloadAll()
zaudio.shutdown()
```

The API also includes `initialized`, `isPlaying`, `isPaused`, `isStopped`, `getVolume`, `isLooping`, playback-position and duration queries, master volume and mute controls, device enumeration/selection, and resource release. Volume is clamped to `[0.0, 1.0]`; invalid handles, unsupported/corrupt files, and unavailable devices report runtime errors. Unloading an active handle stops it before releasing its playback resources. Audio work is performed by miniaudio's device thread; Z+ code is not invoked from its callback.

Interpreter and native Windows x64 integration tests pass. ZBuild detects audio imports and stages/packages the Windows runtime automatically. Native Z+ numbers are integer-oriented, so fractional duration and playback-position results are not generally usable as numeric values in native programs. Linux x86-64 ELF imports and runtime staging are implemented, but have not been built and executed on Linux. Device hotplug recovery is not implemented; device selection is rejected while resources are loaded. See [docs/AUDIO.md](./docs/AUDIO.md) for the complete API, lifecycle rules, platform status, and examples.

### ZPhysics module

The standard-library module `zphysics` wraps Box2D 3.1.1 for fixed-step 2D
simulation, rigid bodies, collision shapes and sensors, joints, contacts, and
queries. Handles are validated numeric IDs; explicitly destroy resources or
destroy their owning world. Polygon vertices use a semicolon-separated
`x,y;x,y;...` string.

```zp
use zphysics

start {
    let world = zphysics.createWorld(0, 980)
    let floor = zphysics.createBody(world, "static", 320, 420)
    zphysics.createBox(world, floor, 600, 20)
    let ball = zphysics.createBody(world, "dynamic", 320, 60)
    zphysics.createCircle(world, ball, 18)
    zphysics.update(world, 1 / 60)
    say(zphysics.getPositionY(ball))
    zphysics.destroyWorld(world)
} stop
```

Worlds use a fixed-step accumulator and cap catch-up work. Box2D solver details
are abstracted; `setIterations` controls its substep count. Interpreter and
Windows native physics bindings exchange numeric values as IEEE-754 doubles.
The engine and Windows native integration are built and tested; Linux native
execution is not verified in this environment. See
[docs/PHYSICS.md](./docs/PHYSICS.md) for API details and limitations.

## 15. Complete Example Programs

### Executable core example

```zp
fn add(a: number, b: number): number {
    return a + b
} end fn

start {
    const label: string = "sum="
    let total: number = add(1, 2)
    total += 1
    say(label + "done")
    say(total)

    let ok: boolean = total == 4 and true
    if ok {
        say("condition")
    } else {
        say("wrong")
    } end if

    let w: number = 0
    while w < 1 {
        w++
    } end while

    repeat(1) {
        say("repeat")
    } end repeat

    for (let i: number = 0; i < 3; i++) {
        if i == 0 {
            continue
        } end if
        if i == 2 {
            break
        } end if
        say(i)
    } end for

    switch total {
        case 4 {
            say("case")
        }
        default {
            say("default")
        }
    } end switch

    say(ok ? "yes" : "no")
    let bits: number = (1 | 2) ^ 3
    say(bits)
    let mark: char = 'Z'
    say(mark)
    printz("end")
} stop
```

This program passes both `zp check` and `zp run` with the current compiler. It exercises functions, declarations, numbers, strings, booleans, characters, arithmetic, assignment, comparisons, logical and bitwise operations, conditionals, loops, switch, `break`, `continue`, and the source-level output built-ins.

### Declaration and module-syntax example

```zp
use "math.zh" as math

start {
    struct Player {
        name: string = "A"
        health: number = 100
    } end struct

    class Counter {
        value: number = 0
        fn current(): number {
            return this.value
        } end fn
    } end class

    enum State {
        Ready
        Done = 1
    } end enum

    namespace Data {
        fn twice(x: number): number {
            return x * 2
        } end fn
    } end namespace

    let player = Player()
    let counter = Counter()
    let state: State = State.Done
    say(math.twice(Data.twice(player.health)))
    say(counter.current())
    say(state == State.Done)
} stop
```

`math.zh` must put its exported declarations inside its own declaration-only
`start` block, as shown above. Unlike the earlier parser-only example, this
sample exercises runtime class, enum, namespace, and module behavior.