# Z+ Backend Runtime

The backend standard library currently provides shared JSON, bounded HTTP/1.x, and
SQLite runtime APIs. Interpreter calls run in the interpreter process. On
Windows x64, native executables use the same APIs through
`zgraphics_runtime.dll`; the SQLite runtime DLL is staged beside generated
executables when available.

## Compiler type and IR contract

Semantic analysis resolves source expressions and declarations to the shared
compiler `Type` representation. The generated IR carries those types on
function signatures, parameters, values, fields, and instruction results.
Before interpretation or native generation, the compiler verifies function
calls, return values, result types, and branch targets. A resolved `unknown`
result marks a dynamic interpreter value; the native backend rejects
operations on such values when it cannot preserve their runtime behavior.
Invalid IR is reported as an `E4001` compiler diagnostic rather than being
passed to a backend.

Builtin signatures, generic argument patterns, arity rules, and execution
availability are registered together during semantic analysis. The registry
is copied into the IR module and is used by semantic checking, IR verification,
the interpreter, and native type inference. Generic type parameters and
substitution are also used by source-level generic functions, structs, and
classes. Before execution, used generic functions and record layouts are
specialized with concrete type arguments; duplicate specializations are
reused, and expanding recursive specializations are rejected. The verifier
sees only concrete executable functions and record layouts. The interpreter
and Windows x64 backend consume the same specialized IR. Linux native
execution is not claimed by this generic support.

## Native ABI and platform support

Windows x64 calls use the four positional argument registers, 32-byte shadow
space, and stack arguments required by the Windows ABI. Generated PE files
include sorted `.pdata` function entries and `UNWIND_INFO` for generated
functions, the entry stub, and the native output helper. The Windows native
ABI test registers the table with the OS, executes nested generated calls,
exercises `RtlVirtualUnwind`, and verifies an OS exception unwinds across
generated frames. Z+ language exceptions continue to use their existing
runtime dispatch.

Linux x86-64 calls use the System V AMD64 ABI: integer/pointer and floating
arguments use their independent register sequences, overflow arguments are
placed in aligned caller stack space, and generated functions load incoming
stack arguments from the ABI-defined locations. This applies to generated
calls, deferred calls, and supported runtime imports.

Native Linux imports are currently limited to the audio, physics, and input
runtime libraries. Managed arrays and structs, garbage-collected memory,
graphics and string runtime operations, and the JSON, HTTP, web, SQLite, and
other Windows-only standard-library runtimes require the Windows x64 runtime
and are rejected by Linux native code generation. Linux native numeric
`say`/`print` formatting is also unsupported; string and boolean output are
available. These limits are intentional until compatible Linux runtimes exist.

## IR optimization and native temporary allocation

Native code generation verifies and optimizes a copy of the IR; interpreter
execution continues to use the original module. The current pipeline includes
constant propagation and folding, unreachable-block elimination, safe local
common-subexpression and dead-code elimination, unary-plus simplification, and
same-destination conditional-branch simplification. Potentially throwing or
effectful operations are retained, and transformed modules are verified.

Liveness analysis computes block and instruction live sets over control-flow
and exception-handler edges. Native temporary allocation is deliberately
limited to short-lived, block-local numeric values in volatile XMM registers,
with other values spilled to stack slots. Block-local number and boolean spill
slots may be reused when the function does not use managed memory. Managed
references are not allocated to XMM registers or reused stack slots.

This is not a general-purpose register allocator: general-purpose registers,
values live across blocks or calls, callee-saved register allocation, and
garbage-collector stack maps are not implemented. SIMD, general strength
reduction, and redundant conversion elimination are also not implemented;
the IR has no explicit conversion instruction, and Z+ numbers retain floating
point semantics.

## Modules

```zp
use zp.web as web
use zp.http as http
use zp.json as json
use zp.sqlite as sqlite
```

These modules are in Z+'s standard library and do not need to be copied into a
project.

`zp.web` is the unified public web module. It builds on the existing HTTP and
JSON operations with routing, URL and form helpers, cookies and in-memory
sessions, multipart uploads, templates, declarative middleware, and safe
static-file responses. The `zp.http` and `zp.json` modules remain available and
backward compatible. See the [ZWeb API guide](ZWEB.md) for signatures,
configuration, and security limitations.

## HTTP server

```zp
let server = http.serverCreate("127.0.0.1", 8080)
http.serverConfigure(server, 1048576, 30000)
http.configureCors(server, "*", "GET, POST, OPTIONS", "Content-Type")
http.serverStart(server)

let serving = true
while serving {
    let request = http.serverNext(server, 250)
    if request != "" {
        let method = json.getString(request, "/method")
        let path = json.getString(request, "/path")
        let id = json.getNumber(request, "/id")
        if method == "GET" and path == "/health" {
            http.serverRespond(server, id, 200, "{\"content-type\":\"application/json\"}", "{\"status\":\"ok\"}")
        } else {
            if method == "POST" and path == "/__stop" {
                http.serverRespond(server, id, 200, "{}", "stopping")
                serving = false
            } else {
                http.serverRespond(server, id, 404, "{}", "not found")
            } end if
        } end if
    } else {
        printz("")
    } end if
} end while

http.serverStop(server)
```

The API signatures are:

```text
serverCreate(address: string, port: number) -> number
serverStart(server: number) -> boolean
serverRunning(server: number) -> boolean
serverPort(server: number) -> number
serverNext(server: number, timeoutMilliseconds: number) -> string
serverRespond(server: number, requestId: number, status: number,
              headersJson: string, body: string) -> boolean
serverStop(server: number)
serverConfigure(server: number, maximumBodyBytes: number,
                timeoutMilliseconds: number)
configureCors(server: number, origin: string, methods: string, headers: string)
```

`serverNext` returns `""` on timeout or a JSON request object with `id`,
`method`, `target`, `path`, `query`, `version`, `headers`, `body`, and
`bodyEncoding`. Header names are normalized to lowercase. Text bodies use
`bodyEncoding: "utf8"` and `body`; non-UTF-8 binary bodies use
`bodyEncoding: "base64"`, an empty `body`, and `bodyBase64`. Response headers
are supplied as a JSON object of string values; `Content-Length`, `Connection`, and
`Transfer-Encoding` are managed by the runtime. Unknown or already answered
request IDs return `false`.

The default body limit is 1 MiB and the default connection/handler timeout is
30 seconds. `serverConfigure` accepts body limits from 1 byte to 16 MiB and
timeouts from 100 ms to 5 minutes. Header blocks are limited to 16 KiB and
100 fields, the pending request queue is bounded, and the worker pool is fixed
at 2–8 workers.
The server supports GET, POST, PUT, PATCH, DELETE, OPTIONS, and HEAD, parses
Content-Length bodies, and supports persistent HTTP/1.0 and HTTP/1.1
connections. Duplicate Content-Length values and request Transfer-Encoding
are rejected; malformed requests receive an error response. Responses with
status 204, 205, or 304 cannot carry a body. `Expect:
100-continue` currently receives 417. This bounded implementation is not
claimed to satisfy every HTTP/1.1 requirement.

`configureCors` allows one exact origin or `*`; exact origins are emitted only
when the request Origin matches, and valid preflights are answered with 204.
Disallowed preflights receive 403 without allow-origin headers. CORS does not
replace CSRF protection. The client decodes bounded chunked responses,
rejects duplicate or conflicting framing headers, and only supports the
`chunked` response transfer coding. DNS resolution itself is not covered by
the client timeout.

Route matching and query/header helpers run on the Z+ execution thread:

```text
routeParams(path: string, pattern: string) -> string
queryValue(query: string, name: string) -> string
requestHeader(requestJson: string, name: string) -> string
```

`routeParams("/users/42", "/users/{id}")` returns `{"id":"42"}`; it returns
`""` for a non-match. Query values are percent-decoded and `+` is interpreted
as a space. A missing query key or header returns `""`.

The unified `zp.web` module also has an immutable JSON route table:

```zp
let routes = web.routeAdd("[]", "GET", "/users/{id}", "showUser")
let versionedRoutes = web.routeGroup("/v1", routes)
let result = web.routeMatch(versionedRoutes, "GET", "/v1/users/42")
```

`routeMatch` returns JSON with `status`, and on a match, `handler`, `method`,
`params`, and optional `name` and `configuration`. It returns status 404 when
no path matches and 405 plus `allow` when the path exists for other methods.
`routeAddConfigured` attaches route metadata and an optional unique name;
`routeUrl` builds a path from a name and parameter object. `routeGroup`
preserves route metadata while adding a prefix. Handler names are identifiers
for the application to dispatch in its normal control flow; this is not
callback registration. Explicit `HEAD` routes are preferred; otherwise a
`GET` route matches `HEAD`.

The networking workers accept connections, read requests, and wait for
responses. Z+ route logic runs on the thread calling `serverNext` and is
therefore serialized; requests can be accepted and held concurrently, but Z+ handlers are not
dispatched concurrently. The language has no first-class function values yet,
so ZWeb keeps handler identifiers and does not provide callback-based handler
registration. The declarative middleware pipeline runs explicit request
preprocessing with `middlewareBefore` and response modification with
`middlewareAfter`; applications call these around their handler dispatch.
Registered steps execute in insertion order. It supports required-header
checks, body-size checks, JSON body validation, request-header injection,
security headers, configured CORS, response-header injection, request timing,
structured request-log entries, and replacement of 5xx response text.
`requestLog` returns entries in a `logs` array; applications choose where to
send them. This is not a callback or exception-catching middleware chain.

## HTTP client

```text
request(method: string, url: string, headersJson: string, body: string,
        timeoutMilliseconds: number) -> string
```

The result is JSON with `status`, `headers`, `body`, and `bodyEncoding`.
Non-UTF-8 response bodies use `bodyEncoding: "base64"` and `bodyBase64`;
`requestBinary` accepts a base64 request body and `serverRespondBinary` accepts
a base64 response body. Text response behavior is unchanged. Request headers
are a JSON object of strings; the runtime manages Host, Content-Length, and
Connection. The client supports `http://` URLs, custom headers, request
bodies, Content-Length responses, chunked responses, and a 4 MiB response
limit. Client request headers are limited to 16 KiB and 100 fields, and request
bodies to 16 MiB. Timeouts apply to connection establishment and socket I/O.
Informational and protocol-upgrade responses are rejected.
`https://` is intentionally rejected: TLS is not implemented, and the runtime
does not pretend that plain HTTP is secure. No TLS library or platform TLS
transport is currently configured in CMake. HTTPS support requires selecting
and wiring a maintained TLS provider, certificate validation, and certificate/key
configuration; until then, deployments should terminate TLS in a trusted
reverse proxy.

## JSON

```text
valid(source: string) -> boolean
compact(source: string) -> string
quote(value: string) -> string
get(source: string, pointer: string) -> string
getString(source: string, pointer: string) -> string
getNumber(source: string, pointer: string) -> number
getBoolean(source: string, pointer: string) -> boolean
arrayLength(source: string, pointer: string) -> number
arrayGet(source: string, pointer: string, index: number) -> string
kind(source: string, pointer: string) -> string
```

`get` and `arrayGet` return compact JSON text for the selected value. Paths use
JSON Pointer syntax. The parser validates UTF-8, surrogate pairs, number
syntax, and nesting. Native Z+ numbers are currently integral, so native
`getNumber` rejects fractional JSON values; the interpreter returns fractional
values. This is a known interpreter/native parity limitation.

## ZWeb utilities

The URL helpers `urlEncode`, `urlDecode`, `parseUrl`, and `buildUrl` operate on
absolute hierarchical URLs and URL components. `parseForm` decodes
`application/x-www-form-urlencoded` data and represents duplicate keys as
arrays. `formHas`, `formValue`, `formNumber`, and `validateForm` support
presence checks, numeric conversion, required values, string-length bounds,
finite numeric checks and ranges, conservative ASCII email validation, and
allowed-value lists. Validation returns
`{"valid":boolean,"errors":[...]}`; missing and invalid conversions are
reported rather than silently coerced.

`parseCookies` returns a JSON object. `cookieHeader` supports Max-Age, Path, Domain, Secure, HttpOnly, and SameSite;
`cookieHeaderExpires` adds Expires; `SameSite=None` requires Secure.
`sessionStoreCreate`,
`sessionCreate`, `sessionSet`, `sessionGet`, `sessionHas`,
`sessionInvalidate`, and `sessionRotate` provide an in-memory session store.
`sessionCsrfToken` and `sessionValidateCsrf` provide session-bound CSRF tokens.
Session IDs and CSRF tokens contain 32 bytes from the operating-system CSPRNG.
Rotation preserves session values, invalidates the old identifier, and
invalidates the prior CSRF token. Tokens expire with their session and are
compared in constant time. Session lifetime is limited to one year; expired
entries are removed when accessed or when new sessions are created. Stores
are limited to 4096 sessions and 64 KiB key/value data per session. Session
data is process-local and is not shared across compiler processes or persisted.

`parseMultipart(requestJson, maximumBytes)` parses multipart/form-data parts.
It preserves repeated text fields as arrays and returns file metadata plus
base64-encoded file bytes. `saveUpload` decodes one file and writes it beneath
a configured root using a caller-supplied relative destination; it rejects
path traversal, out-of-root symlinks, and files over the requested limit.
Client-provided filenames are untrusted metadata only and are never used as
storage paths. Upload destinations are created exclusively and existing files
are not overwritten; incomplete writes are removed. The parser accepts up to
256 parts, up to 32 headers per part, boundaries up to 70 characters, and an
overall limit of 16 MiB. Canonicalization does not eliminate races caused by
concurrent filesystem replacement.

`loadTemplate(root, path, maximumBytes)` safely reads an in-root UTF-8
template. `renderTemplate(source, contextJson)` supports HTML-escaped
`{{name}}` substitutions, nested `{{#if name}}...{{/if}}` sections, and
`{{#each items}}...{{/each}}` arrays (`{{this}}` and `{{@index}}` are
available in loops). There is no raw/unescaped substitution syntax.

`staticFile(root, path, maximumBytes)` canonicalizes the configured root and
resolved file path, rejects traversal outside the root (including symlinks
that resolve outside it), and enforces a caller-supplied limit of at most
16 MiB. Encoded path separators and control characters are rejected after
decoding. File reads are not pinned to an already-open directory handle, so
concurrent filesystem replacement is outside the confinement guarantee.
`serverRespondFile` serves an exact file. `serverRespondStatic` also
supports a directory index file and caller-provided Cache-Control headers;
both respond with file bytes without round-tripping them through a Z+ string.
`mimeType` recognizes common web asset extensions and falls back to
`application/octet-stream`.

Implemented: a bounded HTTP/1.0/1.1 subset with tested request framing and
header limits, text/binary bodies, chunked response decoding, URL-encoded and
multipart forms, route tables, declarative middleware operations, cookies,
in-memory sessions, CSRF tokens, templates, JSON helpers, and root-confined
static-file checks. This is not a claim of full HTTP/1.1 conformance.
Server-level CORS matches a configured origin and handles preflight; it does
not configure credentials or exposed headers. Middleware is
application-invoked and declarative rather than callback-driven; structured
request-log entries contain metadata and sizes but no request bodies or
secrets, and are returned for application logging; session storage is
in-memory only. Canonical file checks have a
concurrent filesystem TOCTOU limitation. Unsupported: HTTPS/TLS, HTTP/2/3,
chunked request decoding, automatic route dispatch, callback-based
middleware, persistent sessions, and concurrent Z+ handler execution.

## SQLite

```text
open(path: string) -> number
close(database: number)
execute(database: number, sql: string, parametersJson: string) -> number
query(database: number, sql: string, parametersJson: string) -> string
changes(database: number) -> number
begin(database: number) -> number
commit(database: number) -> number
rollback(database: number) -> number
```

Parameters must be a JSON array containing strings, numbers, booleans, or
`null`, with one value per SQL placeholder. Values are bound using prepared
SQLite statements; multiple statements per call are rejected.

`query` returns a JSON array of row objects. Repeated column names are
disambiguated with `_2`, `_3`, and so on. BLOB columns are represented as
`{"$blob":"<base64>"}`. SQLite integer columns are serialized as JSON numbers,
which inherit the current Z+ numeric precision limitation.

SQLite is loaded dynamically: Windows looks for `sqlite3.dll` or
`libsqlite3-0.dll`; Linux looks for `libsqlite3.so.0` or `libsqlite3.so`.
Windows CMake builds search common vcpkg/MSYS2 locations and copy the runtime
beside the compiler, native runtime, and generated executable when found.
Native Z+ executables that import SQLite require this runtime DLL. The
interpreter requires the matching system library. Database handles are closed
explicitly by `close` and are also closed when program execution ends.

## Examples and validation

Runnable examples are under [`test_program/backend/`](../test_program/backend/):

- `hello_server.zp` — health route and orderly shutdown.
- `rest_api.zp` — JSON POST, route parameters, status codes, and CORS.
- `multi_client_server.zp` — request isolation via a client header and body.
- `external_api_client.zp` — calls the local health server without external
  service dependencies.
- `database_api.zp` — parameterized SQLite insert/query and transaction.
- [`examples/zweb/server.zp`](../examples/zweb/server.zp) and
  [`examples/zweb/client.zp`](../examples/zweb/client.zp) — grouped routes,
  JSON, forms, cookies, security headers, static files, and HTTP client calls.
- [`examples/zweb/framework_server.zp`](../examples/zweb/framework_server.zp)
  — route dispatch, declarative middleware, JSON/form validation, sessions,
  uploads, static files, and templates.
- [`examples/zweb/framework_features.zp`](../examples/zweb/framework_features.zp)
  — runnable routing, validation, session, template, static, and middleware
  helpers.

Run an example with `zp run test_program/backend/hello_server.zp`; build a
standalone Windows executable with `zp build ... -o app.exe`. `zbuild build`,
`zbuild run`, and `zbuild package` use the same compiler standard-library
modules and stage native runtime dependencies.

Automated tests cover JSON parsing/serialization, local HTTP clients, request
parsing, persistent connections, supported methods, concurrent clients,
timeouts, request limits, routing/query helpers, CORS, SQLite parameter
binding and transactions, and interpreter/native Windows execution. Linux
interpreter support is implemented in the POSIX socket and dynamic SQLite
paths but was not validated in this Windows environment. Linux native runtime
imports are limited to audio, physics, and input; the backend runtime features
described above remain Windows x64 only.

The framework feature smoke test runs through both the interpreter and
Windows x64 native backend. Runtime tests additionally cover binary multipart
and HTTP bodies, upload path confinement, sessions, template rendering,
middleware order, and static index responses. HTTPS/TLS, HTTP/2/3, chunked
request decoding, callback-based route/middleware dispatch, concurrent Z+
handler execution, and native Linux imports for these HTTP, web, SQLite, and
managed-memory features remain unsupported.
