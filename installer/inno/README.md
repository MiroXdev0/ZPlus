# Z+ Windows installer (Inno Setup)

`ZPlus.iss` builds a per-user Windows x64 installer from a verified distribution directory. By default it uses this repository's `bin\`; `-BinaryDirectory` can select an isolated Release staging directory. The script reads the Z+ release version from `zp.exe`, verifies `zpbuild.exe` and the legacy `zbuild.exe` report the same independent ZPBuild version, validates ZPM's version output, and packages the supplied binaries rather than rebuilding or substituting compiler outputs.

## Build

1. Install **Inno Setup 7.1.1** from [jrsoftware.org](https://jrsoftware.org/isinfo.php).
2. On Windows PowerShell, run:

   ```powershell
   .\installer\inno\Build-Installer.ps1
   ```

   The build script checks `PATH` and common Inno Setup 7 installation locations, then verifies that `ISCC.exe --version` reports exactly `7.1.1`. It will not use another version. If the compiler is installed elsewhere, pass its location:

   ```powershell
   .\installer\inno\Build-Installer.ps1 -IsccPath "C:\Program Files\Inno Setup 7\ISCC.exe"
   ```

The installer preserves the distribution layout, including `zp.exe`, `zpbuild.exe`, the `zbuild.exe` compatibility CLI, `zpm.exe`, runtime DLLs, standard-library modules, and ZPBuild templates. It also includes the repository documentation, examples, license, and third-party notices. The setup icon, Inno wizard image, and installed shortcuts are generated from the official light-surface logo in `logo\`; the original logo remains unchanged. Temporary staging files are removed after the build.

On success, the installer is written to `installer\inno\output\ZPlus-<version>-Setup.exe`; the script prints its full path and verifies that the file exists. The script does not download release assets or require the GitHub Releases API.

## Installation behavior

- Installs to `%LOCALAPPDATA%\Programs\ZPlus` by default without administrator privileges.
- Preserves `bin\`, including its adjacent standard library, templates, and runtime DLLs; installs documentation and examples alongside it.
- Adds only this installation's `bin` folder to the current user's PATH, without duplicating an existing entry.
- Provides Start Menu shortcuts and an unchecked optional desktop shortcut.
- On uninstall, removes only this install's PATH entry and files registered as part of the installer. No recursive directory cleanup is configured, so user-created or modified untracked files are left in place.

The installer version is taken from `zp version` and matches the CMake project
version embedded in the compiler and runtimes. Release executables contain
Windows version metadata identifying Z+ and the publisher. The build remains
unsigned unless a trusted Authenticode certificate is used by a release
operator; this repository does not include signing credentials.
