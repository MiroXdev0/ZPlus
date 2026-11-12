Z+ 1.1.2 Windows x64 Portable Distribution

This directory is a relocatable compiler distribution. Keep the executable
files beside the runtime DLLs, stdlib directory, and zbuild-templates folder.

Quick start
-----------
1. Run setup-path.bat to add this directory to your current user's PATH.
2. Open a new terminal.
3. Check the tools with:
       zp --version
       zbuild --version
4. Create a Z+ source file and use:
       zp check hello.zp
       zp run hello.zp
       zp build hello.zp -o hello.exe

ZBuild projects can be built with `zbuild build`, run with `zbuild run`, and
tested with `zbuild test`. Built-in project templates are listed by
`zbuild templates`.

The standard library is in stdlib\ and is discovered beside zp.exe and
zbuild.exe. No repository checkout or development build directory is needed.
Native programs that use runtime APIs may require the applicable runtime DLLs
to remain beside the generated executable; ZBuild stages required runtimes
for supported project targets.

Documentation is in SYNTAX.md, MODULES.md, NATIVE_BACKEND.md, docs\, and
stdlib\README.md. Starter projects are in zbuild-templates\. The examples\
directory contains sample Z+ programs.

The native compiler target is Windows x64. Some language APIs and features
are interpreter-only or platform-specific; see NATIVE_BACKEND.md and
docs\BACKEND.md for current limits. Native try/catch/throw/defer are not
supported and are rejected during native compilation.
