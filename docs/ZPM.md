# ZPM

ZPM is the local package manager for Z+. It is implemented in C++ alongside
ZPBuild and delegates project compilation and tests to the existing `zp` and
`zpbuild` tools.

## Package layout

A package has a `zpm.json` manifest, at least one `.zh` or `.zp` Z+ source
file, and a `.zh` entry module. `zpm init [NAME]` creates a starter package
and its `zproject.toml` build project without overwriting existing files.

```text
example/
├── zpm.json
├── zproject.toml
├── src/
│   ├── main.zh
│   └── main.zp
├── tests/
│   └── main.zp
└── z+_packages/
```

The manifest includes `name`, `version`, `description`, `author`, `license`,
`repository`, `homepage`, `main`, `compiler`, and `dependencies`. Package
names are lowercase ASCII letters, digits, or hyphens; versions use
three-part semantic versions. The package entry must be an existing `.zh`
file within the package. Packages must contain Z+ source and cannot include
foreign-language implementation files or symbolic links. Build outputs and
installed dependencies are not included when packing.
An optional `tags` array of short strings is displayed by the browser registry;
it does not affect dependency resolution.

Project imports use the package name as the module path:

```z+
use "example-package" as example
```

The compiler resolves that name to the package's manifest `main` module.
ZPBuild prepares dependencies before building, checking, or testing; direct
interpreter commands can also resolve installed packages.

## Local workflows

```powershell
zpm init my-app
zpm add ..\example-package
zpm install
zpm list
zpm build
zpm test
zpm remove example-package
```

`zpm install PATH` and `zpm add PATH` accept a package directory or a `.zpm`
archive. Registry dependencies use `zpm add NAME[@CONSTRAINT]`; `zpm install`
resolves the manifest, while `zpm install --locked` restores the exact lockfile
versions. Local directory dependencies are referenced by a path relative to
the project. Archives are extracted into `z+_packages/.sources/` and their
files are installed under `z+_packages/<name>/`. The package cache is an
additional offline source; set `ZPM_CACHE_DIR` to override it. The default
cache is `%LOCALAPPDATA%\ZPlus\zpm\cache` on Windows and
`$XDG_CACHE_HOME/zplus/zpm` (or `~/.cache/zplus/zpm`) on Linux.

ZPM writes `zpm-lock.json` with the project-manifest SHA-256 and resolved
package versions and content hashes. `zpm install --locked` refuses to change
the lock, selects those exact versions, and restores modified managed
installations only from the verified locked source. Dependency constraints support exact
versions, `>`, `>=`, `<`, `<=`, caret, tilde, and whitespace-separated
conjunctions. The resolver tries available cached or registry versions from
highest to lowest and restarts resolution when a transitive constraint makes
an earlier choice impossible.
A package name resolves to one version in a project; incompatible constraints
fail with a dependency conflict instead of silently replacing a version.
`install --locked` uses only the lockfile's exact version and integrity value.

`zpm pack [PACKAGE_DIRECTORY] [ARCHIVE.zpm]` creates a versioned, uncompressed
ZPM1 archive. The archive has bounded entry count and file sizes; extraction
rejects absolute paths, traversal, duplicate names, symlinks, and trailing
data. It is a ZPM-specific format, not a ZIP file. Package downloads do not
run package scripts or otherwise execute package code.

`zpm update` re-resolves declared registry dependencies and refreshes the
lockfile. `zpm cache info|clean` reports the global cache or removes only
integrity-validated ZPM-managed entries, preserving unrelated files in a
configured cache directory. `zpm build` and `zpm test` delegate to ZPBuild.
ZPBuild also resolves dependencies itself before `build`, `check`, or `test`,
and the interpreter resolves installed package imports.

## Official registry

The registry API and browser portal are implemented in the existing website
project. ZPM's default API is
the `/api/registry` path of the canonical origin recorded in
`website/ecosystem-metadata.json`. `ZPM_REGISTRY_URL` may override it
for an HTTPS test registry; plain HTTP is accepted only for localhost
development. Available CLI commands are `search`, `info`, `login`, `logout`,
`publish`, `add`, `install`, and `update`. Registry archives are limited to
4 MiB by the serverless upload/download boundary; local packages are not
subject to that registry limit.

The existing Vercel project root is `website/`; `website/vercel.json` routes
API paths to the registry function and keeps the existing static-site deep
links. The API uses Neon PostgreSQL for relational data and the private
`zplus-packages` Neon Object Storage bucket for archives. New package objects
are stored under version-specific keys; PostgreSQL tracks their key, byte
size, SHA-256, and publication state. Uploads are read back and checked before
a version is published. Downloads pass through the registry API, which checks
the recorded size and SHA-256 before returning bytes. Existing legacy
`blob_url` versions remain downloadable.

The existing Vercel project needs these server-side environment variables for
each configured Development, Preview, and Production environment:

| Variable | Purpose |
| --- | --- |
| `DATABASE_URL` | Pooled Neon connection for registry API requests. |
| `AWS_ACCESS_KEY_ID` | Neon Object Storage branch credential. |
| `AWS_SECRET_ACCESS_KEY` | Neon Object Storage branch secret. |
| `AWS_ENDPOINT_URL_S3` | Neon branch's S3 endpoint. |
| `AWS_REGION` | Neon region, such as `us-east-2`. |
| `ZPM_PUBLIC_ORIGIN` | Optional; defaults to the canonical origin in `website/ecosystem-metadata.json`. |

Use the matching Neon branch's environment for each Vercel environment.
`DATABASE_URL_UNPOOLED` is for schema migrations and should remain in a
secure local migration environment rather than the runtime. Never commit
connection strings or storage credentials. Apply migrations 001 and 002, in
order, to the intended branch using its direct connection. Deployment and
branch-specific environment setup are documented in
[website/README.md](../website/README.md). Production migrations and deploys
require explicit approval.

The API routes used by the portal and ZPM include:

- `POST /auth/register`, `POST /auth/login`, `POST /auth/cli-login`,
  `POST /auth/logout`, and `GET /auth/me`.
- `GET /search?q=...`, `GET /packages/{name}`,
  `GET /packages/{name}/resolve?range=...`,
  `GET /packages/{name}/versions/{version}`, and
  `GET /packages/{name}/versions/{version}/download`.
- `POST /publish`.

CLI login returns a bearer token; browser sessions use HttpOnly cookies.
Publishing requires an active authenticated account that owns the package or
is an authorized maintainer. A package version cannot be replaced after
publication.

Create an account through the existing website's account UI, then use
`zpm login` before running `zpm publish`. Use `zpm logout` to revoke the saved
session. Passwords are stored as scrypt hashes, CLI bearer tokens are stored
with platform-appropriate protections, and sessions can be revoked. Email
verification and password recovery are not implemented.

The local client-to-registry integration scenario can be run after building
the Debug or Release tools:

```powershell
Push-Location website
npm run test:client -- ..\build\Release\zpm.exe ..\build\Release\zpbuild.exe ..\build\Release\zp.exe
Pop-Location
```

It starts an isolated test registry, builds and publishes two Z+ packages,
then verifies registry search/info, transitive dependency downloads,
lockfile installation, ZPBuild execution, interpreter imports, and cached
offline restoration. It does not publish to the official website.

Website unit tests use PGlite and an in-memory storage adapter and are not a
substitute for validating an isolated Neon branch. The current website is
static and has no `npm run build` script.

ZPM protects saved Windows tokens with DPAPI and stores Unix tokens in a
user-only-permission configuration file. Package uploads are validated as
ZPM1 archives, are not executed, and cannot replace an existing package
version. Ownership and maintainer changes are authenticated and audited.
`zpm publish` builds the package with ZPBuild before upload. The browser
upload endpoint validates archive structure, manifest, and source-file
presence but does not compile untrusted Z+ code; use the CLI publishing path
for a successful local build before publication.

ZIP input, development dependencies, package installation hooks, and
server-side compilation are not supported. Local path dependencies refer to
the current filesystem and are not independently portable; use cached package
versions or a self-contained ZPM archive for offline installs. ZPBuild owns
the project build configuration and compiler diagnostics; packages do not
contain or execute custom build hooks.
