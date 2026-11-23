# {{PROJECT_NAME}}

A portable Z+ console application created in {{YEAR}}. Author: {{AUTHOR}}

Build and run with `zbuild build` and `zbuild run`. The interpreter backend is the default for portability. Pass words after `--` to provide command-line arguments, for example `zbuild run -- Ada`.

The entry imports `zp.args` and `zp.string`, then calls the local `commands/greeting.zh` module. The `tests/` directory is ready for test entry programs. `include/`, `assets/`, and `build/` are included for compatibility with the standard project layout.

Z+ standard modules are shipped with the compiler installation. For a relocated compiler, configure `ZPLUS_STDLIB_PATH` to the installed `stdlib` directory if it is not already configured.
