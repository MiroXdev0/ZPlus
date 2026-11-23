# Z+ web interoperability

This experimental, dependency-free Node.js tool generates:

- `zplus-api.js`, an ES module client based on the standard Fetch API.
- `zplus-api.d.ts`, TypeScript declarations for the client and OpenAPI schemas.

It does not compile TypeScript or JavaScript into Z+, and it does not change
the Z+ compiler or ZWeb runtime. Keep using npm, `tsc`, and browser tooling for
frontend code. Z+ remains the server language.

## Install from a repository checkout

The package is not published to npm. From an existing frontend project with
this repository checked out nearby, install its local package:

```powershell
npm install --save-dev ..\Z-lang-release\tools\zplus-web-interop
npx zplus-web-interop generate api\openapi.json --out src\generated
```

By default, the generator refuses to replace `zplus-api.js` or
`zplus-api.d.ts`; pass `--force` to regenerate those files. It writes no other
files. No OpenAPI URL is fetched and external `$ref` values are rejected.

## Use the generated client

```js
import { createClient } from "./generated/zplus-api.js";

const api = createClient({
  baseUrl: "https://api.example.test",
  credentials: "include"
});

const greeting = await api.getGreeting({
  path: { name: "Ada" },
  query: { locale: "en" }
});
```

In TypeScript, import the same JavaScript module. TypeScript resolves the
adjacent declaration file and checks operation parameters and response data:

```ts
import { createClient } from "./generated/zplus-api.js";

const api = createClient({ baseUrl: "https://api.example.test" });
const greeting = await api.getGreeting({ path: { name: "Ada" } });
```

Non-2xx responses reject with `ApiError`, which includes the HTTP status,
parsed response body, and original `Response`. Network, abort, and invalid
JSON errors remain rejected; the client does not turn them into successful
empty responses. Set `headers` to a `HeadersInit` value or synchronous
function for bearer authentication. Browser cookie sessions can use
`credentials: "include"` when the Z+ server and CORS configuration allow it.
Do not put secrets in generated files or browser bundles.

## Supported OpenAPI subset

- OpenAPI JSON 3.0 and 3.1 documents with path operations.
- GET, POST, PUT, PATCH, DELETE, HEAD, and OPTIONS.
- Path, query, and header parameters with scalar values; query arrays use
  repeated form-style parameters.
- JSON request bodies and JSON or bodyless success responses.
- Local `#/components/schemas/...` references, objects, arrays, primitive and
  literal-enum types, nullable types, and oneOf/anyOf/allOf compositions.

External references, parameter/request/response references, cookies,
non-JSON bodies, custom parameter serialization, and unsupported JSON Schema
keywords fail with a diagnostic. Generated types do not prove that the Z+
server implements the described contract: keep the OpenAPI document aligned
with the routes dispatched by the application.

## Z+ server boundary

ZWeb provides HTTP/JSON request and response helpers, route matching, and
explicit CORS configuration. Serve the OpenAPI document as a static file (or
an API response) and implement the described routes using the current
`serverNext`/`routeMatch`/`serverRespondJson` model. Route registration stores
handler identifiers; application code dispatches those handlers.

ZWeb's current HTTP client accepts `http://` only; it rejects HTTPS. Put a
trusted TLS reverse proxy in front of a Z+ server for production HTTPS.
Application dispatch is synchronous. WebSockets and asynchronous Z+ handlers
are not implemented. Browser JavaScript may use native Fetch, DOM, WebSocket,
and npm libraries directly; those are not Z+ features.

## Example and checks

`examples/zweb-openapi/` contains an OpenAPI contract and a small server using `zp.web`.
The generator's unit tests run with:

```powershell
Push-Location tools\zplus-web-interop
npm test
Pop-Location
```

The end-to-end test exercises a real Z+ interpreter server when
`ZP_EXECUTABLE` points to a built compiler executable:

```powershell
$env:ZP_EXECUTABLE = (Resolve-Path ..\build\Release\zp.exe).Path
node --test examples\zweb-openapi\integration.test.mjs
```
