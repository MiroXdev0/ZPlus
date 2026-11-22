(() => {
  window.ZPlusProducts = {
    compiler: {
      id: "compiler", title: "Z+ Compiler", eyebrow: "Language implementation",
      summary: "A C++ compiler, interpreter, and native code generator for the Z+ language.",
      status: "Stable releases are published through the official release feed.",
      sections: [
        { heading: "Two ways to execute", text: "`zp run` executes a checked program in the interpreter. `zp build` generates a native executable for the supported target and language subset; it does not silently fall back to interpretation.", code: "zp check hello.zp\nzp run hello.zp\nzp build hello.zp -o hello.exe" },
        { heading: "Language foundation", text: "The implementation includes typed variables and functions, control flow, arrays, modules, classes, enums, structs, and a growing standard library. See the language reference for constraints on constructors, visibility, aggregate types, and platform support.", href: "/docs/reference" },
        { heading: "Platform support", text: "The interpreter is available on Windows and Linux. Windows x64 has the broadest tested native runtime support. Linux native execution exists for a narrower set of operations; the compiler reports unsupported operations rather than promising parity that is not there.", href: "/docs/native" },
        { heading: "Get a published build", text: "The Downloads page reads actual release assets from the official GitHub Releases API. At the time of this page load, the published feed is the source for the current stable version and available platform installers.", href: "/download" }
      ]
    },
    zpm: {
      id: "zpm", title: "ZPM", eyebrow: "Package manager · 1.0.0",
      summary: "Resolve, install, lock, cache, inspect, and publish Z+ source packages.",
      sections: [
        { heading: "A reproducible project", text: "ZPM reads `zpm.json`, resolves semantic version constraints, and records selected package versions and content hashes in `zpm-lock.json`. `zpm install --locked` restores that exact dependency graph and fails if the lock cannot be honored.", code: "zpm init my-app\nzpm add example-package@^1.2.0\nzpm install --locked\nzpm list" },
        { heading: "Package locations", text: "Managed packages are installed under `z+_packages/`; verified archives and sources also use ZPM's local cache. Local directories and `.zpm` archives can be added without a registry connection. The cache can support offline restoration when the exact source is already present.", href: "/docs/zpm" },
        { heading: "Registry", text: "The official website has a working package registry API for search, version metadata, publication, and archive downloads. Packages are validated and integrity checked. Package code is not executed as an install hook.", href: "/packages" },
        { heading: "Scope", text: "ZPM owns package metadata, dependency resolution, lockfiles, caching, and package archives. ZPBuild owns project build configuration and orchestration; ZPM does not replace the compiler or linker." }
      ]
    },
    zpbuild: {
      id: "zpbuild", title: "ZPBuild", eyebrow: "Build system · 1.0.0",
      summary: "A project-oriented build engine that discovers dependencies and schedules configured targets.",
      sections: [
        { heading: "Project workflow", text: "Create a project with a built-in template, configure targets in `zproject.toml`, then build, run, or test through the project graph. `zpbuild` is the primary command; `zbuild` remains a compatibility alias.", code: "zpbuild init MyApp\ncd MyApp\nzpbuild check\nzpbuild build --release\nzpbuild test" },
        { heading: "Configuration", text: "The project manifest uses a documented TOML subset. Targets may depend on other targets; ready graph nodes can run in parallel, while dependencies complete first. Build signatures track compiler identity, project settings, source dependencies, and declared inputs.", href: "/docs/zpbuild" },
        { heading: "Build outputs", text: "Build artifacts are separated by profile under the configured build directory. `clean`, `rebuild`, cache inspection, dependency graph output, and structured project information support day-to-day maintenance and CI." },
        { heading: "Limitations", text: "Targets are executables and tests; the compiler does not expose a general object-library or archive-link target. Native support still follows the compiler's supported target subset." }
      ]
    },
    vscode: {
      id: "vscode", title: "VS Code extension", eyebrow: "Editor integration · 1.3.4",
      summary: "Official Z+ language support for Visual Studio Code, built around the installed compiler and ZPBuild.",
      sections: [
        { heading: "Install status", text: "The extension is not currently linked from a verified Marketplace listing or official release asset. The website does not provide a guessed Marketplace URL. Check the official release channels before installing a VSIX.", action: { href: "/download#vscode", label: "Check available downloads" } },
        { heading: "What it provides", text: "The extension recognizes `.zp` and `.zh` files, highlights Z+ syntax, provides snippets and compiler-backed checks, and exposes commands for checking, running, building, and project workflows. Workspace features include symbol lookup and navigation from the extension's source analysis; they are not a separate semantic Language Server.", href: "/docs/vscode" },
        { heading: "Tool paths", text: "Compiler, ZPM, and ZPBuild locations can be configured in VS Code settings. When left blank, the extension searches the system PATH. The editor delegates compilation and project builds to the real installed tools." },
        { heading: "Boundaries", text: "The extension does not include the compiler, provide a debugger, or claim type-correct IntelliSense for information unavailable to its analysis. Compiler diagnostics remain authoritative." }
      ]
    },
    zgraphics: {
      id: "zgraphics", title: "ZGraphics", eyebrow: "Graphics module · in development",
      summary: "A native window and 2D drawing layer exposed through the `zgraphics` module.",
      sections: [
        { heading: "Current platform backends", text: "Windows uses Win32 windows and GDI drawing. Linux uses X11 and requires development libraries at build time and a display at runtime. A macOS backend is not implemented. Native Windows executables use the packaged `zgraphics_runtime.dll`; Linux native linking is not implemented.", href: "/docs/zgraphics" },
        { heading: "Windows and drawing", text: "The current API covers window lifecycle, title and geometry, background, text, lines, rectangles and circles, frame updates, basic keyboard and mouse state, and queued events. It is a platform layer, not a full GUI toolkit.", code: "use zgraphics\n\nstart {\n    let app = zgraphics.createWindow(\"Z+ demo\", 720, 420)\n    zgraphics.background(app, \"#18202B\")\n    zgraphics.text(app, \"Hello from Z+\", 32, 36, \"#FFFFFF\", 28)\n    zgraphics.show(app)\n    zgraphics.run(app)\n} stop" },
        { heading: "Runtime behavior", text: "Drawing commands are collected offscreen and published by `update`, `show`, or `run`. Invalid handles, dimensions, colors, and drawing values report runtime errors. Interactive examples require a graphical desktop; headless environments can check them without opening a window." },
        { heading: "Documentation", text: "The module API and backend limits are documented in the repository's ZGraphics guide and in the compiler language reference.", href: "/docs/zgraphics" }
      ]
    }
  };
})();
