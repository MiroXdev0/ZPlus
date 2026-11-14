# Z+ 1.1.5 antivirus false-positive submission information

## Status

No antivirus vendor detection name, alert, or sample report was provided for
this investigation. No binary was submitted to an antivirus vendor, and no
vendor has cleared these files. The observations below identify potential
reputation or heuristic contributors, not a confirmed cause for any specific
detection.

## Release files

Version: **Z+ 1.1.5**  
Platform: **Windows x86_64**  
Publisher string in Windows version metadata: **Miroslav Aleksandrov**  
Repository: <https://github.com/MiroXdev0/Z-lang-release>

The publisher string is ordinary PE metadata and is not proof of identity.
All listed Z+ executables, runtime DLLs, and the installer are unsigned because
no trusted Authenticode code-signing certificate was available. The SQLite
DLL is a third-party dependency and is also unsigned in this distribution.

| File | Purpose | SHA-256 |
| --- | --- | --- |
| `ZPlus-1.1.5-Setup.exe` | Per-user installer built with Inno Setup 7.1.1 | `8F1500C97854533C0C76CED5B311968B7899DEE4C8455588FFDACD687AD81A77` |
| `zp.exe` | Z+ compiler, checker, interpreter, and native compilation driver | `721770A3F643B88C6E3B91092B150FB972ED68DF9E03939AD5AEF56EABB7B528` |
| `zbuild.exe` | ZBuild project/build/run command-line tool | `7109A88905BC203371B1B694CC216EEE2E84A69FDFA720E82C09EE2558C7CEFF` |
| `zgraphics_runtime.dll` | Z+ graphics runtime | `2EC75776D45FAF69C54ACEBF95E8C457E42AF8B40F1EB79ED966F528AF69A758` |
| `zaudio_runtime.dll` | Z+ audio runtime | `D05CCD5455B88675F244D144F129A68D65191D0B2B9D76BBDA5B6D3EC97421EC` |
| `zphysics_runtime.dll` | Z+ physics runtime | `DFCD8D2ABB64703EA2AA4F448D640FEE62A6DDA00C6647FEF3FBA5CD40FC0364` |
| `zplus_input_runtime.dll` | Z+ input runtime | `4F08BD1ED4D7B9243457D44BD4B00CCE799B973FCD7DF5501D9832DDE549029A` |
| `sqlite3.dll` | Third-party SQLite dependency | `12B2E294FC93A175DCB6789E6B503780D899DF668F717FA55FBEAC97F4E3EBE9` |

The authoritative checksums are also in
[`ZPlus-1.1.5-SHA256SUMS.txt`](ZPlus-1.1.5-SHA256SUMS.txt) and the machine-readable
[`ZPlus-1.1.5-release-manifest.json`](ZPlus-1.1.5-release-manifest.json).

## What the software does

`zp.exe` checks and interprets Z+ source, and can invoke native compilation
tools when the user requests a native build. `zbuild.exe` manages Z+ project
templates, builds, and execution. The runtime DLLs implement Z+ graphics,
audio, physics, and input functionality. The installer is a per-user Inno Setup
package: it installs the distribution under the user's local application-data
programs directory, offers an optional desktop shortcut, and adds its `bin`
directory to that user's PATH. It does not need administrator elevation or
perform release-time network downloads.

## Investigation findings

- The previous compiler/runtime binaries and installer were Authenticode
  unsigned. Lack of a trusted signature can reduce download reputation and
  makes publisher authenticity harder to establish.
- The previous compiler/runtime binaries lacked Windows version-resource
  metadata. The 1.1.5 first-party PE files now carry product, file-version,
  publisher-string, and description metadata. This metadata is not a digital
  signature.
- The inspected compiler/runtime PE section layouts were conventional x64
  MSVC outputs; no packer indicators were found. The installer uses Inno Setup
  compression, whose appended compressed payload is expected for this format
  and is not evidence that the program binaries are packed.
- The installer uses per-user privileges, changes only the current user's
  PATH, and records whether it added its own PATH entry so it can remove that
  entry during uninstall. These behaviors are visible in the installer
  configuration.
- Clean Release rebuilds in the configured environment yielded identical
  hashes for the compiler, ZBuild, and four Z+ runtime DLLs. Reproducible output
  helps investigation but does not establish that a vendor will consider a
  file safe.

Without vendor detection names or sample reports, it is not possible to say
which of these factors, if any, caused a particular alert. No claim is made
that these changes eliminate antivirus detections.

## Submitting a sample

For Microsoft Defender, use Microsoft's official
[security intelligence file submission portal](https://www.microsoft.com/en-us/wdsi/filesubmission).
Choose the incorrect-detection path, provide the exact affected file and its
SHA-256, identify it as Z+ 1.1.5 from the official repository, and include the
vendor's detection name and relevant alert details. Submit only files that
match the hashes above. If a binary is rebuilt, use the new release's hashes
and do not reuse this report for that different file.

For another product, use that vendor's current official false-positive/sample
submission channel. Do not send credentials, private source, or unrelated
personal data. Record the vendor case number and outcome; until the vendor
responds, describe the file only as under review, not cleared.
