# MobileGL support matrix (31 gates)

Module `tgles::mobilegl` (`mobilegl_support.h`): every condition a GLES 3.2
host must satisfy for MobileGL-DirectGLES — each gate cites evidence
(MobileGL `dev` branch source, MobileGL-docs, Khronos specs).

Statuses: [PASS] Supported
(implemented + live-probed), Partial (documented limits),
[FAIL] Missing (pinned gap),
NotRequired (MobileGL handles it above the host).

## Sources analyzed locally

- MobileGL source (`/tmp/mobilegl`, sparse checkout of `dev`):
  `DirectGLES.h` (backend surface), `EGLState/Core.h`,
  `BackendLoaders/OpenGL/Loader.h` (**46 EGL + 385 GLES** host funcs,
  `AcquireGLESFunctions` via `eglGetProcAddress`, `INIT_EGL_FUNC` fatal if
  missing, `INIT_GLES_FUNC` required incl. `glBufferStorageEXT` +
  `glBindImageTexture`, OPTIONAL `TextureView/MultiDraw/BaseInstance`).
- MobileGL docs (`docs/reference/mobilegl/`, 43 files): backends,
  capabilities (desktop GL 3.3 Core target, advertises 4.6),
  configuration (`MOBILEGL_BACKEND_TYPE`, `MOBILEGL_USE_ANGLE`,
  `MOBILEGL_DISABLE_TIMERQUERY`, `MOBILEGL_COHERENT_AS_FLUSH`,
  `MOBILEGL_IOS`), platforms
  (iOS = DirectVulkan + Metal surface today), validation (Driver POST).
- Key source findings: shaders go SPIR-V → SPIRV-Cross → ESSL before host
  `glShaderSource`; present = `glFenceSync` + `eglSwapBuffers` + fence
  polling; timer queries need `GL_EXT_disjoint_timer_query`;
  `glShaderStorageBlockBinding` is desktop 4.3 "no real ES driver exposes"
  (`Managers.cpp:8229`, `Utils.cpp:855`) so no host work is needed (SPIRV
  fallback); `TextureView` is OPTIONAL capability-gated
  (`Loader.cpp:1021`, `Managers.cpp:3594`); the host loads via
  dlopen+`eglGetProcAddress`, not build-time C linkage.

## Current status (re-verified from source, old code not trusted)

31 gates: **25 Supported, 0 Partial, 0 Missing, 6 NotRequired**.
`HostReadyForMobileGl()` returns **`true`** (the “when TGL supports the
MobileGL bar” meter is green): MSL compile + PSO + triangle draw +
presentDrawable + fence ring + exact red pixels are all device-verified on
host Metal (`tests/test_metal_device.mm`, Intel KBL).
5 gates previously mislabeled Missing/Partial are fixed or correctly
reclassified: `egl-image` → Supported, `egl-platform-extras` → Supported
(full 46 EGL), `persistent-map` → Supported (`BufferStorageEXT` immutable +
flush), `image-units` → Supported (8 core units), `texture-view` /
`ssbo-block-binding` / `c-abi` → NotRequired (optional/desktop-only/dlopen
with fallbacks).
The last two gates (`metal-execution`, `present`) went Missing → Partial
(compiling/linking `tgles_metal_bridge`) → **Supported** (real pixels).
What remains for end-to-end launcher runs is tracked in
`GlesContext::HostIntegrationGaps`: app VAO/FBO wiring into the bridge +
validation runs on A-series iPhones.

## Reading the matrix

```sh
./build/tgles_tests  # MobileGlMatrix.* : 17 tests, live probe per gate
```

`HostReadyForMobileGl()` returns `false` plus the gap list until no Missing
gate remains — that is the meter for "when TGL supports the MobileGL bar".
Gate details live in `src/mobilegl_support.cpp`.
