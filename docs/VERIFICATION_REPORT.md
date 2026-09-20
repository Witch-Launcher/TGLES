# Verification Report — plan-01.md vs. Official Sources

> Ngôn ngữ trao đổi: Tiếng Việt. Code trong repo: Tiếng Anh.
> Nguyên tắc: KHÔNG tin 100% vào plan. Mọi claim đều đối chiếu với tài liệu gốc đã download trong `docs/reference/`.

## 1. Tài liệu gốc đã download (evidence)

| File | Nguồn | Kích thước | Xác minh |
|------|-------|------------|----------|
| `es_spec_3.2.pdf` | GitHub mirror của Khronos OpenGL-Registry (`specs/es/3.2/es_spec_3.2.pdf`), bản chính là `registry.khronos.org/OpenGL/specs/es/3.2/es_spec_3.2.pdf` (bị Cloudflare chặn curl, đã lấy qua `raw.githubusercontent.com`) | 2.1 MB, 601 pages | Mở được, trang 1: "OpenGL ES Version 3.2 (May 5, 2022), Editor: Jon Leech" |
| `GLSL_ES_Specification_3.20.pdf` | Cùng mirror (`specs/es/3.2/GLSL_ES_Specification_3.20.pdf`) | 4.5 MB, 215 pages | Trang 1: "Version 3.20.8, Mon, 14 Aug 2023" |
| `opengles32-quick-reference-card.pdf` | `khronos.org/files/opengles32-quick-reference-card.pdf` | 1.2 MB | PDF 1.5, đọc được |
| `Metal-Feature-Set-Tables.pdf` | `developer.apple.com/metal/Metal-Feature-Set-Tables.pdf` | 2.9 MB, 18 pages | Trang 1 ghi "May 21, 2026" — mới hơn plan (plan ghi May 2026, khớp) |
| `gl32.h` / `gl31.h` / `gl3.h` / `glext.h` | `KhronosGroup/OpenGL-Registry/api/GLES3/*`, `api/GLES2/gl2ext.h` | 124K/104K/80K/246K | Parse được bằng regex, `gl32.h` có **358 functions** |
| Web (không download PDF, dùng search excerpts) | Khronos registry index, ANGLE README (`chromium.googlesource.com`, `github.com/google/angle`), Khronos news 15-Feb-2024 (M1/M2 conformance), ARM Mali guide, Android docs | — | Ghi rõ URL trong mục 3 |

Toàn bộ script kiểm chứng nằm trong test suite C/C++ (`tests/`), chạy được trên macOS Intel bằng `cmake + ctest`.

## 2. Kết luận nhanh (đúng / sai / phóng đại)

| # | Claim trong plan | Kết quả | Evidence |
|---|------------------|---------|----------|
| 1 | ES 3.2 là bản mới nhất, ra mắt 2015, bảo trì 2022 (spec 5/2022) | **ĐÚNG** | `es_spec_3.2.pdf` bìa: May 5, 2022; Khronos forum/news: ES 3.2 release Aug 10-12, 2015; registry index: "current version is OpenGL ES 3.2" |
| 2 | GLSL ES 3.20 xuất bản 2023, `#version 320 es`, ES 3.2 hỗ trợ GLSL 1.00/3.00/3.10/3.20 | **ĐÚNG** | `GLSL_ES_Specification_3.20.pdf` v3.20.8 Aug 14 2023; trích: "requires __VERSION__ to substitute 320"; "The OpenGL ES 3.2 API is designed to work with GLSL ES v1.00, v3.00, v3.10 and v3.20" |
| 3 | ES 3.2 = AEP (Android Extension Pack) đưa vào core: geometry+tessellation, float render targets, ASTC, enhanced blending, texture buffer / multisample 2D array / cube map array, debug+robustness | **ĐÚNG** | ARM Mali guide + Khronos ES 3.2 feedback thread (10-Aug-2015) liệt kê y hệt 6 nhóm trên; spec PDF chứa Geometry/Tessellation/ASTC/DrawBuffers/texture buffer/cube map array/debug/robust |
| 4 | `glPolygonMode` (chỉ GL_FILL), `glClipControl`, `glMultiDrawArrays/MultiDrawElements` (core), `glDrawTransformFeedback`, `glProvokingVertex`, `glPointSize` là API ES 3.2 | **SAI — không thuộc core ES 3.2** | `gl32.h` KHÔNG có 6 hàm này; `es_spec_3.2.pdf` (1,094,622 chars trích xuất) KHÔNG chứa `glPolygonMode`/`glClipControl`/`glMultiDrawArrays`; `glMultiDraw*`/`glClipControl`/`glPolygonModeNV` chỉ có trong `glext.h` dạng EXT/NV; plan tự mâu thuẫn ("glClipControl: không có trong ES") nhưng vẫn liệt kê ở mục II.1 |
| 5 | `GL_FILL`, `GL_PROGRAM_POINT_SIZE` là enum core | **SAI** | `gl32.h` KHÔNG có 2 enum này; point/line trong core chỉ có `ALIASED_POINT_SIZE_RANGE`, `ALIASSED_LINE_WIDTH_RANGE`, `glLineWidth`, `gl_PointSize` (built-in shader) |
| 6 | Query core gồm `GL_TIME_ELAPSED`, `GL_TIMESTAMP`, `GL_TRANSFORM_FEEDBACK_STREAM_OVERFLOW` | **SAI — chỉ là extension** | Spec Table 4.2 (p.39) chỉ có 3 nhóm: `PRIMITIVES_GENERATED`, `TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN`, `ANY_SAMPLES_PASSED[_CONSERVATIVE]`; `TIME_ELAPSED`/`TIMESTAMP` chỉ có trong `glext.h` (`EXT_disjoint_timer_query`); `STREAM_OVERFLOW` không có cả trong core lẫn `glext.h` |
| 7 | `glGetProgramBinary` "kết xuất SPIR-V" | **SAI** | Spec §7.5 Program Binaries: binary là implementation-specific; SPIR-V là của Vulkan/OpenGL 4.6, không phải ES 3.2 program binary |
| 8 | "Apple M1/M2 đã chính thức đạt chứng nhận ES 3.2 (Feb 2024)" | **PHÓNG ĐẠI / THIẾU NGỮ CẢNH** | Khronos news 15-Feb-2024 + Phoronix/LWN: bên đạt conformance là **Asahi Linux AGX Gallium3D (Mesa) trên Linux**, không phải driver gốc Apple; Apple native OpenGL trên macOS vẫn dừng ở 4.1 non-conformant. Plan viết "Apple M1/M2" gây hiểu nhầm là Apple |
| 9 | "MetalANGLE hiện chỉ hỗ trợ ES 3.0" | **LỖI THỜI / SAI** | ANGLE README (chromium.googlesource): Metal backend: ES 2.0 complete, ES 3.0 complete, ES 3.1 incomplete/complete tùy bản, ES 3.2 in progress; Vulkan backend đã certified ES 3.2 (Sept 2023). Nói "chỉ 3.0" bỏ qua 3.1/3.2 đang làm và chứng nhận Vulkan |
| 10 | Metal không có geometry shader; tessellation có từ iOS 11+; point/line width = 1 | **ĐÚNG MỘT PHẦN** | Đúng: Metal không có geometry stage truyền thống (phải dùng compute/multi-pass/mesh); Tessellation có trong Metal Feature Tables từ Apple3; Sai phần "width=1 luôn": spec ES cho `ALIASED_LINE_WIDTH_RANGE`, max point/line width ≥ 1 (implementation-defined), không phải hằng số 1 — Metal giới hạn nhưng không thể khẳng định "luôn = 1" cho mọi GPU |
| 11 | Ma trận A9–A20 / iOS 14–27 / Apple3–Apple10 trong plan | **SAI NHIỀU Ô + PHỎNG ĐOÁN TƯƠNG LAI** | `Metal-Feature-Set-Tables.pdf` (May 21, 2026, 18 pages): chỉ tới **A19/M5 = Apple10**, KHÔNG có A20; **BC pixel formats = Apple9+** (plan ghi A14+ sai); "Mesh Shaders optional A13" sai (mesh chỉ Apple7+ qua Metal 3); iOS 26/27 và A20 là phỏng đoán, Apple chưa công bố; plan ghi "A9/A10 = Apple3 (Metal 1.x), no argument buffers" đúng hướng nhưng gán OS "iOS9–iOS15" lẫn lộn với min/max test target |
| 12 | Ví dụ MSL trong plan (mục III.8) | **SAI CÚ PHÁP MSL** | `thread float4& outColor [[user(locn0)]]` là tham số hàm — MSL không cho `thread` reference + `[[user()]]` kiểu này; cách đúng là struct varyings (`struct Varyings { float4 color [[user(locn0)]]; }`), vertex return `float4` + ghi varyings qua struct hoặc dùng `[[stage_in]]`/`[[position]]`. Test `test_msl_translation` chứng minh bản sửa |
| 13 | Lộ trình 1/2024–12/2025, "2.5 năm", mốc 1 "done" | **LỖI THỜI** | Hôm nay 2026-09-20 (macOS Darwin 24.6.0): toàn bộ milestone đã quá hạn; không thể "done 2024" khi repo hiện tại chưa có code; cần re-plan |
| 14 | `gl_FragDepth`, `gl_PolygonOffset` (depth bias), KHR_debug/robustness mapping | **ĐÚNG HƯỚNG** | Spec có `gl_FragDepth`, `POLYGON_OFFSET_FILL/FACTOR/UNITS`, KHR_debug/robustness; Metal có depth bias + `MTLObject.label` — mapping trong plan chấp nhận được |

## 3. Nguồn đối chiếu chi tiết

- Registry: `https://registry.khronos.org/OpenGL/index_es.php` — "current version is OpenGL ES 3.2 ... Spec May 5, 2022 ... GLSL 3.20 Aug 14, 2023".
- Spec PDF (qua mirror do Cloudflare): `https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/specs/es/3.2/es_spec_3.2.pdf`.
- GLSL PDF: `https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/specs/es/3.2/GLSL_ES_Specification_3.20.pdf`.
- Headers: `.../api/GLES3/gl32.h` (358 entry points), `.../api/GLES2/gl2ext.h` (chứa `glMultiDrawArraysEXT`, `glClipControlEXT`, `glPolygonModeNV`, `GL_TIME_ELAPSED`, `GL_TIMESTAMP`).
- M1/M2: `https://www.khronos.org/news/permalink/software-in-the-public-interest-announces-apple-m1-and-m2-as-being-opengl-4.6-conformant` (15-Feb-2024) + `https://www.phoronix.com/news/OpenGL-4.1-Asahi-Apple-M1` + LWN XDC 2024 — chủ thể là Asahi/Mesa on Linux.
- ANGLE: `https://chromium.googlesource.com/angle/angle/+/HEAD/README.md` — bảng "Level of OpenGL ES support via backing renderers" + "ANGLE 2.1.2 ... ES 3.2 (Sept, 2023) certified (Vulkan backend)".
- AEP/ES3.2 features: `https://community.khronos.org/t/opengl-es-3-2-feedback-thread/6664`, ARM `arm_mali_gpu_opengl_es_3-x_developer_guide`, `https://developer.android.com/develop/ui/views/graphics/opengl/about-opengl` ("All AEP features are included in base ES 3.2").
- Metal tables: `https://developer.apple.com/metal/Metal-Feature-Set-Tables.pdf` (bản May 21, 2026 trong repo) + `https://developer.apple.com/metal/resources`.

## 4. Hệ quả cho dự án TGL

1. Sửa mục II.1/III.3: tách **core ES 3.2** (358 hàm `gl32.h`) khỏi **EXT** (`glMultiDraw*EXT`, `glClipControlEXT`, `glPolygonModeNV`, timer-query EXT). Không implement `glPolygonMode` core.
2. Sửa mục II.8/III.3: query core chỉ 4 target (`PRIMITIVES_GENERATED`, `TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN`, `ANY_SAMPLES_PASSED`, `ANY_SAMPLES_PASSED_CONSERVATIVE`); timer query là optional EXT.
3. Sửa `glGetProgramBinary` → binary blob implementation-specific, không mặc định SPIR-V.
4. Viết lại ma trận tương thích từ Metal tables May 2026: BC = Apple9+, không claim A20/iOS27, mesh = Metal 3/Apple7+.
5. Sửa ví dụ MSL theo `tests/test_msl_translation.cpp` (struct varyings).
6. Re-plan timeline (2026+) và làm rõ M1/M2 conformance = Asahi Linux, không phải Apple native; MetalANGLE = ES 3.0 complete + 3.1/3.2 in progress.
7. Mọi claim trên đã được mã hóa thành test C/C++ trong `tests/` — test đỏ = plan sai, test xanh = spec đúng.
