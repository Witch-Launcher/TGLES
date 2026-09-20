# EGL 1.5 layer (host integration, step A)

Spec: Khronos EGL 1.5 (Aug 27 2014, `docs/reference/egl-1.5-spec.pdf`),
header `docs/reference/egl.h` (MobileGL loader needs **46 EGL funcs**, all
`INIT_EGL_FUNC`). Module `EglState`
(`include/tgles/egl.h`, `src/egl.cpp`), tests written first:
`tests/test_egl_layer.cpp` (16 cases) + `tests/test_mobilegl_gaps.cpp`.

## Verified from the original spec

- Error latch §3.1: `GetError` returns + clears (second call → `SUCCESS`);
  codes `0x3000–0x300E` match `egl.h` value by value.
- `BindAPI`: the initial API is `EGL_OPENGL_ES_API`; anything else →
  `BAD_PARAMETER` (TGL hosts GLES only, not desktop GL).
- `CreateContext`: the default version is 1.0 → TGL has no 1.x so
  `BAD_MATCH`; 2.0/3.0/3.1 get a 3.2 context (backwards compatible);
  unknown versions → `BAD_MATCH`; `CLIENT_VERSION` aliases `MAJOR_VERSION`
  (footnote 18).
- `MakeCurrent`: full error matrix (`BAD_MATCH` for surface + `NO_CONTEXT`,
  valid draw + `NO_SURFACE` read, etc.).
- `GetProcAddress`: `NULL` means missing, never raises errors (§3.12).
  Full 46 EGL + required `glBufferStorageEXT`; optional EXT (`TextureView`,
  `MultiDraw`, timer) stays `NULL` by capability-gated design.
- `SwapBuffers`: pbuffer/single-buffered is a no-op but still `TRUE`;
  `SwapInterval` silently clamps into `[0, 4]`.
- Platform extras (fixed, previously missing): `GetConfigs` enumerates,
  `GetCurrentSurface` tracks DRAW/READ, `SurfaceAttrib` validates,
  `WaitClient/GL/Native` no-op success, `ReleaseThread` clears current;
  `CreateImage/DestroyImage` manage EGLImages with
  `BAD_CONTEXT/PARAMETER/MATCH`; pixmap/platform stubs are spec-correct.

## Remaining (iOS bridge)

`SwapBuffers` on window surfaces only counts swaps for now — real present
to `CAMetalLayer` belongs to the ObjC++ bridge on the iOS target (together
with Metal execution).
