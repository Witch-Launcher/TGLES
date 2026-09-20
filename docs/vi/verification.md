# Báo cáo kiểm chứng plan-01.md

> Toàn văn báo cáo nằm ở `docs/VERIFICATION_REPORT.md` trong repo.
> Trang này tóm tắt để đọc trên web.

## Tài liệu gốc đã download

| File | Nguồn | Xác minh |
|------|-------|----------|
| `es_spec_3.2.pdf` | Khronos OpenGL-Registry mirror | 601 trang, May 5 2022, Editor Jon Leech |
| `GLSL_ES_Specification_3.20.pdf` | Cùng mirror | 215 trang, v3.20.8 Aug 14 2023 |
| `Metal-Feature-Set-Tables.pdf` | developer.apple.com | 18 trang, May 21 2026 |
| `gl32.h` + `glext.h` | KhronosGroup/OpenGL-Registry | `gl32.h` có **358 entry points** |

## Kết luận (14 claims)

| # | Claim | Kết quả |
|---|-------|---------|
| 1 | ES 3.2 mới nhất, 2015, bảo trì 2022 | [PASS] ĐÚNG |
| 2 | GLSL 3.20 (2023), `#version 320 es`, hỗ trợ 1.00/3.00/3.10/3.20 | [PASS] ĐÚNG |
| 3 | ES 3.2 = AEP đưa vào core | [PASS] ĐÚNG |
| 4 | `glPolygonMode`, `glClipControl`, `glMultiDraw*` là core | [FAIL] SAI — chỉ EXT/NV |
| 5 | `GL_FILL`, `GL_PROGRAM_POINT_SIZE` là core enum | [FAIL] SAI |
| 6 | Query core có `TIME_ELAPSED`/`TIMESTAMP` | [FAIL] SAI — EXT-only |
| 7 | `glGetProgramBinary` kết xuất SPIR-V | [FAIL] SAI |
| 8 | Apple M1/M2 đạt ES 3.2 conformance | [WARN] Thiếu ngữ cảnh — thực chất là Asahi Linux Mesa |
| 9 | MetalANGLE chỉ hỗ trợ ES 3.0 | [WARN] Lỗi thời — 3.1/3.2 in-progress |
| 10 | Metal: không geometry shader, tessellation iOS 11+, width=1 | [WARN] Đúng một phần |
| 11 | Ma trận A9–A20 / iOS 14–27 | [FAIL] Sai BC (Apple9+), không có A20 |
| 12 | Ví dụ MSL trong plan | [FAIL] Sai cú pháp MSL |
| 13 | Lộ trình 2024–2025 | [FAIL] Lỗi thời |
| 14 | `gl_FragDepth`, polygon offset, KHR_debug mapping | [PASS] Đúng hướng |

Mọi dòng trên đều được mã hóa thành test C/C++ trong `tests/`.
