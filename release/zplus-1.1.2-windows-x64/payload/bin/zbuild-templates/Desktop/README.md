# {{PROJECT_NAME}}

A ZGraphics desktop application created in {{YEAR}}. Author: {{AUTHOR}}

Run `zbuild build` to check the project and `zbuild run` to open the window. The template uses the interpreter backend on Windows and Linux; Linux requires an X11 development/runtime environment and an available display. Windows x64 native applications can be built with `zbuild build --release`, but Linux native ZGraphics linking is not implemented.

The window renders text and a clickable counter. Press **Escape** to exit. `src/window.zh` owns window creation and frame drawing; `src/ui/layout.zh` contains the UI layout. `assets/` is staged automatically under the selected build profile and included by `zbuild package`.

The ZGraphics API supports text, shapes, native input events and buffered updates. See the repository's ZGraphics documentation for platform key-code details and API behavior.
