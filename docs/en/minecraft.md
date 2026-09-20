# Minecraft Java on iOS: can TGL be integrated for testing?

**Short answer: yes architecturally, and TGL is testable as a host today
at the GLES + EGL-state layer — but running the full game needs
`CAMetalLayer` present + Metal execution first.**

## The rendering stack involved

```text
Minecraft Java (LWJGL, desktop OpenGL)
  → MobileGL MG_Impl (desktop GL frontend: EGL/CGL/GLX/WGL)
    → DirectGLES backend (default, MOBILEGL_BACKEND_TYPE=DirectGLES)
      → host GLES 3.x  ←←←  TGL plays this role (instead of iOS's deprecated GLES / ANGLE)
        → Metal (Apple GPU)
          → iOS screen
```

Sources: MobileGL (`github.com/MobileGL-Dev/MobileGL`, docs at
`docs.mobilegl.top`), MobileGlues ("GL on top of host OpenGL ES 3.x — best
on 3.2, minimum 3.0", `MobileGL-Dev/MobileGlues-release`), PojavLauncher iOS
(`gl_bridge.m` binds EGL, `POJAV_RENDERER`).

## What DirectGLES needs from the host (verified)

| Host requirement | Source | TGL today |
|------------------|--------|-----------|
| GLES 3.x, best 3.2 (minimum 3.0) | MobileGlues README | [PASS] full 3.2 state core (10 modules, green checklist) |
| EGL entry points (choose config, surface, bind API) | PojavLauncher iOS `gl_bridge.m` | [PASS] state machine (`EglState`, 14 tests); `CAMetalLayer` present still [FAIL] |
| Timer query can be disabled (`MOBILEGL_DISABLE_TIMERQUERY`) | MobileGL README | [PASS] timer query is EXT-only, matches TGL's stance |
| Geometry/tessellation/compute for Sodium/Iris | PojavLauncher RENDERERS.md | [PASS] in core (step 6) |
| iOS builds (arm64 + simulator) | MobileGL Platforms | [PASS] TGL builds both cleanly |

## Why it is worth doing

- iOS deprecated GLES in iOS 12 and the system driver is stuck at **3.0**;
  ANGLE/Metal is complete only for 3.0, with 3.1/3.2 still in progress.
- Minecraft **1.17+ needs a 3.2-class GL layer** (PojavLauncher docs: ANGLE
  "supports OpenGL 3.2 only, works on 1.17+"); Sodium/Iris need
  geometry/tessellation/compute — exactly where TGL beats a GLES-3.0 host.
- TGL gives an independent test point: `ConformanceChecklist()` plus the new
  `test_mobilegl_host_requirements.cpp` proves TGL qualifies as a host.

## Not enough to "just run the game" yet

1. **Present**: the EGL state machine is done (`EglState`); the ObjC++
   bridge `tgles_metal_bridge` already does **real presentDrawable with real
   pixels** (device-verified on host Metal) — what remains is a validation
   run on an A-series iPhone with the app's real layer
   (see [EGL 1.5 layer](egl.md)).
2. **Metal execution bridge**: MSL compile + PSO + draw + blit/readback +
   fence ring all run for real on the GPU (`tests/test_metal_device.mm`);
   app→bridge wiring (`GlesContext::RenderFrame`: VAO float attribs +
   MVP + draw-FBO size + depth) is done for TRIANGLES — indexed draws,
   non-float/non-interleaved attribs and MRT targets remain. Depth testing
   (NEVER..ALWAYS + mask + clear, Depth32Float target) is device-verified,
   see the [test window](viewer.md).
3. **Beyond GPU**: JVM + JIT on iOS (TrollStore/debugger, Azul Zulu JDK),
   LWJGL natives, RAM limits, sideload-only (PojavLauncher iOS README).

## Proposed test ladder

1. [PASS] In-repo: `test_mobilegl_host_requirements` (done, always green).
2. On macOS host: run MobileGL self-tests/Piglit against TGL as the host
   GLES — no iPhone needed.
3. Write a minimal EGL bridge + `CAMetalLayer` present path, trial a
   MobileGL triangle on the simulator.
4. Launcher trial: `MOBILEGL_BACKEND_TYPE=DirectGLES` with TGL as the host
   EGL/GLES, testing Minecraft 1.21 + Sodium (same injection pattern
   MobileGL documents for PrismLauncher on macOS via `DYLD_INSERT_LIBRARIES`).
