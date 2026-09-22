# Host ABI

Mặt C để host GLES `dlopen`: 371 GLES + 50 EGL symbols, không cần system
GLES/EGL headers. Đối chiếu với `tools/mobilegl/host_contract.json`
(MobileGL `dev` @ `fff9d639`, 2026-09-16) và `docs/reference/*.h` của Khronos.

## Bố cục

| File | Vai trò |
|---|---|
| `include/tgles/host/abi.h` | Kiểu scalar (đúng width Khronos) + 50 khai báo EGL |
| `include/tgles/host/abi_gl.h` | 371 prototypes GLES, C thuần (không macro `APIENTRY`) — **generated** |
| `src/host/host_c_api.cpp` | 27 bodies thật trial-subset (buffers/VAO/shader/program/texture/FBO/draw) |
| `src/host/abi/egl_api.cpp` | 38 bodies EGL còn lại trên `EglState` |
| `src/host/abi/gl_real.cpp` | 276 bodies GLES thật và tăng dần (uniforms full + reads + reflection, queries, state, raster, draws, objects, buffers, sync, textures, samplers, vertex attribs, renderbuffers, FBO, programs, compute, debug). Bảng đầy đủ: `host-entries.md` |
| `src/host/abi/gl_gap.cpp` | 68 gaps khai báo: tự ghi ledger, trả default của spec — **generated** |
| `src/host/abi/dispatch.cpp` | Bảng name->address duy nhất sau `eglGetProcAddress` |
| `tools/host_abi/gen_gl_abi.py` | Generator (`--check` bắt drift ở CI) |

## Quy tắc (do `tests/host/test_abi_contract.cpp` pin)

- Mọi tên contract resolve được qua **cả** `dlsym` **và** `eglGetProcAddress`, **cùng** một địa chỉ.
- Tên lạ (kể cả desktop `glPolygonMode`) trả **NULL** — không bao giờ `(void*)0x1`; MobileGL coi non-null là gọi được và sẽ crash.
- `eglQueryString(EXTENSIONS)` chỉ quảng cáo cái đang có; launcher chẩn đoán bằng cách grep chuỗi này.
- Budget gap `TGlesExpectedGapBudget = 68`: hạ xuống khi `gl_real.cpp` lớn dần, không nâng nếu không ghi chú ở đây.

## Vì sao MobileGL load được (đã đọc source upstream)

Tại `MobileGL/.../BackendLoaders/OpenGL/Loader.cpp` @ `fff9d639`:

- GLES đi qua **`eglGetProcAddress`** (`AcquireGLESFunctions`); null thì log `Failed to load GLES function`.
- EGL đi qua **`dlsym` trên library đã dlopen**: Linux là `libEGL.so.1` / `libEGL.so`, iOS là **`libtinygl4angle.dylib`** (`MOBILEGL_IOS`); null là fatal (`MGLOG_F`).
- Entry points optional (`INIT_GLES_FUNC_OPTIONAL`: texture views, TexBuffer spellings, `PolygonModeNV/ANGLE`, multi-draw/base-instance EXT) được phép null — còn lại phải resolve.
- **macOS stock không có đường DirectGLES** — `OpenLib`/`ProcAddress` trả null dưới `__APPLE__` mà không có `MOBILEGL_IOS`. Chạy CTS trên macOS nghĩa là EGL surfaceless (xem `conformance.md`); chạy iOS nghĩa là `libtinygl4angle.dylib` + đúng bảng `eglGetProcAddress` mà TGL đã phục vụ (`src/host/abi/dispatch.cpp`, một bảng theo thiết kế). File TGL build ra là `libtgles.dylib` (`TGLES_HOST_LIBRARY_NAME`).

## Chẩn đoán bring-up

```sh
python3 tools/cts/preflight.py --lib build/libtgles.dylib  # F5 + sentinels + strings
```

Call nào không làm gì mà vẫn `NO_ERROR`, hỏi ledger: `tglesAbiGapCount/Name/Calls` cho biết đúng gap nào, bị gọi bao nhiêu lần.
