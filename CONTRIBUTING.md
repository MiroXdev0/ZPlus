# Contributing to Z+

Thank you for considering a contribution. By intentionally submitting a
contribution for inclusion, you offer it under Apache-2.0 as described in
Section 5 of `LICENSE`. Do not submit code or assets you do not have the right
to license; if you intend a different term, state that before submission so
the maintainer can review it before accepting the contribution.

## Development environment

- CMake 3.20 or newer, Git, and a C++20 compiler.
- Windows: Visual Studio 2022 C++ workload and Windows SDK.
- Linux: C++ toolchain, libcurl development files, X11 development files, and
  the native build tools used by CMake.
- Initial CMake configuration fetches pinned Box2D and miniaudio source, so
  network access is needed unless those sources are already cached.
- Optional website development uses Node.js 20 or newer; see `website/README.md`.
- Optional Windows setup packaging uses the instructions in
  `installer/inno/README.md`; the experimental Z+ installer sources have a
  separate guide in `installer/windows-Z+/README.md`.

## Repository map

- `compiler/`: lexer, parser, semantic analysis, IR, and native backends.
- `runtime/`: interpreter/native runtime and standard-library implementation.
- `stdlib/`: Z+ module declarations and APIs.
- `zbuild/`, `zpm/`: build system and package manager.
- `ZGraphics/`: graphics layer.
- `tests/`: compiler, runtime, backend, integration, and tool tests.
- `docs/`, `MODULES.md`, `NATIVE_BACKEND.md`: language and tool documentation, including [`docs/syntax.md`](docs/syntax.md).
- `website/`, `Z+_VS_code/`: official website and editor integration.

## Build and test

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For single-configuration generators, omit `--config Release`. During compiler
work, run the narrowest relevant test first, then the full CTest suite. Native
tests must execute generated binaries on the target platform; compilation
alone does not establish runtime support. Website checks are run separately:

```sh
cd website
npm ci
npm run check
npm test
```

## Coding conventions

- Follow nearby style and keep changes scoped to the reported issue.
- Preserve interpreter/native behavior and test both paths for language changes.
- Add regression tests for bug fixes; do not weaken or skip existing tests.
- Keep diagnostics actionable and document backend/platform limitations.
- Avoid generated build outputs, credentials, local configuration, and unrelated
  formatting changes in pull requests.
- Preserve third-party license files and attribution notices.

## Contributions and pull requests

1. Search existing issues and discussions for duplicate reports.
2. For bugs, provide a minimal reproducer, command, compiler version, OS,
   backend, expected result, and actual result.
3. For feature proposals, describe the use case, syntax/API impact, backend
   implications, compatibility concerns, and test strategy before large work.
4. Keep a pull request focused. Summarize behavior changes, tests executed, and
   platform limitations. Link relevant issues and update documentation for
   user-visible changes.
5. Do not include binaries or generated files unless the change specifically
   updates a checked-in distribution artifact.

Issue forms are available for bug reports and feature proposals. For security
issues, use the private reporting instructions in `SECURITY.md` rather than a
public issue.
