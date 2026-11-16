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
by recursive directory cleanup.

For build requirements and commands, see `installer\inno\README.md` in the
source repository. The source-tree release build accepts an isolated
distribution directory with `-BinaryDirectory`; it does not need to overwrite
the repository's tracked `bin\` distribution.
