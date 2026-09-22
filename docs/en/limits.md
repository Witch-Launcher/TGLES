# Limits

What TGL can and cannot render today. Check here before porting an app —
everything below is pinned by tests, not by hope.

## Render path

- Draws execute core topologies (`POINTS`/`LINES`/`LINE_STRIP`/`TRIANGLES`/
  `TRIANGLE_STRIP` + CPU-expanded `LINE_LOOP`/`TRIANGLE_FAN`/adjacency),
  indexed/instanced/indirect/range/base-vertex, fixed-point/packed/normalized/
  divisor/integer attribs (integer converts exactly below 2^24).
- Blend executes for real (all factors/equations incl. `CONSTANT_*` via
  per-frame `glBlendColor`, dual-source `SRC1_*` via `DualOut`, per-buffer
  blend via `BlendFunci` + descriptor loop + `PsoKey`; pinned by
  `BlendDepthReal.*` + `Phase4.PerBufferBlendKeyDiffers`). Depth+blend in one
  frame; split stencil refs draw two passes in one frame.
- MRT (up to 8) + MRT+MSAA per-attachment resolve execute (pinned by
  `MrtMsaaReal.*` + `Phase4.MrtMsaaComboExecutesOnMock`).
- Stencil testing executes (front/back func/mask/op, two-pass split refs).
- Textured sampling: multi-sampler 2D/cube/3D/array, mipmaps incl. cube/3D/
  array chains + LOD clamps (`lodMin/MaxClamp`), sRGB decode.
- GLSL→MSL: MRT/dual outs, UBO incl. int/uint/bool, uniform arrays, user
  structs, `gl_InstanceID`/`gl_VertexID`, ivec, relational rewrites.
- Adjacency CPU-expands; `PATCHES`/mesh + geometry/tess programs fail closed
  on Intel (`TGL-DEBUG`, need Apple7+ Metal3); border clamp fail-closed
  (Metal has no border color — correct).
- No standalone `glClear` path (color/depth clear at pass start).

## Pixel paths

- Upload store is **RGBA8-unpacked**. Transfers supported: `RGBA`/`RGB`/`RED`/`RG` x `UNSIGNED_BYTE` (full copy, sub-image row stamping). Other legal combos record `INVALID_OPERATION` (fail closed); unknown enums record `INVALID_ENUM`.
- Compressed (ETC2/ASTC) and multisample storage validate; no CPU decompression or resolve.

## Host surface

- 371 GLES + 50 EGL symbols exported; **303 real**, 68 declared gaps (ledger-measured, budget in `test_abi_contract.cpp`).
- `eglQueryString(EXTENSIONS)` lists only served extensions.
- Depth: all 8 funcs + write mask + clear value through a `Depth32Float` target (`LESS`); overlap proven by pixels with draw order working against the winner.

## Platforms

| Platform | Status |
|---|---|
| macOS Intel x86_64 | Verified: 453 tests + trials + viewer + game window (dylib-driven) |
| Apple Silicon | Same portable path expected, no machine to prove it (mesh needs Apple7+) |
| iOS device/simulator | Cross-compiles clean; on-device run pending |
| GPU families Apple3–Apple10 | Encoded + gated (BC = Apple9+, mesh = Apple7+) |
