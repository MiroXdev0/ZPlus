# Z+ Windows installer

The current Windows x64 release is a per-user Inno Setup installer. Its
version is read from the bundled `zp.exe`, so the installer and compiler
metadata stay aligned. It includes the compiler, ZPBuild (`zpbuild` and its
`zbuild` compatibility alias), ZPM, runtime DLLs, standard-library modules,
project templates, documentation, examples, and license notices.

The default installation is `%LOCALAPPDATA%\Programs\ZPlus` and does not
require administrator privileges. Setup adds the installation's `bin`
directory to the current user's PATH, creates Start Menu shortcuts, and offers
an optional desktop shortcut. Uninstall removes the PATH entry only when this
installer added it. Files not registered with the installer are not removed
by recursive directory cleanup. Repair and upgrade replace files that are part
of the shipped product payload.

For a new installation, the selected destination must be empty or not yet
exist. This prevents Setup from silently replacing unrelated files in an
existing folder.

When upgrading or repairing an existing installation, Setup reuses its
registered directory. It refuses to downgrade a newer registered version or
move an existing installation to another directory, which could otherwise
leave the old files and PATH entry behind. To move Z+, uninstall it first and
then install to the new directory. Same-version repair and upgrades from older
versions remain supported.

For build requirements and commands, see `installer\inno\README.md` in the
source repository. The release workflow stages the complete Release
runtime distribution (tools, required DLLs, standard library, and templates)
and writes the installer, checksum, and release manifest directly to the
repository-root `bin\` directory. Unit-test executables and development linker
files are not included.
