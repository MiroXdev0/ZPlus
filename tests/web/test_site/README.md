# Northstar Studio

A responsive studio website powered by the Z+ interpreter and the existing
`zp.web` standard library. The project is self-contained in this directory.

## Run locally

From the repository root, start the server in PowerShell:

```powershell
Push-Location tests\web\test_site
& ..\..\..\build\Debug\zp.exe run backend\main.zp
```

Open <http://127.0.0.1:8765>. To stop normally, send a local POST request:

```powershell
Invoke-RestMethod -Method Post http://127.0.0.1:8765/__dev/shutdown
```

Run the POST command from a second terminal. The server terminal then exits;
run `Pop-Location` there to restore its previous directory. The shutdown route
is only reachable on loopback because the server binds to `127.0.0.1`.
Alternatively, press Ctrl+C in the server terminal.

If `zp` is available on `PATH`, run `zp run backend\main.zp` from this
directory instead. Run the server with this directory as its working
directory so that its `public` and `data` paths resolve correctly.

## What's included

- `backend/main.zp` — Z+ server startup, route table, dispatch, form validation,
  JSON API responses, contact submission storage and shutdown.
- `public/index.html` — accessible single-page studio site and native HTML
  contact form.
- `public/css/style.css` — responsive visual system, layouts and interactions.
- `public/templates/` — escaped ZWeb-rendered success and invalid-form pages.
- `data/contact_messages.jsonl` — created on first successful submission;
  stores one JSON contact record per line and is not served as a static file.

## Routes

| Method | Path | Behavior |
|---|---|---|
| GET | `/` | Serves the homepage from the configured static root. |
| GET | `/assets/style.css` | Serves the stylesheet with a CSS MIME type and cache header. |
| GET | `/api/health` | Returns a JSON health response. |
| GET | `/api/services/{id}` | Returns service details for `strategy`, `identity`, or `products`; accepts `source` as a query parameter. |
| POST | `/contact` | Validates an HTML form and returns an escaped confirmation or an error page. |
| POST | `/api/contact` | Validates the same URL-encoded fields and returns JSON. |
| POST | `/__dev/shutdown` | Stops the loopback-only development server. |

Wrong methods receive 405 with an `Allow` header; unknown paths and service
IDs receive 404. Contact fields are validated with `web.validateForm`;
email receives an additional basic format check. Accepted submissions are
appended to the local JSONL file. The file contains personal information:
use this as a local demonstration, not as production storage. There is no
database, email delivery, spam protection, or CSRF token mechanism.

## Runtime notes

The server uses ZWeb's `serverCreate`, `serverConfigure`, `serverStart`,
`serverNext`, `serverRespond`, `serverRespondStatic`, `routeAdd`, `routeMatch`,
`parseForm`, `validateForm`, `jsonResponse`/`serverRespondJson`,
`securityHeaders`, and template APIs. `zp.string` is used for basic email
validation and `zp.file` appends accepted submissions. Static responses are
confined to `public`; the data file is outside that root. The backend binds
only to loopback and uses plain HTTP; ZWeb's HTTPS/TLS transport is not
implemented.
