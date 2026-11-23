# ZGraphics

ZGraphics is a graphics module for Z+. The current implementation opens real native windows and draws text and basic shapes. It is an initial platform layer, not a complete GUI toolkit.

## Platform backends

- **Windows:** Win32 windows and GDI drawing into retained offscreen bitmaps; links against the system `user32` and `gdi32` libraries.
- **Linux:** X11 windows and Xlib drawing into retained pixmaps; requires X11 development headers and libraries at build time and an accessible X display at runtime.
- **macOS:** not implemented.

The public C++ facade is in `ZPGraphics/ZGraphics.hpp`; native implementations are separated under `ZPGraphics/Platform/`. Z+ calls use the `zp.graphics` wrapper in `stdlib/zp/graphics/graphics.zh`. `zp run` dispatches through the interpreter; Windows x64 native programs call the same C++ graphics implementation through the packaged `zgraphics_runtime.dll`.

## Build and run

Configure and build with the repository's normal CMake commands:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

On Linux, CMake requires X11 development packages. To locate the source standard library at runtime, the compiler uses its configured source-tree path; set `ZPLUS_STDLIB_PATH` to the `stdlib` directory when running a relocated compiler.

Run an example with the interpreter:

```powershell
build\Release\zp.exe check examples\zgraphics\hello_window.zp
build\Release\zp.exe run examples\zgraphics\hello_window.zp
```

Linux equivalents are `build/zp check examples/zgraphics/hello_window.zp` and `build/zp run examples/zgraphics/hello_window.zp`. Running an example opens a real window; close it to return to the shell.

On Windows x64, native executables are supported:

```powershell
build\Release\zp.exe build examples\zgraphics\hello_window.zp -o build\Release\hello_window.exe
build\Release\hello_window.exe
```

The compiler copies `zgraphics_runtime.dll` beside the requested executable. Keep the `.exe` and `.dll` together when moving or distributing the app. The runtime DLL is built with a static MSVC C++ runtime and links to the Windows system libraries Kernel32, User32, and GDI32; no separate graphics SDK is required to run it. Release builds install both `zp.exe` and the runtime DLL into `bin/`. Linux native linking is not implemented; use `zp run` there.

## Z+ API

Import the module with `use zp.graphics`. Its default namespace alias is `graphics`; an explicit alias can be supplied with `as`. Window handles are numeric IDs owned by the runtime's single graphics application. The `start { ... } stop` example below is complete:

```zp
use zp.graphics

start {
    let app = graphics.createWindow("Z+ demo", 720, 420)
    graphics.background(app, "#18202B")
    graphics.text(app, "Hello from Z+", 32, 36, "#FFFFFF", 28)
    graphics.rectangle(app, 32, 100, 220, 70, "#2563EB", true, 1)
    graphics.show(app)
    graphics.run(app)
} stop
```

### Window and drawing functions

| Function | Behavior |
|---|---|
| `createWindow(title, width, height) -> number` | Creates a hidden native window; dimensions must be positive and no greater than 16384. |
| `setTitle(window, title)` | Changes the title. |
| `setPosition(window, x, y)` | Moves the window to screen coordinates. |
| `resize(window, width, height)` | Requests a client size; dimensions must be between 1 and 16384. |
| `background(window, "#RRGGBB")` | Sets an opaque background color. |
| `show(window)`, `hide(window)` | Shows or hides the window. |
| `minimize(window)`, `maximize(window)`, `restore(window)`, `focus(window)` | Requests the corresponding platform operation. |
| `close(window)`, `isOpen(window) -> boolean` | Closes a window or checks whether it remains open. |
| `run(window) -> number` | Runs the native event loop until that window closes. |
| `text(window, value, x, y, color, size)` | Draws a text string at the specified position. |
| `line(window, x1, y1, x2, y2, color, thickness)` | Draws a line. |
| `rectangle(window, x, y, width, height, color, filled, thickness)` | Draws a filled or outlined rectangle. |
| `circle(window, x, y, radius, color, filled, thickness)` | Draws a filled or outlined circle. |
| `clear(window)` | Clears recorded drawing commands and redraws the background. |
| `numberText(value) -> string` | Formats a number for display. |

Colors must use exactly `#RRGGBB`. A new window defaults to black until a background color is set. Drawing commands are collected offscreen; `update` publishes the completed set once, while window exposure and resizing repaint the most recently completed frame. `show` and `run` also publish any pending drawing. This keeps incremental draw calls from exposing a partially rendered frame. Text size is limited to 1–512; line and outline thickness to 1–256. The APIs raise a runtime error for invalid handles, dimensions, sizes, thicknesses, colors, or frame presentation.

### Frame and input state

Call `update(window)` once per loop iteration before reading input. It waits to meet the configured frame interval, publishes the completed drawing pass from the preceding iteration, then processes pending native messages and updates per-window timing and input state. Initial drawing is published on the first `update`, `show`, or `run`. `setTargetFPS(window, fps)` accepts 0–1000; 0 disables frame limiting. Timing uses a monotonic clock; `deltaTimeMs(window)` reports elapsed milliseconds, clamped to 250, and `framesPerSecond(window)` reports the measured integer FPS. The GDI and X11 backends do not provide a portable VSync control; frame pacing uses the target-FPS limiter.

Call `update` once per frame before reading input. `keyDown`, `keyPressed`, and `keyReleased` provide current and per-update keyboard state. The mouse equivalents are `mouseButtonDown`, `mouseButtonPressed`, and `mouseButtonReleased`; `mouseX` and `mouseY` report the latest client position. A focus-loss event clears held keys/buttons and reports them as released. Key codes are platform-native (Win32 virtual keys or X11 key symbols); mouse buttons are 1=left, 2=right, 3=middle.

Example continuous loop:

```zp
use zp.graphics

start {
    let app = graphics.createWindow("Frame loop", 640, 360)
    graphics.show(app)
    while graphics.isOpen(app) {
        graphics.update(app)
        if graphics.keyDown(app, 27) {
            graphics.close(app)
        } end if
    } end while
} stop
```

### Events

`pollEvent(window)` returns the next queued event code, or `0` when none is queued. `update` also processes native messages and preserves their event records for subsequent polling. `waitEvent(window)` returns an already queued event first, otherwise blocks until an event is queued. Event details are per window:

| Code | Event | Details |
|---:|---|---|
| 0 | None | No queued event (`pollEvent` only). |
| 1 | Closed | Window close requested. |
| 2 | MouseDown | `eventX` / `eventY` give client coordinates. |
| 3 | MouseUp | `eventX` / `eventY` give client coordinates. |
| 4 | MouseMove | `eventX` / `eventY` give client coordinates. |
| 5 | KeyDown | `eventKey` gives the platform key code. |
| 6 | TextInput | `eventText` gives text produced by the key event. |
| 7 | KeyUp | `eventKey` gives the platform key code. |
| 8 | FocusGained | The window gained keyboard focus. |
| 9 | FocusLost | The window lost keyboard focus. |
| 10 | Resized | `eventX` / `eventY` give the client width and height. |
| 11 | MouseWheel | `eventX` / `eventY` give client coordinates; `eventKey` is wheel delta. |

`eventX(window)`, `eventY(window)`, `eventKey(window)`, and `eventText(window)` read the most recently polled or waited event. Key codes are platform-native and are not normalized between Win32 and X11.

The `buttons.zp` example implements a clickable area by checking mouse coordinates. It does not create a native button control or use callbacks.

`tests/graphics/rendering_smoke.zp` is a short animated test that continuously redraws a background, moving rectangle, and text. It is exercised through both the interpreter and native Windows runtime in the test suite.

### Geometry helpers

The Z+ module also provides `pointInRectangle(pointX, pointY, x, y, width, height)`, `rectanglesIntersect(firstX, firstY, firstWidth, firstHeight, secondX, secondY, secondWidth, secondHeight)`, `pointInCircle(pointX, pointY, centerX, centerY, radius)`, and `circlesIntersect(firstX, firstY, firstRadius, secondX, secondY, secondRadius)`. Rectangle edges use half-open point containment and strict overlap; circle tests include the perimeter. These are pure Z+ arithmetic functions, not native graphics calls.

## Examples

The `examples/zgraphics/` directory contains:

- `hello_window.zp` — window, background, and text.
- `shapes.zp` — lines, filled and outlined rectangles, and circles.
- `buttons.zp` — a hand-drawn button area driven by mouse events.
- `text_input.zp` — appends text input events to a displayed string.
- `calculator.zp` — a mouse-driven integer calculator.
- `multi_window.zp` — creates and shows two native windows.

These are interactive desktop programs. `zp check` validates their source and `zp run` launches them. All six were launched and smoke-tested on Windows; synthetic mouse/key input exercised the button, text-input, and calculator paths. That interactive run is not part of CTest and does not validate Linux behavior.

## Current limitations

- Native ZGraphics applications are supported only on Windows x64 and require `zgraphics_runtime.dll` beside the executable. Linux native graphics linking and macOS support are not implemented.
- The native backend currently supports integer numeric values; decimal literals and general floating-point operations are not supported.
- The language runtime has no object values or function references. Handles are numbers, APIs are module functions, and callbacks are unavailable.
- There are no native buttons, labels, text-input widgets, checkboxes, sliders, panels, layout managers, or callback registration.
- Minimum/maximum dimensions, fullscreen, VSync, polygons, ellipses, rounded rectangles, opacity, transforms, clipping, rich text styles, alignment, and text measurement are not exposed.
- Images/textures, sprites, reusable GUI controls, callbacks, and audio are not implemented.
- `update` supplies a basic frame limiter, elapsed time, and input state, but there is no separate presentation/batch API or high-resolution delta-time value exposed to Z+.
- Drawing commands persist until `clear`; callers should clear each frame to avoid retaining old draw operations.
- Windows text conversion handles basic UTF-8 input but does not combine all UTF-16 surrogate sequences. X11 rendering falls back to a legacy font when an UTF-8 font set is unavailable.
- Linux and macOS behavior has not been validated in this Windows environment. Linux requires a live X display; compilation alone would not validate GUI behavior.
- The runtime uses one application per interpreter process. It releases native windows at shutdown; there is no explicit garbage collection or independently destroyable handle object.

## Tests

`zgraphics_core` tests color parsing and, on Windows, hidden-window drawing and lifecycle. `zgraphics_module_check` checks that the public module resolves and type-checks. `zgraphics_interpreter` runs a hidden-window smoke test on Windows. Native integration tests execute the generated PE against the DLL, exercise drawing calls and error reporting, and check runtime packaging and missing-runtime diagnostics. The six visible example windows were also launched in a Windows desktop session and closed through native window messages; button, text, and calculator examples received synthetic input events.
