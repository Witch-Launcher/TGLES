# TGL — OpenGL ES 3.2 → Metal

Test-first **OpenGL ES 3.2 implementation that executes on Metal**.
App code talks GLES 3.2; TGL validates the state machine and draws it
through a real `CAMetalLayer` — on macOS today, on iOS by the same core.

> Test status: **441 tests, 5887 checks, 0 failures** · device-verified on
> Intel Mac · headless trials PASS (`tgl_cube_trial` + `tgl_lit_textured_trial`
> lit+textured+depth, 8 frames each) · game window plays through the built
> `libtgles.dylib` (`tgl_game_window`, dlopen + procaddr only, no direct link).

![Interactive viewer on Metal](assets/screenshots/viewer-street.png)

*Interactive viewer (`tgl_viewer`): lit pillars + spinning cube, 60 FPS.*

## Quick start

Prerequisites: macOS with Xcode (Metal SDK), `cmake >= 3.20`, a GPU.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```sh
cmake --build build --target tgl_viewer && ./build/tgl_viewer
./build/tgl_cube_trial            # headless proof, writes cube_frameN.ppm
./build/tgl_lit_textured_trial    # lit+textured+depth proof, writes lit_tex_frameN.ppm
./build/tgl_game_window           # playable game; ONLY driver is build/libtgles.dylib
```

Docs (Docsify, bilingual VI/EN): `docsify serve docs`.

## Layout

```text
TGLES/
  apps/             # tgl_viewer · tgl_game_window (dylib-driven) · trials · cube_model/render
  include/tgles/    # Public headers, one topic per file (pure C++)
  src/              # base · state · pipeline · gpu · egl · facade · host
  tests/            # One file per area, incl. CTS subset + host contract + official registry gate
  tools/mobilegl/   # Host contract (371 GLES + 46 EGL) + parser
  tools/cts/        # sync_official.py (live Khronos gl.xml/egl.xml → coverage gate)
  tools/host_abi/   # ABI generator (prototypes + gaps + dispatch tables)
  docs/             # Bilingual site (vi/ + en/)
  plan/             # plan-03-game-window-cts.md is current; plan-01 is outdated
```

Architecture, ABI contract, CTS conformance and limits live in `docs/`
(see `plan/plan-02-cts-es32.md` for the work plan).

## Limits (honest)

Draw path executes core topologies (`POINTS`/`LINES`/`LINE_STRIP`/`TRIANGLES`/
`TRIANGLE_STRIP` + CPU-expanded `LINE_LOOP`/`TRIANGLE_FAN`/`LINES_ADJACENCY`/
`LINE_STRIP_ADJACENCY`/`TRIANGLES_ADJACENCY`/`TRIANGLE_STRIP_ADJACENCY`),
indexed/instanced/indirect/range/base-vertex draws, fixed-point/packed/
normalized/divisor attribs, depth + stencil testing (GL clip z remapped into
Metal NDC in-shader, so `[-1, 1]` works like on GL), depth+blend in one frame
(incl. per-buffer blend via `BlendFunci` + descriptor loop + `PsoKey`),
`CONSTANT_*` blend via per-frame `glBlendColor`, **textured sampling**
(multi-sampler 2D/cube/3D/array, mipmaps incl. cube/3D/array chains + LOD
clamps, sRGB decode, device-tested texels), **GLSL→MSL** (app shaders compile
to app-derived MSL: textured + diffuse `dot`/`normalize` lighting, MRT/
dual-source outs, UBO incl. int/uint/bool, uniform arrays, user structs,
`gl_InstanceID`/`gl_VertexID`, ivec attribs, relational rewrites; GPU-lit
pixels device-tested; out-of-subset fails closed), **compute v1**
(`TGL_COMPUTE_ADD_ONE_SSBO0` executes on CPU store + real `MTLCompute`
kernel, bit-exact device test, direct + indirect), MRT + MRT+MSAA
per-attachment resolve, MSAA resolve, integer attribs, split-stencil
two-pass, dual-source and window-surface swap→present (launcher path).
The playable `tgl_game_window` drives all of this through the **built**
`libtgles.dylib` only (dlopen + `eglGetProcAddress`, no direct link).
Still missing/fail-closed: `PATCHES`/mesh + geometry/tessellation programs
(need Apple7+ Metal3; Intel fails closed with `TGL-DEBUG`), border clamp
(correct — Metal has no border color), remaining GLSL beyond (flat integer
varyings, `interpolateAt*`, sampler arrays, nested structs). Hosts resolve
371 GLES + 46 EGL symbols with no gaps in the dispatch table; the live Khronos
registry gate (`test_official_coverage`, 358 + 44 names) proves it against
`gl.xml`/`egl.xml`, not just vendored headers. iOS device + simulator targets
compile clean (`build-ios-dev`, `build-ios-sim`); on-device A-series run is
still pending.

## Credits

Khronos (ES 3.2 / GLSL ES / EGL headers in `docs/reference/`),
Apple (Metal), MobileGL-Dev (host-contract research), Docsify.js.
MIT — see [LICENSE](LICENSE).

## Tóm tắt tiếng Việt

TGL là implementation OpenGL ES 3.2 chạy thật trên Metal: test-first,
state machine đầy đủ, depth test thật trên GPU, present ra cửa sổ macOS
hoặc chạy headless. Core C++ thuần portable cho iOS; docs song ngữ.
Giới hạn và kế hoạch CTS xem `docs/` và `plan/plan-02-cts-es32.md`.
