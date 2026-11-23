# {{PROJECT_NAME}}

An interactive Z+ game starter created in {{YEAR}}. Author: {{AUTHOR}}

## Build and run

From this directory, run `zbuild build` and `zbuild run`. This project uses the ZGraphics interpreter backend by default so it can run on Windows and Linux with a graphical display. On Windows, `zbuild build --release` can be used to produce a native executable; native ZGraphics linking is Windows-only. Linux native graphics executables are not supported.

The window opens at 720x420. Move the red player with **W/A/S/D** and collect the gold circle. Press **Escape** or close the window to quit. Keyboard key codes are interpreted according to the host platform's native graphics backend.

## Project layout

- `src/game/` contains player behavior, world drawing, and game state helpers.
- `src/systems/` contains keyboard input and collision helpers.
- `assets/textures`, `assets/audio`, and `assets/fonts` are ready for project resources. Put files there; ZBuild stages them under `build/<profile>/assets/` and includes them when packaging.
- `tests/` is reserved for test entry programs.

Z+ imports declaration modules from `.zh` files; the compiler cannot import `.zp` implementation files as modules. Graphics calls use the public `zgraphics` module API.
