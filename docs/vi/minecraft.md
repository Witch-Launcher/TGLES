# Minecraft Java trên iOS: tích hợp TGL để test được không?

**Trả lời ngắn: được về mặt kiến trúc, và TGL test được ngay từ hôm nay
ở tầng host-GLES + EGL state — nhưng chạy hết game cần thêm present
`CAMetalLayer` + Metal execution.**

## Stack render liên quan

```text
Minecraft Java (LWJGL, desktop OpenGL)
  → MobileGL MG_Impl (desktop GL frontend: EGL/CGL/GLX/WGL)
    → DirectGLES backend (mặc định, MOBILEGL_BACKEND_TYPE=DirectGLES)
      → host GLES 3.x  ←←←  TGL đóng vai trò này (thay GLES 3.0 deprecated của iOS / ANGLE)
        → Metal (Apple GPU)
          → màn hình iOS
```

Nguồn: MobileGL (`github.com/MobileGL-Dev/MobileGL`, docs `docs.mobilegl.top`),
MobileGlues ("GL on top of host OpenGL ES 3.x — best on 3.2, minimum 3.0",
`MobileGL-Dev/MobileGlues-release`), PojavLauncher iOS (`gl_bridge.m` bind
EGL, `POJAV_RENDERER`).

## DirectGLES cần gì ở host (đã kiểm chứng)

| Yêu cầu host | Nguồn | TGL hôm nay |
|--------------|-------|-------------|
| GLES 3.x, tốt nhất 3.2 (tối thiểu 3.0) | MobileGlues README | [PASS] state core full 3.2 (10 modules, checklist xanh) |
| EGL entry points (choose config, surface, bind API) | `gl_bridge.m` của PojavLauncher iOS | [PASS] state machine (`EglState`, 14 tests); present `CAMetalLayer` vẫn [FAIL] |
| Timer query tắt được (`MOBILEGL_DISABLE_TIMERQUERY`) | MobileGL README | [PASS] timer query là EXT-only, đúng stance của TGL |
| Geometry/tessellation/compute cho Sodium/Iris | PojavLauncher RENDERERS.md | [PASS] có trong core (bước 6) |
| iOS build (arm64 + simulator) | MobileGL Platforms | [PASS] TGL build sạch cả 2 |

## Vì sao đáng làm

- iOS deprecated GLES từ iOS 12, driver hệ thống kẹt ở **3.0**; ANGLE/Metal
  mới complete 3.0, 3.1/3.2 còn dở dang.
- Minecraft **1.17+ cần lớp GL 3.2** (PojavLauncher docs: ANGLE "supports
  OpenGL 3.2 only, works on 1.17+"); Sodium/Iris cần geometry/tessellation/
  compute — đúng phần TGL hơn GLES-3.0 ở chỗ này.
- TGL cho điểm test độc lập: `ConformanceChecklist()` + test mới
  `test_mobilegl_host_requirements.cpp` tự chứng minh TGL đủ điều kiện làm host.

## Chưa đủ để "chạy game luôn"

1. **Present**: state machine EGL đã xong (`EglState`); cầu ObjC++
   `tgles_metal_bridge` đã **presentDrawable thật + pixel thật**
   (device-verified trên host Metal) — còn run validation trên iPhone
    A-series với layer thật của app (xem [Kiến trúc](architecture.md)).
2. **Metal execution bridge**: MSL compile + PSO + draw + blit/readback +
   fence ring đều đã chạy thật trên GPU (`tests/test_metal_device.mm`);
   wiring app→bridge (`GlesContext::RenderFrame`: VAO float attribs +
   MVP + draw-FBO size + depth) đã xong cho TRIANGLES — còn indexed draws,
   attribs non-float/non-interleaved, MRT targets. Depth test (NEVER..ALWAYS
   + mask + clear, target Depth32Float) đã device-verified, xem
   [Cửa sổ test](viewer.md).
3. **Ngoài GPU**: JVM + JIT trên iOS (TrollStore/debugger, Azul Zulu JDK),
   LWJGL natives, giới hạn RAM, chỉ sideload (PojavLauncher iOS README).

## Lộ trình test đề xuất

1. [PASS] Trong repo: `test_mobilegl_host_requirements` (xong, luôn xanh).
2. Trên macOS host: chạy MobileGL self-tests/Piglit chĩa vào TGL làm host
   GLES — không cần iPhone.
3. Viết cầu EGL tối thiểu + present `CAMetalLayer`, chạy thử triangle của
   MobileGL trên simulator.
4. Launcher trial: `MOBILEGL_BACKEND_TYPE=DirectGLES` + TGL làm host EGL/GLES,
   test Minecraft 1.21 + Sodium (giống cách MobileGL inject qua PrismLauncher
   trên macOS bằng `DYLD_INSERT_LIBRARIES`).
