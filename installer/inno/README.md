# Z+ Windows installer (Inno Setup)

`ZPlus.iss` builds a per-user Windows x64 installer from a verified Release directory. The script reads the Z+ version from the selected `zp.exe` and verifies the staged compiler matches it byte-for-byte by SHA-256. It also verifies the ZPBuild aliases and ZPM versions, then stages only the shipped command-line tools, runtime DLLs, standard library, templates, documentation, and examples. It excludes build outputs such as unit-test executables, benchmarks, linker libraries, and export files. `-BinaryDirectory` selects the verified Release directory; `-OutputDirectory` selects the release artifact destination.

## Build

1. Install **Inno Setup 7.1.1** from [jrsoftware.org](https://jrsoftware.org/isinfo.php).
2. From the repository root, build Release and generate the installer directly in `bin\`:

   ```powershell
   cmake -S . -B build
   cmake --build build --config Release
   .\installer\inno\Build-Installer.ps1 `
     -BinaryDirectory .\build\Release `
     -OutputDirectory .\bin
   ```

   The build script checks `PATH` and common Inno Setup 7 installation locations, then verifies that `ISCC.exe --version` reports exactly `7.1.1`. It will not use another version. If the compiler is installed elsewhere, pass its location:

   ```powershell
   .\installer\inno\Build-Installer.ps1 `
     -BinaryDirectory .\build\Release `
     -OutputDirectory .\bin `
     -IsccPath "C:\Program Files\Inno Setup 7\ISCC.exe"
   ```

The installer preserves the distribution layout, including `zp.exe`, `zpbuild.exe`, the `zbuild.exe` compatibility CLI, `zpm.exe`, runtime DLLs, standard-library modules, and ZPBuild templates. It also includes the repository documentation, examples, license, and third-party notices. The setup icon, Inno wizard image, and installed shortcuts are generated from the official light-surface logo in `logo\`; the original logo remains unchanged. Temporary staging files are removed after the build.

On success, `bin\` contains the installer `ZPlus-<version>-Setup.exe`, its `.sha256` file, and a versioned Windows x64 release manifest. The script verifies the installer metadata and records Z+, ZPBuild, and ZPM versions. It does not download release assets or require the GitHub Releases API.

## Installation behavior

- Installs to `%LOCALAPPDATA%\Programs\ZPlus` by default without administrator privileges.
- For a new installation, requires the selected folder to be empty (or not yet exist), preventing silent replacement of unrelated files. Registered upgrades and repairs use the existing installation directory.
- Uses the registered installation directory for upgrades and same-version repairs. It refuses a downgrade from a newer registered version and refuses to relocate an existing installation during upgrade, preventing an orphaned old directory or PATH entry. Uninstall first to move the installation.
- Places the tools and runtime DLLs in `{app}\bin`, with the standard library and templates alongside them; installs documentation and examples under `{app}`. Repair and upgrade replace files that belong to the shipped payload.
- Adds only this installation's `bin` folder to the current user's PATH, without duplicating an existing entry.
- Provides Start Menu shortcuts and an unchecked optional desktop shortcut.
- On uninstall, removes only this install's PATH entry and registered product files. No recursive directory cleanup is configured, so user-created, untracked files remain in place.
- The build script rejects missing distribution inputs before invoking Inno Setup and emits an installer, SHA-256 sidecar, and release manifest only after successful compilation.

The installer version is taken from `zp version` and matches the CMake project
version embedded in the compiler and runtimes. Release executables contain
Windows version metadata identifying Z+ and the publisher. The build remains
unsigned unless a trusted Authenticode certificate is used by a release
operator; this repository does not include signing credentials.
