# Z+ Windows installer

> Archived Z+ 1.1.2 native GUI installer workflow. The current Z+ 1.1.5
> Windows release installer is built with Inno Setup; see
> [installer/inno/README.md](../inno/README.md). The version-locked 1.1.2
> workflow and release files below are retained for historical builds.

The setup application and installation workflow are written in Z+. The
`[build].windows_gui = true` project setting emits a PE executable with the
Windows GUI subsystem, so Explorer launches it without creating a console.
Native Z+ mouse-event identifiers are now returned using the same numeric
encoding as other graphics event values, allowing the wizard's click
dispatcher to match real Windows input. The native
Windows bindings used by the installer live in `runtime/installer` and are
exposed through the Z+ standard library. The current installer targets Windows
x64; Linux packaging is not enabled because this UI and its system integration
use Windows APIs.

## Build

From the repository root, build the compiler and ZBuild in Release mode:

```powershell
cmake --build build-zbuild --config Release --target zp zbuild
```

Then assemble the installer and its payload:

```powershell
.\installer\windows-Z+\BuildRelease.ps1
```

The script builds the project through ZBuild, stages only the compiler,
standard library, runtime DLLs, ZBuild templates, documentation, examples,
license, and third-party notices, and creates a SHA-256 manifest for every
payload file. It includes the official dark-surface logo in the installer UI.
It refuses to overwrite an existing versioned release directory. To build
from an existing, version-matched compiler distribution instead of
`build-zbuild\release-staging`, pass `-CompilerDistributionPath` and that
distribution's `bin` directory; the script still verifies the compiler
version before packaging.

## Output

The release is placed under `release\zplus-1.1.2-windows-x64\` and contains:

- `setup.exe` and the adjacent ZGraphics/SQLite runtime dependencies.
- `payload\` with the installable distribution.
- `payload.sha256`, checked against every selected file before deployment.
- `INSTALLER-README.md`, both applicable license/notice files, and
  `SHA256SUMS.txt` for the outer installer package.

The setup supports fresh installations into an empty directory, optional
documentation and example files, per-user PATH and `.zp` association
registration, a Start Menu uninstaller shortcut, verification of the deployed
compiler and standard library, and manifest-based removal that preserves
modified or untracked files. An `install.log` is retained in the installation
directory. Windows removes the running uninstaller after the next restart.

## Tests

Run the focused Windows API and installer workflow suites with:

```powershell
ctest --test-dir build-zbuild -C Release -R "installer_platform_api|zplus_installer_workflow" --output-on-failure
```

These tests cover native/interpreter API parity, payload hash validation,
copying, optional components, path traversal rejection, failed-install
rollback, preservation of modified and untracked files during uninstall,
installer hit regions, and the PE console/GUI subsystem values. The actual
release wizard still requires an interactive Windows desktop for full
navigation and system-integration testing.

The manifest detects accidental payload changes but is not a publisher signature. The release
executable is not Authenticode-signed. An existing Z+ install must currently
be uninstalled before installing another version. Environment integration is
limited to the current user, and the GUI needs an interactive Windows desktop.
