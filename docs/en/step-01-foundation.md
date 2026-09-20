# Step 1 — Core foundation

## Applicable spec

- §2.3.1 Errors: first-error latch, `GetError` returns + clears, failing
  commands are ignored (except `OUT_OF_MEMORY`, whose results are undefined),
  return 0, never write through pointers.
- §15.1.7 + §18: `DITHER` enabled by default; `DEBUG_OUTPUT` defaults to TRUE
  in debug contexts, FALSE otherwise.
- §20.2–20.3: `GetString` accepts `VENDOR/RENDERER/VERSION/EXTENSIONS/
  SHADING_LANGUAGE_VERSION`; `GetStringi` only accepts `EXTENSIONS`.

## Implemented API (module `tgles_core`)

`include/tgles/gl_types.h`, `error.h`, `context.h`:

- Types: `GLenum`, `GLboolean`, `GLint`, `GLuint`, `GLfloat`, ... plus
  `GL_TRUE/GL_FALSE`, `GL_NO_ERROR`, `GL_INVALID_ENUM/VALUE/OPERATION/
  OUT_OF_MEMORY/CONTEXT_LOST` constants.
- `ErrorQueue`: `Record()`, `Get()` (return + clear; one latch, with room
  for multiple flag-code pairs in distributed implementations),
  `HasPending()`.
- `Context`: `Create(debug)`, `Enable/Disable/IsEnabled`, `GetError`,
  `GetString`, `GetStringi`, `GetIntegerv(MAJOR/MINOR/NUM_EXTENSIONS/...)`.
- 13 core caps — see the table in `context.h`.

## Tests

`tests/test_step01_foundation.cpp` — 30+ cases: latch semantics, defaults,
`INVALID_ENUM` for unknown caps, VERSION/SL string formats, `GetStringi`
bounds, `MAJOR=3/MINOR=2`, independent multi-contexts.

## Gate

```sh
cmake --build build && ctest --test-dir build --output-on-failure
cmake -S . -B build-ios-dev -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 && cmake --build build-ios-dev
```

Both must be green before moving to Step 2.
