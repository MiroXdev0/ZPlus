# Changelog

## 1.3.5

- Verified extension checks, diagnostics, compiler discovery, and run/build commands against Z+ compiler 1.4.0; no compiler compatibility range is claimed.
- Updated release version metadata and packaged the extension as a local VSIX.

## 1.3.4

- Prepared the 1.3.4 Marketplace release with the existing official Z+ proprietary license and website metadata.

## 1.3.3

- Aligned the extension license file and package metadata with the root Z+ Proprietary Software License.
- Removed source-repository Marketplace metadata, set the official Z+ website as the homepage, and clarified that the Z+ language and compiler are proprietary.
- Cached imported-module symbol graphs between editor requests and invalidated them when editor buffers or `.zp`/`.zh` files change.
- Prefer unsaved open module buffers during symbol resolution and retain separate qualified views when a module is imported under multiple aliases.
- Clear outdated compiler diagnostics as soon as a source file changes and cancel active checks, runs, and builds for that document.
- Added cancellable Run and Build progress, process cleanup, and timeout reporting.
- Prevented older overlapping Test Explorer scans from replacing newer project discovery results.
- Limited Format Selection edits to complete selected lines so partial boundary lines remain unchanged.
- Added large-workspace and selection-boundary coverage; prepared the release artifact under `bin/`.

## 1.3.2

- Added cached workspace indexing for document and workspace symbols.
- Added project-aware references and rename edits for declarations and importing files.
- Added type-definition navigation for unambiguously indexed aggregate types.
- Added syntax-aware document and selection indentation formatting for brace and legacy block syntax.
- Added a ZPBuild template project creation command and modern `zpbuild` discovery with `zbuild` fallback.
- Added Test Explorer project discovery and cancellable `zpbuild test` execution.
- Added project-scoped Build, Run, and Test tasks using VS Code's integrated task terminal.
- Cancelled superseded compiler checks when a document changes or closes.
- Improved compiler discovery using workspace build outputs and the standard per-user install location before PATH.
- Kept compiler diagnostics authoritative and updated compiler integration coverage for current Z+ block syntax.
- Added valid and intentionally invalid multi-line compiler stress fixtures for diagnostic coverage.
- Bounded workspace source reads and pruned deleted-file entries from the symbol index cache.
- Split project tasks and Test Explorer integration into a dedicated project-tooling module.
