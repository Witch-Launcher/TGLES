# Detailed roadmap (10 steps to ES 3.2 on iOS)

Each step: **read the spec → write tests → implement → run tests →
cross-compile for iOS → document**. Move on only when the current step is
100% green. All 10 steps are complete, see [Conformance](conformance.md).

## Step 1 — Core foundation [PASS] (done)

Spec §2.3.1 (error model), §18 (debug output state), §20.2–20.3 (string
queries). Module `tgles_core`: `gl_types.h`, `error.h`, `context.h`.

- Error latch: the first error sticks, `glGetError` returns then clears it;
  failing commands (except `OUT_OF_MEMORY`) have no side effects, return 0,
  and never write through pointers.
- 13 core enable caps, `DITHER` defaults to `TRUE`, `DEBUG_OUTPUT` follows
  the context type, the rest `FALSE`; unknown cap → `INVALID_ENUM`.
- `glGetString` (5 names) + `glGetStringi(EXTENSIONS, i)` + `MAJOR/MINOR_VERSION`.

## Step 2 — Buffers + VAO [PASS] (done)

Spec §6 (buffer objects: create/bind/data/map/copy/query), §10.3–10.4
(vertex arrays, VAO, divisors, primitive restart, robust access).

## Step 3 — Shaders + programs [PASS] (done)

Spec §7: 6 shader stages, compile/link/validate, uniforms, UBO, atomic
counters, SSBO, sampler state, images, memory barriers.

## Step 4 — Textures + samplers [PASS] (done)

Spec §8: targets (2D/3D/array/cube/buffer/multisample), ASTC/ETC2, immutable
storage, sampler objects, pixel rectangles/PBO.

## Step 5 — Framebuffers [PASS] (done)

Spec §9: FBO completeness, renderbuffers, MRT (`DrawBuffers`), blit,
invalidate, multisample resolve.

## Step 6 — Tessellation + geometry + compute [PASS] (done)

Spec §11–12: patch params, tess levels, geometry invocations, compute
dispatch + indirect, barriers.

## Step 7 — Sync + query + debug [PASS] (done)

Spec §4 (fence sync, 4 async query targets), §18 (KHR_debug messages,
labels), §2.3.2 graphics reset.

## Step 8 — Draw + raster state [PASS] (done)

Spec §10.5 (11 draw entry points + base-vertex), §13–15 (viewport, cull,
depth/stencil, blend — per-buffer indexed blend, dither, AA).

## Step 9 — Metal backend [PASS] (done)

PSO cache, argument buffers, resource heaps, tessellation factor buffer,
blit encoder, `MTLSharedEvent` fences; family gating Apple3–Apple10
(BC = Apple9+, mesh = Apple7+).

## Step 10 — Facade + conformance [PASS] (done)

dEQP/CTS ES 3.2 (`--type=es32`), FPS/CPU/GPU benchmarks, TestFlight +
iOS 14–26 device farm, framework release.
