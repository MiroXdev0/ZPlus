# Z+ 1.3.0 for Windows x64

This directory contains the Z+ 1.3.0 Windows x64 per-user installer built
from the Release configuration with MSVC 19.51 and Inno Setup 7.1.1.

## Artifact

- `ZPlus-1.3.0-Setup.exe` — compiler, ZPBuild (`zpbuild` and the `zbuild`
  compatibility alias), ZPM, runtime DLLs, standard library, templates,
  documentation, examples, and license notices.
- `checksums.txt` — SHA-256 checksum for the installer.

Installer size: 7,005,801 bytes.
SHA-256: `BDCD9112D61A7F59A8B008D7F79F0BEC6B7C5A430623DD49CE02A5FF2A5A14E5`.

There is no portable ZIP: the project does not define a separately verified
portable distribution.

## Verify

In PowerShell, check the installer checksum with:

```powershell
Get-FileHash .\ZPlus-1.3.0-Setup.exe -Algorithm SHA256
```

The compiler reports `Z+ Compiler 1.3.0`; ZPBuild and ZPM report `0.3.0`.
The installer is unsigned because no trusted Authenticode signing
certificate was available.

The Release build, executable version metadata, interpreter and native
compile/run smoke tests, standard-library import, and ZPBuild/ZPM local
package-resolution tests were verified. An isolated install, PATH and Start
Menu check, installed compile/run and package-resolution test, and uninstall
all passed. The pre-existing Z+ 1.1.2 installation was preserved. The full
46-test CTest run passed 45 tests; its feature-tour test could not run because
the expected root `test.zp` fixture is absent from the repository.
