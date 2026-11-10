# Z+ Backend Runtime

The backend standard library currently provides shared JSON, HTTP/1.1, and
SQLite runtime APIs. Interpreter calls run in the interpreter process. On
Windows x64, native executables use the same APIs through
`zgraphics_runtime.dll`; the SQLite runtime DLL is staged beside generated
executables when available.

## Modules

```zp
use zhttp as http
use zjson as json
use zsqlite as sqlite
```

These modules are in Z+'s standard library and do not need to be copied into a
project.

## HTTP server

```zp
let server = http.serverCreate("127.0.0.1", 8080)
http.serverConfigure(server, 1048576, 30000)
http.configureCors(server, "*", "GET, POST, OPTIONS", "Content-Type")
http.serverStart(server)

let serving = true
while serving:
    let request = http.serverNext(server, 250)
    if request != "":
        let method = json.getString(request, "/method")
        let path = json.getString(request, "/path")
        let id = json.getNumber(request, "/id")
        if method == "GET" and path == "/health":
            http.serverRespond(server, id, 200, "{\"content-type\":\"application/json\"}", "{\"status\":\"ok\"}")
        else:
            if method == "POST" and path == "/__stop":
                http.serverRespond(server, id, 200, "{}", "stopping")
                serving = false
            else:
                http.serverRespond(server, id, 404, "{}", "not found")
            end
        end
    else:
        printz("")
    end
end

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
`method`, `target`, `path`, `query`, `version`, `headers`, and `body`.
Header names are normalized to lowercase. Response headers are supplied as a
JSON object of string values; `Content-Length`, `Connection`, and
`Transfer-Encoding` are managed by the runtime. Unknown or already answered
request IDs return `false`.

The default body limit is 1 MiB and the default connection/handler timeout is
30 seconds. `serverConfigure` accepts body limits from 1 byte to 16 MiB and
timeouts from 100 ms to 5 minutes. Header blocks are limited to 16 KiB, the
pending request queue is bounded, and the worker pool is fixed at 2–8 workers.
The server supports GET, POST, PUT, PATCH, DELETE, OPTIONS, and HEAD, parses
Content-Length bodies, and supports persistent HTTP/1.0 and HTTP/1.1
connections. Malformed requests are rejected; unsupported request
Transfer-Encoding is rejected rather than ambiguously parsed. `Expect:
100-continue` currently receives 417.

Route helpers let the application route requests on the Z+ execution thread:

```text
routeParams(path: string, pattern: string) -> string
queryValue(query: string, name: string) -> string
requestHeader(requestJson: string, name: string) -> string
```

`routeParams("/users/42", "/users/{id}")` returns `{"id":"42"}`; it returns
`""` for a non-match. Query values are percent-decoded and `+` is interpreted
as a space. A missing query key or header returns `""`.

The networking workers accept connections, read requests, and wait for
responses. Z+ route logic runs on the thread calling `serverNext` and is
therefore serialized; requests can be accepted and held concurrently, but Z+
handlers are not dispatched concurrently. The language has no first-class
function values yet, so the current API does not provide callback-based route
registration or middleware chains. Applications implement routing and
middleware in their polling loop.

## HTTP client

```text
request(method: string, url: string, headersJson: string, body: string,
        timeoutMilliseconds: number) -> string
```

The result is JSON with `status`, `headers`, and `body`. Request headers are a
JSON object of strings; the runtime manages Host, Content-Length, and
Connection. The client supports `http://` URLs, custom headers, request
bodies, Content-Length responses, chunked responses, and a 4 MiB response
limit. Timeouts apply to connection establishment and socket I/O. `https://`
is intentionally rejected: TLS is not implemented and the runtime does not
pretend that plain HTTP is secure.

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

Run an example with `zp run test_program/backend/hello_server.zp`; build a
standalone Windows executable with `zp build ... -o app.exe`. `zbuild build`,
`zbuild run`, and `zbuild package` use the same compiler standard-library
modules and stage native runtime dependencies.

Automated tests cover JSON parsing/serialization, local HTTP clients, request
parsing, persistent connections, supported methods, concurrent clients,
timeouts, request limits, routing/query helpers, CORS, SQLite parameter
binding and transactions, and interpreter/native Windows execution. Linux
interpreter support is implemented in the POSIX socket and dynamic SQLite
paths but was not validated in this Windows environment. Native Linux
executables cannot currently import runtime functions; native backend support
for these modules is Windows x64 only.

Remaining limitations: no HTTPS/TLS, chunked request decoding, callback-based
route registration, middleware dispatch, concurrent Z+ handler execution,
or native Linux runtime imports. These are not claimed as implemented.
