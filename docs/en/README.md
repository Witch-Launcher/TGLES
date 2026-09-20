# TGL — OpenGL ES 3.2 for iOS

> Serious, spec-driven

## Goal

Bring **full OpenGL ES 3.2** (spec 05/05/2022 + GLSL ES 3.20) to iOS through
a Metal backend, reach conformance, and replace MetalANGLE (ES 3.0 complete,
3.1/3.2 in-progress only).

## Working principles

1. **Never trust the original plan blindly** — every claim is checked against
   the specs in `docs/reference/` (see [Verification report](verification.md)).
2. **Test-first**: C/C++ tests are written before the implementation.
3. **One step at a time**: a step is done only when its tests pass against
   the spec.
4. Portable C/C++: builds on macOS (Intel) and cross-compiles to `iphoneos` /
   `iphonesimulator`.

## Build & test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

iOS cross-compile check (compile only, no execution):

```sh
cmake -S . -B build-ios-dev -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build-ios-dev
```

## Docs

```sh
docsify serve docs   # preview at http://localhost:3000
# or: python3 -m http.server 3000 --directory docs
```

Docs are bilingual (`/vi/` + `/en/`); see
[Adding a language](i18n.md) to contribute more.

## Step progress

| Step | Content | Spec | Status |
|------|---------|------|--------|
| 1 | Core foundation: types, error, context, enable/disable | §2.3.1, §18, §20.2–20.3 | [PASS] done, green |
| 2 | Buffer objects + VAO + transform feedback | §6, §10.3–10.4 | [PASS] done, green |
| 3 | Shader + program + uniforms | §7 | [PASS] done, green |
| 4 | Textures + samplers + ASTC | §8 | [PASS] done, green |
| 5 | Framebuffers + renderbuffers + MRT | §9 | [PASS] done, green |
| 6 | Tessellation + geometry + compute | §11–12 | [PASS] done, green |
| 7 | Sync + query + debug output | §4, §18 | [PASS] done, green |
| 8 | Draw commands + rasterization state | §10.5, §13–15 | [PASS] done, green |
| 9 | Metal backend (PSO cache, resources, encoder) | Metal tables 05/2026 | [PASS] done, green |
| 10 | Facade + conformance gates + release | CTS | [PASS] done, green |

See the sidebar for each step in detail.
