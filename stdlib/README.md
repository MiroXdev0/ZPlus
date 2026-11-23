# Z+ Standard Library

The compiler locates standard modules from its configured `stdlib/` directory,
independent of the current working directory. Import with an alias:

```zp
use zmath as math
use zstring as text

start {
    let total: number = math.sqrt(144) + math.pow(2, 3)
    say(text.upper("ready"))
} stop
```

`use zmath` resolves to `stdlib/zmath/zmath.zh`. Dotted paths such as
`use zmemory.management as memory` resolve to nested directories. Quoted
relative `.zh` imports remain supported. Public functions are accessed through
their imported namespace.

Terminal input is provided by compiler built-ins rather than a standard-module
import. `read()`, `read_number()`, `read_integer()`, and `read_boolean()` accept
an optional string prompt, which is flushed before input is read. Their
behavior, EOF handling, and examples are documented in the
[built-in function reference](../docs/syntax.md#10-built-in-functions).

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
Z+ arrays are mutable dynamic arrays: `push(values, value)`, `insert(values,
index, value)`, `pop(values)`, `remove(values, index)`, `contains(values,
value)`, `clear(values)`, and `length(values)` provide insertion, removal,
membership, clearing, and size operations. Arrays can be iterated with `for`.

The module also exports generic `List<T>`, `Stack<T>`, `Queue<T>`,
`Set<T: equatable>`, `Map<K: equatable, V>`, and `Optional<T>` classes.
Initialize array-backed collections with typed arrays, for example
`List<number>([])`, `Map<string, number>([], [])`, or
`Optional<string>(["ready"])`.

- `List<T>`: `size()`, `add(value)`, `get(index)`, `removeAt(index)`, and `clear()`.
- `Stack<T>`: `size()`, `push(value)`, and `pop()`; popping an empty stack uses
  the underlying array bounds diagnostic.
- `Queue<T>`: `size()`, `enqueue(value)`, and `dequeue()`; removing from an
  empty queue uses the underlying array bounds diagnostic.
- `Set<T>`: `size()`, `contains(value)`, `add(value)`, and `remove(value)`.
  Initial values must not contain duplicates; adding an existing value leaves
  the set unchanged.
- `Map<K, V>`: `size()`, `set(key, value)`, `get(key)`, and `remove(key)`.
  `get` and `remove` return `Optional<V>`. `set` replaces the value for an
  existing key. Initial key/value arrays must have equal lengths and unique
  keys; the class validates this before operations.
- `Optional<T>`: construct with `[]` for none or `[value]` for some; use
  `isSome()`, `unwrap()`, and `unwrapOr(fallback)`. More than one initial value
  is rejected.

The generic collection classes use Z+ mutable dynamic arrays and generic
specialization. They are supported by the interpreter and Windows x64 native
backend. Native numeric values remain integer-only. Directly accessing the
backing array fields can bypass collection invariants; use the methods above
for normal operations.

### `zfile as file`

`read(path: string) -> string`, `write(path: string, contents: string) -> void`,
`readLines(path: string) -> string[]`,
`writeLines(path: string, lines: string[]) -> void`,
`append(path: string, contents: string) -> void`,
`exists(path: string) -> boolean`, `delete(path: string) -> boolean`,
`createDirectory(path: string) -> boolean`,
`listDirectory(path: string) -> string[]`, `size(path: string) -> number`,
and `isDirectory(path: string) -> boolean`.

Reads and writes are byte-preserving file operations represented as Z+ strings;
write truncates and append preserves existing content. Errors are raised for
failed I/O rather than represented as empty successful results. Directory
entries are returned as sorted file names. `readLines` removes LF or CRLF
terminators and does not add an extra empty item for a final newline;
an empty file reads as an empty array. `writeLines` writes one LF after each
element and rejects elements containing line breaks. `exists`, `size`, and
`isDirectory` provide file metadata checks.
Filesystem paths follow the host platform. All listed functions are supported
by the interpreter and Windows x64 native runtime; filesystem and validation
failures raise runtime errors in both.

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

## Additional modules

These modules are source-backed public APIs. Their signatures below match the
current `.zh` wrappers. Invalid arguments and underlying operation failures
raise runtime errors unless a function explicitly returns a status or
validation result. Native support is per operation; see the support summary
and integration tests before relying on a platform-specific call.

### `zjson as json`

| Function | Signature | Purpose |
|---|---|---|
| `valid` | `(source: string) -> boolean` | Validate JSON text. |
| `compact` | `(source: string) -> string` | Parse and compact JSON text. |
| `quote` | `(value: string) -> string` | Encode a string as a JSON string literal. |
| `get` | `(source: string, pointer: string) -> string` | Get a JSON value at a JSON Pointer as JSON text. |
| `getString` | `(source: string, pointer: string) -> string` | Read a string value. |
| `getNumber` | `(source: string, pointer: string) -> number` | Read a numeric value. |
| `getBoolean` | `(source: string, pointer: string) -> boolean` | Read a boolean value. |
| `arrayLength` | `(source: string, pointer: string) -> number` | Get an array's length. |
| `arrayGet` | `(source: string, pointer: string, index: number) -> string` | Get an array item as JSON text. |
| `kind` | `(source: string, pointer: string) -> string` | Return the JSON value kind. |

`valid` returns `false` for invalid JSON. Other accessors report malformed
JSON, invalid pointers, missing values, type mismatches, and invalid indexes.
Values cross the API as JSON strings; Z+ does not expose a dynamic JSON object.

```zp
use zjson as json
start {
    let name: string = json.getString("{\"name\":\"Mira\"}", "/name")
    say(name)
} stop
```

### `zfilesystem as fs`

`copyFile(source: string, destination: string, overwrite: boolean) -> boolean`,
`moveFile(source: string, destination: string, overwrite: boolean) -> boolean`,
`removeEmptyDirectory(path: string) -> boolean`,
`exists(path: string) -> boolean`, `isDirectory(path: string) -> boolean`,
`createDirectory(path: string) -> boolean`, `listDirectory(path: string) -> string[]`,
`deleteFile(path: string) -> boolean`,
`scheduleDeleteOnReboot(path: string) -> boolean`, and
`size(path: string) -> number`.

These operations report filesystem errors rather than treating failed I/O as
success. `scheduleDeleteOnReboot` is Windows-specific. The remaining APIs use
the host filesystem; native support follows the packaged runtime's platform
coverage.

### `zprocess as process`

`run(executable: string, arguments: string[], workingDirectory: string,
timeoutMilliseconds: number) -> string` starts a child process and returns its
captured output. Invalid arguments, process startup failures, and timeouts are
runtime errors. Use a fully qualified executable path when portability or
search-path behavior matters.

### `zcrypto as crypto`

`sha256File(path: string) -> string` returns the lowercase SHA-256 digest of a
file. File access and hashing failures are runtime errors.

### `zhttp as http`

| Function | Signature |
|---|---|
| `serverCreate` | `(address: string, port: number) -> number` |
| `serverStart` | `(server: number) -> boolean` |
| `serverRunning` | `(server: number) -> boolean` |
| `serverPort` | `(server: number) -> number` |
| `serverNext` | `(server: number, timeoutMilliseconds: number) -> string` |
| `serverRespond` | `(server: number, requestId: number, status: number, headersJson: string, body: string) -> boolean` |
| `serverStop` | `(server: number) -> void` |
| `serverConfigure` | `(server: number, maximumBodyBytes: number, timeoutMilliseconds: number) -> void` |
| `configureCors` | `(server: number, origin: string, methods: string, headers: string) -> void` |
| `routeParams` | `(path: string, pattern: string) -> string` |
| `queryValue` | `(query: string, name: string) -> string` |
| `requestHeader` | `(requestJson: string, name: string) -> string` |
| `request` | `(method: string, url: string, headersJson: string, body: string, timeoutMilliseconds: number) -> string` |

Server and client operations may report invalid handles, malformed JSON,
network, bind, and timeout errors. Request and response objects are exchanged
as JSON strings. HTTP headers are limited to 16 KiB and 100 fields; client
request bodies are limited to 16 MiB. Networking support depends on the
platform runtime.

### `zweb as web`

`zweb` is the unified public web module for HTTP/1.x, route tables,
URL-encoded and multipart forms, validation, cookies, in-memory sessions,
declarative middleware, templates, JSON responses, and safe static files.
Existing `zhttp` and `zjson` imports remain supported.

| Area | Public operations |
|---|---|
| HTTP | `serverCreate`, `serverStart`, `serverRunning`, `serverPort`, `serverNext`, `serverRespond`, `serverRespondBinary`, `serverRespondFile`, `serverRespondStatic`, `serverStop`, `serverConfigure`, `configureCors`, `request`, `requestBinary`, `get`, `head`, `post`, `put`, `patch`, `deleteRequest`, `options` |
| Request/response | `routeParams`, `queryValue`, `requestHeader`, `requestMethod`, `requestPath`, `requestQuery`, `requestBody`, `requestBodyEncoding`, `requestBodyBase64`, `parseJsonBody`, `requestNumber`, `response`, `jsonResponse`, `notFoundResponse`, `methodNotAllowedResponse` |
| JSON | `jsonValid`, `jsonCompact`, `jsonQuote`, `jsonGet`, `jsonGetString`, `jsonGetNumber`, `jsonGetBoolean`, `jsonArrayLength`, `jsonArrayGet`, `jsonKind` |
| URLs and forms | `urlEncode`, `urlDecode`, `parseUrl`, `buildUrl`, `parseForm`, `formHas`, `formValue`, `formNumber`, `validateForm` |
| Cookies and sessions | `parseCookies`, `cookieValue`, `cookieHeader`, `cookieHeaderExpires`, `sessionStoreCreate`, `sessionStoreClose`, `sessionCreate`, `sessionSet`, `sessionGet`, `sessionHas`, `sessionInvalidate`, `sessionRotate`, `sessionCsrfToken`, `sessionValidateCsrf`, `sessionCookie` |
| Routing | `routeAdd`, `routeAddConfigured`, `routeGroup`, `routeMatch`, `routeUrl` |
| Middleware | `middlewareUse`, `middlewareBefore`, `middlewareAfter`, `securityHeaders` |
| Multipart | `parseMultipart`, `multipartFieldValue`, `uploadCount`, `uploadedFile`, `uploadDataBase64`, `saveUpload` |
| Templates and files | `loadTemplate`, `renderTemplate`, `mimeType`, `staticFile` |

`routeAdd` returns an updated route-table JSON string. `routeAddConfigured`
adds route metadata and an optional name; `routeGroup` preserves this data
when prefixing patterns; `routeMatch` returns a status, handler identifier,
extracted parameters, and optional configuration. `routeUrl` builds a path
for a named route. Since Z+ does not have first-class function values,
applications dispatch handler identifiers in their polling loop rather than
registering callbacks.

`parseForm` handles URL-encoded bodies and repeated fields. `validateForm`
supports required values, string-length bounds, finite numeric checks and
ranges, a conservative ASCII email check, and allowed-value lists.
`parseMultipart` returns repeated fields, untrusted filename metadata, and
base64 file data. `saveUpload` writes bytes beneath a configured root using a
caller-supplied relative path, refuses overwrites, and applies a size limit.
Multipart requests are limited to 16 MiB, 256 parts, and 32 headers per part.
`parseCookies` returns a JSON object; cookie creation supports expiration,
Max-Age, Path, Domain, Secure, HttpOnly, and SameSite. The session store is
memory-only and process-local; IDs and CSRF tokens use the operating-system
cryptographic random source. Session rotation invalidates the prior ID;
CSRF tokens are bound to session expiration.

The declarative middleware operations run only when the application invokes
`middlewareBefore` and `middlewareAfter`; they do not provide callbacks or
automatic exception handling. Request logs contain method, path, status,
duration when available, byte counts, and a broad error category, not request
bodies or secrets. Server CORS checks the configured origin and answers
preflight requests, but is not CSRF protection. `renderTemplate` HTML-escapes
variables for HTML text-node use and supports simple `if` and `each` sections;
untrusted interpolations must not be used in script, style, or URL contexts.
`serverRespondStatic` supports a document root, index file, MIME type, and
Cache-Control header while rejecting canonical path escape. Concurrent
filesystem replacement can still race those path checks. Binary HTTP data
uses base64 fields rather than unsafe UTF-8 conversion.

TLS/HTTPS, HTTP/2/3, chunked request decoding, persistent sessions,
callback-based middleware, concurrent Z+ handlers, and native Linux runtime
imports remain unsupported. HTTP parsing is bounded and tested but is not
claimed to implement every HTTP/1.1 requirement. Canonical filesystem checks
cannot prevent races from concurrent path replacement. Native execution for
these runtime functions is Windows x64 only, matching the current backend.
See the [ZWeb API guide](../docs/ZWEB.md),
[backend runtime guide](../docs/BACKEND.md), and runnable
[`examples/zweb/`](../examples/zweb/) programs.

### `zsqlite as sqlite`

`open(path: string) -> number`, `close(database: number) -> void`,
`execute(database: number, sql: string, parametersJson: string) -> number`,
`query(database: number, sql: string, parametersJson: string) -> string`,
`changes(database: number) -> number`, `begin(database: number) -> number`,
`commit(database: number) -> number`, and
`rollback(database: number) -> number`.

Database handles and SQL failures are reported as runtime errors. Parameters
are supplied as a JSON array, and query results are JSON text. The interpreter
and Windows x64 runtime are covered by SQLite integration tests.

### `zregistry as registry`

`readString(keyPath: string, valueName: string) -> string`,
`valueKind(keyPath: string, valueName: string) -> string`,
`writeString(keyPath: string, valueName: string, value: string, kind: string) -> boolean`,
and `deleteValue(keyPath: string, valueName: string) -> boolean`.

These APIs access the Windows registry and are unavailable on other platforms.
Access and type failures are runtime errors; write/delete return whether an
operation changed a value.

### `zsystem as system`

`platform() -> string`, `architecture() -> string`, `osVersion() -> string`,
`executablePath() -> string`, `availableSpace(path: string) -> number`,
`selectFolder(title: string, initialPath: string) -> string`,
`messageBox(title: string, message: string, buttons: string, icon: string) -> number`,
`createShortcut(path: string, target: string, arguments: string, workingDirectory: string) -> boolean`,
`shortcutTarget(path: string) -> string`,
`shortcutArguments(path: string) -> string`, and
`removeShortcutIfMatches(path: string, target: string, arguments: string) -> boolean`.

System-information and filesystem queries use the host platform. Folder
selection, message boxes, and shortcut operations are platform-dependent
(Windows UI/shell integrations); unsupported operations report runtime errors.

`zaudio` and `zphysics` provide the APIs documented in
[AUDIO.md](../docs/AUDIO.md) and [PHYSICS.md](../docs/PHYSICS.md).
`zgraphics` is documented in [ZGraphics](../ZGraphics/README.md), and
`zmemory.management` in [managed memory](../docs/memory-management.md).
`stdlib/math/math.zh` retains the source-level `abs`, `min`, `max`, and
`clamp` helpers.

## User modules and runtime errors

Imports such as `use "storage/tasks.zh" as tasks` resolve relative to the
importing source file, not the current working directory. Imported source
files use `.zh`; the entry program uses `.zp`. Nested paths, repeated imports,
and cycle detection are supported. Mark declarations `pub` to expose them
outside their module; unmarked declarations remain private. Module imports
can be qualified through an alias, and standard-library imports keep the same
resolution rules. For example:

```zp
use "storage/tasks.zh" as tasks
start {
    say(tasks.count())
} stop
```

`try`/`catch` catches runtime failures and exposes an `Error` value with a
`message: string` field. `throw "message"` raises a catchable error; rethrowing
the caught `Error` preserves its message. `defer` cleanup runs when leaving a
scope and before a pending error reaches an outer handler. Error subtypes and
stable error codes are not currently part of the language API.

## Compatibility and backend support

The `collections`, `graphics`, `io`, `math`, and `memory` directories are
legacy compatibility locations, not additional complete modules. Import the
documented `z...` module names for supported APIs.

The interpreter and Windows x64 native runtime are the primary tested
execution targets. Runtime coverage is module-specific: CTest includes
interpreter/native integration for core modules and platform APIs, and
Windows-only tests exercise installer integrations. Merely importing a
module validates its source and name resolution; it does not prove every API
call is supported. Linux native availability varies by runtime and is not
certified by the Windows test suite.

## Backend support summary

| Module | Interpreter | Windows x64 native | Linux native |
|---|---|---|---|
| `zmath` | Double-precision | Integer results only; fractional results and constants error | Unsupported |
| `zstring` | Yes | Yes; byte-oriented strings | Unsupported |
| `zrandom` | Yes | Integer/bool/array APIs; `number` errors | Unsupported |
| `zcollections` | Yes | Numeric arrays; integer numbers | Unsupported |
| `zfile`, `zpath`, `zenv`, `ztime`, `zargs`, `zlog` | Yes | Yes, with integer-only native numbers | Unsupported |
| `zaudio`, `zgraphics`, `zphysics` | Yes | Windows x64 runtime integrations; see the linked module documentation | Platform-specific |
| `zjson`, `zhttp`, `zsqlite`, installer/system modules | Runtime bindings exist; see module API and tests | Runtime and platform coverage is per operation | Platform-specific |

Native standard-library calls use the packaged `zgraphics_runtime.dll`; keep
it beside the generated executable. The API wrappers do not add a C++ source
dependency to Z+ programs.
