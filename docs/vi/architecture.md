# Kiến trúc

Một frame đi qua TGL:

```text
App gọi GLES 3.2
  -> GlesContext (facade: sở hữu mọi manager, thứ tự error theo spec 2.3.1)
  -> RenderFrame  (trạng thái VAO/FBO -> bridge frame; chỉ TRIANGLES)
  -> MetalBridge  (contract C++ thuần: Null / Mock / Apple)
  -> AppleMetalBridge (MSL, PSO cache, Depth32Float target, present)
  -> CAMetalLayer drawable
```

## Các tầng (`src/<layer>/` mirror `include/tgles/<layer>/`)

| Tầng | Sở hữu | Quy tắc |
|---|---|---|
| `base` | kiểu scalar, error queue, bảng spec (358 entry points) | Không phụ thuộc ai |
| `state` | 13 managers: buffers, VAO, shaders, programs, textures, samplers, FBO/RBO, tessellation, geometry, compute, sync, query, debug, pixels, images, transform feedback | Validate trước, ghi đúng 1 error, không bao giờ crash |
| `pipeline` | draw validator, raster state (viewport/blend/depth/stencil) | Chỉ validation, không pixels |
| `gpu` | PSO cache, format gating, MSL mẫu, bridge contract | C++17 thuần, cấm `<Metal/*>` |
| `gpu/apple` | file `.mm` duy nhất chứa mọi lệnh MTL | File platform duy nhất |
| `egl` | EGL 1.5 CPU model: display/config/context/surface/current/swap/sync/image | Handle là integer id |
| `facade` | `GlesContext`: 3 mối nối chéo module, `RenderFrame`, thứ tự `GetError` | Sở hữu mọi manager |
| `host` | C ABI: trial subset, 38 EGL, 341 gaps, queries thật, dispatch | Xem `host-abi.md` |
| `support` | Ma trận gate MobileGL (31 gates) | Có evidence, test pin |

## Bất biến (đọc là suy ra mọi quy tắc)

1. **Fail closed.** Enum lạ -> `INVALID_ENUM`, gọi GL không context -> `INVALID_OPERATION`, transfer hợp lệ nhưng chưa có unpacker -> `INVALID_OPERATION`. Không sentinel, không corrupt thầm lặng.
2. **Dispatch duy nhất.** `eglGetProcAddress` và `dlsym` cùng phục vụ một bảng tĩnh (`dispatch.cpp`) nên không thể lệch nhau.
3. **Gap đo được.** Mọi call đủ ABI nhưng chưa chạy đều tự ghi vào ledger (`tglesAbiGapCount/Name/Calls`). "Hỗ trợ X" là câu check được.
4. **Core thuần.** `tgles_core` biên dịch không cần platform headers (`iphoneos`/`iphonesimulator` sạch); mọi lệnh MTL nằm trong một file `.mm`.

## Thêm một call end-to-end

1. Thêm validation + storage vào manager sở hữu trong `state/` (viết test trong `tests/state/` trước).
2. Nếu là tên trong host contract: chuyển từ `abi/gl_gap.cpp` sang `abi/gl_real.cpp` (đi qua `HostRuntime`, guard `RequireContext()`), chạy `python3 tools/host_abi/gen_gl_abi.py` — budget gap tự giảm.
3. Nếu cần GPU: mở rộng wiring `RenderFrame` + Apple bridge sau contract `Mock` trong `tests/gpu/test_metal_bridge.cpp`.
4. Hạ `TGlesExpectedGapBudget` trong `tests/host/test_abi_contract.cpp` bằng đúng số regenerate. `gen_gl_abi.py --check` bắt drift ngay ở build.
