# Tầng EGL 1.5 (host integration, bước A)

Spec: Khronos EGL 1.5 (27/08/2014, `docs/reference/egl-1.5-spec.pdf`),
header `docs/reference/egl.h` (MobileGL loader cần **46 EGL funcs**, tất cả
`INIT_EGL_FUNC`). Module `EglState`
(`include/tgles/egl.h`, `src/egl.cpp`), test viết trước:
`tests/test_egl_layer.cpp` (16 cases) + `tests/test_mobilegl_gaps.cpp`.

## Đã kiểm chứng từ spec gốc

- Error latch §3.1: `GetError` trả + xóa (gọi 2 lần → `SUCCESS` lần 2);
  mã lỗi `0x3000–0x300E` khớp `egl.h` từng giá trị.
- `BindAPI`: API ban đầu là `EGL_OPENGL_ES_API`; API khác → `BAD_PARAMETER`
  (TGL chỉ host GLES, không host desktop GL).
- `CreateContext`: default version là 1.0 → TGL không có 1.x nên `BAD_MATCH`;
  2.0/3.0/3.1 được context 3.2 (backwards compatible); version lạ → `BAD_MATCH`;
  `CLIENT_VERSION` là alias của `MAJOR_VERSION` (footnote 18).
- `MakeCurrent`: ma trận lỗi đầy đủ (`BAD_MATCH` khi surface + `NO_CONTEXT`,
  draw hợp lệ + read `NO_SURFACE`, v.v.).
- `GetProcAddress`: `NULL` = không tồn tại, không bao giờ sinh lỗi (§3.12).
  Đủ 46 EGL + `glBufferStorageEXT` required; EXT optional (`TextureView`,
  `MultiDraw`, timer) giữ `NULL` đúng thiết kế capability-gated.
- `SwapBuffers`: pbuffer/single-buffered là no-op nhưng vẫn `TRUE`;
  `SwapInterval` clamp lặng lẽ vào `[0, 4]`.
- Platform extras (đã fix, từng thiếu): `GetConfigs` liệt kê, `GetCurrentSurface`
  track DRAW/READ, `SurfaceAttrib` validate, `WaitClient/GL/Native` no-op success,
  `ReleaseThread` clear current; `CreateImage/DestroyImage` quản lý EGLImage
  với `BAD_CONTEXT/PARAMETER/MATCH`; pixmap/platform stubs spec-correct.

## Còn lại (cầu iOS)

`SwapBuffers` trên window surface mới chỉ đếm swap — present thật ra
`CAMetalLayer` thuộc cầu ObjC++ trên target iOS (cùng với Metal execution).
