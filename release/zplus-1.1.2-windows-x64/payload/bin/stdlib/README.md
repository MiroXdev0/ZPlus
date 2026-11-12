# Z+ Standard Library

The compiler locates standard modules from its configured `stdlib/` directory,
independent of the current working directory. Import with an alias:

```zp
use zmath as math
use zstring as text

start:
    let total: number = math.sqrt(144) + math.pow(2, 3)
    say(text.upper("ready"))
stop
```

`use zmath` resolves to `stdlib/zmath/zmath.zh`. Dotted paths such as
`use zmemory.management as memory` resolve to nested directories. Quoted
relative `.zh` imports remain supported. Public functions are accessed through
their imported namespace.

## Implemented modules

### `zmath as math`

The unary functions `abs`, `round`, `floor`, `ceil`, `sqrt`, `sin`, `cos`,
`tan`, `asin`, `acos`, `atan`, `log`, `log10`, and `exp` have signature
`(value: number) -> number`. `pow(base: number, exponent: number) -> number`,
`min(left: number, right: number) -> number`, and
`max(left: number, right: number) -> number` are binary functions.
`pi() -> number` and `e() -> number` return the corresponding constants.
Domain and overflow errors are reported (for example, negative `sqrt` or
non-positive `log` inputs).

The interpreter uses double-precision numbers. The Windows x64 native backend
currently represents numbers as signed 64-bit integers: a math result must be
an exactly representable integer or the runtime reports an error. `pi()` and
`e()` therefore report an unsupported fractional result natively. Linux
native execution does not provide the standard-library runtime.

### `zstring as text`

| Function | Signature / behavior |
|---|---|
| `upper`, `lower`, `trim`, `reverse` | `(value: string) -> string` |
| `split` | `(value: string, delimiter: string) -> string[]`; delimiter cannot be empty |
| `join` | `(values: string[], separator: string) -> string` |
| `replace` | `(value: string, search: string, replacement: string) -> string`; search cannot be empty |
| `contains`, `startsWith`, `endsWith` | `(value: string, text: string) -> boolean` |
| `substring` | `(value: string, offset: number, size: number) -> string`; zero-based Unicode code-point range |
| `length` | `(value: string) -> number`; Unicode code-point count |
| `repeat` | `(value: string, repetitions: number) -> string`; repetitions must be non-negative |

All `zstring` inputs are validated UTF-8. Length, substring, and reverse use
Unicode code points; trimming recognizes Unicode whitespace. Upper/lower case
conversion uses the Windows invariant Unicode mapping on Windows and the
system `C.UTF-8` locale (falling back to the environment locale) on POSIX.
Searching, splitting, joining, and replacement match exact UTF-8 sequences;
they do not normalize canonically equivalent strings. Generated strings and
split arrays are managed by ZMemory.

### `zrandom as random`

- `integer(minimum: number, maximum: number) -> number`: inclusive integer bounds.
- `number(minimum: number, maximum: number) -> number`: lower-inclusive, upper-exclusive.
- `boolean() -> boolean`.
- `choice(values: number[]) -> number`: rejects an empty or non-numeric array.
- `shuffle(values: number[]) -> number[]`: shuffles the input array in place.
- `seed(value: number) -> void`: requires a non-negative integer.

The interpreter uses a `std::mt19937_64` engine initialized from
`std::random_device`; seeding makes later draws repeatable within the same
runtime implementation. Cross-library/platform streams are not guaranteed to
match. Native Windows x64 supports integer, boolean, choice, and shuffle;
`number()` is explicitly unsupported because native floating-point values are
not implemented.

### `zcollections as collections`

`sum(values: number[]) -> number`, `average(values: number[]) -> number`,
`min(values: number[]) -> number`, and `max(values: number[]) -> number`.
`reverse`, `sort`, and `unique` each take `number[]` and return `number[]`.
`contains(values: number[], value: number) -> boolean`,
`count(values: number[], value: number) -> number`,
`first(values: number[]) -> number`, `last(values: number[]) -> number`, and
`isEmpty(values: number[]) -> boolean`. `average`,
`min`, `max`, `first`, and `last` reject empty arrays. `reverse`, `sort`, and
`unique` return new arrays and do not mutate their input. `contains` and
`count` compare numbers by value. These operations are available in the
interpreter and Windows x64 native runtime; native numbers are integer-only.

### `zfile as file`

`read(path: string) -> string`, `write(path: string, contents: string) -> void`,
`append(path: string, contents: string) -> void`,
`exists(path: string) -> boolean`, `delete(path: string) -> boolean`,
`createDirectory(path: string) -> boolean`,
`listDirectory(path: string) -> string[]`, `size(path: string) -> number`,
and `isDirectory(path: string) -> boolean`.

Reads and writes are byte-preserving file operations represented as Z+ strings;
write truncates and append preserves existing content. Errors are raised for
failed I/O rather than represented as empty successful results. Directory
entries are returned as sorted file names. Filesystem paths follow the host
platform. The module is supported by the interpreter and Windows x64 native
runtime.

### `zpath as path`

`join(left: string, right: string) -> string`,
`normalize(value: string) -> string`, `extension(value: string) -> string`,
`filename(value: string) -> string`, `directory(value: string) -> string`,
and `absolute(value: string) -> string`; `isAbsolute(value: string) -> boolean`.
These use `std::filesystem` path semantics, including platform-specific
separators and roots.

### `zenv as env`

`get(name: string) -> string`, `set(name: string, value: string) -> void`,
`exists(name: string) -> boolean`, `homeDirectory() -> string`,
`currentDirectory() -> string`, and `arguments() -> string[]`. A missing variable reads as an empty string; use
`exists` to distinguish it from a present empty value. Environment changes
affect only the current process. The interpreter receives program arguments
after `--` in `zp run file.zp -- arg...`; native programs see their process
arguments, excluding the executable name.

### `ztime as time`

`now() -> number` returns Unix seconds.
`format(timestamp: number, pattern: string) -> string` and
`parse(value: string, pattern: string) -> number` use the host C++
`strftime`/`get_time` format directives and local time zone.
`year`, `month`, `day`, `hour`, `minute`, and `second` each take a timestamp
`number` and return a `number` component.
`sleep(milliseconds: number) -> void` blocks the current thread and rejects negative values.
Native Windows x64 timestamps and numeric results must be representable as
integers.

### `zargs as args`

`count() -> number`, `get(index: number) -> string`,
`has(value: string) -> boolean`, and `all() -> string[]`. Indexing is zero-based and excludes the executable name.
An out-of-range index raises a runtime error. Interpreter arguments are
provided after `--` to `zp run`; native arguments come from the process command
line.

### `zlog as log`

`debug(message: string) -> void`, `info(message: string) -> void`,
`warning(message: string) -> void`, `error(message: string) -> void`,
`setLevel(level: string) -> void`, and `setOutput(path: string) -> void`.
The default threshold is `debug`.
Accepted levels are `debug`, `info`, `warning`, and `error`; messages below the
threshold are filtered. Output includes a local-time timestamp and severity.
An empty output path selects standard output; a non-empty path opens an
append-mode file. File and console write failures are errors.

## Unsupported modules

There are no `zjson`, `zhttp`, `zregex`, `zencoding`, `zcompression`, or
`zsystem` modules yet. Z+ does not currently have a dynamic object/map value
model for JSON objects or HTTP response records; publishing string-only
placeholders would misrepresent support. HTTP and JSON are therefore not
implemented in either backend.

The existing `stdlib/math/math.zh` remains a small source module with `abs`,
`min`, `max`, and `clamp`. `zgraphics` and `zmemory.management` are documented
in [ZGraphics](../ZGraphics/README.md) and
[managed memory](../docs/memory-management.md).

## Backend support summary

| Module | Interpreter | Windows x64 native | Linux native |
|---|---|---|---|
| `zmath` | Double-precision | Integer results only; fractional results and constants error | Unsupported |
| `zstring` | Yes | Yes; byte-oriented strings | Unsupported |
| `zrandom` | Yes | Integer/bool/array APIs; `number` errors | Unsupported |
| `zcollections` | Yes | Numeric arrays; integer numbers | Unsupported |
| `zfile`, `zpath`, `zenv`, `ztime`, `zargs`, `zlog` | Yes | Yes, with integer-only native numbers | Unsupported |
| `zjson`, `zhttp` and advanced modules | Unsupported | Unsupported | Unsupported |

Native standard-library calls use the packaged `zgraphics_runtime.dll`; keep
it beside the generated executable. The API wrappers do not add a C++ source
dependency to Z+ programs.
