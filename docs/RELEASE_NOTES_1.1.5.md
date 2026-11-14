# Z+ 1.1.5

Z+ 1.1.5 is a Windows x64 maintenance release focused on release consistency,
installer transparency, and executable metadata.

## Changes

- The compiler reports the CMake project version, avoiding separate compiler
  and project version constants.
- Windows compiler, ZBuild, and Z+ runtime binaries include the release
  version, publisher, product, and file-description metadata.
- MSVC release binaries use deterministic linking (`/Brepro`); clean rebuilds
  in the same configured environment produced matching SHA-256 hashes.
- The Inno Setup build includes publisher copyright metadata and packages the
  verified `bin\` distribution using the required Inno Setup 7.1.1 compiler.
- Added release hashes and an antivirus false-positive submission guide.

No Z+ language syntax or runtime behavior was changed in this maintenance
release. The release binaries are unsigned because no trusted Authenticode
code-signing certificate was available. These changes do not constitute
antivirus-vendor clearance and cannot guarantee that heuristic detections will
not occur.
