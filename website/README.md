# Official Z+ website

The official website is a static single-page application with a local Node development server. It presents the Z+ language, implementation-aware documentation, real release downloads, the learning course, examples, and the live package registry. The toolchain has dedicated compiler, ZPM, ZPBuild, VS Code, and ZGraphics pages. Existing registry, account, package publishing, and course progress workflows remain in the same app. First-party website source is licensed under Apache-2.0; bundled third-party code retains its own notices.

The page shell and metadata are in `index.html`; learning/reference content is in `content.js`; product facts are in `ecosystem-content.js`; client routing, release handling, and registry UI live in `app.js`; presentation is in `styles.css`. The package registry API and database migrations remain in `api/`, `lib/`, and `migrations/`.

## Run locally

Serve this directory over HTTP (the release API and browser clipboard require an HTTP origin). The included development server provides SPA fallback for deep routes:

```powershell
node website\server.mjs
```

Open `http://localhost:8000`. Set the `PORT` environment variable to use another port. `_redirects` supplies a Netlify rewrite; `404.html` recovers deep links on static hosts that serve a root-level 404 page. Deploy the contents of this directory at the domain root and configure host fallback/rewrite behavior for `index.html`.

There is no client-side compile/build step. Release information is fetched from the official GitHub Releases API; only uploaded assets from the official release repository are linked. Release-note HTML continues to render as HTML and is sanitized in the browser with the locally served DOMPurify 3.4.18 bundle. The bundled sanitizer is tracked alongside its npm dependency and should be refreshed from `node_modules/dompurify/dist/purify.min.js` when updating that dependency. The official Releases page is shown as a fallback. Course progress is stored in this browser's local storage.

The shared transparent logo artwork is maintained in the repository's `logo/` directory. The website header uses the dark-surface artwork; the favicon selects the corresponding light or dark artwork from the visitor's color-scheme preference.

## Registry deployment

The existing Vercel project keeps `website/` as its root directory. The
registry uses Neon PostgreSQL for accounts, sessions, package metadata, and
publication state, and the private Neon Object Storage bucket
`zplus-packages` for package archives. Vercel's API function uses the pooled
`DATABASE_URL` and branch-scoped S3 credentials; it does not expose storage
credentials to the website or ZPM clients.

Configure these server-side environment variables in the existing Vercel
project's Development, Preview, and Production environments using the Neon
branch intended for each environment:

| Variable | Use |
| --- | --- |
| `DATABASE_URL` | Pooled Neon PostgreSQL connection for API requests. |
| `AWS_ACCESS_KEY_ID` | Neon Object Storage branch credential. |
| `AWS_SECRET_ACCESS_KEY` | Neon Object Storage branch secret. |
| `AWS_ENDPOINT_URL_S3` | Branch's S3-compatible endpoint. |
| `AWS_REGION` | Branch region, for example `us-east-2`. |
| `ZPM_PUBLIC_ORIGIN` | Optional website origin; defaults to `https://zpluse.vercel.app`. |

Keep `DATABASE_URL_UNPOOLED` in a secure local migration environment only; it
is not needed by the running API. For local development, link or check out a
non-production Neon branch with the `zplus-packages` bucket configured, then
pull that branch's environment into an ignored local env file. Never commit
connection strings, tokens, or generated credentials.

Apply migrations in order to the intended branch using its direct,
non-pooled `DATABASE_URL_UNPOOLED` connection:

```powershell
psql $env:DATABASE_URL_UNPOOLED -v ON_ERROR_STOP=1 -f migrations\001_zpm_registry.sql
psql $env:DATABASE_URL_UNPOOLED -v ON_ERROR_STOP=1 -f migrations\002_neon_object_storage.sql
```

Migration 002 adds private storage keys, archive sizes, pending/published
states, and account status while preserving the original schema. Existing
published rows retain their historical `blob_url`; downloads continue to
redirect those legacy artifacts. New versions are stored privately and
downloaded through the registry API after their size and SHA-256 are checked.
Do not apply migrations to Production until the isolated branch has been
validated and production rollout is approved.

The Vercel rewrite in `vercel.json` sends `/api/registry/*` requests to
`api/registry.js`; all other deep links continue to fall back to `index.html`.
The browser account and publish UI uses same-origin requests. ZPM's default
registry URL is `https://zpluse.vercel.app/api/registry`; set
`ZPM_REGISTRY_URL` to an HTTPS registry when testing another deployment.

The registry API supports account registration, browser and CLI login/logout,
session verification, package search and metadata, version resolution,
publishing, and archive download. CLI login returns a revocable bearer token;
the browser uses an HttpOnly session cookie. Passwords are stored as scrypt
hashes. Account email verification and password recovery are not provided.

Run `npm install`, `npm run check`, and `npm test` from this directory. The tests use PGlite and
a private-storage adapter mock. `npm run test:client -- ..\build\Release\zpm.exe
..\build\Release\zpbuild.exe ..\build\Release\zp.exe` exercises the ZPM client
against an isolated local registry. These tests do not deploy the site or
replace a genuine Neon branch integration test. Published archives are limited
to 4 MiB and validated as ZPM1 archives without executing package code.
`zpm publish` builds locally with ZPBuild first; browser uploads are
structure-validated but are not compiled server-side. This static website has
no `npm run build` script.

## Accuracy and scope

The learning course has 54 numbered lessons. The documentation sidebar covers getting started, language syntax, types, functions, classes and visibility, enums, structs, modules, errors, both execution modes, standard-library use, CLI, ZPM, ZPBuild, the VS Code extension, and ZGraphics. Technical content is grounded in the checked-in `docs/syntax.md`, `MODULES.md`, `docs/ZBUILD.md`, `stdlib/README.md`, extension README/metadata, and ZGraphics guide. Backend limits and unavailable downloads are called out. The Wiki has three destinations: the Z+ Syntax reference, The Idea Behind Z+, and What Is Z+ Used For? Its technical reference is organized from `docs/syntax.md`, then checked against implementation and tests where the specification notes limitations. Every lesson links back to the syntax reference; multi-file examples display their companion `.zh` source.

The downloads page reads published release information from the official release API and distinguishes the Windows x64 installer from unavailable platform or extension packages. Page titles, descriptions, Open Graph tags, canonical URLs, `robots.txt`, and `sitemap.xml` support sharing and indexing.

`npm run check` verifies JavaScript syntax. `npm test` covers the registry, release-note sanitizer, website routes, metadata, documentation filtering, clipboard behavior, download assets, and mobile navigation. There is no automated browser-based visual-diff or accessibility scanner in this project yet; do a manual viewport review before deployment.

The existing static interface and its content remain the website foundation; the small Vercel function is limited to registry API routes.

## ZTS and ZJS interoperability

The website's `/zts` and `/zjs` pages document the experimental TypeScript and
JavaScript API-client workflow. The dependency-free generator and its supported
OpenAPI subset are in `../tools/zplus-web-interop/`; a real ZWeb server example
is in `../examples/zweb-openapi/`. The tool is not published to npm, does not
compile TS/JS into Z+, and does not add WebSockets or async server handlers.
