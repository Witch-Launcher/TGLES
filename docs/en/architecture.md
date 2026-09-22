# Architecture

One frame through TGL:

```text
App GLES 3.2 calls
  -> GlesContext (facade: owns every manager, spec 2.3.1 error order)
  -> RenderFrame  (VAO/FBO state -> bridge frame; TRIANGLES only)
  -> MetalBridge  (pure C++ contract: Null / Mock / Apple)
  -> AppleMetalBridge (MSL, PSO cache, Depth32Float target, present)
  -> CAMetalLayer drawable
```

## Layers (`src/<layer>/` mirrors `include/tgles/<layer>/`)

| Layer | Owns | Rule |
|---|---|---|
| `base` | scalar types, error queue, spec tables (358 entry points) | No dependencies |
| `state` | 13 managers: buffers, VAO, shaders, programs, textures, samplers, FBO/RBO, tessellation, geometry, compute, sync, query, debug, pixels, images, transform feedback | Validate first, record one error, never crash |
| `pipeline` | draw validator, raster state (viewport/blend/depth/stencil) | Validation only, no pixels |
| `gpu` | PSO cache, format gating, MSL snippets, bridge contract | Pure C++17, no `<Metal/*>` |
| `gpu/apple` | the single `.mm` with every MTL call | Only platform file |
| `egl` | EGL 1.5 CPU model: display/config/context/surface/current/swap/sync/image | Handles are integer ids |
| `facade` | `GlesContext`: cross-module wiring (EAB->VAO, indirect->validator, compute->dispatcher), `RenderFrame`, `GetError` order | Owns all managers |
| `host` | C ABI: trial subset (`host_c_api.cpp`), 38 EGL (`abi/egl_api.cpp`), 341 gaps (`abi/gl_gap.cpp`), real queries (`abi/gl_real.cpp`), dispatch (`abi/dispatch.cpp`) | See `host-abi.md` |
| `support` | MobileGL gate matrix (31 gates) | Evidence-cited, test-pinned |

## Invariants (a reader can derive every rule from these)

1. **Fail closed.** Unknown enum -> `INVALID_ENUM`, no-context GL -> `INVALID_OPERATION`, unimplemented-but-legal transfer -> `INVALID_OPERATION`. Never a sentinel address, never silent corruption.
2. **Single dispatch.** `eglGetProcAddress` and `dlsym` serve the same static table (`dispatch.cpp`), so they can never diverge.
3. **Measured gaps.** Every ABI-complete-but-unexecuted call records itself in the ledger (`tglesAbiGapCount/Name/Calls`). "Support" is a checkable statement.
4. **Pure core.** `tgles_core` compiles with no platform headers (`iphoneos`/`iphonesimulator` clean); all MTL lives in one `.mm`.

## How to add a call end to end

1. Add validation + storage to the owning manager in `state/` (with a test in `tests/state/` first).
2. If it is a host-contract name: move it from `abi/gl_gap.cpp` into `abi/gl_real.cpp` (funneled through `HostRuntime`, `RequireContext()` guard) and run `python3 tools/host_abi/gen_gl_abi.py` — the gap budget drops by itself.
3. If it needs the GPU: extend `RenderFrame` wiring and the Apple bridge behind the `Mock` contract in `tests/gpu/test_metal_bridge.cpp`.
4. Lower `TGlesExpectedGapBudget` in `tests/host/test_abi_contract.cpp` to the regenerated count. `gen_gl_abi.py --check` fails the build on drift.
