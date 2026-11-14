# ZWeb

ZWeb is Z+'s unified web-development standard module. Import it with:

```zp
use zweb as web
```

The public API follows Z+'s current value model: HTTP requests, responses,
routes, middleware declarations, forms, and JSON values are represented as
JSON strings. `zhttp` and `zjson` remain available and compatible.

## Status and runtime requirements

| Capability | Status |
|---|---|
| HTTP client/server, bounded HTTP/1.0 and HTTP/1.1 subset | Implemented; not full protocol conformance |
| GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS | Implemented |
| Text and base64-carried binary request/response bodies | Implemented |
| Route tables, parameters, groups, names, configuration | Implemented |
| Form URL encoding, repeated fields, validation | Implemented |
| Multipart fields and exclusive file creation | Implemented |
| Cookies and process-local in-memory sessions | Implemented |
| Session rotation and session-bound CSRF tokens | Implemented |
| Server-level CORS origin checks and preflight | Implemented |
| Safe static files, index files, MIME and cache headers | Implemented |
| HTML templates with escaped variables, if, and each | Implemented |
| Declarative, application-invoked middleware steps | Partially implemented |
| Callback routes/middleware and automatic dispatch | Unsupported |
| HTTPS/TLS, HTTP/2, HTTP/3 | Unsupported |
| Persistent sessions and concurrent Z+ handlers | Unsupported |

Interpreter web functions use the shared C++ runtime. Windows x64 native
executables call the same implementation through `zgraphics_runtime.dll`.
Native imports for this runtime are not currently available on Linux. No TLS
library or platform TLS transport is configured. Do not send sensitive
traffic over the plain HTTP client; terminate TLS in a trusted reverse proxy.

The server accepts and reads connections on worker threads, but Z+ application
logic runs synchronously on the thread calling `serverNext`. Request headers
are limited to 16 KiB and 100 fields; request bodies default to 1 MiB and can
be configured from 1 byte to 16 MiB. Server response bodies are limited to 16 MiB.
Client response headers are limited to 16 KiB and 100 fields, and response
bodies to 4 MiB. Duplicate
Content-Length values and Transfer-Encoding request bodies are rejected.
Chunked responses are accepted only with a single `chunked` transfer coding;
trailers have the same 16 KiB/100-field bounds. These checks cover tested
framing and bounds, not every HTTP/1.1 requirement.

## HTTP

```text
serverCreate(address, port) -> number
serverStart(server) -> boolean
serverRunning(server) -> boolean
serverPort(server) -> number
serverNext(server, timeoutMilliseconds) -> string
serverRespond(server, requestId, status, headersJson, body) -> boolean
serverRespondBinary(server, requestId, status, headersJson, bodyBase64) -> boolean
serverRespondFile(server, requestId, status, headersJson, root, path, maximumBytes) -> boolean
serverRespondStatic(server, requestId, status, headersJson, root, path,
                    indexFile, maximumBytes, cacheControl) -> boolean
serverStop(server)
serverConfigure(server, maximumBodyBytes, timeoutMilliseconds)
configureCors(server, origin, methods, headers)

request(method, url, headersJson, body, timeoutMilliseconds) -> string
requestBinary(method, url, headersJson, bodyBase64, timeoutMilliseconds) -> string
get(url, headersJson, timeoutMilliseconds) -> string
head(url, headersJson, timeoutMilliseconds) -> string
post(url, headersJson, body, timeoutMilliseconds) -> string
put(url, headersJson, body, timeoutMilliseconds) -> string
patch(url, headersJson, body, timeoutMilliseconds) -> string
deleteRequest(url, headersJson, body, timeoutMilliseconds) -> string
options(url, headersJson, timeoutMilliseconds) -> string
```

Request/response headers are JSON objects whose values are strings. A
`serverNext` request contains `id`, `method`, `target`, `path`, `query`,
`version`, `headers`, `body`, and `bodyEncoding`. For non-UTF-8 data,
`bodyEncoding` is `"base64"` and `bodyBase64` contains the bytes; `body` is
empty. The client response uses the same `bodyEncoding`/`bodyBase64` convention.
Use `requestBody`, `requestBodyEncoding`, `requestBodyBase64`,
`responseBody`, `responseBodyEncoding`, and `responseBodyBase64` to access
these fields. `parseJsonBody` rejects binary bodies and malformed JSON.

`jsonResponse(status, headersJson, bodyJson)` validates and compacts a JSON
value, and supplies a JSON content type unless the caller already set one.
`response`, `notFoundResponse`, and `methodNotAllowedResponse` create
text-oriented response objects. `serverRespondJson` sends a JSON value
directly.

The server supports GET, POST, PUT, PATCH, DELETE, HEAD, and OPTIONS. The
client accepts the same methods and supports `http://` URLs only. Timeouts
apply to connect and socket operations after DNS resolution; system name
resolution itself is not bounded. HTTPS is rejected, not downgraded. Final
responses must use status codes 200–599; 204 and 304 responses cannot carry a
body.

`configureCors(server, origin, methods, headers)` accepts one exact origin or
`*`, plus comma-separated method and request-header token lists. Exact origins
are reflected only when the request Origin matches, and responses vary on
Origin. Valid preflight requests receive 204; disallowed preflights receive
403 without allow-origin headers. Server-level credentials and exposed-header
configuration are not provided. CORS is not CSRF protection.

## Routing

```text
routeAdd(routesJson, method, pattern, handlerIdentifier) -> string
routeAddConfigured(routesJson, method, pattern, handlerIdentifier,
                   name, configurationJson) -> string
routeGroup(prefix, routesJson) -> string
routeMatch(routesJson, method, path) -> string
routeUrl(routesJson, name, parametersJson) -> string
routeParams(path, pattern) -> string
queryValue(query, name) -> string
requestHeader(requestJson, name) -> string
```

Patterns use `{identifier}` path parameters. Route matching returns status
200 plus `handler`, `method`, `params`, and optional `name` and
`configuration`; unmatched paths return 404, and method mismatches return 405
with an `allow` field. HEAD falls back to GET when no explicit HEAD route
matches. Named routes generate paths with URL-encoded parameter values.
Applications dispatch handler identifiers themselves; route registration does
not store callbacks.

## Forms and validation

`parseForm` decodes `application/x-www-form-urlencoded` data and preserves
duplicate fields as arrays. `formValue` returns the first value for repeated
fields; it reports a missing field. `formHas` checks presence, and
`formNumber` converts a present field to a finite number.

`validateForm(formJson, rulesJson)` accepts an array of rule objects:

```json
[
  {"name":"email","required":true,"minLength":3,"maxLength":254,"email":true},
  {"name":"age","numeric":true,"minimum":18,"maximum":120},
  {"name":"role","allowed":["reader","writer"]}
]
```

It returns `{"valid":boolean,"errors":[{"field":...,"rule":...,"message":...}]}`.
String lengths count UTF-8 code points. Repeated values are checked
individually. Missing or empty values fail a `required` rule. Numeric bounds
use `minimum` and `maximum`. The `email` rule is a conservative ASCII
address check (including a dotted DNS-style domain), not full RFC mailbox
validation or proof of deliverability.

## Multipart and uploads

`parseMultipart(requestJson, maximumBytes)` requires
`multipart/form-data; boundary=...`. It supports multiple text fields and
files, repeated text values, quoted parameters, and binary file data. The
result shape is:

```json
{
  "fields":{"tag":["one","two"]},
  "files":[{
    "fieldName":"asset",
    "filename":"report.bin",
    "contentType":"application/octet-stream",
    "size":4,
    "dataBase64":"AAH/"
  }]
}
```

`uploadCount`, `uploadedFile`, `uploadDataBase64`, and `multipartFieldValue`
access the result. `saveUpload(multipartJson, index, root, destination,
maximumBytes)` stores the decoded bytes under the configured root. The
destination must be relative; traversal and resolved out-of-root symlinks are
rejected. The original filename is untrusted metadata only and must not be
used as a destination. Existing upload destinations are never overwritten;
files are created exclusively and removed if writing fails. Boundaries are
limited to 70 permitted characters; each part has at most 32 headers and the
request has at most 256 parts, within the 16 MiB request cap. Canonical path
checks reject traversal and resolved out-of-root symlinks, but concurrent
filesystem replacement can still create a time-of-check/time-of-use race.

## Cookies and sessions

`parseCookies` returns a JSON object. `cookieValue` reads a cookie. Cookie
serialization is available through:

```text
cookieHeader(name, value, maxAge, path, domain, secure, httpOnly, sameSite)
cookieHeaderExpires(name, value, maxAge, expires, path, domain,
                    secure, httpOnly, sameSite)
```

Expiration dates must be RFC 1123 GMT strings. `SameSite` accepts `Lax`,
`Strict`, `None`, or empty; `SameSite=None` requires `Secure`.

The in-memory session API is:

```text
sessionStoreCreate() -> number
sessionStoreClose(store)
sessionCreate(store, lifetimeSeconds) -> string
sessionSet(store, sessionId, key, value) -> boolean
sessionGet(store, sessionId, key) -> string
sessionHas(store, sessionId, key) -> boolean
sessionInvalidate(store, sessionId) -> boolean
sessionRotate(store, sessionId, lifetimeSeconds) -> string
sessionCsrfToken(store, sessionId) -> string
sessionValidateCsrf(store, sessionId, token) -> boolean
sessionCookie(name, sessionId, lifetimeSeconds, secure, sameSite) -> string
```

Session IDs are generated from 32 bytes supplied by the operating system's
cryptographic random source. Lifetime is 1 second through 1 year. Expiration
is checked on access and stale entries are also removed during session
creation. Rotation preserves session values, invalidates the old identifier,
and issues a fresh CSRF token. CSRF tokens are 32-byte random values compared
in constant time and expire with their session. Each in-memory store is
limited to 4096 sessions, 16 KiB per value, and 64 KiB of key/value data per
session. The store is process-local, memory-only, and not a persistent or
distributed session backend. Cookie flags do not encrypt or sign session data.

## Middleware

Middleware declarations are JSON and are invoked explicitly around the
application's handler loop:

```text
middlewareUse(pipelineJson, name, optionsJson) -> string
middlewareBefore(pipelineJson, requestJson) -> string
middlewareAfter(pipelineJson, requestJson, responseJson) -> string
```

Steps execute in registration order. Supported names:

| Name | Phase | Options and effect |
|---|---|---|
| `requireHeader` | before | `header`, optional `equals`, `status`, `message`; rejects with 401 by default |
| `maxBodyBytes` | before | positive integer `bytes`; rejects oversized requests with 413 |
| `jsonBody` | before | parses bodies marked `application/json`, rejects malformed data with 400 |
| `setRequestHeader` | before | `name` and `value` to add or replace a request header |
| `requestTiming` | both | adds a `Server-Timing` response header |
| `securityHeaders` | after | adds default hardening headers without replacing supplied values |
| `cors` | after | `origin`, optional `methods`, `headers`, `exposedHeaders`, `credentials`; reflects only a matching Origin or `*` |
| `setResponseHeader` | after | `name` and `value` to add or replace a response header |
| `requestLog` | after | appends a structured method/path/status/duration/size/error-category entry to response JSON `logs` |
| `errorResponse` | after | replaces response text for status 500–599 |

`middlewareBefore` returns `{"proceed":boolean,"request":...,"response":...}`.
`middlewareAfter` returns the modified response object. Applications decide
where to log returned entries. The request-log step records method, path,
status, elapsed time when timing middleware ran, request/response byte counts,
and a broad error category; it does not record request bodies or secrets.
These steps do not
run callbacks, intercept language exceptions, or create a fully automatic
handler chain. Middleware CORS does not itself handle preflight; use
`configureCors` for the server-level preflight behavior.

## Static files and templates

`mimeType(path)` maps common extensions and falls back to
`application/octet-stream`. `staticFile(root,path,maximumBytes)` reads a file
under a canonicalized root. `serverRespondStatic` additionally serves an
index file for directory paths and sets a default content type and optional
Cache-Control header. Root escape, traversal, and out-of-root symlinks are
rejected after canonicalization. File reads are not pinned to an already-open
directory handle, so concurrent filesystem replacement is outside the
confinement guarantee.

`loadTemplate(root,path,maximumBytes)` reads an in-root UTF-8 template.
`renderTemplate(source,contextJson)` supports:

```text
{{user.name}}
{{#if active}}...{{/if}}
{{#each items}}...{{/each}}
```

Values are HTML-escaped for HTML text-node use. Inside `each`, `{{this}}`,
`{{this.name}}`, and `{{@index}}` are available. Nested conditionals and loops
are supported up to 32 levels. No raw HTML substitution exists. HTML entity
escaping is not JavaScript, CSS, or URL-scheme validation; do not put
untrusted interpolations in script, style, or URL contexts.

## JSON, examples, and limits

The `json*` functions are wrappers around the compatible `zjson` module.
`parseJsonBody` parses JSON request text; `jsonResponse` creates JSON
responses. Dynamic JSON is not exposed as native Z+ object/array values; use
the JSON-string API and JSON Pointer helpers.

Runnable programs:

- [`examples/zweb/server.zp`](../examples/zweb/server.zp) and
  [`examples/zweb/client.zp`](../examples/zweb/client.zp): local HTTP service
  and client.
- [`examples/zweb/framework_server.zp`](../examples/zweb/framework_server.zp):
  route dispatch, middleware, JSON/form validation, sessions, uploads,
  templates, and static files.
- [`examples/zweb/framework_features.zp`](../examples/zweb/framework_features.zp):
  non-server feature walkthrough.

There is deliberately no HTTPS configuration example: no HTTPS API exists
until a real TLS transport, certificate loading, and validation are integrated.
HTTP/2/3, chunked request decoding, callback route/middleware dispatch,
persistent sessions, and concurrent Z+ handlers are also unsupported.
