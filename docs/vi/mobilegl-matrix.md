# Ma trận hỗ trợ MobileGL (31 gates)

Module `tgles::mobilegl` (`mobilegl_support.h`): mọi điều kiện một host
GLES 3.2 phải thỏa để MobileGL-DirectGLES chạy được — mỗi gate kèm evidence
(source MobileGL nhánh `dev`, MobileGL-docs, spec Khronos).

Trạng thái: [PASS] Supported
(implement + live probe), Partial (giới hạn đã ghi rõ),
[FAIL] Missing (gap đã chốt),
NotRequired (MobileGL tự xử phía trên host).

## Nguồn đã tải về phân tích

- Source MobileGL (`/tmp/mobilegl`, sparse checkout nhánh `dev`):
  `DirectGLES.h` (surface backend), `EGLState/Core.h`,
  `BackendLoaders/OpenGL/Loader.h` (**46 EGL + 385 GLES** host funcs,
  `AcquireGLESFunctions` qua `eglGetProcAddress`, `INIT_EGL_FUNC` fatal nếu
  thiếu, `INIT_GLES_FUNC` required gồm `glBufferStorageEXT` +
  `glBindImageTexture`, OPTIONAL `TextureView/MultiDraw/BaseInstance`).
- Docs MobileGL (`docs/reference/mobilegl/`, 43 files): backends,
  capabilities (target desktop GL 3.3 Core, quảng cáo 4.6),
  configuration (`MOBILEGL_BACKEND_TYPE`, `MOBILEGL_USE_ANGLE`,
  `MOBILEGL_DISABLE_TIMERQUERY`, `MOBILEGL_COHERENT_AS_FLUSH`,
  `MOBILEGL_IOS`), platforms
  (iOS = DirectVulkan + Metal surface hôm nay), validation (Driver POST).
- Khẳng định quan trọng từ source: shader đi SPIR-V → SPIRV-Cross → ESSL rồi
  mới `glShaderSource` trên host; present = `glFenceSync` + `eglSwapBuffers`
  + poll fence; timer query cần `GL_EXT_disjoint_timer_query`;
  `glShaderStorageBlockBinding` là desktop 4.3 "no real ES driver exposes"
  (`Managers.cpp:8229`, `Utils.cpp:855`) nên host KHÔNG cần, fallback bake
  qua SPIRV; `TextureView` OPTIONAL capability-gated
  (`Loader.cpp:1021`, `Managers.cpp:3594`); host load qua
  dlopen+`eglGetProcAddress`, không link C symbols lúc build.

## Trạng thái hiện tại (đã verify lại từ source, không tin code cũ)

31 gates: **25 Supported, 0 Partial, 0 Missing, 6 NotRequired**.
`HostReadyForMobileGl()` trả **`true`** (thước đo “khi nào hỗ trợ chuẩn
MobileGL” đã xanh): MSL compile + PSO + draw tam giác + presentDrawable +
fence ring + pixel đỏ exact đều đã device-verified trên host Metal
(`tests/test_metal_device.mm`, Intel KBL).
5 gates từng ghi Missing/Partial sai đã fix hoặc phân loại lại đúng:
`egl-image` → Supported, `egl-platform-extras` → Supported (đủ 46 EGL),
`persistent-map` → Supported (`BufferStorageEXT` immutable + flush),
`image-units` → Supported (8 units core), `texture-view`/`ssbo-block-binding`/
`c-abi` → NotRequired (optional/desktop-only/dlopen, có fallback).
2 gates cuối (`metal-execution`, `present`) từng Missing → Partial (cầu
`tgles_metal_bridge` biên dịch + link) → **Supported** (pixel thật).
Còn lại cho launcher end-to-end nằm ở `GlesContext::HostIntegrationGaps`:
wiring VAO/FBO của app vào bridge + run validation trên iPhone A-series.

## Đọc matrix

```sh
./build/tgles_tests  # MobileGlMatrix.* : 17 tests, live probe từng gate
```

`HostReadyForMobileGl()` trả `false` + danh sách gaps cho tới khi hết
Missing — đó là thước đo "khi nào TGL hỗ trợ chuẩn MobileGL".
Chi tiết từng gate xem `src/mobilegl_support.cpp`.
