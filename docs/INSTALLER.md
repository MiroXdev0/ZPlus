# Z+ Windows installer

The Windows x64 setup application is a per-user installer for Z+ 1.1.2 Stable.
Its default destination is `%LOCALAPPDATA%\Programs\ZPlus`; it does not require
administrator access. It packages the compiler, its built runtimes, the
standard library, the ZBuild tool and templates, documentation, examples, the
Z+ license, and third-party notices. Optional components are selectable in the
installer.

The installer is distributed with a `payload` directory and a signed-by-hash
`payload-manifest.json`. The SHA-256 of that exact manifest is embedded in the
setup executable at release-build time. Each selected file is verified against
the embedded manifest before it is staged. This detects accidental or
uncoordinated payload changes; it is not a publisher digital signature and
does not establish who built the package.

## Install and uninstall

Run `ZPlus-1.1.2-Setup.exe` and follow the welcome, destination, component, and
confirmation steps. Installation adds only the installed `bin` directory to
the current user's PATH, registers a `.zp` association that opens source in
Notepad (with a separate "Run with Z+" context-menu action), and creates Start
Menu shortcuts. A desktop shortcut is optional. Existing unrelated PATH
entries and environment variables are preserved.

The installed `ZPlusUninstall.exe` removes only unchanged files recorded as
installed, the installer-created PATH entry and associations, and shortcuts
that still point to this installation. Modified or untracked files are kept.
If files remain, the installation directory is left in place for the user to
review. Uninstall does not remove projects outside the installation directory.
The same setup executable supports automation:

```powershell
ZPlus-1.1.2-Setup.exe --install-dir "$env:TEMP\ZPlus-Test" --quiet
ZPlus-1.1.2-Setup.exe --uninstall --install-dir "$env:TEMP\ZPlus-Test" --quiet
```

Quiet mode is intended for testing and managed deployment; it does not disable
payload verification, compiler verification, or rollback. The uninstaller does
not delete untracked user data even when invoked quietly.

## Build the Windows release

Requirements: Windows x64, CMake 3.20+, Visual Studio 2022/2026 C++ workload,
.NET 10 SDK with Windows Desktop targeting packs, and the project dependencies
used by the CMake build (Box2D, miniaudio, and the SQLite runtime discovered by
CMake). An internet connection may be needed on the first CMake build to fetch
the pinned Box2D and miniaudio sources. The setup build itself has no
third-party NuGet package dependencies.

From the repository root:

```powershell
cmake -S . -B build-zbuild
cmake --build build-zbuild --config Release --target zp zbuild
ctest --test-dir build-zbuild -C Release --output-on-failure
.\installer\windows\BuildRelease.ps1
```

`BuildRelease.ps1` builds the compiler and setup, assembles and hashes the
payload, runs installation lifecycle tests in a disposable user directory,
and writes the distributable archive to `release\windows\ZPlus-1.1.2-Windows-x64.zip`.
The archive contains the setup executable, its verified payload, the logo, and
release documentation. The build does not download or execute arbitrary
software.

## Verification and platform scope

Setup checks the compiler's reported version, required runtime files, standard
library module resolution, `zp check`/`zp run`, and a native Windows x64
compile-and-run smoke test. Failure is reported with the process output, logged,
and does not register a successful installation.

This release artifact is for Windows x64 only and has not been signed with an
Authenticode certificate. The project has a Linux x86-64 compiler build path,
but a Linux installer is not included in this Windows-built release because
its package and install lifecycle were not built or tested on Linux.
