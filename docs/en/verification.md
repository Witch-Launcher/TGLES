# Plan verification report

> The full report lives at `docs/VERIFICATION_REPORT.md` in the repo.
> This page is a web-readable summary. Principle: never trust the plan at
> 100% — every claim is checked against the original specs in
> `docs/reference/`.

## Downloaded sources

| File | Source | Verified |
|------|--------|----------|
| `es_spec_3.2.pdf` | Khronos OpenGL-Registry mirror | 601 pages, May 5 2022, Editor Jon Leech |
| `GLSL_ES_Specification_3.20.pdf` | Same mirror | 215 pages, v3.20.8 Aug 14 2023 |
| `Metal-Feature-Set-Tables.pdf` | developer.apple.com | 18 pages, May 21 2026 |
| `gl32.h` + `glext.h` | KhronosGroup/OpenGL-Registry | `gl32.h` has **358 entry points** |

## Findings (14 claims)

| # | Claim | Result |
|---|-------|--------|
| 1 | ES 3.2 latest, 2015, maintained 2022 | [PASS] CORRECT |
| 2 | GLSL 3.20 (2023), `#version 320 es`, supports 1.00/3.00/3.10/3.20 | [PASS] CORRECT |
| 3 | ES 3.2 = AEP merged into core | [PASS] CORRECT |
| 4 | `glPolygonMode`, `glClipControl`, `glMultiDraw*` are core | [FAIL] WRONG — EXT/NV only |
| 5 | `GL_FILL`, `GL_PROGRAM_POINT_SIZE` are core enums | [FAIL] WRONG |
| 6 | Core queries include `TIME_ELAPSED`/`TIMESTAMP` | [FAIL] WRONG — EXT-only |
| 7 | `glGetProgramBinary` outputs SPIR-V | [FAIL] WRONG |
| 8 | Apple M1/M2 reached ES 3.2 conformance | [WARN] Missing context — actually Asahi Linux Mesa |
| 9 | MetalANGLE only supports ES 3.0 | [WARN] Outdated — 3.1/3.2 in progress |
| 10 | Metal: no geometry shader, tessellation since iOS 11+, width=1 | [WARN] Partly correct |
| 11 | Matrix A9–A20 / iOS 14–27 | [FAIL] Wrong BC (Apple9+), no A20 exists |
| 12 | MSL example in the plan | [FAIL] Invalid MSL syntax |
| 13 | Timeline 2024–2025 | [FAIL] Outdated |
| 14 | `gl_FragDepth`, polygon offset, KHR_debug mapping | [PASS] Right direction |

Every line above is encoded as a C/C++ test in `tests/`.
