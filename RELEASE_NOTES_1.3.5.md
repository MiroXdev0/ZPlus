# Z+ Compiler 1.3.5

Z+ 1.3.5 is the final stabilization release of the 1.3.x series. ZPM and
ZPBuild remain at 1.0.0.

## Included and verified

- Compiler release metadata and the CLI version identify 1.3.5.
- The release regression suite covers the existing parser, interpreter,
  native backend, runtime, standard library, package manager, build system,
  and installer workflows.
- Parser recovery now guarantees token progress when synchronization stops on
  a token that the enclosing parse loop would otherwise retry. Regression
  cases cover malformed switch terminators, malformed struct and enum members,
  legacy switch recovery, and an imported class member using the unsupported
  `pub` modifier. These cases terminate with diagnostics; class visibility
  modifiers remain outside the 1.3.5 language.
- The existing imported ZShell project passes `zp check`, `zp dependencies`,
  and `zp build`. Its interactive `zp run` displays the greeting and prompt;
  the application remains in its command loop by design.
- The input runtime changes present in this worktree support prompted and
  unprompted line and typed input, with interpreter/native Windows coverage,
  EOF and conversion error handling, and native string-rooting stress cases.
- The Windows x64 release was built with Inno Setup 7.1.1. The generated
  installer, manifest, and SHA-256 sidecar were checked; an isolated install
  ran the installed project workflow and uninstalled cleanly.
- The complete Windows Release CTest run passed 50/50 tests, including package
  resolution, ZPBuild workflows, runtime/input tests, native execution, and
  installer workflow tests.

## Compatibility

No intentional language or package-format incompatibility is introduced by
the version update. ZPM and ZPBuild versions are unchanged. The compiler's
native backend continues to have a narrower supported platform and feature
set than the interpreter; see `NATIVE_BACKEND.md`.

## Limitations

Linux native runtime behavior and installation are not claimed as verified
unless the release test run is executed on Linux. The architecture work
recommended for 1.4.0 is described in `docs/1.4.0-ROADMAP.md`.
