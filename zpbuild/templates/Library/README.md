# {{PROJECT_NAME}}

Reusable Z+ declaration modules created in {{YEAR}}. Author: {{AUTHOR}}

## Build and test

Run `zbuild build` to compile the test executable and `zbuild test` to run it.

## Use the source library

Copy the `src/` directory into a consumer project (or configure a local dependency path), then import the public declaration module:

```zp
use "path/to/{{PROJECT_NAME}}/src/main.zh" as my_library
```

Call `my_library.greeting(name)` or `my_library.bounded(value, lower, upper)`. Imported Z+ modules must be `.zh` declaration files, and their paths are resolved relative to the importing source.

The current compiler does not produce static or shared library artifacts or link library targets. This template is an ordinary reusable source module with a test executable, not a prebuilt library binary.
