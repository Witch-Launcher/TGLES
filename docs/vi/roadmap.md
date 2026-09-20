# Lộ trình chi tiết (10 bước tới ES 3.2 trên iOS)

Mỗi bước: **đọc spec → viết test → implement → chạy test → cross-compile
iOS → ghi docs**. Chỉ qua bước sau khi bước hiện tại xanh 100%.
Toàn bộ 10 bước đã hoàn thành, xem [Conformance](conformance.md).

## Bước 1 — Core foundation [PASS] (xong)

Spec §2.3.1 (error model), §18 (debug output state), §20.2–20.3 (string
queries). Module `tgles_core`: `gl_types.h`, `error.h`, `context.h`.

- Error latch: lỗi đầu tiên được giữ, `glGetError` trả về rồi xóa; lệnh lỗi
  (trừ `OUT_OF_MEMORY`) không có side-effect, trả về 0, không ghi pointer.
- 13 enable caps core, `DITHER` mặc định `TRUE`, `DEBUG_OUTPUT` theo loại
  context, còn lại `FALSE`; cap lạ → `INVALID_ENUM`.
- `glGetString` (5 names) + `glGetStringi(EXTENSIONS, i)` + `MAJOR/MINOR_VERSION`.

## Bước 2 — Buffers + VAO [PASS] (xong)

Spec §6 (buffer objects: create/bind/data/map/copy/query), §10.3–10.4
(vertex arrays, VAO, divisors, primitive restart, robust access).

## Bước 3 — Shaders + programs [PASS] (xong)

Spec §7: 6 shader stages, compile/link/validate, uniforms, UBO, atomic
counter, SSBO, samplers state, images, memory barriers.

## Bước 4 — Textures + samplers [PASS] (xong)

Spec §8: targets (2D/3D/array/cube/buffer/multisample), ASTC/ETC2, immutable
storage, sampler objects, pixel rectangles/PBO.

## Bước 5 — Framebuffers [PASS] (xong)

Spec §9: FBO completeness, renderbuffers, MRT (`DrawBuffers`), blit,
invalidate, multisample resolve.

## Bước 6 — Tessellation + geometry + compute [PASS] (xong)

Spec §11–12: patch params, tess levels, geometry invocations, compute
dispatch + indirect, barriers.

## Bước 7 — Sync + query + debug [PASS] (xong)

Spec §4 (fence sync, 4 async query targets), §18 (KHR_debug messages,
labels), §2.3.2 graphics reset.

## Bước 8 — Draw + raster state [PASS] (xong)

Spec §10.5 (11 draw entry points + base-vertex), §13–15 (viewport, cull,
depth/stencil, blend — per-buffer indexed blend, dither, AA).

## Bước 9 — Metal backend [PASS] (xong)

PSO cache, argument buffers, resource heaps, tessellation factor buffer,
blit encoder, `MTLSharedEvent` fences; family gating Apple3–Apple10
(BC = Apple9+, mesh = Apple7+).

## Bước 10 — Facade + conformance [PASS] (xong)

dEQP/CTS ES 3.2 (`--type=es32`), benchmark FPS/CPU/GPU, TestFlight +
farm thiết bị iOS 14–26, phát hành framework.
